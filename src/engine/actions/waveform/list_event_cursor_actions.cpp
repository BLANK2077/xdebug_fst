// list_event_cursor_actions.cpp — List, Event, Cursor, and nwave.rc.generate handlers
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/list/list_manager.h"
#include "waveform/cursor/cursor_manager.h"
#include "waveform/time_contract.h"
#include "waveform/expr/expr_eval.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <map>
#include <sstream>
#include <set>
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

static Json render_typed_value_json(const IWaveformBackend::WaveformValue& value,
                                    uint32_t width, ValueRenderFormat fmt) {
    if (value.kind == IWaveformBackend::ValueKind::BitVector)
        return render_value_json(value.text, width, fmt);
    if (value.kind == IWaveformBackend::ValueKind::Real)
        return {{"value", std::to_string(value.real)}, {"known", true}};
    return {{"value", value.kind == IWaveformBackend::ValueKind::Event
                          ? "event" : value.text}, {"known", true}};
}

static bool waveform_values_equal(const IWaveformBackend::WaveformValue& lhs,
                                  const IWaveformBackend::WaveformValue& rhs) {
    if (lhs.kind != rhs.kind) return false;
    if (lhs.kind == IWaveformBackend::ValueKind::Real) return lhs.real == rhs.real;
    return lhs.text == rhs.text;
}

static std::string export_stem(const std::string& signal, size_t index) {
    std::string stem = std::to_string(index) + "_";
    for (char c : signal)
        stem.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return stem;
}

static void write_u64_le(std::ofstream& output, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        output.put(static_cast<char>((value >> shift) & 0xffu));
}

static bool write_logic_row(std::ofstream& output, uint64_t time,
                            const std::string& bits, size_t word_count) {
    write_u64_le(output, time);
    std::vector<uint64_t> values(word_count, 0), known(word_count, 0);
    for (size_t bit = 0; bit < bits.size(); ++bit) {
        const char state = bits[bits.size() - bit - 1];
        const size_t word = bit / 64;
        const uint64_t mask = uint64_t{1} << (bit % 64);
        if (state == '1' || state == 'H' || state == 'h') values[word] |= mask;
        if (state == '0' || state == '1' || state == 'L' || state == 'l' ||
            state == 'H' || state == 'h') known[word] |= mask;
    }
    for (uint64_t word : values) write_u64_le(output, word);
    for (uint64_t word : known) write_u64_le(output, word);
    return static_cast<bool>(output);
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

static Json cursor_metadata(const WaveformCursor& cursor) {
    return {{"note", cursor.note}, {"origin", cursor.origin},
            {"clock", cursor.clock}};
}

static bool cursor_render_unit(const Json& args, TimeRenderUnit& unit,
                               Json& error) {
    std::string message;
    if (parse_time_render_unit(args.value("render_time_unit", "ns"), unit,
                               message)) return true;
    error = make_error("INVALID_TIME_UNIT", message);
    return false;
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

        std::vector<std::string> signals;
        for (const auto& item : args.value("signals", Json::array())) {
            const std::string signal = item.get<std::string>();
            if (engine_globals().waveform->find_signal(signal) ==
                IWaveformBackend::kInvalidSignalRef)
                return make_error("SIGNAL_NOT_FOUND",
                                  "signal not found in waveform: " + signal);
            signals.push_back(signal);
        }
        std::string error;
        if (!ListManager::instance().create(name, error)) {
            // "list already exists" → LIST_EXISTS
            if (error.find("already exists") != std::string::npos)
                return make_error("LIST_EXISTS", error);
            return make_error("ACTION_FAILED", error);
        }
        if (!signals.empty() && !ListManager::instance().add(name, signals, error))
            return make_error("ACTION_FAILED", error);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"status", "created"},
                          {"created", true}, {"signal_count", signals.size()}};
        out["data"] = {{"signals", signals}};
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

        const std::string signal = args.at("signal").get<std::string>();
        auto* wf = engine_globals().waveform.get();
        if (wf->find_signal(signal) == IWaveformBackend::kInvalidSignalRef)
            return make_error("SIGNAL_NOT_FOUND",
                              "signal not found in waveform: " + signal);

        std::string error;
        if (!ListManager::instance().add(name, {signal}, error))
            return make_error("ACTION_FAILED", error);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"signal", signal},
                          {"status", "added"}, {"added", true}};
        out["data"] = Json::object();
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

        const SignalList* list = ListManager::instance().get(name);
        std::string removed;
        if (args.contains("signal")) {
            removed = args.at("signal").get<std::string>();
            if (std::find(list->signals.begin(), list->signals.end(), removed) ==
                list->signals.end())
                return make_error("SIGNAL_NOT_FOUND", "signal not found in list: " + removed);
        } else {
            const size_t index = args.at("index").get<size_t>();
            if (index == 0 || index > list->signals.size())
                return make_error("INDEX_OUT_OF_RANGE", "list index is out of range");
            removed = list->signals[index - 1];
        }
        std::string error;
        if (!ListManager::instance().remove(name, {removed}, error))
            return make_error("ACTION_FAILED", error);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"deleted", true}, {"removed", removed}};
        out["data"] = Json::object();
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
        Json config;
        if (args.contains("config")) {
            config = args.at("config");
        } else {
            std::ifstream input(args.at("config_path").get<std::string>());
            if (!input) return make_error("CONFIG_READ_FAILED", "cannot open list config");
            try { input >> config; }
            catch (const std::exception& ex) {
                return make_error("CONFIG_PARSE_FAILED", ex.what());
            }
        }
        const std::string mode = args.value("mode", "replace");
        auto* wf = engine_globals().waveform.get();
        for (const auto& item : config.at("lists")) {
            for (const auto& signal_json : item.at("signals")) {
                const std::string signal = signal_json;
                if (!wf->find_signal(signal))
                    return make_error("SIGNAL_NOT_FOUND",
                                      "signal not found in waveform: " + signal);
            }
        }
        Json names = Json::array();
        Json validation = Json::array();
        std::string error;
        for (const auto& item : config.at("lists")) {
            const std::string name = item.at("name");
            std::vector<std::string> signals = item.at("signals");
            if (mode == "replace" && ListManager::instance().exists(name))
                ListManager::instance().erase(name);
            if (!ListManager::instance().exists(name) &&
                !ListManager::instance().create(name, error))
                return make_error("ACTION_FAILED", error);
            if (!ListManager::instance().add(name, signals, error))
                return make_error("ACTION_FAILED", error);
            names.push_back(name);
            Json signal_rows = Json::array();
            for (const auto& signal : signals)
                signal_rows.push_back({{"signal", signal}, {"status", "ok"}});
            validation.push_back({{"name", name}, {"status", "ok"},
                                  {"signals", signal_rows}});
        }
        Json recommended = Json::array({
            {{"action", "value.at"}, {"purpose", "读取命名列表在指定时间的值"}},
            {{"action", "list.show"}, {"purpose", "显示信号列表内容"}},
            {{"action", "list.validate"}, {"purpose", "验证列表信号存在性"}},
            {{"action", "list.first_change"}, {"purpose", "查找窗口内首次差异"}},
            {{"action", "list.export"}, {"purpose", "导出列表数据"}}
        });
        Json out;
        out["ok"] = true;
        out["summary"] = {{"loaded", names.size()}, {"mode", mode}};
        out["data"] = {{"lists", names}, {"validation", validation},
                       {"recommended_actions", recommended}};
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
        size_t index = 1;
        for (const auto& s : lst->signals)
            signals_arr.push_back({{"index", index++}, {"signal", s}});

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", lst->name},
                          {"signal_count", lst->signals.size()}};
        out["data"] = {{"signals", signals_arr}};
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
        Json signals = Json::array();

        for (const auto& s : lst->signals) {
            if (wf->find_signal(s) != IWaveformBackend::kInvalidSignalRef) {
                signals.push_back({{"signal", s}, {"status", "ok"}});
            } else {
                return make_error("SIGNAL_NOT_FOUND",
                                  "signal not found in waveform: " + s);
            }
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"all_found", true}};
        out["data"] = {{"signals", signals}};
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
        uint64_t begin = wf->min_time(), end = wf->max_time();
        const Json range = args.value("time_range", Json::object());
        std::string time_error;
        if (range.contains("begin") &&
            !wf->parse_time(range.at("begin"), begin, time_error))
            return make_error("INVALID_TIME", time_error);
        if (range.contains("end") &&
            !wf->parse_time(range.at("end"), end, time_error, true))
            return make_error("INVALID_TIME", time_error);
        if (begin > end) return make_error("TIME_RANGE_INVALID", "end is before begin");
        TimeRenderUnit unit;
        Json unit_error;
        if (!cursor_render_unit(args, unit, unit_error)) return unit_error;
        const std::string rendered_begin = wf->format_time(begin, unit);
        const std::string rendered_end = wf->format_time(end, unit);
        const size_t total = lst->signals.size();
        if (!args.contains("output") ||
            !args.at("output").contains("path")) {
            const int line_limit = args.value("line_limit", 16);
            const size_t returned = std::min(total, static_cast<size_t>(line_limit));
            Json signals = Json::array();
            for (size_t i = 0; i < returned; ++i)
                signals.push_back({{"index", i}, {"signal", lst->signals[i]}});
            const bool truncated = returned < total;
            return {{"ok", true}, {"summary", {
                {"name", name}, {"row_count", 0}, {"format", "u64bin.v1"},
                {"begin", rendered_begin}, {"end", rendered_end},
                {"status", "preview"}, {"output_written", false},
                {"line_limit", line_limit}, {"scan_complete", true},
                {"analysis_complete", true}, {"response_truncated", truncated},
                {"total_count", total}, {"returned_count", returned},
                {"truncation_scopes", truncated
                    ? Json::array({"response_signals"}) : Json::array()}}},
                {"data", {{"signals", signals}}}};
        }

        const std::filesystem::path output_dir =
            args.at("output").at("path").get<std::string>();
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_dir, filesystem_error);
        if (filesystem_error)
            return make_error("EXPORT_FAILED", filesystem_error.message());
        Json manifest{{"version", 1}, {"format", "u64bin.v1"},
                      {"list", name}, {"begin", rendered_begin},
                      {"end", rendered_end},
                      {"time_unit", "waveform_tick"},
                      {"row_layout", "uint64_le: time_tick, value_words, known_mask_words"},
                      {"signals", Json::array()}};
        size_t row_count = 0;
        for (size_t i = 0; i < total; ++i) {
            const std::string& signal = lst->signals[i];
            const uint32_t ref = wf->find_signal(signal);
            if (!ref) return make_error("SIGNAL_NOT_FOUND",
                                       "signal not found in waveform: " + signal);
            wf->load_signals({ref});
            IWaveformBackend::SignalInfo info;
            wf->signal_info(ref, info);
            if (info.encoding != IWaveformBackend::ValueKind::BitVector)
                return make_error("UNSUPPORTED_VALUE_TYPE",
                                  "u64bin requires a bit-vector signal: " + signal);
            const size_t words = std::max<size_t>(1, (info.width + 63) / 64);
            const std::string filename = export_stem(signal, i) + ".u64bin";
            std::ofstream output(output_dir / filename,
                                 std::ios::binary | std::ios::trunc);
            if (!output) return make_error("EXPORT_FAILED",
                                           "cannot create export file: " + filename);
            size_t rows = 0;
            const uint32_t begin_ti = wf->time_idx_of(begin);
            IWaveformBackend::SampledValue baseline;
            if (wf->sampled_value_at(ref, begin_ti,
                    IWaveformBackend::ObservationPoint::Raw, baseline) &&
                write_logic_row(output, begin, baseline.value.text, words)) ++rows;
            for (uint32_t ti : wf->time_indices_of(ref)) {
                const uint64_t time = wf->time_at(ti);
                if (time <= begin || time > end) continue;
                IWaveformBackend::SampledValue sampled;
                if (!wf->sampled_value_at(ref, ti,
                        IWaveformBackend::ObservationPoint::Raw, sampled)) continue;
                if (!write_logic_row(output, time, sampled.value.text, words))
                    return make_error("EXPORT_FAILED", "failed to write: " + filename);
                ++rows;
            }
            manifest["signals"].push_back({{"index", i}, {"signal", signal},
                {"file", filename}, {"row_count", rows}, {"width", info.width},
                {"word_count", words}, {"columns", 1 + words * 2}});
            row_count += rows;
        }
        manifest["signal_count"] = total;
        manifest["row_count"] = row_count;
        const std::filesystem::path manifest_path = output_dir / "manifest.json";
        std::ofstream manifest_output(manifest_path, std::ios::trunc);
        manifest_output << manifest.dump(2) << '\n';
        if (!manifest_output) return make_error("EXPORT_FAILED", "cannot write manifest");
        return {{"ok", true}, {"summary", {
            {"name", name}, {"row_count", row_count}, {"format", "u64bin.v1"},
            {"begin", rendered_begin}, {"end", rendered_end},
            {"status", "written"}, {"output_written", true},
            {"output", {{"path", output_dir.string()},
                        {"manifest_path", manifest_path.string()}}},
            {"scan_complete", true}, {"analysis_complete", true},
            {"response_truncated", false}, {"total_count", total},
            {"returned_count", total}, {"truncation_scopes", Json::array()}}},
            {"data", Json::object()}};
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
        const Json range = args.at("time_range");
        uint64_t begin = wf->min_time(), end = wf->max_time();
        std::string time_error;
        if (range.contains("begin") &&
            !wf->parse_time(range.at("begin"), begin, time_error))
            return make_error("INVALID_TIME", time_error);
        if (range.contains("end") &&
            !wf->parse_time(range.at("end"), end, time_error, true))
            return make_error("INVALID_TIME", time_error);
        if (begin > end) return make_error("INVALID_TIME_RANGE", "begin exceeds end");
        TimeRenderUnit unit;
        Json unit_error;
        if (!cursor_render_unit(args, unit, unit_error)) return unit_error;
        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);

        struct Candidate {
            std::string signal;
            uint32_t ref = 0;
            uint64_t time = 0;
            IWaveformBackend::WaveformValue before;
            IWaveformBackend::WaveformValue after;
            uint32_t width = 0;
        };
        std::vector<Candidate> candidates;
        uint64_t first_time = end;
        bool found_any = false;
        const uint32_t begin_ti = wf->time_idx_of(begin);
        for (const auto& signal : lst->signals) {
            const uint32_t ref = wf->find_signal(signal);
            if (!ref) return make_error("SIGNAL_NOT_FOUND",
                                       "signal not found in waveform: " + signal);
            wf->load_signals({ref});
            IWaveformBackend::SignalInfo info;
            wf->signal_info(ref, info);
            IWaveformBackend::SampledValue baseline;
            if (!wf->sampled_value_at(ref, begin_ti,
                                      IWaveformBackend::ObservationPoint::Raw,
                                      baseline)) continue;
            for (uint32_t ti : wf->time_indices_of(ref)) {
                const uint64_t time = wf->time_at(ti);
                if (time <= begin || time > end) continue;
                IWaveformBackend::SampledValue after;
                if (!wf->sampled_value_at(ref, ti,
                        IWaveformBackend::ObservationPoint::Raw, after)) continue;
                if (waveform_values_equal(baseline.value, after.value)) continue;
                found_any = true;
                first_time = std::min(first_time, time);
                candidates.push_back({signal, ref, time, baseline.value,
                                      after.value, info.width});
                break;
            }
        }
        Json changed = Json::array();
        for (const auto& candidate : candidates) {
            if (candidate.time != first_time) continue;
            changed.push_back({
                {"signal", candidate.signal},
                {"before_time", wf->format_time(begin, unit)},
                {"change_time", wf->format_time(candidate.time, unit)},
                {"before", render_typed_value_json(candidate.before,
                                                    candidate.width, fmt)},
                {"after", render_typed_value_json(candidate.after,
                                                   candidate.width, fmt)}});
        }
        Json summary{{"name", name}, {"diff_found", found_any},
                     {"diff_time", found_any ? Json(wf->format_time(first_time, unit))
                                             : Json(nullptr)},
                     {"changed_signal_count", changed.size()}};
        return {{"ok", true}, {"summary", summary},
                {"data", {{"changed_signals", changed}}}};
    }
};

// ============================================================================
// 9. event.config.list
// ============================================================================

static std::map<std::string, Json>& event_configs() {
    static std::map<std::string, Json> configs;
    return configs;
}

static bool normalize_event_config(const std::string& name, const Json& input,
                                   Json& config, std::string& error) {
    if (!input.is_object() || !input.contains("clock") ||
        !input.contains("signals") || !input.at("signals").is_object() ||
        input.at("signals").empty()) {
        error = "event config requires clock and non-empty signals";
        return false;
    }
    static const std::set<std::string> allowed{
        "clock", "edge", "sample_point", "reset", "signals", "fields"};
    for (auto it = input.begin(); it != input.end(); ++it) {
        if (!allowed.count(it.key())) {
            error = "unknown event config field: " + it.key();
            return false;
        }
    }
    auto* wf = engine_globals().waveform.get();
    const std::string clock = input.at("clock");
    if (!wf->find_signal(clock)) {
        error = "clock signal not found: " + clock;
        return false;
    }
    Json signals = Json::object();
    for (auto it = input.at("signals").begin();
         it != input.at("signals").end(); ++it) {
        if (it.key().empty() || !it.value().is_string() ||
            it.value().get<std::string>().empty()) {
            error = "event signal aliases and paths must be non-empty strings";
            return false;
        }
        const std::string path = it.value();
        if (!wf->find_signal(path)) {
            error = "signal not found in waveform: " + path;
            return false;
        }
        signals[it.key()] = path;
    }
    Json fields = Json::object();
    const Json field_input = input.value("fields", Json::object());
    for (auto it = field_input.begin(); it != field_input.end(); ++it) {
        if (!it.value().is_object() || !it.value().contains("signal") ||
            !it.value().contains("left") || !it.value().contains("right")) {
            error = "event field must contain signal, left, and right: " + it.key();
            return false;
        }
        const std::string alias = it.value().at("signal");
        if (!signals.contains(alias)) {
            error = "field references unknown signal alias: " + alias;
            return false;
        }
        fields[it.key()] = {{"signal", alias}, {"left", it.value().at("left")},
                            {"right", it.value().at("right")}};
    }
    config = {{"name", name}, {"clock", clock},
              {"edge", input.value("edge", "negedge")},
              {"signals", signals}, {"fields", fields}};
    if (input.contains("sample_point"))
        config["sample_point"] = input.at("sample_point");
    if (input.contains("reset")) {
        const Json reset = input.at("reset");
        if (!reset.is_object() || !reset.contains("signal") ||
            !reset.contains("polarity") ||
            !wf->find_signal(reset.at("signal").get<std::string>())) {
            error = "invalid event reset definition";
            return false;
        }
        config["reset"] = reset;
    }
    return true;
}

struct EventConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "event.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        const Json args = req.value("args", Json::object());
        if (args.contains("name")) {
            const std::string name = args.at("name");
            auto found = event_configs().find(name);
            if (found == event_configs().end())
                return make_error("CONFIG_NOT_FOUND", "event config not found: " + name);
            return {{"ok", true}, {"summary", {{"status", "found"}}},
                    {"data", {{"config", found->second}}}};
        }
        const size_t total = event_configs().size();
        const size_t limit = args.value("line_limit", 16u);
        const size_t returned = std::min(total, limit);
        Json names = Json::array();
        size_t index = 0;
        for (const auto& [name, config] : event_configs()) {
            (void)config;
            if (index++ >= returned) break;
            names.push_back(name);
        }
        const bool truncated = returned < total;
        return {{"ok", true}, {"summary", {
            {"scan_complete", true}, {"analysis_complete", true},
            {"response_truncated", truncated}, {"total_count", total},
            {"returned_count", returned}, {"truncation_scopes", truncated
                ? Json::array({"response_events"}) : Json::array()}}},
            {"data", {{"events", names}}}};
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

        std::ifstream input(args.at("config_path").get<std::string>());
        if (!input) return make_error("CONFIG_READ_FAILED", "cannot open event config");
        Json document;
        try { input >> document; }
        catch (const std::exception& ex) {
            return make_error("CONFIG_PARSE_FAILED", ex.what());
        }
        Json config;
        std::string error;
        if (!normalize_event_config(name, document, config, error))
            return make_error("CONFIG_INVALID", error);
        event_configs()[name] = config;
        return {{"ok", true}, {"summary", {{"status", "loaded"}}},
                {"data", {{"config", config}}}};
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

static bool bind_event_expression(ExprNode* node, const Json& signals,
                                  const Json& fields, std::string& error) {
    if (!node) return false;
    if (node->kind == ExprNode::Kind::Signal ||
        node->kind == ExprNode::Kind::Slice) {
        if (signals.contains(node->signal)) {
            node->signal = signals.at(node->signal).get<std::string>();
        } else if (fields.contains(node->signal)) {
            const Json field = fields.at(node->signal);
            node->signal = signals.at(field.at("signal").get<std::string>());
            node->kind = ExprNode::Kind::Slice;
            node->msb = field.at("left");
            node->lsb = field.at("right");
        } else {
            error = "expression references unknown event alias: " + node->signal;
            return false;
        }
    }
    if (node->left && !bind_event_expression(node->left, signals, fields, error))
        return false;
    if (node->right && !bind_event_expression(node->right, signals, fields, error))
        return false;
    return true;
}

static Json event_sampling_contract(const std::string& edge,
                                    const std::string& requested_point) {
    const bool negedge = edge == "negedge";
    const std::string effective_point = negedge ? "" :
        (requested_point.empty() ? "before" : requested_point);
    Json result{{"requested", {{"edge", edge}, {"sample_point",
        requested_point.empty() ? Json(nullptr) : Json(requested_point)}}},
        {"effective", {{"edge", edge}, {"sample_point",
        effective_point.empty() ? Json(nullptr) : Json(effective_point)}}},
        {"sample_point_applied", !negedge},
        {"sample_point_ignored_for_negedge", negedge && !requested_point.empty()}};
    if (negedge && !requested_point.empty())
        result["sample_point_not_applied_reason"] =
            "negedge keeps the established current-value sampling semantics";
    return result;
}

static Json run_event_find(const Json& args) {
    auto* wf = engine_globals().waveform.get();
    Json config;
    const bool named = args.contains("name");
    if (named) {
        auto found = event_configs().find(args.at("name").get<std::string>());
        if (found == event_configs().end())
            return make_error("CONFIG_NOT_FOUND", "event config not found");
        config = found->second;
    } else {
        config = {{"clock", args.at("clock")},
                  {"edge", args.value("edge", "negedge")},
                  {"signals", args.at("signals")}, {"fields", Json::object()}};
        if (args.contains("sample_point")) config["sample_point"] = args.at("sample_point");
        if (args.contains("reset")) config["reset"] = args.at("reset");
    }
    const std::string clock = config.at("clock");
    const uint32_t clock_ref = wf->find_signal(clock);
    if (!clock_ref) return make_error("CLOCK_NOT_FOUND", "clock signal not found: " + clock);
    wf->load_signals({clock_ref});
    const Json signals = config.at("signals");
    const Json fields = config.value("fields", Json::object());
    for (auto it = signals.begin(); it != signals.end(); ++it) {
        const uint32_t ref = wf->find_signal(it.value().get<std::string>());
        if (!ref) return make_error("SIGNAL_NOT_FOUND", "event signal not found");
        wf->load_signals({ref});
    }
    std::string parse_error;
    std::unique_ptr<ExprNode> expression(parse_expression(args.at("expr"), parse_error));
    if (!expression || !bind_event_expression(expression.get(), signals, fields,
                                               parse_error))
        return make_error("EXPRESSION_INVALID", parse_error);
    uint64_t begin = wf->min_time(), end = wf->max_time();
    const Json range = args.value("time_range", Json::object());
    if (range.contains("begin") && !wf->parse_time(range.at("begin"), begin, parse_error))
        return make_error("INVALID_TIME", parse_error);
    if (range.contains("end") && !wf->parse_time(range.at("end"), end, parse_error, true))
        return make_error("INVALID_TIME", parse_error);
    if (begin > end) return make_error("TIME_RANGE_INVALID", "end is before begin");
    TimeRenderUnit unit;
    Json unit_error;
    if (!cursor_render_unit(args, unit, unit_error)) return unit_error;
    ValueRenderFormat format = ValueRenderFormat::Hex;
    parse_value_render_format(args.value("value_format", "hex"), format);
    const std::string edge = config.value("edge", "negedge");
    const std::string requested_point = config.value("sample_point", "");
    const IWaveformBackend::ObservationPoint point = edge == "negedge"
        ? IWaveformBackend::ObservationPoint::Raw
        : (requested_point == "after" ? IWaveformBackend::ObservationPoint::After
                                       : IWaveformBackend::ObservationPoint::Before);
    const std::string mode = args.value("mode", "first");
    const size_t line_limit = args.value("line_limit", 16u);
    const size_t max_samples = args.value("max_samples",
                                          std::numeric_limits<size_t>::max());
    Json all_events = Json::array();
    size_t sample_count = 0, total_count = 0;
    bool analysis_complete = true;
    for (uint32_t ti : wf->time_indices_of(clock_ref)) {
        const uint64_t time = wf->time_at(ti);
        if (time < begin || time > end) continue;
        IWaveformBackend::SampledValue before_clock, raw_clock;
        if (!wf->sampled_value_at(clock_ref, ti,
                IWaveformBackend::ObservationPoint::Before, before_clock) ||
            !wf->sampled_value_at(clock_ref, ti,
                IWaveformBackend::ObservationPoint::Raw, raw_clock)) continue;
        const bool rise = is_rising_edge(before_clock.value.text, raw_clock.value.text);
        const bool fall = is_falling_edge(before_clock.value.text, raw_clock.value.text);
        if (!(edge == "dual" ? (rise || fall) : edge == "posedge" ? rise : fall)) continue;
        if (sample_count >= max_samples) { analysis_complete = false; break; }
        ++sample_count;
        if (config.contains("reset")) {
            const Json reset = config.at("reset");
            const uint32_t reset_ref = wf->find_signal(reset.at("signal"));
            IWaveformBackend::SampledValue reset_value;
            if (!reset_ref || !wf->sampled_value_at(reset_ref, ti, point, reset_value) ||
                reset_value.value.text.empty() ||
                (reset.at("polarity") == "active_low"
                    ? reset_value.value.text.back() != '1'
                    : reset_value.value.text.back() != '0')) continue;
        }
        const LogicValue matched = eval_expression(expression.get(), *wf, ti, nullptr, point);
        if (!matched.known || matched.bits.find('1') == std::string::npos) continue;
        ++total_count;
        Json signal_values = Json::object();
        for (auto it = signals.begin(); it != signals.end(); ++it) {
            const uint32_t ref = wf->find_signal(it.value());
            IWaveformBackend::SignalInfo info;
            IWaveformBackend::SampledValue sampled;
            wf->signal_info(ref, info);
            wf->sampled_value_at(ref, ti, point, sampled);
            signal_values[it.key()] = render_typed_value_json(sampled.value,
                                                               info.width, format);
        }
        Json field_values = Json::object();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const Json field = it.value();
            const std::string alias = field.at("signal");
            const Json value = signal_values.at(alias);
            LogicValue logic;
            if (parse_sv_literal(value.at("value"), logic)) {
                const int left = field.at("left"), right = field.at("right");
                const int width = static_cast<int>(logic.bits.size());
                std::string bits;
                for (int bit = left; bit >= right; --bit) {
                    const int index = width - 1 - bit;
                    bits.push_back(index >= 0 && index < width ? logic.bits[index] : '0');
                }
                field_values[it.key()] = render_value_json(bits, bits.size(), format);
            }
        }
        all_events.push_back({{"time", wf->format_time(time, unit)},
                              {"signals", signal_values}, {"fields", field_values}});
        if (mode == "first") break;
    }
    Json returned = Json::array();
    if (mode == "last" && !all_events.empty()) returned.push_back(all_events.back());
    else if (mode == "first") returned = all_events;
    else for (size_t i = 0; i < std::min(all_events.size(), line_limit); ++i)
        returned.push_back(all_events[i]);
    const bool truncated = mode == "all" && returned.size() < all_events.size();
    Json summary{{"sample_count", sample_count}, {"mode", mode}, {"inline", !named},
        {"sampling_mode", "clock_edge"}, {"clock", clock},
        {"sample_time_semantics", "time is sample_time"},
        {"begin", wf->format_time(begin, unit)}, {"end", wf->format_time(end, unit)},
        {"scan_complete", analysis_complete}, {"analysis_complete", analysis_complete},
        {"response_truncated", truncated}, {"total_count", total_count},
        {"returned_count", returned.size()}, {"truncation_scopes", truncated
            ? Json::array({"response_events"}) : Json::array()}};
    if (total_count) {
        summary["first"] = all_events.front().at("time");
        summary["last"] = all_events.back().at("time");
    }
    return {{"ok", true}, {"summary", summary},
            {"data", {{"events", returned},
                      {"sampling", event_sampling_contract(edge, requested_point)}}}};
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

        return run_event_find(req.at("args"));
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

        const Json args = req.at("args");
        Json query = args;
        query["mode"] = "all";
        query["line_limit"] = args.value("max_events",
            std::numeric_limits<uint32_t>::max());
        Json result = run_event_find(query);
        if (!result.value("ok", false)) return result;
        Json events = result.at("data").at("events");
        Json aggregate{{"count", result.at("summary").at("total_count")},
                       {"groups", Json::object()}, {"group_count", 0}};
        if (args.contains("aggregate") &&
            args.at("aggregate").contains("group_by")) {
            for (const Json& event : events) {
                std::string key;
                for (const Json& group_json : args.at("aggregate").at("group_by")) {
                    const std::string group = group_json;
                    const Json* value = nullptr;
                    if (event.at("signals").contains(group))
                        value = &event.at("signals").at(group);
                    else if (event.at("fields").contains(group))
                        value = &event.at("fields").at(group);
                    if (!key.empty()) key += "|";
                    key += group + "=" + (value ? value->at("value").get<std::string>()
                                             : std::string("<missing>"));
                }
                aggregate["groups"][key] =
                    aggregate["groups"].value(key, 0u) + 1;
            }
            aggregate["group_count"] = aggregate["groups"].size();
        }
        Json data{{"sampling", result.at("data").at("sampling")}};
        const bool include_events = !args.contains("aggregate") ||
            args.at("aggregate").value("events", true);
        if (include_events) data["events"] = events;
        if (args.contains("aggregate")) data["aggregate"] = aggregate;
        Json summary = result.at("summary");
        summary["mode"] = "export";
        summary["row_count"] = result.at("summary").at("total_count");
        summary["line_limit"] = args.value("line_limit", 16);
        const bool written = args.contains("output") &&
            args.at("output").contains("path");
        summary["status"] = written ? "written" : "preview";
        summary["output_written"] = written;
        if (!written) {
            if (include_events) {
                const size_t limit = args.value("line_limit", 16u);
                while (data["events"].size() > limit)
                    data["events"].erase(data["events"].end() - 1);
                summary["returned_count"] = data["events"].size();
                const bool truncated = data["events"].size() < events.size();
                summary["response_truncated"] = truncated;
                summary["truncation_scopes"] = truncated
                    ? Json::array({"response_events"}) : Json::array();
            } else {
                summary["returned_count"] = 0;
                summary["response_truncated"] = false;
                summary["truncation_scopes"] = Json::array();
            }
        } else {
            const std::filesystem::path path =
                args.at("output").at("path").get<std::string>();
            if (!path.parent_path().empty()) {
                std::error_code error;
                std::filesystem::create_directories(path.parent_path(), error);
                if (error) return make_error("EXPORT_FAILED", error.message());
            }
            std::ofstream output(path, std::ios::trunc);
            output << Json{{"events", events}, {"aggregate", aggregate},
                           {"sampling", data.at("sampling")}}.dump(2) << '\n';
            if (!output) return make_error("EXPORT_FAILED", "cannot write event output");
            summary["output"] = {{"path", path.string()}, {"file_format", "json"}};
            summary["returned_count"] = result.at("summary").at("total_count");
            summary["response_truncated"] = false;
            summary["truncation_scopes"] = Json::array();
            data = {{"sampling", result.at("data").at("sampling")}};
        }
        return {{"ok", true}, {"summary", summary}, {"data", data}};
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
        std::string parse_error;
        if (!engine_globals().waveform->parse_time(args.at("time"), time,
                                                   parse_error))
            return make_error("INVALID_TIME", parse_error);
        TimeRenderUnit unit;
        Json unit_error;
        if (!cursor_render_unit(args, unit, unit_error)) return unit_error;

        CursorManager::instance().set(name, time);
        WaveformCursor cursor;
        CursorManager::instance().get(name, cursor);
        const std::string rendered = engine_globals().waveform->format_time(time, unit);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", name}, {"time", rendered},
                          {"status", "set"},
                          {"active", CursorManager::instance().active_name() == name}};
        out["data"] = {
            {"resolved_time", {{"source", "explicit"}, {"time", rendered}}},
            {"metadata", cursor_metadata(cursor)}};
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

        TimeRenderUnit unit;
        Json unit_error;
        if (!cursor_render_unit(args, unit, unit_error)) return unit_error;
        const std::string rendered = engine_globals().waveform->format_time(c.time, unit);
        Json out;
        out["ok"] = true;
        out["summary"] = {{"name", c.name}, {"time", rendered},
                          {"status", "found"}};
        out["data"] = {{"metadata", cursor_metadata(c)}};
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
        auto args = req.value("args", Json::object());
        TimeRenderUnit unit;
        Json unit_error;
        if (!cursor_render_unit(args, unit, unit_error)) return unit_error;

        auto cursors = CursorManager::instance().all();
        Json arr = Json::array();
        for (const auto& c : cursors)
            arr.push_back({{"name", c.name},
                {"time", engine_globals().waveform->format_time(c.time, unit)},
                {"note", c.note}, {"origin", c.origin}, {"clock", c.clock},
                {"created_at", c.created_at}, {"updated_at", c.updated_at}});

        Json out;
        out["ok"] = true;
        const std::string active = CursorManager::instance().active_name();
        out["summary"] = {{"cursor_count", arr.size()},
                          {"active_cursor", active.empty() ? Json(nullptr) : Json(active)}};
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
        out["summary"] = {{"name", name}, {"deleted", true},
                          {"status", "deleted"}};
        out["data"] = Json::object();
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

        WaveformCursor cursor;
        if (!CursorManager::instance().use(name, cursor))
            return make_error("CURSOR_NOT_FOUND", "cursor not found: " + name);
        TimeRenderUnit unit;
        Json unit_error;
        if (!cursor_render_unit(args, unit, unit_error)) return unit_error;
        const std::string rendered =
            engine_globals().waveform->format_time(cursor.time, unit);

        Json out;
        out["ok"] = true;
        out["summary"] = {{"status", "active"}, {"active_cursor", name},
                          {"time", rendered}};
        out["data"] = {{"metadata", cursor_metadata(cursor)}};
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
#if 1
        const std::string config_path = args.at("config_path");
        const std::string output_path = args.at("output").at("path");
        std::ifstream input(config_path);
        if (!input) return make_error("CONFIG_READ_FAILED", "cannot open nWave config");
        Json config;
        try { input >> config; }
        catch (const std::exception& ex) {
            return make_error("CONFIG_PARSE_FAILED", ex.what());
        }
        if (!config.is_object() || !config.contains("groups") ||
            !config.at("groups").is_array())
            return make_error("CONFIG_INVALID", "nWave config requires groups array");
        std::vector<std::string> lines{
            "; Generated by xdebug nwave.rc.generate",
            "; Signal list/view rc only; open the FST separately in nWave.",
            "windowTimeUnit " + config.value("window_time_unit", "1ns"),
            "fileTimeScale " + config.value("file_time_scale", "1ns"),
            "signalSpacing " + std::to_string(config.value("signal_spacing", 5))};
        size_t group_count = 0, signal_count = 0;
        auto* wf = engine_globals().waveform.get();
        std::function<bool(const Json&)> render_groups;
        render_groups = [&](const Json& groups) {
            for (const Json& group : groups) {
                if (!group.is_object() || !group.contains("name")) return false;
                ++group_count;
                lines.push_back("addGroup " + group.at("name").get<std::string>());
                for (const Json& signal_entry : group.value("signals", Json::array())) {
                    const std::string signal = signal_entry.is_string()
                        ? signal_entry.get<std::string>()
                        : signal_entry.value("path", signal_entry.value("signal", ""));
                    if (signal.empty() || !wf->find_signal(signal)) return false;
                    ++signal_count;
                    std::string rc_path = signal;
                    std::replace(rc_path.begin(), rc_path.end(), '.', '/');
                    lines.push_back("addSignal " + rc_path);
                }
                if (group.contains("subgroups") &&
                    !render_groups(group.at("subgroups"))) return false;
            }
            return true;
        };
        if (!render_groups(config.at("groups")))
            return make_error("CONFIG_INVALID", "invalid group or signal in nWave config");
        const size_t times = config.value("times", Json::array()).size();
        const std::filesystem::path destination(output_path);
        if (!destination.parent_path().empty()) {
            std::error_code error;
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error) return make_error("EXPORT_FAILED", error.message());
        }
        std::ofstream output(destination, std::ios::trunc);
        for (const std::string& line : lines) output << line << '\n';
        if (!output) return make_error("EXPORT_FAILED", "cannot write nWave rc");
        Json preview = Json::array();
        for (size_t i = 0; i < std::min<size_t>(5, lines.size()); ++i)
            preview.push_back(lines[i]);
        return {{"ok", true}, {"summary", {
            {"config_path", config_path}, {"output", {{"path", output_path}}},
            {"group_count", group_count}, {"signal_count", signal_count},
            {"written", true}, {"valid", true}}},
            {"data", {{"validation", {{"signals", signal_count}, {"times", times}}},
                      {"rc_preview", preview}}}};
#else
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
#endif
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
