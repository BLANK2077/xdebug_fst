// value_at.cpp — value.at and signal.changes actions (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "api/json_types.h"
#include <algorithm>

namespace xdebug_fst {

// ── Shared helpers ──

// Render a raw wellen bit string as a canonical logic value JSON.
static Json render_value_json(const std::string& bits, uint32_t width,
                              ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, static_cast<int>(width));
    return logic_value_json(v, fmt);
}

// ── value.at ──

struct ValueAtHandler : public EngineActionHandler {
    const char* action_name() const override { return "value.at"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: value.at"}}}};
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
                        {"error", {{"code", "INVALID_TIME"}, {"message", "time must be an integer"}}}};
        }

        auto* wf = g.waveform.get();
        uint32_t ref = wf->find_signal(sig);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            return Json{{"ok", false},
                        {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                   {"message", "signal not found in waveform: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        uint32_t ti = wf->time_idx_of(t);
        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(ref, ti, off)) {
            return Json{{"ok", false},
                        {"error", {{"code", "VALUE_NOT_AVAILABLE"},
                                   {"message", "no value available for signal at time"}}}};
        }

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);

        std::string bits = wf->signal_value_str(ref, off.start, 0);
        Json value = render_value_json(bits, info.width, fmt);

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", sig},
            {"time", t},
            {"time_idx", ti},
            {"time_match", off.time_match},
            {"has_next", off.has_next},
        };
        if (off.has_next) out["summary"]["next_time_idx"] = off.next_idx;
        out["data"] = value;
        return out;
    }
};

// ── signal.changes ──

struct SignalChangesHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.changes"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: signal.changes"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        if (sig.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal is required"}}}};
        }
        auto* wf = g.waveform.get();
        uint32_t ref = wf->find_signal(sig);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            return Json{{"ok", false},
                        {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                   {"message", "signal not found in waveform: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        uint64_t begin = 0, end = wf->max_time();
        const Json time_range = args.value("time_range", Json::object());
        if (time_range.contains("begin")) begin = std::stoull(time_range["begin"].get<std::string>());
        if (time_range.contains("end")) end = std::stoull(time_range["end"].get<std::string>());

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        Json changes = Json::array();
        // O(N) iteration over the signal's change-time indices
        std::vector<uint32_t> indices = wf->time_indices_of(ref);
        int limit = args.value("line_limit", 1000);
        for (uint32_t ti : indices) {
            if ((int)changes.size() >= limit) break;
            uint64_t t = wf->time_at(ti);
            if (t < begin || t > end) continue;
            IWaveformBackend::SignalOffset off;
            if (!wf->signal_offset_at(ref, ti, off)) continue;
            if (!off.time_match) continue;
            std::string bits = wf->signal_value_str(ref, off.start, 0);
            changes.push_back({
                {"time", t},
                {"time_idx", ti},
                {"value", render_value_json(bits, info.width, fmt)},
            });
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", sig},
            {"change_count", changes.size()},
            {"range", {{"begin", begin}, {"end", end}}},
            {"truncated", (int)changes.size() >= limit},
        };
        out["data"] = {{"changes", changes}};
        return out;
    }
};

std::unique_ptr<EngineActionHandler> make_value_at_handler() { return std::make_unique<ValueAtHandler>(); }
std::unique_ptr<EngineActionHandler> make_signal_changes_handler() { return std::make_unique<SignalChangesHandler>(); }

} // namespace xdebug_fst
