// stream_actions.cpp — stream.config.list/get/load, stream.describe/query/export/validate
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── StreamConfig (xdebug-fst simplified config) ──

struct StreamConfig {
    std::string name;
    std::string clock;   // clock signal name
    std::string valid;   // valid signal name
    std::string ready;   // ready signal name
    std::string data;    // data signal name (optional)
    uint64_t start = 0;  // window begin (0 = full)
    uint64_t end = 0;    // window end (0 = full)
    bool active_high = true;
};

static Json config_to_json(const StreamConfig& cfg) {
    Json j;
    j["name"] = cfg.name;
    j["clock"] = cfg.clock;
    j["valid"] = cfg.valid;
    j["ready"] = cfg.ready;
    if (!cfg.data.empty()) j["data"] = cfg.data;
    j["start"] = cfg.start;
    j["end"] = cfg.end;
    j["active_high"] = cfg.active_high;
    return j;
}

// ── In-process config store ──

static std::map<std::string, StreamConfig>& config_store() {
    static std::map<std::string, StreamConfig> store;
    if (store.empty()) {
        // Built-in "default" config with reasonable signal name defaults
        StreamConfig def;
        def.name = "default";
        def.clock = "clk";
        def.valid = "valid";
        def.ready = "ready";
        store["default"] = def;
    }
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

    Json run(const Json& /*req*/) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto& store = config_store();
        Json arr = Json::array();
        for (const auto& kv : store) {
            arr.push_back({{"name", kv.second.name}});
        }

        return Json{{"ok", true},
                    {"data", {{"configs", arr}}}};
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
        std::string name = args.value("name", "default");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        return Json{{"ok", true},
                    {"data", {{"config", config_to_json(cfg)}}}};
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
        auto cfg_json = args.value("config", Json::object());

        // Validation: name is required
        std::string name = cfg_json.value("name", "");
        if (name.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "VALIDATION_FAILED"},
                                   {"message", "config.name is required"}}}};
        }

        StreamConfig cfg;
        cfg.name = name;
        cfg.clock = cfg_json.value("clock", "");
        cfg.valid = cfg_json.value("valid", "");
        cfg.ready = cfg_json.value("ready", "");
        cfg.data = cfg_json.value("data", "");

        if (cfg_json.contains("start")) {
            if (cfg_json["start"].is_number()) cfg.start = cfg_json["start"].get<uint64_t>();
            else if (cfg_json["start"].is_string()) {
                try { cfg.start = std::stoull(cfg_json["start"].get<std::string>()); }
                catch (...) { cfg.start = 0; }
            }
        }
        if (cfg_json.contains("end")) {
            if (cfg_json["end"].is_number()) cfg.end = cfg_json["end"].get<uint64_t>();
            else if (cfg_json["end"].is_string()) {
                try { cfg.end = std::stoull(cfg_json["end"].get<std::string>()); }
                catch (...) { cfg.end = 0; }
            }
        }
        cfg.active_high = cfg_json.value("active_high", true);

        // Validate: clock, valid, ready are required
        if (cfg.clock.empty() || cfg.valid.empty() || cfg.ready.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "VALIDATION_FAILED"},
                                   {"message", "config.clock, config.valid, and config.ready are required"}}}};
        }

        // Store
        config_store()[name] = cfg;

        return Json{{"ok", true},
                    {"data", {{"config", config_to_json(cfg)}}}};
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
        std::string name = args.value("name", "default");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        // Load signals
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

        // Resolve window
        uint64_t begin_time = cfg.start;
        uint64_t end_time = cfg.end > 0 ? cfg.end : wf->max_time();
        if (args.contains("begin")) {
            if (args["begin"].is_number()) begin_time = args["begin"].get<uint64_t>();
        }
        if (args.contains("end")) {
            if (args["end"].is_number()) end_time = args["end"].get<uint64_t>();
        }

        uint32_t begin_ti = wf->time_idx_of(begin_time);
        uint32_t end_ti = wf->time_idx_of(end_time);

        uint64_t vhc = 0, rhc = 0, hc = 0;
        count_handshake_stats(wf, clk_ref, vld_ref, rdy_ref,
                              begin_ti, end_ti, vhc, rhc, hc);

        IWaveformBackend::SignalInfo info;
        int data_width = -1;
        if (data_ref && wf->signal_info(data_ref, info)) {
            data_width = static_cast<int>(info.width);
        }

        Json desc;
        desc["valid_high_count"] = vhc;
        desc["ready_high_count"] = rhc;
        desc["handshake_count"] = hc;
        if (data_width >= 0) desc["data_width"] = data_width;
        desc["window"] = {{"begin", begin_time}, {"end", end_time}};

        return Json{{"ok", true},
                    {"data", {{"description", desc}}}};
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
        std::string name = args.value("name", "default");

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

        uint64_t begin_time = cfg.start;
        uint64_t end_time = cfg.end > 0 ? cfg.end : wf->max_time();
        if (args.contains("begin")) {
            if (args["begin"].is_number()) begin_time = args["begin"].get<uint64_t>();
            else if (args["begin"].is_string()) {
                try { begin_time = std::stoull(args["begin"].get<std::string>()); }
                catch (...) {}
            }
        }
        if (args.contains("end")) {
            if (args["end"].is_number()) end_time = args["end"].get<uint64_t>();
            else if (args["end"].is_string()) {
                try { end_time = std::stoull(args["end"].get<std::string>()); }
                catch (...) {}
            }
        }

        int max_rows = 1000;
        if (args.contains("max_rows")) {
            if (args["max_rows"].is_number()) max_rows = args["max_rows"].get<int>();
        }

        uint32_t begin_ti = wf->time_idx_of(begin_time);
        uint32_t end_ti = wf->time_idx_of(end_time);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        IWaveformBackend::SignalInfo info;
        int data_width = 0;
        if (data_ref && wf->signal_info(data_ref, info)) {
            data_width = static_cast<int>(info.width);
        }

        auto events = scan_handshakes(wf, clk_ref, vld_ref, rdy_ref, data_ref,
                                      begin_ti, end_ti, max_rows);

        Json handshakes = Json::array();
        for (auto& ev : events) {
            Json item;
            item["time"] = ev.time;
            item["time_idx"] = ev.time_idx;
            if (data_ref && !ev.data_bits.empty()) {
                LogicValue lv = logic_value_from_bits(ev.data_bits, data_width);
                item["data"] = logic_value_json(lv, fmt);
            }
            handshakes.push_back(item);
        }

        // Count total handshakes for summary
        uint64_t vhc = 0, rhc = 0, hc = 0;
        count_handshake_stats(wf, clk_ref, vld_ref, rdy_ref,
                              begin_ti, end_ti, vhc, rhc, hc);

        bool truncated = (int)events.size() >= max_rows && hc > (uint64_t)events.size();

        Json summary;
        summary["handshake_count"] = hc;
        summary["truncated"] = truncated;

        return Json{{"ok", true},
                    {"summary", summary},
                    {"data", {{"handshakes", handshakes}}}};
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
        std::string name = args.value("name", "default");

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

        uint64_t begin_time = cfg.start;
        uint64_t end_time = cfg.end > 0 ? cfg.end : wf->max_time();
        if (args.contains("begin")) {
            if (args["begin"].is_number()) begin_time = args["begin"].get<uint64_t>();
            else if (args["begin"].is_string()) {
                try { begin_time = std::stoull(args["begin"].get<std::string>()); }
                catch (...) {}
            }
        }
        if (args.contains("end")) {
            if (args["end"].is_number()) end_time = args["end"].get<uint64_t>();
            else if (args["end"].is_string()) {
                try { end_time = std::stoull(args["end"].get<std::string>()); }
                catch (...) {}
            }
        }

        uint32_t begin_ti = wf->time_idx_of(begin_time);
        uint32_t end_ti = wf->time_idx_of(end_time);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        IWaveformBackend::SignalInfo info;
        int data_width = 0;
        if (data_ref && wf->signal_info(data_ref, info)) {
            data_width = static_cast<int>(info.width);
        }

        // No row limit for export
        auto events = scan_handshakes(wf, clk_ref, vld_ref, rdy_ref, data_ref,
                                      begin_ti, end_ti, 0);

        Json handshakes = Json::array();
        for (auto& ev : events) {
            Json item;
            item["time"] = ev.time;
            item["time_idx"] = ev.time_idx;
            if (data_ref && !ev.data_bits.empty()) {
                LogicValue lv = logic_value_from_bits(ev.data_bits, data_width);
                item["data"] = logic_value_json(lv, fmt);
            }
            handshakes.push_back(item);
        }

        Json summary;
        summary["handshake_count"] = handshakes.size();

        return Json{{"ok", true},
                    {"summary", summary},
                    {"data", {{"handshakes", handshakes}}}};
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
        std::string name = args.value("name", "default");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        std::vector<std::string> missing;
        bool all_found = true;

        auto check_signal = [&](const std::string& sig_name, const std::string& /*role*/) {
            if (sig_name.empty()) return;
            uint32_t ref = wf->find_signal(sig_name);
            if (ref == IWaveformBackend::kInvalidSignalRef) {
                missing.push_back(sig_name);
                all_found = false;
            }
        };

        check_signal(cfg.clock, "clock");
        check_signal(cfg.valid, "valid");
        check_signal(cfg.ready, "ready");
        check_signal(cfg.data, "data");

        Json arr = Json::array();
        for (auto& m : missing) arr.push_back(m);

        return Json{{"ok", true},
                    {"data", {{"valid", all_found},
                               {"missing_signals", arr}}}};
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
