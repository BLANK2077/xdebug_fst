// stream_actions.cpp — stream.config.list/get/load, stream.describe/query/export/validate
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/expr/expr_eval.h"
#include "api/json_types.h"
#include "engine/actions/value_source_entries.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
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
    std::string backpressure; // active-high back-pressure signal name
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

static bool is_known_binary(const std::string& bits) {
    if (bits.empty()) return false;
    for (char bit : bits)
        if (bit != '0' && bit != '1') return false;
    return true;
}

static std::string signal_bits_at(IWaveformBackend* wf, uint32_t ref,
                                  uint32_t ti) {
    IWaveformBackend::SignalOffset off;
    if (!wf->signal_offset_at(ref, ti, off)) return {};
    return wf->signal_value_str(ref, off.start, 0);
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
    if (!cfg.backpressure.empty() &&
        wf->find_signal(cfg.backpressure) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.backpressure);
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
    out = {{"clock", config.clock}, {"valid", config.valid}};
    if (!config.ready.empty()) out.push_back({"ready",config.ready});
    if (!config.backpressure.empty()) out.push_back({"bp",config.backpressure});
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
    if (input.contains("rdy")) cfg.ready = resolve(input.at("rdy"));
    if (input.contains("bp")) cfg.backpressure = resolve(input.at("bp"));
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
    if (cfg.clock.empty() || cfg.valid.empty() ||
        (input.contains("rdy")&&cfg.ready.empty()) ||
        (input.contains("bp")&&cfg.backpressure.empty())) {
        message = "stream clock/vld/flow-control alias is missing from signals";
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

static std::string stream_handshake(const StreamConfig& cfg) {
    if (!cfg.ready.empty()&&!cfg.backpressure.empty()) return "vld/rdy/bp";
    if (!cfg.ready.empty()) return "vld/rdy";
    if (!cfg.backpressure.empty()) return "vld/bp";
    return "vld";
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

static std::string resolve_field_expression(const std::string& expression,
                                            const StreamConfig& cfg) {
    std::string resolved;
    for (size_t pos = 0; pos < expression.size();) {
        const unsigned char ch = static_cast<unsigned char>(expression[pos]);
        if (std::isalpha(ch) || expression[pos] == '_') {
            size_t end = pos + 1;
            while (end < expression.size()) {
                const unsigned char next = static_cast<unsigned char>(expression[end]);
                if (!std::isalnum(next) && expression[end] != '_' && expression[end] != '$') break;
                ++end;
            }
            const std::string token = expression.substr(pos,end-pos);
            auto found = cfg.signals.find(token);
            resolved += found == cfg.signals.end() ? token : found->second;
            pos = end;
        } else {
            resolved.push_back(expression[pos++]);
        }
    }
    return resolved;
}

static bool evaluate_field_expression(const std::string& expression,
                                      const StreamConfig& cfg,
                                      const IWaveformBackend& wf,
                                      uint32_t time_idx, LogicValue& value,
                                      std::string& message) {
    std::string text = expression;
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if (first == std::string::npos) { message = "empty stream field expression"; return false; }
    text = text.substr(first,last-first+1);
    if (text.front() == '{' && text.back() == '}') {
        const std::string inner = text.substr(1,text.size()-2);
        std::string part;
        std::vector<std::string> parts;
        int square_depth = 0;
        for (char c : inner) {
            if (c == '[') ++square_depth;
            if (c == ']') --square_depth;
            if (c == ',' && square_depth == 0) {
                parts.push_back(part); part.clear();
            } else part.push_back(c);
        }
        parts.push_back(part);
        std::string bits;
        for (const auto& item : parts) {
            LogicValue component;
            if (!evaluate_field_expression(item,cfg,wf,time_idx,component,message)) return false;
            bits += component.bits;
        }
        value = logic_value_from_bits(bits,static_cast<int>(bits.size()));
        return true;
    }
    const std::string resolved = resolve_field_expression(text,cfg);
    std::unique_ptr<ExprNode> root(parse_expression(resolved,message));
    if (!root) return false;
    value = eval_expression(root.get(),wf,time_idx);
    return !value.bits.empty();
}

static Json stream_fields_at(const StreamConfig& cfg, const IWaveformBackend& wf,
                             uint32_t time_idx, ValueRenderFormat format,
                             bool& complete) {
    Json fields = Json::object();
    for (const auto& item : cfg.beat_fields.items()) {
        LogicValue value; std::string message;
        if (!item.value().is_string() ||
            !evaluate_field_expression(item.value(),cfg,wf,time_idx,value,message)) {
            complete = false;
            continue;
        }
        fields[item.key()] = logic_value_json(value,format);
    }
    return fields;
}

struct StreamSample {
    size_t cycle = 0;
    uint32_t time_idx = 0;
    uint64_t time = 0;
    bool vld = false, rdy = false, bp = false, sop = false, eop = false;
    bool transfer = false, stall = false;
    bool control_known = true, data_known = true;
    bool ready_bp_conflict = false;
    Json fields = Json::object();
};

static std::vector<StreamSample> scan_stream_samples(
    IWaveformBackend* wf, const StreamConfig& cfg,
    uint32_t begin_ti, uint32_t end_ti, ValueRenderFormat format,
    bool& fields_complete) {
    std::vector<StreamSample> samples;
    const uint32_t clk_ref=load_signal(wf,cfg.clock),vld_ref=load_signal(wf,cfg.valid),
        rdy_ref=cfg.ready.empty()?0:load_signal(wf,cfg.ready),
        bp_ref=cfg.backpressure.empty()?0:load_signal(wf,cfg.backpressure);
    const uint32_t sop_ref=cfg.sop.empty()?0:load_signal(wf,cfg.sop),
        eop_ref=cfg.eop.empty()?0:load_signal(wf,cfg.eop);
    std::string previous_clock;
    bool have_previous = false;
    size_t cycle = 0;
    for (uint32_t ti : wf->time_indices_of(clk_ref)) {
        IWaveformBackend::SignalOffset offset;
        if (!wf->signal_offset_at(clk_ref,ti,offset) || !offset.time_match) continue;
        const std::string current_clock=wf->signal_value_str(clk_ref,offset.start,0);
        const bool rising=have_previous && is_rising_edge(previous_clock,current_clock);
        previous_clock=current_clock; have_previous=true;
        if (!rising || ti < begin_ti) continue;
        if (ti > end_ti) break;
        StreamSample sample;
        sample.cycle=cycle++; sample.time_idx=ti; sample.time=wf->time_at(ti);
        const std::string vld_bits=signal_bits_at(wf,vld_ref,ti);
        const std::string rdy_bits=rdy_ref?signal_bits_at(wf,rdy_ref,ti):"1";
        const std::string bp_bits=bp_ref?signal_bits_at(wf,bp_ref,ti):"0";
        const std::string sop_bits=sop_ref?signal_bits_at(wf,sop_ref,ti):"0";
        const std::string eop_bits=eop_ref?signal_bits_at(wf,eop_ref,ti):"0";
        sample.control_known=is_known_binary(vld_bits)&&
            is_known_binary(rdy_bits)&&is_known_binary(bp_bits)&&
            is_known_binary(sop_bits)&&is_known_binary(eop_bits);
        sample.ready_bp_conflict=rdy_ref&&bp_ref&&
            is_high(rdy_bits)&&is_high(bp_bits);
        const bool v_now=signal_is_high_at(wf,vld_ref,ti),
            v_prev=ti>0&&signal_is_high_at(wf,vld_ref,ti-1);
        bool ready_now=true,ready_prev=true;
        if (rdy_ref) {
            ready_now=signal_is_high_at(wf,rdy_ref,ti);
            ready_prev=ti>0&&signal_is_high_at(wf,rdy_ref,ti-1);
        }
        if (bp_ref) {
            sample.bp=signal_is_high_at(wf,bp_ref,ti);
            ready_now=ready_now&&!sample.bp;
            ready_prev=ready_prev&&!(ti>0&&signal_is_high_at(wf,bp_ref,ti-1));
        }
        sample.transfer=v_now&&ready_now;
        if (rdy_ref&&!bp_ref)
            sample.transfer=sample.transfer||(v_now&&!v_prev&&ready_prev)||
                (ready_now&&!ready_prev&&v_prev);
        sample.vld=sample.transfer||v_now;
        sample.rdy=rdy_ref&&(sample.transfer||ready_now);
        sample.stall=sample.vld&&!ready_now;
        sample.sop=sample.transfer&&sop_ref&&signal_is_high_at(wf,sop_ref,ti);
        sample.eop=sample.transfer&&eop_ref&&signal_is_high_at(wf,eop_ref,ti);
        if (sample.transfer) {
            sample.fields=stream_fields_at(cfg,*wf,ti,format,fields_complete);
            for (const auto& field : sample.fields.items())
                if (!field.value().value("known",false)) sample.data_known=false;
        }
        samples.push_back(std::move(sample));
    }
    return samples;
}

static bool known_u64(const Json& value, uint64_t& result) {
    if (!value.value("known",false) || !value.contains("bits")) return false;
    const std::string bits=value.at("bits");
    if (bits.size()>64) return false;
    result=0;
    for (char bit : bits) {
        if (bit!='0'&&bit!='1') return false;
        result=(result<<1)|(bit=='1'?1u:0u);
    }
    return true;
}

static bool literal_u64(const std::string& text, uint64_t& result) {
    LogicValue value;
    if (!parse_sv_literal(text,value) || !value.known || value.bits.size()>64) return false;
    result=0;
    for (char bit : value.bits) result=(result<<1)|(bit=='1'?1u:0u);
    return true;
}

static bool stream_field_matches(const Json& value, const Json& rule) {
    uint64_t actual=0;
    if (!known_u64(value,actual)) return false;
    const std::string mode=rule.at("mode");
    if (mode=="exact") {
        for (const Json& candidate : rule.at("values")) {
            uint64_t expected=0;
            if (literal_u64(candidate,expected)&&actual==expected) return true;
        }
        return false;
    }
    if (mode=="range") {
        uint64_t begin=0,end=0;
        return literal_u64(rule.at("begin"),begin)&&literal_u64(rule.at("end"),end)&&
            actual>=begin&&actual<=end;
    }
    uint64_t expected=0,mask=0;
    return literal_u64(rule.at("value"),expected)&&literal_u64(rule.at("mask"),mask)&&
        (actual&mask)==(expected&mask);
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
                {"handshake",stream_handshake(cfg)},{"packet",(!cfg.sop.empty()&&!cfg.eop.empty())?"sop/eop":"none"},
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
        return {{"ok",true},{"summary",{{"stream",name},{"handshake",stream_handshake(cfg)},
            {"packet_enabled",!cfg.sop.empty()&&!cfg.eop.empty()}}},
            {"data",{{"config",config_to_json(cfg)},{"issues",issues},
                {"validation",validation},{"semantics",{{"transfer",stream_handshake(cfg)},{"stall","enabled"}}}}}};
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
        uint32_t rdy_ref = cfg.ready.empty()?0:load_signal(wf, cfg.ready);
        uint32_t bp_ref = cfg.backpressure.empty()?0:load_signal(wf,cfg.backpressure);
        uint32_t data_ref = cfg.data.empty() ? 0 : load_signal(wf, cfg.data);

        if (clk_ref == IWaveformBackend::kInvalidSignalRef ||
            vld_ref == IWaveformBackend::kInvalidSignalRef ||
            (!cfg.ready.empty()&&rdy_ref == IWaveformBackend::kInvalidSignalRef) ||
            (!cfg.backpressure.empty()&&bp_ref == IWaveformBackend::kInvalidSignalRef)) {
            std::vector<std::string> missing;
            if (clk_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.clock);
            if (vld_ref == IWaveformBackend::kInvalidSignalRef) missing.push_back(cfg.valid);
            if (!cfg.ready.empty()&&rdy_ref == IWaveformBackend::kInvalidSignalRef)
                missing.push_back(cfg.ready);
            if (!cfg.backpressure.empty()&&bp_ref == IWaveformBackend::kInvalidSignalRef)
                missing.push_back(cfg.backpressure);
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

        const std::string query = args.at("query");
        const bool packet_enabled=!cfg.sop.empty()&&!cfg.eop.empty();
        if (!packet_enabled && (query=="first_packet"||query=="last_packet"||
            query=="packet_at"||query=="packet_window"))
            return action_error("PACKET_NOT_CONFIGURED","stream has no sop/eop packet boundaries");

        bool fields_complete=true;
        auto samples=scan_stream_samples(wf,cfg,begin_ti,end_ti,fmt,fields_complete);
        std::vector<const StreamSample*> transfers;
        size_t vld_cycles=0,stall_cycles=0,control_xz_count=0,
            data_xz_count=0,ready_bp_conflict_count=0;
        for (const auto& sample : samples) {
            if (sample.vld) ++vld_cycles;
            if (sample.stall) ++stall_cycles;
            if (sample.transfer) transfers.push_back(&sample);
            if (!sample.control_known) ++control_xz_count;
            if (sample.transfer && !sample.data_known) ++data_xz_count;
            if (sample.ready_bp_conflict) ++ready_bp_conflict_count;
        }

        auto row_json=[&](const StreamSample& sample, size_t beat_index) {
            return Json{{"cycle",sample.cycle},{"time",wf->format_time(sample.time,unit)},
                {"vld",sample.vld},{"rdy",sample.rdy},{"bp",sample.bp},
                {"sop",sample.sop},{"eop",sample.eop},{"transfer",sample.transfer},
                {"stall",sample.stall},{"beat_index",beat_index},{"fields",sample.fields}};
        };
        Json all_rows=Json::array();
        for (const auto* transfer : transfers) all_rows.push_back(row_json(*transfer,0));

        Json stalls=Json::array();
        for (size_t pos=0;pos<samples.size();) {
            if (!samples[pos].stall) { ++pos; continue; }
            const size_t first=pos;
            while (pos+1<samples.size()&&samples[pos+1].stall) ++pos;
            stalls.push_back({{"start_cycle",samples[first].cycle},
                {"end_cycle",samples[pos].cycle},
                {"start_time",wf->format_time(samples[first].time,unit)},
                {"end_time",wf->format_time(samples[pos].time,unit)},
                {"cycles",pos-first+1},{"reason","vld_without_rdy"}});
            ++pos;
        }

        Json packets=Json::array();
        std::vector<const StreamSample*> packet_beats;
        size_t packet_index=0,complete_packets=0,partial_packets=0;
        auto append_packet=[&]() {
            if (packet_beats.empty()) return;
            const bool partial_begin=!packet_beats.front()->sop,
                partial_end=!packet_beats.back()->eop;
            Json head=Json::array(),tail=Json::array();
            for (size_t index=0;index<packet_beats.size();++index) {
                Json beat{{"cycle",packet_beats[index]->cycle},
                    {"time",wf->format_time(packet_beats[index]->time,unit)},
                    {"beat_index",index},{"fields",packet_beats[index]->fields}};
                if (index<2) head.push_back(beat);
                if (index+2>=packet_beats.size()) tail.push_back(beat);
            }
            Json preview{{"head",head},{"tail",tail},{"scan_complete",true},
                {"analysis_complete",fields_complete},{"response_truncated",false},
                {"total_count",packet_beats.size()},
                {"returned_count",head.size()+tail.size()},
                {"truncation_scopes",Json::array()}};
            packets.push_back({{"packet_index",packet_index++},
                {"start_cycle",packet_beats.front()->cycle},
                {"end_cycle",packet_beats.back()->cycle},
                {"start_time",wf->format_time(packet_beats.front()->time,unit)},
                {"end_time",wf->format_time(packet_beats.back()->time,unit)},
                {"beat_count",packet_beats.size()},{"partial_begin",partial_begin},
                {"partial_end",partial_end},{"packet_stable_fields",Json::object()},
                {"packet_stable_mismatches",Json::array()},
                {"beat_fields_preview",preview},
                {"first_fields",packet_beats.front()->fields},
                {"last_fields",packet_beats.back()->fields}});
            partial_begin||partial_end?++partial_packets:++complete_packets;
            packet_beats.clear();
        };
        if (packet_enabled) {
            for (const auto* transfer : transfers) {
                if (transfer->sop && !packet_beats.empty()) append_packet();
                packet_beats.push_back(transfer);
                if (transfer->eop) append_packet();
            }
            append_packet();
        }

        const bool filter_applied=args.contains("filter");
        Json filtered_packets=Json::array();
        Json normalized_filter=Json::object();
        if (filter_applied) {
            normalized_filter=args.at("filter");
            if (!normalized_filter.contains("position")) normalized_filter["position"]="sop";
            const bool at_eop=normalized_filter.at("position")=="eop";
            for (const Json& packet : packets) {
                const Json& fields=at_eop?packet.at("last_fields"):packet.at("first_fields");
                bool matched=true;
                for (const auto& criterion : normalized_filter.at("fields").items()) {
                    if (!fields.contains(criterion.key())||
                        !stream_field_matches(fields.at(criterion.key()),criterion.value())) {
                        matched=false; break;
                    }
                }
                if (matched) filtered_packets.push_back(packet);
            }
        } else filtered_packets=packets;

        size_t total_count=0,returned_count=0;
        bool truncated=false;
        Json data=Json::object();
        if (query=="summary") total_count=packet_enabled?packets.size():transfers.size();
        else if (query=="first_transfer"||query=="last_transfer") {
            total_count=transfers.size(); returned_count=transfers.empty()?0:1;
            if (!transfers.empty()) data["row"]=row_json(
                query=="first_transfer"?*transfers.front():*transfers.back(),0);
        } else if (query=="transfer_window") {
            total_count=all_rows.size(); returned_count=std::min<size_t>(max_rows,total_count);
            Json rows=Json::array();
            for (size_t index=0;index<returned_count;++index) rows.push_back(all_rows[index]);
            data["rows"]=rows; truncated=returned_count<total_count;
            if (truncated) data["hint"]="increase line_limit to return more transfer rows";
        } else if (query=="first_stall"||query=="last_stall") {
            total_count=stalls.size(); returned_count=stalls.empty()?0:1;
            if (!stalls.empty()) data["stall"]=query=="first_stall"?stalls.front():stalls.back();
        } else if (query=="stall_window") {
            total_count=stalls.size(); returned_count=std::min<size_t>(max_rows,total_count);
            Json selected=Json::array();
            for (size_t index=0;index<returned_count;++index) selected.push_back(stalls[index]);
            data["stalls"]=selected; truncated=returned_count<total_count;
            if (truncated) data["hint"]="increase line_limit to return more stall windows";
        } else if (query=="first_packet"||query=="last_packet"||query=="packet_at") {
            total_count=filtered_packets.size(); size_t index=0;
            if (query=="last_packet"&&!filtered_packets.empty()) index=filtered_packets.size()-1;
            if (query=="packet_at") index=args.at("packet_index");
            const bool found=index<filtered_packets.size(); returned_count=found?1:0;
            data["found"]=found;
            if (found) data["packet"]=filtered_packets[index];
        } else {
            total_count=filtered_packets.size(); returned_count=std::min<size_t>(max_rows,total_count);
            Json selected=Json::array();
            for (size_t index=0;index<returned_count;++index) selected.push_back(filtered_packets[index]);
            data["packets"]=selected; truncated=returned_count<total_count;
            if (truncated) data["hint"]="increase line_limit to return more packets";
        }
        if (filter_applied) {
            data["filter"]=normalized_filter;
            data["notes"]={{"unresolved_filter_count","0"}};
        }

        Json summary{{"stream",name},{"query",query},{"sampling_mode","clock_edge"},
            {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
            {"sample_time_semantics","time is sample_time"},{"handshake",stream_handshake(cfg)},
            {"packet_enabled",packet_enabled},{"clock_edges",samples.size()},
            {"vld_cycles",vld_cycles},{"transfer_count",transfers.size()},
            {"stall_cycles",stall_cycles},{"stall_windows",stalls.size()},
            {"complete_packet_count",complete_packets},{"partial_packet_count",partial_packets},
            {"packet_count_status",packet_enabled?"exact":"not_configured"},
            {"control_xz_count",control_xz_count},{"data_xz_count",data_xz_count},
            {"ready_bp_conflict_count",ready_bp_conflict_count},
            {"packet_stable_mismatch_count",0},
            {"requested_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},
            {"scanned_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},{"filter_applied",filter_applied},
            {"scan_complete",control_xz_count==0},
            {"analysis_complete",control_xz_count==0&&data_xz_count==0&&
                fields_complete},
            {"response_truncated",truncated},{"total_count",total_count},
            {"returned_count",returned_count},
            {"truncation_scopes",[&]() {
                Json scopes=Json::array();
                if (control_xz_count||data_xz_count||!fields_complete)
                    scopes.push_back("analysis_samples");
                if (truncated) scopes.push_back("response_rows");
                return scopes;
            }()}};
        if (filter_applied) {
            summary["unresolved_filter_count"]=0;
            summary["matched_packet_count"]=filtered_packets.size();
            summary["retained_packet_count"]=filtered_packets.size();
        }
        if (cfg.edge != "negedge") summary["sample_point"] = cfg.sample_point;
        if (!transfers.empty()) {
            summary["first_transfer_time"] = wf->format_time(transfers.front()->time,unit);
            summary["last_transfer_time"] = wf->format_time(transfers.back()->time,unit);
        }
        if (!stalls.empty()) {
            summary["first_stall_time"]=stalls.front().at("start_time");
            summary["last_stall_time"]=stalls.back().at("start_time");
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

        const std::string kind=args.value("kind","transfer");
        const size_t line_limit=args.value("line_limit",16u);
        Json query_args=args;
        query_args.erase("kind"); query_args.erase("output");
        query_args["line_limit"]=line_limit;
        query_args["query"]=kind=="transfer"?"transfer_window":"packet_window";
        StreamQueryHandler query_handler;
        Json analyzed=query_handler.run({{"args",query_args}});
        if (!analyzed.value("ok",false)) return analyzed;

        Json preview;
        if (kind=="transfer") preview=analyzed.at("data").at("rows");
        else if (kind=="packet") preview=analyzed.at("data").at("packets");
        else {
            preview=Json::array();
            for (const Json& packet : analyzed.at("data").at("packets"))
                for (const Json& beat : packet.at("beat_fields_preview").at("head")) {
                    Json row{{"cycle",beat.at("cycle")},{"time",beat.at("time")},
                        {"vld",true},{"rdy",true},{"bp",false},{"sop",false},{"eop",false},
                        {"transfer",true},{"stall",false},{"beat_index",beat.at("beat_index")},
                        {"fields",beat.at("fields")}};
                    preview.push_back(std::move(row));
                }
        }

        const bool written=args.contains("output")&&args.at("output").contains("path");
        Json output_summary;
        if (written) {
            const Json output=args.at("output");
            const std::string path=output.at("path"),
                file_format=output.value("file_format","tsv"),meta_path=path+".meta.json";
            std::ofstream rows_file(path),meta_file(meta_path);
            if (!rows_file||!meta_file)
                return action_error("OUTPUT_WRITE_FAILED","cannot open stream export output");
            const char separator=file_format=="csv"?',':'\t';
            if (kind=="transfer") {
                rows_file << "cycle" << separator << "time" << separator << "data\n";
                for (const Json& row : preview) {
                    std::string data_value;
                    if (row.at("fields").contains("data"))
                        data_value=row.at("fields").at("data").at("value");
                    rows_file << row.at("cycle") << separator << row.at("time")
                              << separator << data_value << '\n';
                }
            } else {
                rows_file << "index" << separator << "record\n";
                for (size_t index=0;index<preview.size();++index)
                    rows_file << index << separator << preview[index].dump() << '\n';
            }
            meta_file << Json{{"stream",name},{"kind",kind},{"row_count",preview.size()},
                {"source","current_session_fst"}}.dump(2) << '\n';
            output_summary={{"path",path},{"meta_path",meta_path},{"file_format",file_format}};
        }

        const Json& base=analyzed.at("summary");
        Json summary;
        for (const char* key : {"stream","sampling_mode","clock","edge","sample_point",
             "sample_time_semantics","handshake","packet_enabled","clock_edges","vld_cycles",
             "transfer_count","stall_cycles","stall_windows","complete_packet_count",
             "partial_packet_count","packet_count_status","control_xz_count","data_xz_count",
             "ready_bp_conflict_count","packet_stable_mismatch_count","requested_range",
             "scanned_range","first_transfer_time","last_transfer_time","first_stall_time",
             "last_stall_time","scan_complete","analysis_complete","response_truncated",
             "total_count","returned_count","truncation_scopes"})
            if (base.contains(key)) summary[key]=base.at(key);
        summary["status"]=written?"written":"preview";
        summary["output_written"]=written;
        summary["row_count"]=preview.size();
        summary["line_limit"]=line_limit;
        summary["kind"]=kind;
        if (written) summary["output"]=output_summary;
        Json data=written?Json::object():Json{{"preview",preview}};
        return {{"ok",true},{"summary",summary},{"data",data}};
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
            {"severity","ERROR"},{"message","stream contains unresolved signals"}});
        const bool dynamic_requested = args.value("dynamic",false);
        Json dynamic = Json::object();
        bool scan_complete = false, analysis_complete = static_ok;
        if (dynamic_requested && static_ok) {
            Json query_args=args;
            query_args.erase("dynamic");
            query_args["query"]="summary";
            StreamQueryHandler query_handler;
            Json analyzed=query_handler.run({{"args",query_args}});
            if (!analyzed.value("ok",false)) return analyzed;
            const Json& base=analyzed.at("summary");
            for (const char* key : {"stream","sampling_mode","clock","edge","sample_point",
                 "sample_time_semantics","handshake","packet_enabled","clock_edges","vld_cycles",
                 "transfer_count","stall_cycles","stall_windows","complete_packet_count",
                 "partial_packet_count","packet_count_status","control_xz_count","data_xz_count",
                 "ready_bp_conflict_count","packet_stable_mismatch_count","requested_range",
                 "scanned_range","first_transfer_time","last_transfer_time","first_stall_time",
                 "last_stall_time"})
                if (base.contains(key)) dynamic[key]=base.at(key);
            scan_complete=base.at("scan_complete");
            analysis_complete=base.at("analysis_complete");
            auto add_dynamic_issue=[&](const char* counter, const char* code,
                                       const char* message) {
                if (base.value(counter,0u)>0) issues.push_back({{"code",code},
                    {"severity","WARNING"},{"message",message}});
            };
            add_dynamic_issue("control_xz_count","control_xz",
                "stream control signals contain X/Z at sampled clock edges");
            add_dynamic_issue("data_xz_count","data_xz",
                "stream transfer data contains X/Z");
            add_dynamic_issue("ready_bp_conflict_count","ready_bp_conflict",
                "stream ready and backpressure are asserted together");
            add_dynamic_issue("packet_stable_mismatch_count",
                "packet_stable_mismatch",
                "packet-stable fields changed within a packet");
        }
        const size_t total=issues.size();
        const size_t limit=args.value("line_limit",1000u);
        Json returned_issues=Json::array();
        for (size_t index=0;index<std::min(total,limit);++index)
            returned_issues.push_back(issues[index]);
        const bool response_truncated=returned_issues.size()<total;
        Json scopes=Json::array();
        if (dynamic_requested&&(!scan_complete||!analysis_complete))
            scopes.push_back("analysis_samples");
        if (response_truncated) scopes.push_back("response_issues");
        const bool ok=static_ok;
        return {{"ok",true},{"summary",{{"stream",name},{"ok",ok},
            {"static_validation_complete",true},{"dynamic_requested",dynamic_requested},
            {"scan_complete",scan_complete},{"analysis_complete",analysis_complete},
            {"response_truncated",response_truncated},{"total_count",total},
            {"returned_count",returned_issues.size()},{"truncation_scopes",scopes}}},
            {"data",{{"issues",returned_issues},{"dynamic",dynamic}}}};
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
