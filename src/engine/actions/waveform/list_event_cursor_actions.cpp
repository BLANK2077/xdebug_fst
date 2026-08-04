// list_event_cursor_actions.cpp — List, Event, Cursor, and nwave.rc.generate handlers
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/list/list_manager.h"
#include "waveform/cursor/cursor_manager.h"
#include "api/json_types.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace xdebug_fst {

// ============================================================================
// Shared helpers
// ============================================================================

/// Parse an integer from a string (or JSON number). Returns true on success.
static bool parse_time_arg(const Json& val, uint64_t& out) {
    if (val.is_string()) {
        try { out = std::stoull(val.get<std::string>()); return true; }
        catch (...) { return false; }
    }
    if (val.is_number()) {
        out = static_cast<uint64_t>(val.get<int64_t>());
        return true;
    }
    return false;
}

/// Build a standard error response.
static Json make_error(const std::string& code, const std::string& message) {
    return Json{{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}

/// Render a raw wellen bit string as a canonical logic value JSON.
static Json render_value_json(const std::string& bits, uint32_t width,
                              ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, static_cast<int>(width));
    return logic_value_json(v, fmt);
}

/// Check waveform backend is loaded; return error response if not.
/// Returns a null Json (is_null() == true) when OK.
static Json check_waveform(const char* action) {
    auto& g = engine_globals();
    if (!g.has_waveform || !g.waveform) {
        return make_error("WAVEFORM_NOT_LOADED",
                          std::string("action requires waveform file: ") + action);
    }
    return Json(); // null
}

// ============================================================================
// 1. list.create
// ============================================================================

struct ListCreateHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.create"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.create");

        std::string error;
        if (!ListManager::instance().create(name, error)) {
            // "list already exists" → LIST_EXISTS
            if (error.find("already exists") != std::string::npos)
                return make_error("LIST_EXISTS", error);
            return make_error("ACTION_FAILED", error);
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"created", true}};
        out["data"] = {{"list", {{"name", name}, {"signals", Json::array()}, {"signal_count", 0}}}};
        return out;
    }
};

// ============================================================================
// 2. list.add
// ============================================================================

struct ListAddHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.add"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.add");

        if (!ListManager::instance().exists(name))
            return make_error("LIST_NOT_FOUND", "list not found: " + name);

        // Parse signals array
        auto sigs = args.value("signals", Json::array());
        if (!sigs.is_array() || sigs.empty())
            return make_error("MISSING_FIELD", "args.signals is required and must be a non-empty array");

        std::vector<std::string> signals;
        auto* wf = engine_globals().waveform.get();

        for (size_t i = 0; i < sigs.size(); ++i) {
            if (!sigs[i].is_string())
                return make_error("INVALID_ARGUMENT",
                    "args.signals[" + std::to_string(i) + "] must be a string");
            std::string s = sigs[i].get<std::string>();
            if (s.empty())
                return make_error("INVALID_ARGUMENT",
                    "args.signals[" + std::to_string(i) + "] must be non-empty");
            // Validate signal exists in waveform
            if (wf->find_signal(s) == IWaveformBackend::kInvalidSignalRef)
                return make_error("SIGNAL_NOT_FOUND", "signal not found in waveform: " + s);
            signals.push_back(s);
        }

        std::string error;
        if (!ListManager::instance().add(name, signals, error))
            return make_error("ACTION_FAILED", error);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"added_count", static_cast<int>(signals.size())}};
        return out;
    }
};

// ============================================================================
// 3. list.delete
// ============================================================================

struct ListDeleteHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.delete"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.delete");

        if (!ListManager::instance().exists(name))
            return make_error("LIST_NOT_FOUND", "list not found: " + name);

        // Remove all signals from the list (effectively delete it)
        const SignalList* lst = ListManager::instance().get(name);
        std::string error;
        if (lst && !lst->signals.empty()) {
            if (!ListManager::instance().remove(name, lst->signals, error))
                return make_error("ACTION_FAILED", error);
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"deleted", true}};
        return out;
    }
};

// ============================================================================
// 4. list.load
// ============================================================================

struct ListLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        std::string file = args.value("file", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.load");
        if (file.empty())
            return make_error("MISSING_FIELD", "args.file is required for list.load");

        std::string error;
        if (!ListManager::instance().load(name, file, error))
            return make_error("ACTION_FAILED", error);

        const SignalList* lst = ListManager::instance().get(name);
        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"name", name},
            {"loaded", true},
            {"signal_count", lst ? static_cast<int>(lst->signals.size()) : 0},
            {"file", file}
        };
        return out;
    }
};

// ============================================================================
// 5. list.show
// ============================================================================

struct ListShowHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.show"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.show");

        const SignalList* lst = ListManager::instance().get(name);
        if (!lst)
            return make_error("LIST_NOT_FOUND", "list not found: " + name);

        Json signals_arr = Json::array();
        for (const auto& s : lst->signals)
            signals_arr.push_back(s);

        Json out;
        out["ok"] = true;
        out["data"] = {{"list", {
            {"name", lst->name},
            {"signals", signals_arr},
            {"signal_count", static_cast<int>(lst->signals.size())}
        }}};
        return out;
    }
};

// ============================================================================
// 6. list.validate
// ============================================================================

struct ListValidateHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.validate"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.validate");

        const SignalList* lst = ListManager::instance().get(name);
        if (!lst)
            return make_error("LIST_NOT_FOUND", "list not found: " + name);

        auto* wf = engine_globals().waveform.get();
        Json missing = Json::array();
        int valid_count = 0, missing_count = 0;

        for (const auto& s : lst->signals) {
            if (wf->find_signal(s)) {
                ++valid_count;
            } else {
                ++missing_count;
                missing.push_back(s);
            }
        }

        Json out;
        out["ok"] = true;
        out["data"] = {
            {"valid", missing_count == 0},
            {"missing", missing},
            {"valid_count", valid_count},
            {"missing_count", missing_count}
        };
        return out;
    }
};

// ============================================================================
// 7. list.export
// ============================================================================

struct ListExportHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.export"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.export");

        const SignalList* lst = ListManager::instance().get(name);
        if (!lst)
            return make_error("LIST_NOT_FOUND", "list not found: " + name);

        auto* wf = engine_globals().waveform.get();
        uint64_t begin = 0, end = wf->max_time();
        if (args.contains("begin") && !parse_time_arg(args["begin"], begin))
            return make_error("INVALID_TIME", "args.begin must be an integer");
        if (args.contains("end") && !parse_time_arg(args["end"], end))
            return make_error("INVALID_TIME", "args.end must be an integer");

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        Json exports = Json::array();
        for (const auto& sig_name : lst->signals) {
            uint32_t ref = wf->find_signal(sig_name);
            if (ref == IWaveformBackend::kInvalidSignalRef) continue;
            if (!wf->is_loaded(ref)) wf->load_signals({ref});

            IWaveformBackend::SignalInfo info;
            wf->signal_info(ref, info);

            Json changes = Json::array();
            std::vector<uint32_t> indices = wf->time_indices_of(ref);
            for (uint32_t ti : indices) {
                uint64_t t = wf->time_at(ti);
                if (t < begin || t > end) continue;
                IWaveformBackend::SignalOffset off;
                if (!wf->signal_offset_at(ref, ti, off)) continue;
                if (!off.time_match) continue;
                std::string bits = wf->signal_value_str(ref, off.start, 0);
                changes.push_back({
                    {"time", t},
                    {"value", render_value_json(bits, info.width, fmt)}
                });
            }
            exports.push_back({
                {"signal", sig_name},
                {"changes", changes}
            });
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"exported_signals", static_cast<int>(exports.size())}};
        out["data"] = {{"exports", exports}};
        return out;
    }
};

// ============================================================================
// 8. list.first_change
// ============================================================================

struct ListFirstChangeHandler : public EngineActionHandler {
    const char* action_name() const override { return "list.first_change"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for list.first_change");

        const SignalList* lst = ListManager::instance().get(name);
        if (!lst)
            return make_error("LIST_NOT_FOUND", "list not found: " + name);

        auto* wf = engine_globals().waveform.get();
        uint64_t begin = 0, end = wf->max_time();
        if (args.contains("begin") && !parse_time_arg(args["begin"], begin))
            return make_error("INVALID_TIME", "args.begin must be an integer");
        if (args.contains("end") && !parse_time_arg(args["end"], end))
            return make_error("INVALID_TIME", "args.end must be an integer");

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        Json first_changes = Json::array();
        bool all_no_change = true;

        for (const auto& sig_name : lst->signals) {
            uint32_t ref = wf->find_signal(sig_name);
            if (ref == IWaveformBackend::kInvalidSignalRef) continue;
            if (!wf->is_loaded(ref)) wf->load_signals({ref});

            IWaveformBackend::SignalInfo info;
            wf->signal_info(ref, info);

            std::vector<uint32_t> indices = wf->time_indices_of(ref);
            bool found = false;
            for (uint32_t ti : indices) {
                uint64_t t = wf->time_at(ti);
                if (t < begin || t > end) continue;
                IWaveformBackend::SignalOffset off;
                if (!wf->signal_offset_at(ref, ti, off)) continue;
                if (!off.time_match) continue;
                std::string bits = wf->signal_value_str(ref, off.start, 0);
                first_changes.push_back({
                    {"signal", sig_name},
                    {"time", t},
                    {"time_idx", ti},
                    {"value", render_value_json(bits, info.width, fmt)}
                });
                all_no_change = false;
                found = true;
                break;
            }
            if (!found) {
                // No change found in range – still include with null data
                first_changes.push_back({
                    {"signal", sig_name},
                    {"time", Json(nullptr)},
                    {"time_idx", Json(nullptr)},
                    {"value", Json(nullptr)}
                });
            }
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"range", {{"begin", begin}, {"end", end}}}};
        out["data"] = {
            {"first_changes", first_changes},
            {"all_no_change", all_no_change}
        };
        return out;
    }
};

// ============================================================================
// 9. event.config.list
// ============================================================================

struct EventConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "event.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        (void)req;
        Json configs = Json::array();
        configs.push_back({{"name", "rising_edge"}, {"description", "Rising edge: 0 → 1 transition"}});
        configs.push_back({{"name", "falling_edge"}, {"description", "Falling edge: 1 → 0 transition"}});
        configs.push_back({{"name", "any_change"}, {"description", "Any value change on the signal"}});
        configs.push_back({{"name", "value_equals"}, {"description", "Signal value equals a specified value"}});
        configs.push_back({{"name", "x_occurrence"}, {"description", "Signal has X (unknown) bits"}});

        Json out;
        out["ok"] = true;
        out["data"] = {{"configs", configs}};
        return out;
    }
};

// ============================================================================
// 10. event.config.load
// ============================================================================

struct EventConfigLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "event.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for event.config.load");

        // Built-in config details
        Json config;
        if (name == "rising_edge") {
            config = {{"name", "rising_edge"}, {"description", "Rising edge: 0 → 1 transition"},
                      {"kind", "edge"}, {"params", {{"direction", "rising"}}}};
        } else if (name == "falling_edge") {
            config = {{"name", "falling_edge"}, {"description", "Falling edge: 1 → 0 transition"},
                      {"kind", "edge"}, {"params", {{"direction", "falling"}}}};
        } else if (name == "any_change") {
            config = {{"name", "any_change"}, {"description", "Any value change on the signal"},
                      {"kind", "change"}, {"params", Json::object()}};
        } else if (name == "value_equals") {
            config = {{"name", "value_equals"}, {"description", "Signal value equals a specified value"},
                      {"kind", "value"}, {"params", {{"value", "any"}}}};
        } else if (name == "x_occurrence") {
            config = {{"name", "x_occurrence"}, {"description", "Signal has X (unknown) bits"},
                      {"kind", "x"}, {"params", Json::object()}};
        } else {
            return make_error("CONFIG_NOT_FOUND", "event config not found: " + name);
        }

        Json out;
        out["ok"] = true;
        out["data"] = {{"config", config}};
        return out;
    }
};

// ============================================================================
// Shared event detection logic (used by event.find and event.export)
// ============================================================================

/// Detect whether a change from `prev_bits` to `cur_bits` matches the event kind.
/// `prev_bits` may be empty (no previous value).
static bool matches_event(const std::string& event_kind, const std::string& prev_bits,
                          const std::string& cur_bits, const std::string& target_value) {
    if (event_kind == "any_change") {
        if (prev_bits.empty()) return true; // first change
        return prev_bits != cur_bits;
    }
    if (event_kind == "rising_edge") {
        if (prev_bits.empty()) return false;
        return prev_bits.back() == '0' && cur_bits.back() == '1';
    }
    if (event_kind == "falling_edge") {
        if (prev_bits.empty()) return false;
        return prev_bits.back() == '1' && cur_bits.back() == '0';
    }
    if (event_kind == "value_equals") {
        if (target_value.empty()) return false;
        // Compare with SV-literal support (e.g. "8'h05")
        LogicValue exp_v;
        if (parse_sv_literal(target_value, exp_v)) {
            return cur_bits == exp_v.bits;
        }
        return cur_bits == target_value;
    }
    if (event_kind == "x_occurrence") {
        for (char c : cur_bits) {
            if (c == 'x' || c == 'X') return true;
        }
        return false;
    }
    // Default: any_change
    return prev_bits != cur_bits;
}

/// Run event detection on a signal in [begin_ti, end_ti], append to events array.
static void find_events_on_signal(IWaveformBackend* wf, uint32_t ref,
                                  const std::string& event_kind,
                                  uint32_t begin_ti, uint32_t end_ti,
                                  const std::string& target_value,
                                  ValueRenderFormat fmt,
                                  Json& events_arr, int& found_count,
                                  int max_results) {
    if (!wf->is_loaded(ref)) wf->load_signals({ref});

    IWaveformBackend::SignalInfo info;
    wf->signal_info(ref, info);

    std::vector<uint32_t> indices = wf->time_indices_of(ref);
    std::string prev_bits;
    bool have_prev = false;

    for (uint32_t ti : indices) {
        if (ti < begin_ti) {
            // track the last value before begin_ti for edge detection
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(ref, ti, off) && off.time_match) {
                prev_bits = wf->signal_value_str(ref, off.start, 0);
                have_prev = true;
            }
            continue;
        }
        if (ti > end_ti) break;
        if (max_results >= 0 && found_count >= max_results) break;

        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(ref, ti, off)) continue;
        if (!off.time_match) continue;

        std::string cur_bits = wf->signal_value_str(ref, off.start, 0);
        std::string prev_for_check = have_prev ? prev_bits : std::string();

        if (matches_event(event_kind, prev_for_check, cur_bits, target_value)) {
            uint64_t t = wf->time_at(ti);
            events_arr.push_back({
                {"time", t},
                {"time_idx", ti},
                {"kind", event_kind},
                {"value", render_value_json(cur_bits, info.width, fmt)}
            });
            ++found_count;
        }

        prev_bits = cur_bits;
        have_prev = true;
    }
}

// ============================================================================
// 11. event.find
// ============================================================================

struct EventFindHandler : public EngineActionHandler {
    const char* action_name() const override { return "event.find"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string signal = args.value("signal", "");
        if (signal.empty())
            return make_error("MISSING_FIELD", "args.signal is required for event.find");

        std::string event_kind = args.value("event", "any_change");

        auto* wf = engine_globals().waveform.get();
        uint32_t ref = wf->find_signal(signal);
        if (ref == IWaveformBackend::kInvalidSignalRef)
            return make_error("SIGNAL_NOT_FOUND", "signal not found in waveform: " + signal);

        uint64_t begin = 0, end = wf->max_time();
        if (args.contains("begin") && !parse_time_arg(args["begin"], begin))
            return make_error("INVALID_TIME", "args.begin must be an integer");
        if (args.contains("end") && !parse_time_arg(args["end"], end))
            return make_error("INVALID_TIME", "args.end must be an integer");

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        std::string target_value = args.value("value", "");

        Json events_arr = Json::array();
        int found_count = 0;
        int max_results = args.value("max_results", 1000);

        find_events_on_signal(wf, ref, event_kind, begin_ti, end_ti,
                              target_value, fmt, events_arr, found_count, max_results);

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", signal},
            {"event", event_kind},
            {"event_count", found_count},
            {"range", {{"begin", begin}, {"end", end}}}
        };
        out["data"] = {{"events", events_arr}};
        return out;
    }
};

// ============================================================================
// 12. event.export
// ============================================================================

struct EventExportHandler : public EngineActionHandler {
    const char* action_name() const override { return "event.export"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string signal = args.value("signal", "");
        if (signal.empty())
            return make_error("MISSING_FIELD", "args.signal is required for event.export");

        std::string event_kind = args.value("event", "any_change");

        auto* wf = engine_globals().waveform.get();
        uint32_t ref = wf->find_signal(signal);
        if (ref == IWaveformBackend::kInvalidSignalRef)
            return make_error("SIGNAL_NOT_FOUND", "signal not found in waveform: " + signal);

        uint64_t begin = 0, end = wf->max_time();
        if (args.contains("begin") && !parse_time_arg(args["begin"], begin))
            return make_error("INVALID_TIME", "args.begin must be an integer");
        if (args.contains("end") && !parse_time_arg(args["end"], end))
            return make_error("INVALID_TIME", "args.end must be an integer");

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        std::string target_value = args.value("value", "");

        Json events_arr = Json::array();
        int found_count = 0;
        find_events_on_signal(wf, ref, event_kind, begin_ti, end_ti,
                              target_value, fmt, events_arr, found_count, -1);

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", signal},
            {"event", event_kind},
            {"event_count", found_count},
            {"range", {{"begin", begin}, {"end", end}}}
        };
        out["data"] = {{"events", events_arr}};
        return out;
    }
};

// ============================================================================
// 13. waveform.cursor.set
// ============================================================================

struct CursorSetHandler : public EngineActionHandler {
    const char* action_name() const override { return "waveform.cursor.set"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for waveform.cursor.set");

        uint64_t time = 0;
        if (!args.contains("time"))
            return make_error("MISSING_FIELD", "args.time is required for waveform.cursor.set");
        if (!parse_time_arg(args["time"], time))
            return make_error("INVALID_TIME", "args.time must be an integer");

        CursorManager::instance().set(name, time);

        Json out;
        out["ok"] = true;
        out["data"] = {{"cursor", {{"name", name}, {"time", time}}}};
        return out;
    }
};

// ============================================================================
// 14. waveform.cursor.get
// ============================================================================

struct CursorGetHandler : public EngineActionHandler {
    const char* action_name() const override { return "waveform.cursor.get"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for waveform.cursor.get");

        WaveformCursor c;
        if (!CursorManager::instance().get(name, c))
            return make_error("CURSOR_NOT_FOUND", "cursor not found: " + name);

        Json out;
        out["ok"] = true;
        out["data"] = {{"cursor", {{"name", c.name}, {"time", c.time}}}};
        return out;
    }
};

// ============================================================================
// 15. waveform.cursor.list
// ============================================================================

struct CursorListHandler : public EngineActionHandler {
    const char* action_name() const override { return "waveform.cursor.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;
        (void)req;

        auto cursors = CursorManager::instance().all();
        Json arr = Json::array();
        for (const auto& c : cursors)
            arr.push_back({{"name", c.name}, {"time", c.time}});

        Json out;
        out["ok"] = true;
        out["data"] = {{"cursors", arr}};
        return out;
    }
};

// ============================================================================
// 16. waveform.cursor.delete
// ============================================================================

struct CursorDeleteHandler : public EngineActionHandler {
    const char* action_name() const override { return "waveform.cursor.delete"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for waveform.cursor.delete");

        if (!CursorManager::instance().remove(name))
            return make_error("CURSOR_NOT_FOUND", "cursor not found: " + name);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"deleted", true}};
        return out;
    }
};

// ============================================================================
// 17. waveform.cursor.use
// ============================================================================

struct CursorUseHandler : public EngineActionHandler {
    const char* action_name() const override { return "waveform.cursor.use"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return make_error("MISSING_FIELD", "args.name is required for waveform.cursor.use");

        uint64_t time = 0;
        if (!CursorManager::instance().use(name, time))
            return make_error("CURSOR_NOT_FOUND", "cursor not found: " + name);

        Json out;
        out["ok"] = true;
        out["data"] = {{"cursor", {{"name", name}, {"time", time}}}};
        return out;
    }
};

// ============================================================================
// 18. nwave.rc.generate
// ============================================================================

struct NwaveRcGenerateHandler : public EngineActionHandler {
    const char* action_name() const override { return "nwave.rc.generate"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string signal = args.value("signal", "");
        if (signal.empty())
            return make_error("MISSING_FIELD", "args.signal is required for nwave.rc.generate");

        auto* wf = engine_globals().waveform.get();
        uint32_t ref = wf->find_signal(signal);
        if (ref == IWaveformBackend::kInvalidSignalRef)
            return make_error("SIGNAL_NOT_FOUND", "signal not found in waveform: " + signal);

        uint64_t begin = 0, end = wf->max_time();
        if (args.contains("begin") && !parse_time_arg(args["begin"], begin))
            return make_error("INVALID_TIME", "args.begin must be an integer");
        if (args.contains("end") && !parse_time_arg(args["end"], end))
            return make_error("INVALID_TIME", "args.end must be an integer");

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);

        // If a clock signal is provided, generate recovery/removal constraints
        std::string clock_signal = args.value("clock", "");
        uint32_t clk_ref = 0;
        if (!clock_signal.empty()) {
            clk_ref = wf->find_signal(clock_signal);
            if (clk_ref == IWaveformBackend::kInvalidSignalRef)
                return make_error("SIGNAL_NOT_FOUND", "clock signal not found: " + clock_signal);
        }

        Json constraints = Json::array();

        if (clk_ref) {
            // Recovery/removal mode: detect signal changes relative to clock edges
            if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});

            std::vector<uint32_t> sig_indices = wf->time_indices_of(ref);
            std::vector<uint32_t> clk_indices = wf->time_indices_of(clk_ref);

            // For each signal change, find the nearest clock edge and classify
            for (uint32_t si : sig_indices) {
                if (si < begin_ti || si > end_ti) continue;

                IWaveformBackend::SignalOffset sig_off;
                if (!wf->signal_offset_at(ref, si, sig_off)) continue;
                if (!sig_off.time_match) continue;

                uint64_t sig_time = wf->time_at(si);
                std::string sig_bits = wf->signal_value_str(ref, sig_off.start, 0);

                // Find nearest clock edge before and after this change
                uint32_t prev_clk_ti = 0, next_clk_ti = 0;
                bool have_prev = false, have_next = false;

                for (uint32_t ci : clk_indices) {
                    if (ci < si) { prev_clk_ti = ci; have_prev = true; }
                    if (ci >= si && !have_next) { next_clk_ti = ci; have_next = true; }
                }

                // Recovery: time from signal change to next clock edge
                if (have_next && next_clk_ti >= si) {
                    uint64_t rec_time = wf->time_at(next_clk_ti) - sig_time;
                    constraints.push_back({
                        {"signal", signal},
                        {"clock", clock_signal},
                        {"edge", "next_rising"},
                        {"time", rec_time},
                        {"time_idx", si},
                        {"kind", "recovery"}
                    });
                }

                // Removal: time from previous clock edge to signal change
                if (have_prev && prev_clk_ti <= si) {
                    uint64_t rem_time = sig_time - wf->time_at(prev_clk_ti);
                    constraints.push_back({
                        {"signal", signal},
                        {"clock", clock_signal},
                        {"edge", "prev_rising"},
                        {"time", rem_time},
                        {"time_idx", si},
                        {"kind", "removal"}
                    });
                }
            }
        } else {
            // No clock: just output all change points
            std::vector<uint32_t> indices = wf->time_indices_of(ref);
            for (uint32_t ti : indices) {
                if (ti < begin_ti || ti > end_ti) continue;

                IWaveformBackend::SignalOffset off;
                if (!wf->signal_offset_at(ref, ti, off)) continue;
                if (!off.time_match) continue;

                uint64_t t = wf->time_at(ti);
                std::string bits = wf->signal_value_str(ref, off.start, 0);

                constraints.push_back({
                    {"signal", signal},
                    {"time", t},
                    {"time_idx", ti},
                    {"kind", "change"},
                    {"value", render_value_json(bits, info.width, fmt)}
                });
            }
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", signal},
            {"constraint_count", static_cast<int>(constraints.size())},
            {"range", {{"begin", begin}, {"end", end}}}
        };
        if (!clock_signal.empty())
            out["summary"]["clock"] = clock_signal;
        out["data"] = {{"constraints", constraints}};
        return out;
    }
};

// ============================================================================
// Factory functions
// ============================================================================

std::unique_ptr<EngineActionHandler> make_list_create_handler()     { return std::make_unique<ListCreateHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_add_handler()        { return std::make_unique<ListAddHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_delete_handler()     { return std::make_unique<ListDeleteHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_load_handler()       { return std::make_unique<ListLoadHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_show_handler()       { return std::make_unique<ListShowHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_validate_handler()   { return std::make_unique<ListValidateHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_export_handler()     { return std::make_unique<ListExportHandler>(); }
std::unique_ptr<EngineActionHandler> make_list_first_change_handler() { return std::make_unique<ListFirstChangeHandler>(); }
std::unique_ptr<EngineActionHandler> make_event_config_list_handler() { return std::make_unique<EventConfigListHandler>(); }
std::unique_ptr<EngineActionHandler> make_event_config_load_handler() { return std::make_unique<EventConfigLoadHandler>(); }
std::unique_ptr<EngineActionHandler> make_event_find_handler()      { return std::make_unique<EventFindHandler>(); }
std::unique_ptr<EngineActionHandler> make_event_export_handler()    { return std::make_unique<EventExportHandler>(); }
std::unique_ptr<EngineActionHandler> make_cursor_set_handler()      { return std::make_unique<CursorSetHandler>(); }
std::unique_ptr<EngineActionHandler> make_cursor_get_handler()      { return std::make_unique<CursorGetHandler>(); }
std::unique_ptr<EngineActionHandler> make_cursor_list_handler()     { return std::make_unique<CursorListHandler>(); }
std::unique_ptr<EngineActionHandler> make_cursor_delete_handler()   { return std::make_unique<CursorDeleteHandler>(); }
std::unique_ptr<EngineActionHandler> make_cursor_use_handler()      { return std::make_unique<CursorUseHandler>(); }
std::unique_ptr<EngineActionHandler> make_nwave_rc_generate_handler() { return std::make_unique<NwaveRcGenerateHandler>(); }

} // namespace xdebug_fst
