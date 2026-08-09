// signal_analysis_actions.cpp — signal statistics/stability/xz/anomaly
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

using Value = IWaveformBackend::WaveformValue;

struct ChangeRow {
    uint64_t time = 0;
    Value value;
};

Json action_error(const std::string& code, const std::string& message) {
    return Json{{"ok", false},
                {"error", {{"code", code}, {"message", message}}}};
}

Json typed_logic_value(const Value& value, uint32_t width,
                       ValueRenderFormat format) {
    if (value.kind == IWaveformBackend::ValueKind::BitVector) {
        LogicValue logic = logic_value_from_bits(value.text,
                                                  static_cast<int>(width));
        return logic_value_json(logic, format);
    }
    if (value.kind == IWaveformBackend::ValueKind::Real)
        return Json{{"value", std::to_string(value.real)}, {"known", true}};
    return Json{{"value", value.kind == IWaveformBackend::ValueKind::Event
                              ? "event" : value.text},
                {"known", true}};
}

bool value_equal(const Value& left, const Value& right) {
    if (left.kind != right.kind) return false;
    return left.kind == IWaveformBackend::ValueKind::Real
        ? left.real == right.real : left.text == right.text;
}

std::string lowercase(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool contains_xz(const std::string& bits) {
    const std::string value = lowercase(bits);
    return value.find('x') != std::string::npos ||
           value.find('z') != std::string::npos;
}

bool contains_bit(const std::string& bits, char expected) {
    return lowercase(bits).find(expected) != std::string::npos;
}

bool all_bits(const std::string& bits, char expected) {
    if (bits.empty()) return false;
    const std::string value = lowercase(bits);
    return std::all_of(value.begin(), value.end(),
                       [expected](char bit) { return bit == expected; });
}

bool parse_range(IWaveformBackend& wf, const Json& args,
                 uint64_t& begin, uint64_t& end, Json& error) {
    begin = wf.min_time();
    end = wf.max_time();
    const Json range = args.value("time_range", Json::object());
    std::string message;
    if (range.contains("begin") &&
        !wf.parse_time(range.at("begin"), begin, message)) {
        error = action_error("INVALID_TIME", message);
        return false;
    }
    if (range.contains("end") &&
        !wf.parse_time(range.at("end"), end, message, true)) {
        error = action_error("INVALID_TIME", message);
        return false;
    }
    if (begin > end) {
        error = action_error("TIME_RANGE_INVALID", "end is before begin");
        return false;
    }
    return true;
}

bool parse_render_unit(const Json& args, TimeRenderUnit& unit, Json& error) {
    std::string message;
    if (!parse_time_render_unit(args.value("render_time_unit", "ns"),
                                unit, message)) {
        error = action_error("INVALID_TIME_UNIT", message);
        return false;
    }
    return true;
}

bool prepare_signal(IWaveformBackend& wf, const std::string& signal,
                    uint32_t& ref, IWaveformBackend::SignalInfo& info,
                    Json& error) {
    ref = wf.find_signal(signal);
    if (!ref) {
        error = action_error("SIGNAL_NOT_FOUND",
                             "signal not found in waveform: " + signal);
        return false;
    }
    if (!wf.is_loaded(ref) && wf.load_signals({ref}) != 1) {
        error = action_error("VALUE_NOT_AVAILABLE",
                             "failed to load waveform signal: " + signal);
        return false;
    }
    if (!wf.signal_info(ref, info)) {
        error = action_error("VALUE_NOT_AVAILABLE",
                             "signal metadata is unavailable: " + signal);
        return false;
    }
    return true;
}

std::vector<ChangeRow> collect_changes(IWaveformBackend& wf, uint32_t ref,
                                       uint64_t begin, uint64_t end) {
    std::vector<ChangeRow> rows;
    if (begin > wf.max_time() || end < wf.min_time()) return rows;
    const uint32_t begin_ti = wf.time_idx_of(begin);
    IWaveformBackend::SampledValue initial;
    if (wf.sampled_value_at(ref, begin_ti,
                            IWaveformBackend::ObservationPoint::Raw, initial))
        rows.push_back({begin, initial.value});

    for (uint32_t ti : wf.time_indices_of(ref)) {
        const uint64_t time = wf.time_at(ti);
        if (time <= begin || time > end) continue;
        IWaveformBackend::SampledValue sampled;
        if (!wf.sampled_value_at(ref, ti,
                IWaveformBackend::ObservationPoint::Raw, sampled)) continue;
        if (!rows.empty() && value_equal(rows.back().value, sampled.value))
            continue;
        rows.push_back({time, sampled.value});
    }
    return rows;
}

Json completeness(bool scan_complete, bool analysis_complete,
                  bool response_truncated, size_t total_count,
                  size_t returned_count, const Json& scopes) {
    return Json{{"scan_complete", scan_complete},
                {"analysis_complete", analysis_complete},
                {"response_truncated", response_truncated},
                {"total_count", total_count},
                {"returned_count", returned_count},
                {"truncation_scopes", scopes}};
}

void merge_object(Json& target, const Json& source) {
    for (auto it = source.begin(); it != source.end(); ++it)
        target[it.key()] = it.value();
}

Json sampling_contract(const std::string& edge,
                       const std::string& requested_point) {
    const bool negedge = edge == "negedge";
    const std::string effective_point = negedge ? std::string()
        : (requested_point.empty() ? "before" : requested_point);
    Json result{
        {"requested", {{"edge", edge},
                       {"sample_point", requested_point.empty()
                           ? Json(nullptr) : Json(requested_point)}}},
        {"effective", {{"edge", edge},
                       {"sample_point", effective_point.empty()
                           ? Json(nullptr) : Json(effective_point)}}},
        {"sample_point_applied", !negedge},
        {"sample_point_ignored_for_negedge",
         negedge && !requested_point.empty()}
    };
    if (negedge && !requested_point.empty())
        result["sample_point_not_applied_reason"] =
            "negedge keeps the established current-value sampling semantics";
    return result;
}

IWaveformBackend::ObservationPoint observation_point(
    const std::string& edge, const std::string& requested_point) {
    if (edge == "negedge") return IWaveformBackend::ObservationPoint::Raw;
    return requested_point == "after"
        ? IWaveformBackend::ObservationPoint::After
        : IWaveformBackend::ObservationPoint::Before;
}

bool edge_selected(const std::string& edge, bool rising, bool falling) {
    return edge == "dual" ? rising || falling
        : edge == "posedge" ? rising : falling;
}

}  // namespace

struct SignalStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const std::string signal = args.at("signal");
        Json error;
        uint32_t ref = 0;
        IWaveformBackend::SignalInfo info;
        if (!prepare_signal(*wf, signal, ref, info, error)) return error;
        uint64_t begin = 0, end = 0;
        if (!parse_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_render_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);

        if (!args.contains("clock")) {
            if (args.contains("edge") || args.contains("sample_point"))
                return action_error("INVALID_FIELD",
                    "edge and sample_point require args.clock for signal.statistics");
            const std::vector<ChangeRow> rows =
                collect_changes(*wf, ref, begin, end);
            const size_t limit = args.value("line_limit", 100u);
            const size_t returned = std::min(rows.size(), limit);
            const bool response_truncated = returned < rows.size();
            Json summary{{"signal", signal},
                         {"sampling_mode", "raw_value_changes"},
                         {"begin", wf->format_time(begin, unit)},
                         {"end", wf->format_time(end, unit)},
                         {"actual_transition_count",
                          rows.empty() ? 0 : rows.size() - 1}};
            merge_object(summary, completeness(
                true, true, response_truncated, rows.size(), returned,
                response_truncated ? Json::array({"response_evidence"})
                                   : Json::array()));

            int high_bursts = 0;
            bool previous_high = false;
            Json first_high = nullptr, last_high = nullptr, last_fall = nullptr;
            for (const ChangeRow& row : rows) {
                const bool high = row.value.kind ==
                        IWaveformBackend::ValueKind::BitVector &&
                    !contains_xz(row.value.text) &&
                    row.value.text.find('1') != std::string::npos;
                if (high) {
                    if (!previous_high) ++high_bursts;
                    if (first_high.is_null())
                        first_high = wf->format_time(row.time, unit);
                    last_high = wf->format_time(row.time, unit);
                } else if (previous_high) {
                    last_fall = wf->format_time(row.time, unit);
                }
                previous_high = high;
            }
            Json data{
                {"includes_initial_value", !rows.empty()},
                {"activity", {{"high_burst_count", high_bursts},
                              {"first_high_time", first_high},
                              {"last_high_time", last_high},
                              {"last_fall_time", last_fall},
                              {"max_high_cycles", nullptr}}}
            };
            if (!rows.empty()) {
                data["initial_value"] = typed_logic_value(
                    rows.front().value, info.width, format);
                data["final_value"] = typed_logic_value(
                    rows.back().value, info.width, format);
                data["first_change_time"] =
                    wf->format_time(rows.front().time, unit);
                data["last_change_time"] =
                    wf->format_time(rows.back().time, unit);
            }
            Json evidence = Json::array();
            for (size_t i = 0; i < returned; ++i)
                evidence.push_back({
                    {"time", wf->format_time(rows[i].time, unit)},
                    {"kind", i == 0 ? "initial" : "value_change"},
                    {"value", typed_logic_value(rows[i].value,
                                                 info.width, format)}});
            data["evidence"] = evidence;
            return Json{{"ok", true}, {"summary", summary}, {"data", data}};
        }

        if (info.encoding != IWaveformBackend::ValueKind::BitVector)
            return action_error("INVALID_SIGNAL_TYPE",
                                "clock-sampled statistics requires a bit-vector signal");
        const std::string clock = args.at("clock");
        uint32_t clock_ref = 0;
        IWaveformBackend::SignalInfo clock_info;
        if (!prepare_signal(*wf, clock, clock_ref, clock_info, error))
            return action_error("CLOCK_NOT_FOUND",
                                "clock signal not found: " + clock);
        const std::string edge = args.value("edge", "negedge");
        if (edge != "posedge" && edge != "negedge" && edge != "dual")
            return action_error("INVALID_FIELD",
                                "args.edge must be posedge, negedge, or dual");
        const std::string requested_point =
            args.value("sample_point", std::string());
        const auto point = observation_point(edge, requested_point);
        const size_t evidence_limit = args.value("line_limit", 100u);
        const size_t sample_limit = args.value(
            "max_samples", std::numeric_limits<size_t>::max());

        size_t samples = 0, known = 0, unknown = 0;
        size_t high_cycles = 0, low_cycles = 0, high_bursts = 0;
        size_t current_high = 0, max_high_cycles = 0, transitions = 0;
        bool analysis_truncated = false, have_known = false, previous_high = false;
        std::string first_bits, final_bits, min_bits, max_bits, previous_bits;
        Json first_change = nullptr, last_change = nullptr;
        Json first_high = nullptr, last_high = nullptr, last_fall = nullptr;
        Json evidence = Json::array();
        size_t evidence_count = 0;
        auto add_evidence = [&](uint64_t time, const char* kind,
                                const Value& value) {
            ++evidence_count;
            if (evidence.size() < evidence_limit)
                evidence.push_back({{"time", wf->format_time(time, unit)},
                    {"kind", kind},
                    {"value", typed_logic_value(value, info.width, format)}});
        };

        uint32_t previous_ti = std::numeric_limits<uint32_t>::max();
        for (uint32_t ti : wf->time_indices_of(clock_ref)) {
            if (ti == previous_ti) continue;
            previous_ti = ti;
            const uint64_t time = wf->time_at(ti);
            if (time < begin || time > end) continue;
            IWaveformBackend::SampledValue before_clock, raw_clock;
            if (!wf->sampled_value_at(clock_ref, ti,
                    IWaveformBackend::ObservationPoint::Before, before_clock) ||
                !wf->sampled_value_at(clock_ref, ti,
                    IWaveformBackend::ObservationPoint::Raw, raw_clock)) continue;
            const bool rising = is_rising_edge(before_clock.value.text,
                                                raw_clock.value.text);
            const bool falling = is_falling_edge(before_clock.value.text,
                                                  raw_clock.value.text);
            if (!edge_selected(edge, rising, falling)) continue;
            if (samples >= sample_limit) {
                analysis_truncated = true;
                break;
            }
            ++samples;
            IWaveformBackend::SampledValue sampled;
            if (!wf->sampled_value_at(ref, ti, point, sampled) ||
                sampled.value.kind != IWaveformBackend::ValueKind::BitVector ||
                contains_xz(sampled.value.text)) {
                ++unknown;
                Value unknown_value;
                unknown_value.kind = IWaveformBackend::ValueKind::BitVector;
                unknown_value.text = sampled.value.text.empty()
                    ? std::string(std::max(1u, info.width), 'x')
                    : sampled.value.text;
                add_evidence(time, "unknown", unknown_value);
                if (previous_high) {
                    max_high_cycles = std::max(max_high_cycles, current_high);
                    current_high = 0;
                    last_fall = wf->format_time(time, unit);
                    previous_high = false;
                }
                continue;
            }
            ++known;
            const std::string bits = lowercase(sampled.value.text);
            const bool nonzero = bits.find('1') != std::string::npos;
            const bool high = nonzero && bits.size() == 1;
            if (!nonzero) ++low_cycles;
            else if (high) ++high_cycles;
            if (high) {
                if (!previous_high) {
                    ++high_bursts;
                    current_high = 0;
                    if (first_high.is_null())
                        first_high = wf->format_time(time, unit);
                }
                ++current_high;
                last_high = wf->format_time(time, unit);
            } else if (previous_high) {
                max_high_cycles = std::max(max_high_cycles, current_high);
                current_high = 0;
                last_fall = wf->format_time(time, unit);
            }
            previous_high = high;
            if (!have_known) {
                first_bits = final_bits = min_bits = max_bits = previous_bits = bits;
                have_known = true;
            } else {
                if (bits != previous_bits) {
                    ++transitions;
                    add_evidence(time, "value_change", sampled.value);
                    if (first_change.is_null())
                        first_change = wf->format_time(time, unit);
                    last_change = wf->format_time(time, unit);
                }
                if (bits < min_bits) min_bits = bits;
                if (bits > max_bits) max_bits = bits;
                previous_bits = final_bits = bits;
            }
        }
        if (previous_high)
            max_high_cycles = std::max(max_high_cycles, current_high);

        const bool response_truncated = evidence.size() < evidence_count;
        Json scopes = Json::array();
        if (analysis_truncated) scopes.push_back("analysis_samples");
        if (response_truncated) scopes.push_back("response_evidence");
        Json summary{{"signal", signal}, {"sampling_mode", "clock_edge"},
            {"clock", clock}, {"sample_time_semantics", "time is sample_time"},
            {"sample_count", samples}, {"known_count", known},
            {"unknown_count", unknown},
            {"begin", wf->format_time(begin, unit)},
            {"end", wf->format_time(end, unit)}};
        merge_object(summary, completeness(!analysis_truncated,
            !analysis_truncated, response_truncated, evidence_count,
            evidence.size(), scopes));
        Json data{{"evidence", evidence}, {"transition_count", transitions},
                  {"sampling", sampling_contract(edge, requested_point)}};
        if (have_known) {
            auto bits_value = [&](const std::string& bits) {
                Value value; value.kind = IWaveformBackend::ValueKind::BitVector;
                value.text = bits;
                return typed_logic_value(value, info.width, format);
            };
            data["first"] = bits_value(first_bits);
            data["final"] = bits_value(final_bits);
            data["min"] = bits_value(min_bits);
            data["max"] = bits_value(max_bits);
            data["low_cycles"] = low_cycles;
            data["high_cycles"] = high_cycles;
            data["high_ratio"] = known == 0 ? 0.0
                : static_cast<double>(high_cycles) / static_cast<double>(known);
            if (!first_change.is_null()) data["first_change_time"] = first_change;
            if (!last_change.is_null()) data["last_change_time"] = last_change;
            data["activity"] = {{"high_burst_count", high_bursts},
                {"first_high_time", first_high}, {"last_high_time", last_high},
                {"last_fall_time", last_fall},
                {"max_high_cycles", max_high_cycles}};
        }
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

struct SignalStabilityHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.stability"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const std::string signal = args.at("signal");
        Json error;
        uint32_t ref = 0;
        IWaveformBackend::SignalInfo info;
        if (!prepare_signal(*wf, signal, ref, info, error)) return error;
        uint64_t begin = 0, end = 0;
        if (!parse_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_render_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);

        const std::vector<ChangeRow> all_rows =
            collect_changes(*wf, ref, begin, end);
        Json changes = Json::array();
        bool stable = true;
        for (size_t i = 0; i < all_rows.size(); ++i) {
            changes.push_back({{"time", wf->format_time(all_rows[i].time, unit)},
                {"value", typed_logic_value(all_rows[i].value,
                                             info.width, format)}});
            if (i > 0 && !value_equal(all_rows[i].value, all_rows.front().value)) {
                stable = false;
                break;
            }
        }
        const size_t count = changes.size();
        Json summary{{"stable", stable}, {"change_row_count", count},
            {"actual_transition_count", stable ? 0 : 1},
            {"scan_stopped_on_first_transition", !stable}};
        merge_object(summary, completeness(stable, true, false, count, count,
            stable ? Json::array()
                   : Json::array({"scan_after_first_transition"})));
        Json data{{"signal", signal},
                  {"begin", wf->format_time(begin, unit)},
                  {"end", wf->format_time(end, unit)},
                  {"changes", changes},
                  {"includes_initial_value", count > 0}};
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

struct SignalXzVerifyHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.xz_verify"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const std::string signal = args.at("signal");
        const std::string expected = args.at("expected_state");
        const std::string mode = args.value("match_mode", "exact");
        Json error;
        uint32_t ref = 0;
        IWaveformBackend::SignalInfo info;
        if (!prepare_signal(*wf, signal, ref, info, error)) return error;
        if (info.encoding != IWaveformBackend::ValueKind::BitVector)
            return action_error("INVALID_SIGNAL_TYPE",
                                "signal.xz_verify requires a bit-vector signal");
        uint64_t begin = 0, end = 0;
        if (!parse_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_render_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);

        const std::vector<ChangeRow> rows = collect_changes(*wf, ref, begin, end);
        if (rows.empty())
            return action_error("VALUE_NOT_AVAILABLE",
                "no waveform value is available for signal " + signal +
                " in the requested window");
        bool matched_all = true;
        size_t checked = 0;
        Json first_mismatch = nullptr;
        for (const ChangeRow& row : rows) {
            ++checked;
            const bool matched = mode == "exact"
                ? all_bits(row.value.text, expected[0])
                : contains_bit(row.value.text, expected[0]);
            if (!matched) {
                matched_all = false;
                first_mismatch = {
                    {"sample_time", wf->format_time(row.time, unit)},
                    {"value", typed_logic_value(row.value, info.width, format)}};
                break;
            }
        }
        Json summary{{"signal", signal}, {"expected_state", expected},
            {"match_mode", mode}, {"verdict", matched_all ? "pass" : "fail"},
            {"always_matched", matched_all}, {"checked_value_count", checked},
            {"stop_reason", matched_all ? "window_end" : "first_mismatch"}};
        merge_object(summary, completeness(matched_all, true, false,
                                           checked, checked, Json::array()));
        Json data{{"time_range", {{"begin", wf->format_time(begin, unit)},
                                  {"end", wf->format_time(end, unit)}}},
            {"initial_value", typed_logic_value(rows.front().value,
                                                info.width, format)},
            {"first_mismatch", first_mismatch},
            {"sample_time_semantics",
             "sample_time is the finalized raw waveform value-change time in the closed interval"}};
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

struct SignalAnomalyInspectHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.anomaly.inspect"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        Json error;
        uint64_t begin = 0, end = 0;
        if (!parse_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_render_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);

        const Json checks = args.value("checks", Json::array());
        bool check_unknown = checks.empty();
        bool check_glitch = false;
        bool check_stuck = checks.empty();
        uint64_t glitch_width = 0, stuck_duration = 0;
        if (checks.empty()) {
            std::string message;
            if (!wf->parse_time("1us", stuck_duration, message))
                return action_error("INVALID_TIME", message);
        }
        for (const Json& check : checks) {
            const std::string type = check.at("type");
            std::string message;
            if (type == "unknown_xz") check_unknown = true;
            else if (type == "glitch") {
                check_glitch = true;
                if (!wf->parse_time(check.at("min_pulse_width"),
                                    glitch_width, message) || glitch_width == 0)
                    return action_error("INVALID_TIME",
                        message.empty() ? "min_pulse_width must be greater than zero"
                                        : message);
            } else if (type == "stuck") {
                check_stuck = true;
                if (!wf->parse_time(check.at("min_duration"),
                                    stuck_duration, message) || stuck_duration == 0)
                    return action_error("INVALID_TIME",
                        message.empty() ? "min_duration must be greater than zero"
                                        : message);
            }
        }

        const size_t limit = args.value("line_limit", 50u);
        Json findings = Json::array(), scan_status = Json::array();
        size_t finding_count = 0;
        bool analysis_complete = true;
        auto add_finding = [&](const Json& finding, size_t& signal_count) {
            ++finding_count;
            ++signal_count;
            if (findings.size() < limit) findings.push_back(finding);
        };

        for (const Json& signal_json : args.at("signals")) {
            const std::string signal = signal_json;
            uint32_t ref = 0;
            IWaveformBackend::SignalInfo info;
            Json signal_error;
            if (!prepare_signal(*wf, signal, ref, info, signal_error)) {
                analysis_complete = false;
                scan_status.push_back({{"signal", signal}, {"status", "error"},
                    {"analysis_complete", false},
                    {"message", signal_error.at("error").at("message")}});
                continue;
            }
            const std::vector<ChangeRow> rows =
                collect_changes(*wf, ref, begin, end);
            size_t signal_findings = 0;
            for (size_t i = 0; i < rows.size(); ++i) {
                if (check_unknown &&
                    rows[i].value.kind == IWaveformBackend::ValueKind::BitVector &&
                    contains_xz(rows[i].value.text)) {
                    add_finding({{"type", "unknown_xz"}, {"signal", signal},
                        {"severity", "warning"},
                        {"time", wf->format_time(rows[i].time, unit)},
                        {"value", typed_logic_value(rows[i].value,
                                                     info.width, format)}},
                        signal_findings);
                }
                if (i + 1 >= rows.size()) continue;
                const uint64_t duration = rows[i + 1].time - rows[i].time;
                if (check_glitch && duration > 0 && duration < glitch_width)
                    add_finding({{"type", "glitch"}, {"signal", signal},
                        {"severity", "info"},
                        {"time", wf->format_time(rows[i].time, unit)},
                        {"pulse_width", wf->format_time(duration, unit)}},
                        signal_findings);
                if (check_stuck && duration >= stuck_duration)
                    add_finding({{"type", "stuck"}, {"signal", signal},
                        {"severity", "warning"},
                        {"begin", wf->format_time(rows[i].time, unit)},
                        {"end", wf->format_time(rows[i + 1].time, unit)},
                        {"duration", wf->format_time(duration, unit)},
                        {"value", typed_logic_value(rows[i].value,
                                                     info.width, format)}},
                        signal_findings);
            }
            if (check_stuck && !rows.empty() && end >= rows.back().time &&
                end - rows.back().time >= stuck_duration) {
                const uint64_t duration = end - rows.back().time;
                add_finding({{"type", "stuck"}, {"signal", signal},
                    {"severity", "warning"},
                    {"begin", wf->format_time(rows.back().time, unit)},
                    {"end", wf->format_time(end, unit)},
                    {"duration", wf->format_time(duration, unit)},
                    {"value", typed_logic_value(rows.back().value,
                                                 info.width, format)},
                    {"open_at_window_end", true}}, signal_findings);
            }
            scan_status.push_back({{"signal", signal}, {"status", "ok"},
                {"analysis_complete", true}, {"change_row_count", rows.size()},
                {"finding_count", signal_findings},
                {"no_finding_reason", signal_findings == 0
                    ? Json("no configured rule matched in the requested window")
                    : Json(nullptr)}});
        }

        const bool response_truncated = findings.size() < finding_count;
        Json scopes = Json::array();
        if (!analysis_complete) scopes.push_back("analysis_signals");
        if (response_truncated) scopes.push_back("response_findings");
        Json summary{{"signal_count", scan_status.size()}, {"checks", checks},
            {"glitch_threshold", check_glitch
                ? Json(wf->format_time(glitch_width, unit)) : Json(nullptr)},
            {"stuck_threshold", check_stuck
                ? Json(wf->format_time(stuck_duration, unit)) : Json(nullptr)}};
        merge_object(summary, completeness(analysis_complete, analysis_complete,
            response_truncated, finding_count, findings.size(), scopes));
        return Json{{"ok", true}, {"summary", summary},
                    {"data", {{"findings", findings},
                              {"scan_status", scan_status}}}};
    }
};

std::unique_ptr<EngineActionHandler> make_signal_statistics_handler() {
    return std::make_unique<SignalStatisticsHandler>();
}
std::unique_ptr<EngineActionHandler> make_signal_stability_handler() {
    return std::make_unique<SignalStabilityHandler>();
}
std::unique_ptr<EngineActionHandler> make_signal_xz_verify_handler() {
    return std::make_unique<SignalXzVerifyHandler>();
}
std::unique_ptr<EngineActionHandler> make_signal_anomaly_inspect_handler() {
    return std::make_unique<SignalAnomalyInspectHandler>();
}

}  // namespace xdebug_fst
