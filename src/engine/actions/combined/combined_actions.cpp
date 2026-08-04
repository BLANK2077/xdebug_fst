// combined_actions.cpp — trace.active_driver, trace.active_driver_chain, trace.x_origin (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"
#include "core/value/logic_value.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── shared helpers ──

static Json render_val_json(const std::string& bits, int width, ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, width);
    return logic_value_json(v, fmt);
}

static bool has_x_bit(const std::string& bits) {
    return bits.find('x') != std::string::npos || bits.find('X') != std::string::npos;
}

// Read a signal's value string at a given time (absolute).
// Returns empty string on failure.
static std::string read_signal_value_at_time(IWaveformBackend* wf, const std::string& sig_name,
                                              uint64_t time, uint32_t* out_width = nullptr) {
    uint32_t ref = wf->find_signal(sig_name);
    if (ref == IWaveformBackend::kInvalidSignalRef) return "";
    if (!wf->is_loaded(ref)) wf->load_signals({ref});
    uint32_t ti = wf->time_idx_of(time);
    IWaveformBackend::SignalOffset off;
    if (!wf->signal_offset_at(ref, ti, off)) return "";
    if (out_width) {
        IWaveformBackend::SignalInfo info;
        if (wf->signal_info(ref, info)) *out_width = info.width;
    }
    return wf->signal_value_str(ref, off.start, 0);
}

// Determine termination reason from design context
static std::string termination_reason(IDesignBackend* design, int sig_idx,
                                       bool has_drivers, bool hit_max_depth) {
    if (hit_max_depth) return "max_depth";
    if (!has_drivers) {
        if (sig_idx >= 0) {
            int dir = design->signal_direction(sig_idx);
            if (dir == 1) return "primary_input";   // input
            const char* ty = design->signal_type(sig_idx);
            if (ty && std::string(ty) == "port" && dir == 2) return "input_port";
        }
        return "no_driver";
    }
    if (sig_idx >= 0) {
        int dir = design->signal_direction(sig_idx);
        if (dir == 1) return "primary_input";
    }
    return "no_driver";
}

// ── trace.active_driver ──

struct TraceActiveDriverHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.active_driver"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design || !g.design) {
            return Json{{"ok", false},
                        {"error", {{"code", "DESIGN_NOT_LOADED"},
                                   {"message", "action requires design database: trace.active_driver"}}}};
        }
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: trace.active_driver"}}}};
        }

        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string ts = args.value("time", "");
        if (sig.empty() || ts.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal and args.time are required"}}}};
        }

        uint64_t t = 0;
        try { t = std::stoull(ts); }
        catch (...) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_TIME"},
                                   {"message", "args.time must be an integer"}}}};
        }

        int depth_limit = 20;
        if (args.contains("depth") && args["depth"].is_number())
            depth_limit = args["depth"].get<int>();

        auto* wf = g.waveform.get();
        auto* design = g.design.get();

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        Json chain = Json::array();
        std::set<std::string> visited;
        std::string cur_sig = sig;
        int depth = 0;
        std::string term_reason = "no_driver";

        while (depth < depth_limit) {
            if (visited.count(cur_sig)) {
                term_reason = "max_depth";  // loop detected
                break;
            }
            visited.insert(cur_sig);

            int idx = design->resolve(cur_sig.c_str());
            uint32_t width = 0;
            std::string bits = read_signal_value_at_time(wf, cur_sig, t, &width);

            Json drivers_arr = Json::array();
            bool has_any_driver = false;
            if (idx >= 0) {
                std::vector<IDesignBackend::DriverRecord> drivers;
                design->trace_driver(idx, drivers);
                for (auto& d : drivers) {
                    has_any_driver = true;
                    drivers_arr.push_back({
                        {"src_signal", d.src_signal >= 0 ? Json(design->signal_name(d.src_signal)) : Json(nullptr)},
                        {"kind", d.kind},
                        {"file", d.file},
                        {"line", d.line}
                    });
                }
            }

            Json entry;
            entry["signal"] = cur_sig;
            entry["index"] = idx;
            entry["type"] = (idx >= 0 && design->signal_type(idx)) ? Json(design->signal_type(idx)) : Json(nullptr);
            entry["value"] = bits.empty() ? Json(nullptr) : render_val_json(bits, static_cast<int>(width), fmt);
            entry["drivers"] = drivers_arr;
            chain.push_back(entry);

            // Find the active driver: first driver with src_signal whose value matches
            bool found_next = false;
            if (idx >= 0) {
                std::vector<IDesignBackend::DriverRecord> drivers;
                design->trace_driver(idx, drivers);
                for (auto& d : drivers) {
                    if (d.src_signal < 0) continue;
                    std::string up_name = design->signal_name(d.src_signal);
                    if (!up_name.empty() && up_name[0]) {
                        std::string up_bits = read_signal_value_at_time(wf, up_name, t);
                        if (!up_bits.empty()) {
                            cur_sig = up_name;
                            found_next = true;
                            break;
                        }
                    }
                }
            }

            if (!found_next) {
                term_reason = termination_reason(design, idx, has_any_driver, false);
                break;
            }
            depth++;
        }

        if (depth >= depth_limit) term_reason = "max_depth";

        return Json{
            {"ok", true},
            {"summary", {{"signal", sig}, {"time", t}, {"depth", depth},
                         {"termination_reason", term_reason}}},
            {"data", {{"chain", chain}}}
        };
    }
};

// ── trace.active_driver_chain ──

struct TraceActiveDriverChainHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.active_driver_chain"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design || !g.design) {
            return Json{{"ok", false},
                        {"error", {{"code", "DESIGN_NOT_LOADED"},
                                   {"message", "action requires design database: trace.active_driver_chain"}}}};
        }
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: trace.active_driver_chain"}}}};
        }

        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string ts = args.value("time", "");
        if (sig.empty() || ts.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal and args.time are required"}}}};
        }

        uint64_t t = 0;
        try { t = std::stoull(ts); }
        catch (...) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_TIME"},
                                   {"message", "args.time must be an integer"}}}};
        }

        int max_depth = 20;
        if (args.contains("max_depth") && args["max_depth"].is_number())
            max_depth = args["max_depth"].get<int>();

        auto* wf = g.waveform.get();
        auto* design = g.design.get();

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        Json chain = Json::array();
        std::set<std::string> visited;
        std::string cur_sig = sig;
        int depth = 0;

        while (depth < max_depth) {
            if (visited.count(cur_sig)) break;
            visited.insert(cur_sig);

            int idx = design->resolve(cur_sig.c_str());
            uint32_t width = 0;
            std::string bits = read_signal_value_at_time(wf, cur_sig, t, &width);

            // Get the first driver info for this signal
            std::string driver_kind;
            std::string driver_file;
            int driver_line = 0;
            int next_src = -1;

            if (idx >= 0) {
                std::vector<IDesignBackend::DriverRecord> drivers;
                design->trace_driver(idx, drivers);
                // Find first driver that has a readable upstream signal
                for (auto& d : drivers) {
                    if (d.src_signal >= 0) {
                        std::string up_name = design->signal_name(d.src_signal);
                        if (!up_name.empty() && up_name[0]) {
                            std::string up_bits = read_signal_value_at_time(wf, up_name, t);
                            if (!up_bits.empty()) {
                                driver_kind = d.kind;
                                driver_file = d.file;
                                driver_line = d.line;
                                next_src = d.src_signal;
                                break;
                            }
                        }
                    }
                    if (driver_kind.empty() && !d.kind.empty()) {
                        driver_kind = d.kind;
                        driver_file = d.file;
                        driver_line = d.line;
                    }
                }
            }

            Json entry;
            entry["signal"] = cur_sig;
            entry["value"] = bits.empty() ? Json(nullptr) : render_val_json(bits, static_cast<int>(width), fmt);
            entry["driver_kind"] = driver_kind.empty() ? Json(nullptr) : Json(driver_kind);
            entry["file"] = driver_file.empty() ? Json(nullptr) : Json(driver_file);
            entry["line"] = driver_line > 0 ? Json(driver_line) : Json(nullptr);
            chain.push_back(entry);

            if (next_src < 0) break;
            cur_sig = design->signal_name(next_src);
            depth++;
        }

        return Json{
            {"ok", true},
            {"summary", {{"signal", sig}, {"time", t}, {"depth", depth}}},
            {"data", {{"chain", chain}}}
        };
    }
};

// ── trace.x_origin ──

struct TraceXOriginHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.x_origin"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design || !g.design) {
            return Json{{"ok", false},
                        {"error", {{"code", "DESIGN_NOT_LOADED"},
                                   {"message", "action requires design database: trace.x_origin"}}}};
        }
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: trace.x_origin"}}}};
        }

        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string ts = args.value("time", "");
        if (sig.empty() || ts.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal and args.time are required"}}}};
        }

        uint64_t t = 0;
        try { t = std::stoull(ts); }
        catch (...) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_TIME"},
                                   {"message", "args.time must be an integer"}}}};
        }

        int max_depth = 20;
        if (args.contains("max_depth") && args["max_depth"].is_number())
            max_depth = args["max_depth"].get<int>();

        auto* wf = g.waveform.get();
        auto* design = g.design.get();

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        // Step 1: read signal value at requested time
        uint32_t ref = wf->find_signal(sig);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            return Json{{"ok", false},
                        {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                   {"message", "signal not found: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        uint32_t query_ti = wf->time_idx_of(t);
        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(ref, query_ti, off)) {
            return Json{{"ok", false},
                        {"error", {{"code", "VALUE_NOT_AVAILABLE"},
                                   {"message", "no value at requested time"}}}};
        }
        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);
        std::string bits = wf->signal_value_str(ref, off.start, 0);

        // Step 2: if no x, return directly
        if (!has_x_bit(bits)) {
            return Json{
                {"ok", true},
                {"data", {{"is_x", false},
                          {"value", render_val_json(bits, static_cast<int>(info.width), fmt)}}}
            };
        }

        // Step 3: find x origin — walk time_indices_of backwards
        std::vector<uint32_t> ti_vec = wf->time_indices_of(ref);
        uint32_t origin_ti = query_ti;
        uint64_t origin_time = t;
        std::string origin_bits = bits;

        // Find the last time_idx <= query_ti whose value has x
        // Walk backwards to find the FIRST time where x appears (origin)
        for (auto it = ti_vec.rbegin(); it != ti_vec.rend(); ++it) {
            if (*it > query_ti) continue;
            IWaveformBackend::SignalOffset o2;
            if (!wf->signal_offset_at(ref, *it, o2)) continue;
            std::string b2 = wf->signal_value_str(ref, o2.start, 0);
            if (has_x_bit(b2)) {
                origin_ti = *it;
                origin_time = wf->time_at(origin_ti);
                origin_bits = b2;
            } else {
                // Found the last non-x before x region; stop
                break;
            }
        }

        // Step 4: build propagation chain by following driver chain from origin
        Json prop_chain = Json::array();
        std::set<std::string> visited;
        std::string cur_sig = sig;
        uint64_t cur_time = origin_time;
        uint32_t cur_ti = origin_ti;
        std::string cur_bits = origin_bits;
        int depth = 0;
        std::string term_reason = "no_driver";

        while (depth < max_depth) {
            if (visited.count(cur_sig)) break;
            visited.insert(cur_sig);

            Json hop;
            hop["signal"] = cur_sig;
            hop["value"] = render_val_json(cur_bits, static_cast<int>(info.width), fmt);
            hop["time"] = cur_time;
            hop["time_idx"] = cur_ti;
            prop_chain.push_back(hop);

            // Look for upstream x source via design drivers
            int idx = design->resolve(cur_sig.c_str());
            bool found_upstream_x = false;
            bool has_any_driver = false;

            if (idx >= 0) {
                std::vector<IDesignBackend::DriverRecord> drivers;
                design->trace_driver(idx, drivers);
                for (auto& d : drivers) {
                    has_any_driver = true;
                    if (d.src_signal < 0) continue;
                    std::string up_name = design->signal_name(d.src_signal);
                    if (up_name.empty() || !up_name[0]) continue;
                    // Read upstream signal at same time
                    uint32_t up_ref = wf->find_signal(up_name);
                    if (up_ref == IWaveformBackend::kInvalidSignalRef) continue;
                    if (!wf->is_loaded(up_ref)) wf->load_signals({up_ref});
                    IWaveformBackend::SignalOffset up_off;
                    if (!wf->signal_offset_at(up_ref, cur_ti, up_off)) continue;
                    std::string up_bits = wf->signal_value_str(up_ref, up_off.start, 0);
                    if (has_x_bit(up_bits)) {
                        cur_sig = up_name;
                        cur_bits = up_bits;
                        found_upstream_x = true;
                        break;
                    }
                }
            }

            if (!found_upstream_x) {
                if (idx >= 0) {
                    int dir = design->signal_direction(idx);
                    if (dir == 1) term_reason = "primary_input";
                    else if (!has_any_driver) term_reason = "no_driver";
                    else term_reason = "driver_x";
                } else {
                    term_reason = "no_driver";
                }
                break;
            }
            depth++;
        }

        if (depth >= max_depth) term_reason = "max_depth";

        return Json{
            {"ok", true},
            {"summary", {{"termination_reason", term_reason}}},
            {"data", {
                {"is_x", true},
                {"origin_time", origin_time},
                {"origin_time_idx", origin_ti},
                {"propagation_chain", prop_chain}
            }}
        };
    }
};

std::unique_ptr<EngineActionHandler> make_trace_active_driver_handler() {
    return std::make_unique<TraceActiveDriverHandler>();
}
std::unique_ptr<EngineActionHandler> make_trace_active_driver_chain_handler() {
    return std::make_unique<TraceActiveDriverChainHandler>();
}
std::unique_ptr<EngineActionHandler> make_trace_x_origin_handler() {
    return std::make_unique<TraceXOriginHandler>();
}

} // namespace xdebug_fst
