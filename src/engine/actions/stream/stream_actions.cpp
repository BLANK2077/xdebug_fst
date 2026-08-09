// stream_actions.cpp — stream.config.list/get/load, stream.describe/query/export/validate
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"
#include "engine/actions/value_source_entries.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── StreamConfig (xdebug-fst simplified config) ──

struct StreamConfig {
    std::string name;
    Json source;
    std::map<std::string,std::string> signals;
    std::string clock;   // clock signal name
    std::string valid;   // valid signal name
    std::string ready;   // ready signal name
    std::string data;    // data signal name (optional)
    std::string edge = "posedge";
    std::string sample_point = "before";
    std::string reset;
    std::string reset_polarity;
    std::string sop, eop;
    Json beat_fields = Json::object();
    std::string channel_id_valid = "every_beat";
    bool allow_interleaving = false;
    uint64_t start = 0;  // window begin (0 = full)
    uint64_t end = 0;    // window end (0 = full)
    bool active_high = true;
};

static Json config_to_json(const StreamConfig& cfg) {
    return cfg.source;
}

// ── In-process config store ──

static std::map<std::string, StreamConfig>& config_store() {
    static std::map<std::string, StreamConfig> store;
    return store;
}

/// Clear all user-loaded stream configs (called on session.open).
void clear_stream_configs() {
    config_store().clear();
}


// ── Helpers ──

/// True if bits represent a logic "high" (not all zero, no x/z/X/Z).
/// Per task spec: 位串非全零且不含 x/z（back 位为 '1'）
static bool is_high(const std::string& bits) {
    if (bits.empty()) return false;
    for (char c : bits) {
        if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') return false;
    }
    // Not all zeros?
    for (char c : bits) {
        if (c != '0') return true;
    }
    return false;
}

/// Check if a signal value at a time index is considered "high".
/// Returns false if signal not found / not loaded.
static bool signal_is_high_at(IWaveformBackend* wf, uint32_t ref, uint32_t ti) {
    IWaveformBackend::SignalOffset off;
    if (!wf->signal_offset_at(ref, ti, off)) return false;
    std::string bits = wf->signal_value_str(ref, off.start, 0);
    return is_high(bits);
}

/// Load a signal by name; return ref (0 if not found). Load if needed.
static uint32_t load_signal(IWaveformBackend* wf,
                           const std::string& name) {
    uint32_t ref = wf->find_signal(name);
    if (ref == IWaveformBackend::kInvalidSignalRef) return IWaveformBackend::kInvalidSignalRef;
    if (!wf->is_loaded(ref)) wf->load_signals({ref});
    return ref;
}

/// Resolve a time value from args or default to waveform bounds.
static bool resolve_time_range(const Json& args, IWaveformBackend* wf,
                               uint64_t& begin, uint64_t& end) {
    begin = 0;
    end = wf->max_time();
    if (args.contains("begin")) {
        if (args["begin"].is_number()) begin = args["begin"].get<uint64_t>();
        else if (args["begin"].is_string()) {
            try { begin = std::stoull(args["begin"].get<std::string>()); }
            catch (...) { return false; }
        } else return false;
    }
    if (args.contains("end")) {
        if (args["end"].is_number()) end = args["end"].get<uint64_t>();
        else if (args["end"].is_string()) {
            try { end = std::stoull(args["end"].get<std::string>()); }
            catch (...) { return false; }
        } else return false;
    }
    return true;
}

/// Check waveform loaded, return error Json if not.
static Json check_waveform(const char* action) {
    auto& g = engine_globals();
    if (!g.has_waveform || !g.waveform) {
        return Json{{"ok", false},
                    {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                               {"message", std::string("action requires waveform file: ") + action}}}};
    }
    return Json::object(); // empty = ok
}

/// Find and load all signals referenced by a config. Returns error Json if any missing.
static Json validate_config_signals(IWaveformBackend* wf, const StreamConfig& cfg,
                                    std::vector<std::string>& missing) {
    missing.clear();
    if (!cfg.clock.empty() && wf->find_signal(cfg.clock) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.clock);
    if (!cfg.valid.empty() && wf->find_signal(cfg.valid) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.valid);
    if (!cfg.ready.empty() && wf->find_signal(cfg.ready) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.ready);
    if (!cfg.data.empty() && wf->find_signal(cfg.data) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.data);
    if (!missing.empty()) {
        Json arr = Json::array();
        for (auto& m : missing) arr.push_back(m);
        return Json{{"ok", false},
                    {"error", {{"code", "CONFIG_SIGNAL_NOT_FOUND"},
                               {"message", "some signals not found in waveform"},
                               {"missing_signals", arr}}}};
    }
    return Json::object(); // ok
}

/// Load the named config (defaults to "default"). Returns error if not found.
static bool get_config(const std::string& name, StreamConfig& cfg, Json& err) {
    auto& store = config_store();
    auto it = store.find(name);
    if (it == store.end()) {
        err = Json{{"ok", false},
                   {"error", {{"code", "CONFIG_NOT_FOUND"},
                              {"message", "stream config not found: " + name}}}};
        return false;
    }
    cfg = it->second;
    return true;
}

bool stream_value_source_entries(const std::string& name,
                                 std::vector<ValueSourceEntry>& out) {
    Json error;
    StreamConfig config;
    if (!get_config(name, config, error)) return false;
    out = {{"clock", config.clock}, {"valid", config.valid},
           {"ready", config.ready}};
    if (!config.data.empty()) out.push_back({"data", config.data});
    return true;
}

static Json action_error(const std::string& code, const std::string& message) {
    return {{"ok",false},{"error",{{"code",code},{"message",message}}}};
}

static bool parse_stream(const Json& input, StreamConfig& cfg, std::string& message) {
    cfg = {};
    cfg.name = input.at("name");
    cfg.signals = input.at("signals").get<std::map<std::string,std::string>>();
    auto resolve = [&](const std::string& alias) -> std::string {
        auto found = cfg.signals.find(alias);
        return found == cfg.signals.end() ? std::string() : found->second;
    };
    cfg.clock = resolve(input.at("clock"));
    cfg.valid = resolve(input.at("vld"));
    cfg.ready = resolve(input.at("rdy"));
    if (input.contains("sop")) cfg.sop = resolve(input.at("sop"));
    if (input.contains("eop")) cfg.eop = resolve(input.at("eop"));
    cfg.edge = input.value("edge","posedge");
    cfg.sample_point = input.value("sample_point", cfg.edge == "negedge" ? "" : "before");
    if (input.contains("reset")) {
        cfg.reset = input.at("reset").at("signal");
        cfg.reset_polarity = input.at("reset").at("polarity");
    }
    cfg.beat_fields = input.value("beat_fields",Json::object());
    cfg.channel_id_valid = input.value("channel_id_valid","every_beat");
    cfg.allow_interleaving = input.value("allow_interleaving",false);
    if (cfg.clock.empty() || cfg.valid.empty() || cfg.ready.empty()) {
        message = "stream clock/vld/rdy alias is missing from signals";
        return false;
    }
    cfg.data.clear();
    if (!cfg.beat_fields.empty()) {
        const std::string expression = cfg.beat_fields.begin().value();
        cfg.data = resolve(expression);
    }
    cfg.source = input;
    cfg.source["edge"] = cfg.edge;
    if (cfg.edge != "negedge") cfg.source["sample_point"] = cfg.sample_point;
    cfg.source["channel_id_valid"] = cfg.channel_id_valid;
    cfg.source["allow_interleaving"] = cfg.allow_interleaving;
    return true;
}

static Json validate_stream(IWaveformBackend* wf, const StreamConfig& cfg,
                            bool include_stream) {
    Json signals = Json::array();
    bool ok = true;
    for (const auto& item : cfg.signals) {
        const uint32_t ref = wf->find_signal(item.second);
        Json signal{{"alias",item.first},{"requested_path",item.second}};
        if (!ref) { signal["status"] = "signal_not_found"; ok = false; }
        else {
            if (!wf->is_loaded(ref)) wf->load_signals({ref});
            IWaveformBackend::SignalInfo info;
            if (!wf->signal_info(ref,info)) { signal["status"] = "signal_not_found"; ok = false; }
            else { signal["status"]="ok"; signal["resolved_path"]=item.second; signal["width"]=info.width; }
        }
        signals.push_back(std::move(signal));
    }
    Json result{{"status",ok?"ok":"error"},{"signals",signals},
        {"sampling",{{"clock",cfg.source.at("clock")},{"edge",cfg.edge},
            {"sample_point",cfg.edge == "negedge" ? Json(nullptr) : Json(cfg.sample_point)}}},
        {"packet_rules",{{"packet_enabled",!cfg.sop.empty() && !cfg.eop.empty()},
            {"channel_id_valid",cfg.channel_id_valid},
            {"allow_interleaving",cfg.allow_interleaving}}}};
    if (include_stream) result["stream"] = cfg.name;
    return result;
}

static Json recommendations() {
    return Json::array({
        {{"action","value.at"},{"purpose","按一个或多个指定时间读取单信号、命名信号列表或接口配置维护的值。"}},
        {{"action","stream.describe"},{"purpose","显示 stream 定义和摘要。"}},
        {{"action","stream.validate"},{"purpose","验证 stream 配置；动态验证可显式选择 full 或 range 基础分析缓存范围。"}},
        {{"action","stream.query"},{"purpose","以显式 full 或 range 基础分析缓存范围查询并按多个字段过滤 stream transfer 或 packet。"}},
        {{"action","stream.export"},{"purpose","从显式 full 或 range 基础分析缓存范围导出 stream 查询结果。"}}
    });
}

// ── Helper: scan handshake events on clock edges ──

struct HandshakeEvent {
    uint32_t time_idx;
    uint64_t time;
    std::string data_bits;  // empty if no data signal configured
};

/// Scan for handshake events in [begin_ti, end_ti] using clock edges.
/// A handshake occurs at a rising clock edge where both valid and ready are high.
static std::vector<HandshakeEvent> scan_handshakes(
    IWaveformBackend* wf,
    uint32_t clk_ref, uint32_t vld_ref, uint32_t rdy_ref,
    uint32_t data_ref,    // 0 = no data
    uint32_t begin_ti, uint32_t end_ti,
    int max_rows = 0)     // 0 = no limit
{
    std::vector<HandshakeEvent> events;
    if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});
    if (!wf->is_loaded(vld_ref)) wf->load_signals({vld_ref});
    if (!wf->is_loaded(rdy_ref)) wf->load_signals({rdy_ref});
    if (data_ref && !wf->is_loaded(data_ref)) wf->load_signals({data_ref});

    // Get clock change time indices
    std::vector<uint32_t> clk_tis = wf->time_indices_of(clk_ref);
    if (clk_tis.empty()) return events;

    std::string prev_clk_bits;
    bool have_prev = false;

    // Find previous clock value before begin_ti for edge detection
    for (uint32_t ti : clk_tis) {
        if (ti > begin_ti && !have_prev) {
            // Find the last change before or at begin_ti
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(clk_ref, begin_ti, off)) {
                prev_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);
                have_prev = true;
            }
            break;
        }
    }

    for (uint32_t ti : clk_tis) {
        if (ti < begin_ti) {
            // Update previous value as we scan
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(clk_ref, ti, off) && off.time_match) {
                prev_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);
                have_prev = true;
            }
            continue;
        }
        if (ti > end_ti) break;

        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(clk_ref, ti, off)) continue;
        if (!off.time_match) continue;
        std::string cur_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);

        // Detect rising edge: prev = 0, cur = 1
        bool rising = have_prev && is_rising_edge(prev_clk_bits, cur_clk_bits);

        prev_clk_bits = cur_clk_bits;
        have_prev = true;

        if (!rising) continue;

        // Check valid and ready at this clock edge. Handshakes are level
        // sampled at the edge, but wave dumps record the post-edge state
        // (the slave may deassert ready in the same cycle it samples).
        // Accept cross combinations only when valid or ready just rose.
        bool v_now = signal_is_high_at(wf, vld_ref, ti);
        bool r_now = signal_is_high_at(wf, rdy_ref, ti);
        bool v_prev = ti > 0 && signal_is_high_at(wf, vld_ref, ti - 1);
        bool r_prev = ti > 0 && signal_is_high_at(wf, rdy_ref, ti - 1);
        bool v_rise = v_now && !v_prev;
        bool r_rise = r_now && !r_prev;
        bool hs = (v_now && r_now) || (v_now && v_rise && r_prev) ||
                  (r_now && r_rise && v_prev);
        if (!hs) continue;

        HandshakeEvent ev;
        ev.time_idx = ti;
        ev.time = wf->time_at(ti);

        if (data_ref) {
            IWaveformBackend::SignalOffset doff;
            if (wf->signal_offset_at(data_ref, ti, doff)) {
                ev.data_bits = wf->signal_value_str(data_ref, doff.start, 0);
            }
        }

        events.push_back(ev);

        if (max_rows > 0 && (int)events.size() >= max_rows) break;
    }

    return events;
}

/// Count valid_high, ready_high, handshake on clock edges in window.
/// Returns {valid_high_count, ready_high_count, handshake_count}.
static void count_handshake_stats(
    IWaveformBackend* wf,
    uint32_t clk_ref, uint32_t vld_ref, uint32_t rdy_ref,
    uint32_t begin_ti, uint32_t end_ti,
    uint64_t& valid_high_count, uint64_t& ready_high_count, uint64_t& handshake_count)
{
    valid_high_count = 0;
    ready_high_count = 0;
    handshake_count = 0;

    if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});
    if (!wf->is_loaded(vld_ref)) wf->load_signals({vld_ref});
    if (!wf->is_loaded(rdy_ref)) wf->load_signals({rdy_ref});

    std::vector<uint32_t> clk_tis = wf->time_indices_of(clk_ref);
    if (clk_tis.empty()) return;

    std::string prev_clk_bits;
    bool have_prev = false;

    for (uint32_t ti : clk_tis) {
        if (ti < begin_ti) {
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(clk_ref, ti, off) && off.time_match) {
                prev_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);
                have_prev = true;
            }
            continue;
        }
        if (ti > end_ti) break;

        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(clk_ref, ti, off)) continue;
        if (!off.time_match) continue;
        std::string cur_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);

        bool rising = have_prev && is_rising_edge(prev_clk_bits, cur_clk_bits);
        prev_clk_bits = cur_clk_bits;
        have_prev = true;

        if (!rising) continue;

        bool vh = signal_is_high_at(wf, vld_ref, ti);
        bool rh = signal_is_high_at(wf, rdy_ref, ti);
        bool vp = ti > 0 && signal_is_high_at(wf, vld_ref, ti - 1);
        bool rp = ti > 0 && signal_is_high_at(wf, rdy_ref, ti - 1);
        bool hs = (vh && rh) || (vh && !vp && rp) || (rh && !rp && vp);

        if (vh) valid_high_count++;
        if (rh) ready_high_count++;
        if (hs) handshake_count++;
    }
}

// ═══════════════════════════════════════════════════════════════
// 1. stream.config.list
// ═══════════════════════════════════════════════════════════════

struct StreamConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto& store = config_store();
        const Json args = req.value("args",Json::object());
        const bool verbose = args.value("output",Json::object()).value("verbose",false);
        Json arr = Json::array();
        for (const auto& kv : store) {
            const auto& cfg = kv.second;
            Json item{{"name",cfg.name},{"sampling_mode","clock_edge"},
                {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
                {"handshake","vld/rdy"},{"packet",(!cfg.sop.empty()&&!cfg.eop.empty())?"sop/eop":"disabled"},
                {"field_count",cfg.beat_fields.size()},{"channel_id_valid",cfg.channel_id_valid},
                {"allow_interleaving",cfg.allow_interleaving}};
            if (cfg.edge != "negedge") item["sample_point"] = cfg.sample_point;
            if (verbose) item["config"] = config_to_json(cfg);
            arr.push_back(std::move(item));
        }
        return {{"ok",true},{"summary",{{"count",arr.size()}}},
            {"data",{{"streams",arr}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 2. stream.config.get
// ═══════════════════════════════════════════════════════════════

struct StreamConfigGetHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.config.get"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("name");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        return {{"ok",true},{"summary",{{"name",name}}},
            {"data",{{"stream",config_to_json(cfg)}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 3. stream.config.load
// ═══════════════════════════════════════════════════════════════

struct StreamConfigLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        Json document;
        if (args.contains("config")) document = args.at("config");
        else {
            std::ifstream stream(args.at("config_path").get<std::string>());
            if (!stream) return action_error("INVALID_FIELD","cannot read stream config_path");
            try { stream >> document; } catch (...) {
                return action_error("INVALID_FIELD","invalid stream config JSON");
            }
        }
        const std::string mode = args.value("mode","replace");
        if (mode == "replace") config_store().clear();
        Json names = Json::array(), validations = Json::array(), issues = Json::array();
        for (const Json& item : document.at("streams")) {
            StreamConfig cfg; std::string message;
            if (!parse_stream(item,cfg,message))
                return action_error("VALIDATION_FAILED",message);
            Json validation = validate_stream(engine_globals().waveform.get(),cfg,true);
            if (validation.at("status") != "ok")
                return action_error("CONFIG_SIGNAL_NOT_FOUND","stream config signal not found");
            names.push_back(cfg.name); validations.push_back(validation);
            config_store()[cfg.name] = std::move(cfg);
        }
        return {{"ok",true},{"summary",{{"loaded",names.size()},{"mode",mode}}},
            {"data",{{"streams",names},{"issues",issues},{"validation",validations},
                {"recommended_actions",recommendations()}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 4. stream.describe
// ═══════════════════════════════════════════════════════════════

struct StreamDescribeHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.describe"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        Json validation = validate_stream(engine_globals().waveform.get(),cfg,false);
        Json issues = Json::array();
        return {{"ok",true},{"summary",{{"stream",name},{"handshake","vld/rdy"},
            {"packet_enabled",!cfg.sop.empty()&&!cfg.eop.empty()}}},
            {"data",{{"config",config_to_json(cfg)},{"issues",issues},
                {"validation",validation},{"semantics",{{"transfer","vld/rdy"},{"stall","enabled"}}}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 5. stream.query
// ═══════════════════════════════════════════════════════════════

struct StreamQueryHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.query"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        uint32_t clk_ref = load_signal(wf, cfg.clock);
        uint32_t vld_ref = load_signal(wf, cfg.valid);
        uint32_t rdy_ref = load_signal(wf, cfg.ready);
        uint32_t data_ref = cfg.data.empty() ? 0 : load_signal(wf, cfg.data);

        if (clk_ref == IWaveformBackend::kInvalidSignalRef ||
            vld_ref == IWaveformBackend::kInvalidSignalRef ||
            rdy_ref == IWaveformBackend::kInvalidSignalRef) {
            std::vector<std::string> missing;
            if (clk_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.clock);
            if (vld_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.valid);
            if (rdy_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.ready);
            Json arr = Json::array();
            for (auto& m : missing) arr.push_back(m);
            return Json{{"ok", false},
                        {"error", {{"code", "CONFIG_SIGNAL_NOT_FOUND"},
                                   {"message", "some signals not found in waveform"},
                                   {"missing_signals", arr}}}};
        }

        uint64_t begin_time = wf->min_time(), end_time = wf->max_time();
        std::string message;
        const Json range = args.value("time_range",Json::object());
        if (range.contains("begin") && !wf->parse_time(range.at("begin"),begin_time,message))
            return action_error("INVALID_TIME",message);
        if (range.contains("end") && !wf->parse_time(range.at("end"),end_time,message,true))
            return action_error("INVALID_TIME",message);
        if (begin_time > end_time) return action_error("TIME_RANGE_INVALID","end is before begin");

        int max_rows = args.value("line_limit",1000);

        uint32_t begin_ti = wf->time_idx_of(begin_time);
        uint32_t end_ti = wf->time_idx_of(end_time);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);
        TimeRenderUnit unit; parse_time_render_unit(args.value("render_time_unit","ns"),unit,message);

        IWaveformBackend::SignalInfo info;
        int data_width = 0;
        if (data_ref && wf->signal_info(data_ref, info)) {
            data_width = static_cast<int>(info.width);
        }

        auto events = scan_handshakes(wf, clk_ref, vld_ref, rdy_ref, data_ref,
                                      begin_ti, end_ti, max_rows);

        Json rows = Json::array();
        size_t cycle = 0;
        for (auto& ev : events) {
            Json fields = Json::object();
            if (data_ref && !ev.data_bits.empty())
                fields["data"] = logic_value_json(
                    logic_value_from_bits(ev.data_bits,data_width),fmt);
            rows.push_back({{"cycle",cycle++},{"time",wf->format_time(ev.time,unit)},
                {"vld",true},{"rdy",true},{"bp",false},{"sop",false},{"eop",false},
                {"transfer",true},{"stall",false},{"beat_index",0},{"fields",fields}});
        }

        // Count total handshakes for summary
        uint64_t vhc = 0, rhc = 0, hc = 0;
        count_handshake_stats(wf, clk_ref, vld_ref, rdy_ref,
                              begin_ti, end_ti, vhc, rhc, hc);

        bool truncated = events.size() < hc;
        const std::string query = args.at("query");
        Json summary{{"stream",name},{"query",query},{"sampling_mode","clock_edge"},
            {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
            {"sample_time_semantics","time is sample_time"},{"handshake","vld/rdy"},
            {"packet_enabled",!cfg.sop.empty()&&!cfg.eop.empty()},
            {"clock_edges",wf->time_indices_of(clk_ref).size()/2},{"vld_cycles",vhc},
            {"transfer_count",hc},{"stall_cycles",vhc-hc},{"stall_windows",vhc>hc?1:0},
            {"complete_packet_count",0},{"partial_packet_count",0},
            {"packet_count_status","exact"},{"control_xz_count",0},{"data_xz_count",0},
            {"ready_bp_conflict_count",0},{"packet_stable_mismatch_count",0},
            {"requested_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},
            {"scanned_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},{"filter_applied",false},
            {"scan_complete",true},{"analysis_complete",true},
            {"response_truncated",truncated},{"total_count",query=="summary"?0:hc},
            {"returned_count",query=="transfer_window"?rows.size():0},
            {"truncation_scopes",truncated?Json::array({"response_rows"}):Json::array()}};
        if (cfg.edge != "negedge") summary["sample_point"] = cfg.sample_point;
        if (!events.empty()) {
            summary["first_transfer_time"] = wf->format_time(events.front().time,unit);
            summary["last_transfer_time"] = wf->format_time(events.back().time,unit);
        }
        Json data = Json::object();
        if (query == "transfer_window") {
            data["rows"] = rows;
            if (truncated) data["hint"] = "increase line_limit to return more transfer rows";
        }
        else if (query == "first_transfer" || query == "last_transfer") {
            if (!rows.empty()) data["row"] = query=="first_transfer"?rows.front():rows.back();
        }
        return {{"ok",true},{"summary",summary},{"data",data}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 6. stream.export
// ═══════════════════════════════════════════════════════════════

struct StreamExportHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.export"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        uint32_t clk_ref = load_signal(wf, cfg.clock);
        uint32_t vld_ref = load_signal(wf, cfg.valid);
        uint32_t rdy_ref = load_signal(wf, cfg.ready);
        uint32_t data_ref = cfg.data.empty() ? 0 : load_signal(wf, cfg.data);

        if (clk_ref == IWaveformBackend::kInvalidSignalRef ||
            vld_ref == IWaveformBackend::kInvalidSignalRef ||
            rdy_ref == IWaveformBackend::kInvalidSignalRef) {
            std::vector<std::string> missing;
            if (clk_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.clock);
            if (vld_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.valid);
            if (rdy_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.ready);
            Json arr = Json::array();
            for (auto& m : missing) arr.push_back(m);
            return Json{{"ok", false},
                        {"error", {{"code", "CONFIG_SIGNAL_NOT_FOUND"},
                                   {"message", "some signals not found in waveform"},
                                   {"missing_signals", arr}}}};
        }

        uint64_t begin_time = wf->min_time(), end_time = wf->max_time();
        std::string message;
        const Json range = args.value("time_range",Json::object());
        if (range.contains("begin") && !wf->parse_time(range.at("begin"),begin_time,message))
            return action_error("INVALID_TIME",message);
        if (range.contains("end") && !wf->parse_time(range.at("end"),end_time,message,true))
            return action_error("INVALID_TIME",message);
        if (begin_time > end_time) return action_error("TIME_RANGE_INVALID","end is before begin");

        uint32_t begin_ti = wf->time_idx_of(begin_time);
        uint32_t end_ti = wf->time_idx_of(end_time);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);
        TimeRenderUnit unit;
        if (!parse_time_render_unit(args.value("render_time_unit","ns"),unit,message))
            return action_error("INVALID_FIELD",message);

        IWaveformBackend::SignalInfo info;
        int data_width = 0;
        if (data_ref && wf->signal_info(data_ref, info)) {
            data_width = static_cast<int>(info.width);
        }

        const size_t line_limit = args.value("line_limit",1000u);
        auto events = scan_handshakes(wf, clk_ref, vld_ref, rdy_ref, data_ref,
                                      begin_ti, end_ti, 0);
        const size_t row_count = std::min(line_limit,events.size());
        if (!args.contains("output") || !args.at("output").contains("path"))
            return action_error("INVALID_FIELD","stream.export requires output.path");
        const Json output = args.at("output");
        const std::string path = output.at("path");
        const std::string file_format = output.value("file_format","tsv");
        const std::string meta_path = path + ".meta.json";
        std::ofstream rows_file(path), meta_file(meta_path);
        if (!rows_file || !meta_file)
            return action_error("OUTPUT_WRITE_FAILED","cannot open stream export output");
        const char separator = file_format == "csv" ? ',' : '\t';
        rows_file << "cycle" << separator << "time" << separator << "data\n";
        for (size_t index = 0; index < row_count; ++index) {
            const auto& event = events[index];
            std::string rendered;
            if (data_ref && !event.data_bits.empty())
                rendered = logic_value_json(
                    logic_value_from_bits(event.data_bits,data_width),fmt).at("value");
            rows_file << index << separator << wf->format_time(event.time,unit)
                      << separator << rendered << '\n';
        }
        meta_file << Json{{"stream",name},{"kind",args.value("kind","transfer")},
            {"row_count",row_count},{"source","current_session_fst"}}.dump(2) << '\n';

        uint64_t vhc = 0, rhc = 0, hc = 0;
        count_handshake_stats(wf,clk_ref,vld_ref,rdy_ref,begin_ti,end_ti,vhc,rhc,hc);
        const bool truncated = row_count < events.size();
        Json summary{{"stream",name},{"sampling_mode","clock_edge"},
            {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
            {"sample_time_semantics","time is sample_time"},{"handshake","vld/rdy"},
            {"packet_enabled",!cfg.sop.empty()&&!cfg.eop.empty()},
            {"clock_edges",wf->time_indices_of(clk_ref).size()/2},{"vld_cycles",vhc},
            {"transfer_count",hc},{"stall_cycles",vhc-hc},{"stall_windows",vhc>hc?1:0},
            {"complete_packet_count",0},{"partial_packet_count",0},
            {"packet_count_status","exact"},{"control_xz_count",0},{"data_xz_count",0},
            {"ready_bp_conflict_count",0},{"packet_stable_mismatch_count",0},
            {"requested_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},
            {"scanned_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},
            {"status","written"},{"output_written",true},{"row_count",row_count},
            {"line_limit",line_limit},{"kind",args.value("kind","transfer")},
            {"output",{{"path",path},{"meta_path",meta_path},{"file_format",file_format}}},
            {"scan_complete",true},{"analysis_complete",true},
            {"response_truncated",truncated},{"total_count",events.size()},
            {"returned_count",row_count},
            {"truncation_scopes",truncated?Json::array({"export_rows"}):Json::array()}};
        if (cfg.edge != "negedge") summary["sample_point"] = cfg.sample_point;
        if (!events.empty()) {
            summary["first_transfer_time"] = wf->format_time(events.front().time,unit);
            summary["last_transfer_time"] = wf->format_time(events.back().time,unit);
        }
        return {{"ok",true},{"summary",summary},{"data",Json::object()}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 7. stream.validate
// ═══════════════════════════════════════════════════════════════

struct StreamValidateHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.validate"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        Json validation = validate_stream(wf,cfg,false);
        const bool static_ok = validation.at("status") == "ok";
        Json issues = Json::array();
        if (!static_ok) issues.push_back({{"code","signal_not_found"},
            {"severity","error"},{"message","stream contains unresolved signals"}});
        const bool dynamic_requested = args.value("dynamic",false);
        Json dynamic = Json::object();
        bool scan_complete = false, analysis_complete = static_ok;
        if (dynamic_requested && static_ok) {
            uint64_t begin_time = wf->min_time(), end_time = wf->max_time();
            std::string message;
            const Json range = args.value("time_range",Json::object());
            if (range.contains("begin") && !wf->parse_time(range.at("begin"),begin_time,message))
                return action_error("INVALID_TIME",message);
            if (range.contains("end") && !wf->parse_time(range.at("end"),end_time,message,true))
                return action_error("INVALID_TIME",message);
            if (begin_time > end_time)
                return action_error("TIME_RANGE_INVALID","end is before begin");
            TimeRenderUnit unit;
            if (!parse_time_render_unit(args.value("render_time_unit","ns"),unit,message))
                return action_error("INVALID_FIELD",message);
            const uint32_t clk_ref=load_signal(wf,cfg.clock),
                vld_ref=load_signal(wf,cfg.valid),rdy_ref=load_signal(wf,cfg.ready);
            const uint32_t begin_ti=wf->time_idx_of(begin_time),end_ti=wf->time_idx_of(end_time);
            uint64_t vhc=0,rhc=0,hc=0;
            count_handshake_stats(wf,clk_ref,vld_ref,rdy_ref,begin_ti,end_ti,vhc,rhc,hc);
            auto events=scan_handshakes(wf,clk_ref,vld_ref,rdy_ref,0,begin_ti,end_ti,0);
            dynamic={{"stream",name},{"sampling_mode","clock_edge"},
                {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
                {"sample_time_semantics","time is sample_time"},{"handshake","vld/rdy"},
                {"packet_enabled",!cfg.sop.empty()&&!cfg.eop.empty()},
                {"clock_edges",wf->time_indices_of(clk_ref).size()/2},{"vld_cycles",vhc},
                {"transfer_count",hc},{"stall_cycles",vhc-hc},{"stall_windows",vhc>hc?1:0},
                {"complete_packet_count",0},{"partial_packet_count",0},
                {"packet_count_status","exact"},{"control_xz_count",0},{"data_xz_count",0},
                {"ready_bp_conflict_count",0},{"packet_stable_mismatch_count",0},
                {"requested_range",{{"begin",wf->format_time(begin_time,unit)},
                    {"end",wf->format_time(end_time,unit)}}},
                {"scanned_range",{{"begin",wf->format_time(begin_time,unit)},
                    {"end",wf->format_time(end_time,unit)}}}};
            if (cfg.edge != "negedge") dynamic["sample_point"] = cfg.sample_point;
            if (!events.empty()) {
                dynamic["first_transfer_time"]=wf->format_time(events.front().time,unit);
                dynamic["last_transfer_time"]=wf->format_time(events.back().time,unit);
            }
            scan_complete=true;
        }
        const bool ok=static_ok && (!dynamic_requested || scan_complete);
        return {{"ok",true},{"summary",{{"stream",name},{"ok",ok},
            {"static_validation_complete",true},{"dynamic_requested",dynamic_requested},
            {"scan_complete",scan_complete},{"analysis_complete",analysis_complete},
            {"response_truncated",false},{"total_count",issues.size()},
            {"returned_count",issues.size()},{"truncation_scopes",Json::array()}}},
            {"data",{{"issues",issues},{"dynamic",dynamic}}}};
    }
};

// ── Factory functions ──

std::unique_ptr<EngineActionHandler> make_stream_config_list_handler() {
    return std::make_unique<StreamConfigListHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_config_get_handler() {
    return std::make_unique<StreamConfigGetHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_config_load_handler() {
    return std::make_unique<StreamConfigLoadHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_describe_handler() {
    return std::make_unique<StreamDescribeHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_query_handler() {
    return std::make_unique<StreamQueryHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_export_handler() {
    return std::make_unique<StreamExportHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_validate_handler() {
    return std::make_unique<StreamValidateHandler>();
}

} // namespace xdebug_fst
