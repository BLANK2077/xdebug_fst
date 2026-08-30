// clock_counter_actions.cpp — clock_point_query, expr.eval_at,
// signal.sampled_pulse.inspect, protocol.handshake.inspect,
// counter.statistics handlers (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/cursor/cursor_manager.h"
#include "waveform/expr/expr_eval.h"
#include "api/json_types.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── Shared helpers ──

static Json render_value_json(const std::string& bits, uint32_t width,
                              ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, static_cast<int>(width));
    return logic_value_json(v, fmt);
}

// Parse a time value from args: accepts string or integer JSON.
static uint64_t parse_time_arg(const Json& args, const char* key,
                               uint64_t default_val) {
    if (!args.contains(key)) return default_val;
    const Json& v = args[key];
    if (v.is_string()) {
        try { return std::stoull(v.get<std::string>()); } catch (...) { return default_val; }
    }
    if (v.is_number()) return v.get<uint64_t>();
    return default_val;
}

// Parse edge argument: "rising"|"falling"|"both" (default "rising").
static bool parse_edge(const std::string& edge, bool& rising, bool& falling) {
    if (edge.empty() || edge == "rising" || edge == "posedge") {
        rising = true; falling = false; return true;
    }
    if (edge == "falling" || edge == "negedge") {
        rising = false; falling = true; return true;
    }
    if (edge == "both" || edge == "dual") {
        rising = true; falling = true; return true;
    }
    return false;
}

// Load a signal by name, return kInvalidSignalRef on failure.
static uint32_t load_signal(const std::string& name, IWaveformBackend* wf,
                            Json& error_out) {
    if (name.empty()) return IWaveformBackend::kInvalidSignalRef;
    uint32_t ref = wf->find_signal(name);
    if (ref == IWaveformBackend::kInvalidSignalRef) {
        error_out = Json{{"ok", false},
                         {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                    {"message", "signal not found in waveform: " + name}}}};
        return IWaveformBackend::kInvalidSignalRef;
    }
    if (!wf->is_loaded(ref)) wf->load_signals({ref});
    return ref;
}

// Try parsing a bit string as unsigned integer. Returns false on x/z.
static bool bits_to_u64(const std::string& bits, uint64_t& out) {
    out = 0;
    for (char c : bits) {
        out <<= 1;
        if (c == '1') out |= 1;
        else if (c == '0') continue;
        else return false; // x/z
    }
    return true;
}

static bool bind_expr_aliases(ExprNode* node, const Json& aliases,
                              std::string& error) {
    if (!node) return false;
    if (node->kind == ExprNode::Kind::Signal ||
        node->kind == ExprNode::Kind::Slice) {
        if (!aliases.contains(node->signal)) {
            error = "expression references unknown alias: " + node->signal;
            return false;
        }
        node->signal = aliases.at(node->signal).get<std::string>();
    }
    return (!node->left || bind_expr_aliases(node->left, aliases, error)) &&
           (!node->right || bind_expr_aliases(node->right, aliases, error));
}

static std::string logic_status(const LogicValue& value) {
    if (!value.known) return "unknown";
    return value.bits.find('1') == std::string::npos ? "false" : "true";
}

using WaveValue = IWaveformBackend::WaveformValue;

static Json analysis_error(const std::string& code,
                           const std::string& message) {
    return Json{{"ok", false},
                {"error", {{"code", code}, {"message", message}}}};
}

static bool parse_analysis_time(IWaveformBackend& wf, const Json& input,
                                uint64_t& value, std::string& error,
                                bool allow_max) {
    if (input.is_string()) {
        const std::string text = input.get<std::string>();
        if (text.size() > 1 && text.front() == '@') {
            WaveformCursor cursor;
            const std::string name = text.substr(1);
            if (!CursorManager::instance().get(name, cursor)) {
                error = "waveform cursor not found: " + name;
                return false;
            }
            value = cursor.time;
            return true;
        }
    }
    return wf.parse_time(input, value, error, allow_max);
}

static bool parse_analysis_range(IWaveformBackend& wf, const Json& args,
                                 uint64_t& begin, uint64_t& end,
                                 Json& error) {
    begin = wf.min_time();
    end = wf.max_time();
    const Json range = args.value("time_range", Json::object());
    std::string message;
    if (range.contains("begin") &&
        !parse_analysis_time(wf, range.at("begin"), begin, message, false)) {
        error = analysis_error("INVALID_TIME", message);
        return false;
    }
    if (range.contains("end") &&
        !parse_analysis_time(wf, range.at("end"), end, message, true)) {
        error = analysis_error("INVALID_TIME", message);
        return false;
    }
    if (begin > end) {
        error = analysis_error("TIME_RANGE_INVALID", "end is before begin");
        return false;
    }
    return true;
}

static bool parse_analysis_unit(const Json& args, TimeRenderUnit& unit,
                                Json& error) {
    std::string message;
    if (!parse_time_render_unit(args.value("render_time_unit", "ns"),
                                unit, message)) {
        error = analysis_error("INVALID_TIME_UNIT", message);
        return false;
    }
    return true;
}

static bool prepare_analysis_signal(IWaveformBackend& wf,
                                    const std::string& signal,
                                    uint32_t& ref,
                                    IWaveformBackend::SignalInfo& info,
                                    Json& error) {
    ref = wf.find_signal(signal);
    if (!ref) {
        error = analysis_error("SIGNAL_NOT_FOUND",
            "signal not found in waveform: " + signal);
        return false;
    }
    if (!wf.is_loaded(ref) && wf.load_signals({ref}) != 1) {
        error = analysis_error("VALUE_NOT_AVAILABLE",
            "failed to load waveform signal: " + signal);
        return false;
    }
    if (!wf.signal_info(ref, info)) {
        error = analysis_error("VALUE_NOT_AVAILABLE",
            "signal metadata is unavailable: " + signal);
        return false;
    }
    return true;
}

static Json analysis_sampling_contract(const std::string& edge,
                                       const std::string& requested_point) {
    const bool negedge = edge == "negedge";
    const std::string effective = negedge ? std::string()
        : (requested_point.empty() ? "before" : requested_point);
    Json result{{"requested", {{"edge", edge}, {"sample_point",
        requested_point.empty() ? Json(nullptr) : Json(requested_point)}}},
        {"effective", {{"edge", edge}, {"sample_point",
        effective.empty() ? Json(nullptr) : Json(effective)}}},
        {"sample_point_applied", !negedge},
        {"sample_point_ignored_for_negedge",
         negedge && !requested_point.empty()}};
    if (negedge && !requested_point.empty())
        result["sample_point_not_applied_reason"] =
            "negedge keeps the established current-value sampling semantics";
    return result;
}

static IWaveformBackend::ObservationPoint analysis_observation_point(
        const std::string& edge, const std::string& requested_point) {
    if (edge == "negedge") return IWaveformBackend::ObservationPoint::Raw;
    return requested_point == "after"
        ? IWaveformBackend::ObservationPoint::After
        : IWaveformBackend::ObservationPoint::Before;
}

static bool analysis_edge_selected(const std::string& edge,
                                   bool rising, bool falling) {
    return edge == "dual" ? rising || falling
        : edge == "posedge" ? rising : falling;
}

struct AnalysisEdge {
    uint32_t time_idx = 0;
    uint64_t time = 0;
};

static std::vector<AnalysisEdge> collect_analysis_edges(
        IWaveformBackend& wf, uint32_t clock_ref, uint64_t begin,
        uint64_t end, const std::string& edge) {
    std::vector<AnalysisEdge> result;
    uint32_t previous_ti = std::numeric_limits<uint32_t>::max();
    for (uint32_t ti : wf.time_indices_of(clock_ref)) {
        if (ti == previous_ti) continue;
        previous_ti = ti;
        const uint64_t time = wf.time_at(ti);
        if (time < begin || time > end) continue;
        IWaveformBackend::SampledValue before, raw;
        if (!wf.sampled_value_at(clock_ref, ti,
                IWaveformBackend::ObservationPoint::Before, before) ||
            !wf.sampled_value_at(clock_ref, ti,
                IWaveformBackend::ObservationPoint::Raw, raw)) continue;
        const bool rising = is_rising_edge(before.value.text, raw.value.text);
        const bool falling = is_falling_edge(before.value.text, raw.value.text);
        if (analysis_edge_selected(edge, rising, falling))
            result.push_back({ti, time});
    }
    return result;
}

enum class AnalysisTri { False, True, Unknown };

static AnalysisTri analysis_truth(const WaveValue& value) {
    if (value.kind != IWaveformBackend::ValueKind::BitVector ||
        value.text.empty()) return AnalysisTri::Unknown;
    bool one = false;
    for (char c : value.text) {
        if (c == 'x' || c == 'X' || c == 'z' || c == 'Z')
            return AnalysisTri::Unknown;
        if (c == '1') one = true;
    }
    return one ? AnalysisTri::True : AnalysisTri::False;
}

static Json analysis_logic_json(const WaveValue& value, uint32_t width,
                                ValueRenderFormat format) {
    if (value.kind != IWaveformBackend::ValueKind::BitVector)
        return Json{{"value", value.text}, {"known", true}};
    return logic_value_json(
        logic_value_from_bits(value.text, static_cast<int>(width)), format);
}

static std::string decimal_average(long double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(18) << value;
    std::string result = stream.str();
    while (result.size() > 1 && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result.empty() ? "0" : result;
}

// ── 1. clock_point_query ──

struct ClockPointQueryHandler : public EngineActionHandler {
    const char* action_name() const override { return "clock_point_query"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: clock_point_query"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string ts = args.value("time", "");
        std::string clk_name = args.value("clock", "");

        if (sig.empty() || ts.empty() || clk_name.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal, args.time, and args.clock are required"}}}};
        }

        uint64_t t = 0;
        try { t = std::stoull(ts); }
        catch (...) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_TIME"}, {"message", "time must be an integer"}}}};
        }

        auto* wf = g.waveform.get();

        Json err;
        uint32_t sig_ref = load_signal(sig, wf, err);
        if (sig_ref == IWaveformBackend::kInvalidSignalRef) return err;
        uint32_t clk_ref = load_signal(clk_name, wf, err);
        if (clk_ref == IWaveformBackend::kInvalidSignalRef) return err;

        IWaveformBackend::SignalInfo sig_info;
        wf->signal_info(sig_ref, sig_info);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        bool rising = true, falling = false;
        std::string edge_str = args.value("edge", "rising");
        if (!parse_edge(edge_str, rising, falling)) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_FIELD"},
                                   {"message", "edge must be rising, falling, or both"}}}};
        }

        std::string sample_point = args.value("sample_point", "middle");
        if (sample_point != "middle" && sample_point != "before" &&
            sample_point != "after" && sample_point != "all") {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_FIELD"},
                                   {"message", "sample_point must be middle, before, after, or all"}}}};
        }

        uint32_t ti = wf->time_idx_of(t);

        // Build a scanner to find clock edges
        ClockSampleScanner scanner(*wf, clk_ref, sig_ref, rising, falling);

        // Find the nearest clock edge at or before the requested time
        const auto& edges = scanner.edge_indices();
        int best_edge_idx = -1;
        uint32_t best_edge_ti = 0;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i] <= ti) {
                best_edge_idx = static_cast<int>(i);
                best_edge_ti = edges[i];
            } else {
                break;
            }
        }

        if (best_edge_idx < 0) {
            // No edge at or before requested time — try the first edge after
            if (!edges.empty()) {
                best_edge_idx = 0;
                best_edge_ti = edges[0];
            } else {
                return Json{{"ok", false},
                            {"error", {{"code", "VALUE_NOT_AVAILABLE"},
                                       {"message", "no clock edge found in waveform"}}}};
            }
        }

        uint64_t edge_time = wf->time_at(best_edge_ti);
        bool time_match = (edge_time == t);

        // Read before/middle/after at the edge time index
        PointValues pv = read_point_values(*wf, sig_ref, best_edge_ti);

        Json sample;
        if (sample_point == "all" || sample_point == "middle")
            sample["middle"] = pv.middle_match
                ? render_value_json(pv.middle, sig_info.width, fmt)
                : Json(nullptr);
        if (sample_point == "all" || sample_point == "before")
            sample["before"] = pv.has_before
                ? render_value_json(pv.before, sig_info.width, fmt)
                : Json(nullptr);
        if (sample_point == "all" || sample_point == "after")
            sample["after"] = pv.has_after
                ? render_value_json(pv.after, sig_info.width, fmt)
                : Json(nullptr);

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", sig},
            {"clock", clk_name},
            {"requested_time", t},
            {"edge", edge_str},
            {"sample_point", sample_point},
        };
        out["data"] = {
            {"signal", sig},
            {"clock", clk_name},
            {"time", t},
            {"edge_found", best_edge_idx >= 0},
            {"edge_time", edge_time},
            {"edge_time_idx", best_edge_ti},
            {"sample", sample},
            {"time_match", time_match},
        };
        return out;
    }
};

// ── 2. expr.eval_at ──

struct ExprEvalAtHandler : public EngineActionHandler {
    const char* action_name() const override { return "expr.eval_at"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: expr.eval_at"}}}};
        }
        const Json args = req.at("args");
        const std::string expr = args.at("expr");
        const Json aliases = args.at("signals");
        auto* wf = g.waveform.get();
        uint64_t time = 0;
        std::string error;
        if (!wf->parse_time(args.at("time"), time, error))
            return Json{{"ok", false}, {"error", {{"code", "INVALID_TIME"},
                                                    {"message", error}}}};
        TimeRenderUnit unit;
        if (!parse_time_render_unit(args.value("render_time_unit", "ns"), unit,
                                    error))
            return Json{{"ok", false}, {"error", {{"code", "INVALID_TIME_UNIT"},
                                                    {"message", error}}}};
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);
        std::unique_ptr<ExprNode> root(parse_expression(expr, error));
        if (!root || !bind_expr_aliases(root.get(), aliases, error))
            return Json{{"ok", false}, {"error", {{"code", "PARSE_ERROR"},
                                                    {"message", error}}}};
        Json operands = Json::array();
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            const std::string signal = it.value();
            const uint32_t ref = wf->find_signal(signal);
            if (!ref) return Json{{"ok", false}, {"error", {
                {"code", "SIGNAL_NOT_FOUND"}, {"message", signal}}}};
            wf->load_signals({ref});
        }
        const std::string clock = args.at("clock");
        const uint32_t clock_ref = wf->find_signal(clock);
        if (!clock_ref) return Json{{"ok", false}, {"error", {
            {"code", "CLOCK_NOT_FOUND"}, {"message", clock}}}};
        wf->load_signals({clock_ref});
        const std::string edge = args.value("edge", "negedge");
        const std::string requested_point = args.value("sample_point", "");
        const bool negedge = edge == "negedge";
        const std::string effective_point = negedge ? "" :
            (requested_point.empty() ? "before" : requested_point);
        const auto point = effective_point == "before"
            ? IWaveformBackend::ObservationPoint::Before
            : effective_point == "after" ? IWaveformBackend::ObservationPoint::After
                                          : IWaveformBackend::ObservationPoint::Raw;
        const uint32_t ti = wf->time_idx_of(time);
        const LogicValue result = eval_expression(root.get(), *wf, ti, nullptr, point);
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            const uint32_t ref = wf->find_signal(it.value());
            IWaveformBackend::SignalInfo info;
            IWaveformBackend::SampledValue sample;
            wf->signal_info(ref, info);
            wf->sampled_value_at(ref, ti, point, sample);
            operands.push_back({{"alias", it.key()}, {"signal", it.value()},
                {"value", render_value_json(sample.value.text, info.width, format)}});
        }
        Json previous = nullptr, next = nullptr;
        std::string exact_kind;
        for (uint32_t edge_ti : wf->time_indices_of(clock_ref)) {
            IWaveformBackend::SampledValue before, raw;
            if (!wf->sampled_value_at(clock_ref, edge_ti,
                    IWaveformBackend::ObservationPoint::Before, before) ||
                !wf->sampled_value_at(clock_ref, edge_ti,
                    IWaveformBackend::ObservationPoint::Raw, raw)) continue;
            const bool rise = is_rising_edge(before.value.text, raw.value.text);
            const bool fall = is_falling_edge(before.value.text, raw.value.text);
            if (!rise && !fall) continue;
            const std::string kind = rise ? "posedge" : "negedge";
            const uint64_t edge_time = wf->time_at(edge_ti);
            if (edge_time == time) exact_kind = kind;
            if (!(edge == "dual" || edge == kind)) continue;
            if (edge_time < time) previous = wf->format_time(edge_time, unit);
            else if (edge_time > time && next.is_null())
                next = wf->format_time(edge_time, unit);
        }
        Json context{{"clock", clock},
            {"requested_sampling", {{"edge", edge}, {"sample_point",
                requested_point.empty() ? Json(nullptr) : Json(requested_point)}}},
            {"effective_sampling", {{"edge", edge}, {"sample_point",
                effective_point.empty() ? Json(nullptr) : Json(effective_point)}}},
            {"sample_point_applied", !negedge},
            {"sample_point_ignored_for_negedge", negedge && !requested_point.empty()},
            {"requested_time", wf->format_time(time, unit)},
            {"requested_any_edge_hit", !exact_kind.empty()},
            {"clock_edge_kind", exact_kind.empty() ? Json(nullptr) : Json(exact_kind)},
            {"requested_target_edge_hit", !exact_kind.empty() &&
                (edge == "dual" || edge == exact_kind)},
            {"previous_sample_time", previous}, {"next_sample_time", next},
            {"bracket_complete", !previous.is_null() && !next.is_null()}};
        if (negedge && !requested_point.empty())
            context["sample_point_not_applied_reason"] =
                "negedge keeps the established current-value sampling semantics";
        const LogicValue before_value = eval_expression(root.get(), *wf, ti, nullptr,
            IWaveformBackend::ObservationPoint::Before);
        const LogicValue raw_value = eval_expression(root.get(), *wf, ti, nullptr,
            IWaveformBackend::ObservationPoint::Raw);
        const LogicValue after_value = eval_expression(root.get(), *wf, ti, nullptr,
            IWaveformBackend::ObservationPoint::After);
        return {{"ok", true}, {"summary", {{"expr", expr},
            {"time", wf->format_time(time, unit)}, {"status", logic_status(result)},
            {"known", result.known}, {"value_width_complete", true},
            {"width_diagnostics", Json::array()}}}, {"data", {
            {"expr_value", result.known ? Json(logic_status(result) == "true") : Json(nullptr)},
            {"operands", operands}, {"clock_context", context},
            {"expr_samples", {{"before", logic_status(before_value)},
                              {"middle", logic_status(raw_value)},
                              {"after", logic_status(after_value)}}}}}};
    }
};

// ── 3. signal.sampled_pulse.inspect ──

struct SignalSampledPulseInspectHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.sampled_pulse.inspect"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const std::string clock = args.at("clock");
        const std::string valid = args.at("valid");
        Json error;
        uint32_t clock_ref = 0, valid_ref = 0;
        IWaveformBackend::SignalInfo clock_info, valid_info;
        if (!prepare_analysis_signal(*wf, clock, clock_ref, clock_info, error))
            return analysis_error("CLOCK_NOT_FOUND", "clock signal not found: " + clock);
        if (!prepare_analysis_signal(*wf, valid, valid_ref, valid_info, error))
            return error;
        uint64_t begin = 0, end = 0;
        if (!parse_analysis_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_analysis_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);
        const std::string edge = args.value("edge", "negedge");
        if (edge != "posedge" && edge != "negedge" && edge != "dual")
            return analysis_error("INVALID_FIELD",
                "args.edge must be posedge, negedge, or dual");
        const std::string requested_point = args.value("sample_point", "");
        const auto point = analysis_observation_point(edge, requested_point);
        const size_t line_limit = args.value("line_limit", 100u);
        const Json rules = args.value("rules", Json::object());
        const std::string payload_reporting = rules.value(
            "payload_changed_without_sampled_valid", "summary");

        struct Payload { std::string alias, signal; uint32_t ref; uint32_t width; };
        std::vector<Payload> payloads;
        if (args.contains("payloads")) {
            size_t index = 0;
            for (const Json& item : args.at("payloads")) {
                const std::string signal = item.get<std::string>();
                uint32_t ref = 0;
                IWaveformBackend::SignalInfo info;
                if (!prepare_analysis_signal(*wf, signal, ref, info, error)) return error;
                payloads.push_back({"payload" + std::to_string(index++),
                                    signal, ref, info.width});
            }
        }
        if (rules.contains("payload_changed_without_sampled_valid") &&
            payloads.empty())
            return analysis_error("INVALID_FIELD",
                "rules.payload_changed_without_sampled_valid requires args.payloads");

        struct Sample {
            uint64_t time;
            WaveValue valid;
            std::vector<WaveValue> payload_values;
        };
        std::vector<Sample> samples;
        size_t sampled_high = 0, sampled_low = 0, sampled_unknown = 0;
        Json first_high = nullptr, last_high = nullptr;
        for (const AnalysisEdge& edge_row :
             collect_analysis_edges(*wf, clock_ref, begin, end, edge)) {
            IWaveformBackend::SampledValue sampled;
            if (!wf->sampled_value_at(valid_ref, edge_row.time_idx, point, sampled))
                continue;
            Sample row{edge_row.time, sampled.value, {}};
            for (const Payload& payload : payloads) {
                IWaveformBackend::SampledValue value;
                wf->sampled_value_at(payload.ref, edge_row.time_idx, point, value);
                row.payload_values.push_back(value.value);
            }
            samples.push_back(row);
            const AnalysisTri state = analysis_truth(sampled.value);
            if (state == AnalysisTri::True) {
                ++sampled_high;
                if (first_high.is_null()) first_high = wf->format_time(edge_row.time, unit);
                last_high = wf->format_time(edge_row.time, unit);
            } else if (state == AnalysisTri::False) ++sampled_low;
            else ++sampled_unknown;
        }

        auto lower_sample = [&](uint64_t time) {
            return static_cast<int>(std::lower_bound(samples.begin(), samples.end(), time,
                [](const Sample& row, uint64_t value) { return row.time < value; }) - samples.begin());
        };
        auto nearest_sample = [&](uint64_t time) {
            if (samples.empty()) return -1;
            const int next = lower_sample(time);
            if (next <= 0) return 0;
            if (next >= static_cast<int>(samples.size()))
                return static_cast<int>(samples.size()) - 1;
            return time - samples[next - 1].time <= samples[next].time - time
                ? next - 1 : next;
        };
        auto sample_time_json = [&](int index) -> Json {
            return index >= 0 && index < static_cast<int>(samples.size())
                ? Json(wf->format_time(samples[index].time, unit)) : Json(nullptr);
        };
        auto sampled_valid_json = [&](int index) -> Json {
            return index >= 0 && index < static_cast<int>(samples.size())
                ? analysis_logic_json(samples[index].valid, valid_info.width, format)
                : Json(nullptr);
        };
        auto sampled_payloads_json = [&](int index) {
            Json result = Json::array();
            if (index < 0 || index >= static_cast<int>(samples.size())) return result;
            for (size_t i = 0; i < payloads.size(); ++i)
                result.push_back({{"alias", payloads[i].alias},
                    {"signal", payloads[i].signal},
                    {"value", analysis_logic_json(samples[index].payload_values[i],
                                                    payloads[i].width, format)}});
            return result;
        };
        auto sampled_high_between = [&](uint64_t lo, uint64_t hi) {
            for (int i = lower_sample(lo); i < static_cast<int>(samples.size()) &&
                 samples[i].time < hi; ++i)
                if (analysis_truth(samples[i].valid) == AnalysisTri::True) return true;
            return false;
        };

        Json findings = Json::array();
        size_t total_findings = 0, unsampled_pulses = 0, payload_risks = 0;
        bool findings_truncated = false;
        auto add_finding = [&](const Json& finding) {
            ++total_findings;
            if (findings.size() < line_limit) findings.push_back(finding);
            else findings_truncated = true;
        };

        std::vector<uint32_t> valid_changes;
        for (uint32_t ti : wf->time_indices_of(valid_ref)) {
            const uint64_t time = wf->time_at(ti);
            if (time >= begin && time <= end) valid_changes.push_back(ti);
        }
        IWaveformBackend::SampledValue initial_valid;
        WaveValue current_valid;
        if (wf->sampled_value_at(valid_ref, wf->time_idx_of(begin),
                IWaveformBackend::ObservationPoint::Raw, initial_valid))
            current_valid = initial_valid.value;
        uint64_t segment_begin = begin;
        auto emit_pulse = [&](uint64_t lo, uint64_t hi, const WaveValue& raw) {
            if (hi <= lo || analysis_truth(raw) != AnalysisTri::True ||
                sampled_high_between(lo, hi)) return;
            const int next = lower_sample(lo);
            const int previous = next - 1;
            const int nearest = nearest_sample(lo);
            ++unsampled_pulses;
            add_finding({{"type", "unsampled_valid_pulse"},
                {"severity", "warning"}, {"raw_begin", wf->format_time(lo, unit)},
                {"raw_end", wf->format_time(hi, unit)},
                {"previous_sample_edge", sample_time_json(previous)},
                {"next_sample_edge", sample_time_json(next)},
                {"nearest_sample_edge", sample_time_json(nearest)},
                {"raw_valid", analysis_logic_json(raw, valid_info.width, format)},
                {"sampled_valid", sampled_valid_json(nearest)},
                {"sampled_payloads", sampled_payloads_json(nearest)},
                {"reason", "valid was high between sample edges but not high at any sampled edge"}});
        };
        for (uint32_t ti : valid_changes) {
            const uint64_t time = wf->time_at(ti);
            if (time == begin) {
                segment_begin = begin;
                continue;
            }
            emit_pulse(segment_begin, time, current_valid);
            IWaveformBackend::SampledValue changed;
            if (wf->sampled_value_at(valid_ref, ti,
                    IWaveformBackend::ObservationPoint::Raw, changed))
                current_valid = changed.value;
            segment_begin = time;
        }
        emit_pulse(segment_begin, end, current_valid);

        size_t payload_transition_count = 0;
        if (payload_reporting != "off") for (const Payload& payload : payloads) {
            for (uint32_t ti : wf->time_indices_of(payload.ref)) {
                const uint64_t time = wf->time_at(ti);
                if (time < begin || time > end) continue;
                ++payload_transition_count;
                const int nearest = nearest_sample(time);
                const AnalysisTri state = nearest < 0 ? AnalysisTri::Unknown
                    : analysis_truth(samples[nearest].valid);
                if (state == AnalysisTri::True) continue;
                ++payload_risks;
                if (payload_reporting != "all") continue;
                IWaveformBackend::SampledValue changed;
                wf->sampled_value_at(payload.ref, ti,
                    IWaveformBackend::ObservationPoint::Raw, changed);
                const int next = lower_sample(time);
                add_finding({{"type", "payload_changed_without_sampled_valid"},
                    {"severity", "warning"}, {"raw_time", wf->format_time(time, unit)},
                    {"previous_sample_edge", sample_time_json(next - 1)},
                    {"next_sample_edge", sample_time_json(next)},
                    {"nearest_sample_edge", sample_time_json(nearest)},
                    {"payload", {{"alias", payload.alias}, {"signal", payload.signal},
                        {"value", analysis_logic_json(changed.value, payload.width, format)}}},
                    {"sampled_valid", sampled_valid_json(nearest)},
                    {"sampled_payloads", sampled_payloads_json(nearest)},
                    {"reason", state == AnalysisTri::Unknown
                        ? "payload changed but sampled valid was unknown"
                        : "payload changed but valid was not sampled high by the DUT clock"}});
            }
        }
        if (payload_reporting == "summary") total_findings += payload_risks;

        const bool response_truncated = findings_truncated;
        Json scopes = Json::array();
        if (response_truncated) scopes.push_back("response_findings");
        Json summary{{"sampling_mode", "clock_edge"}, {"clock", clock},
            {"sample_time_semantics", "time is sample_time"},
            {"sample_count", samples.size()}, {"sampled_high_cycles", sampled_high},
            {"unsampled_valid_pulse_count", unsampled_pulses},
            {"payload_risk_count", payload_risks},
            {"payload_changed_without_sampled_valid_reporting", payload_reporting},
            {"scan_complete", true}, {"analysis_complete", true},
            {"response_truncated", response_truncated},
            {"total_count", total_findings}, {"returned_count", findings.size()},
            {"truncation_scopes", scopes}};
        Json payload_json = Json::array();
        for (const Payload& payload : payloads)
            payload_json.push_back({{"alias", payload.alias}, {"signal", payload.signal}});
        return {{"ok", true}, {"summary", summary}, {"data", {
            {"valid", valid}, {"payloads", payload_json},
            {"begin", wf->format_time(begin, unit)}, {"end", wf->format_time(end, unit)},
            {"sampled_low_cycles", sampled_low},
            {"sampled_unknown_cycles", sampled_unknown},
            {"raw_valid_transition_count", valid_changes.size()},
            {"payload_transition_count", payload_transition_count},
            {"first_sampled_high_time", first_high},
            {"last_sampled_high_time", last_high}, {"findings", findings},
            {"sampling", analysis_sampling_contract(edge, requested_point)}}}};
    }
};

// ── 4. protocol.handshake.inspect ──

struct ProtocolHandshakeInspectHandler : public EngineActionHandler {
    const char* action_name() const override { return "protocol.handshake.inspect"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const std::string clock = args.at("clock");
        const std::string valid = args.at("valid");
        const std::string ready = args.at("ready");
        Json error;
        uint32_t clock_ref = 0, valid_ref = 0, ready_ref = 0;
        IWaveformBackend::SignalInfo clock_info, valid_info, ready_info;
        if (!prepare_analysis_signal(*wf, clock, clock_ref, clock_info, error))
            return analysis_error("CLOCK_NOT_FOUND", "clock signal not found: " + clock);
        if (!prepare_analysis_signal(*wf, valid, valid_ref, valid_info, error)) return error;
        if (!prepare_analysis_signal(*wf, ready, ready_ref, ready_info, error)) return error;
        uint64_t begin = 0, end = 0;
        if (!parse_analysis_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_analysis_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);
        const std::string edge = args.value("edge", "negedge");
        if (edge != "posedge" && edge != "negedge" && edge != "dual")
            return analysis_error("INVALID_FIELD",
                "args.edge must be posedge, negedge, or dual");
        const std::string requested_point = args.value("sample_point", "");
        const auto point = analysis_observation_point(edge, requested_point);

        struct DataSignal { std::string alias; uint32_t ref; };
        std::vector<DataSignal> data_signals;
        if (args.contains("data")) {
            std::vector<std::string> paths;
            if (args.at("data").is_string()) paths.push_back(args.at("data"));
            else for (const Json& item : args.at("data")) paths.push_back(item);
            for (size_t i = 0; i < paths.size(); ++i) {
                uint32_t ref = 0;
                IWaveformBackend::SignalInfo info;
                if (!prepare_analysis_signal(*wf, paths[i], ref, info, error)) return error;
                data_signals.push_back({"data" + std::to_string(i), ref});
            }
        }
        const Json rules = args.value("rules", Json::object());
        const size_t max_wait = rules.value("max_wait_cycles", 100u);
        const bool check_data = rules.value("check_data_stable_when_stalled", false);
        const bool require_hold = rules.value("require_valid_hold_until_handshake", true);
        const std::string ready_reporting = rules.value("ready_without_valid", "summary");
        if (check_data != !data_signals.empty())
            return analysis_error("INVALID_FIELD", !data_signals.empty()
                ? "args.data requires rules.check_data_stable_when_stalled=true"
                : "rules.check_data_stable_when_stalled=true requires args.data");

        const size_t line_limit = args.value("line_limit", 100u);
        Json findings = Json::array();
        size_t finding_count = 0;
        auto add_finding = [&](const Json& finding) {
            ++finding_count;
            if (findings.size() < line_limit) findings.push_back(finding);
        };
        size_t samples = 0, transfers = 0, stall_cycles = 0, max_stall = 0;
        size_t ready_only_cycles = 0, ready_interval_count = 0;
        size_t data_violations = 0, valid_hold_violations = 0;
        bool in_stall = false, awaiting_handshake = false, in_ready_only = false;
        uint64_t stall_begin = 0, valid_wait_begin = 0;
        uint64_t ready_begin = 0, ready_end = 0;
        size_t ready_interval_cycles = 0;
        std::map<std::string, std::string> stall_data;
        Json ready_intervals = Json::array();
        auto finish_ready_interval = [&](uint64_t interval_end, bool open) {
            if (!in_ready_only) return;
            ++ready_interval_count;
            if (ready_reporting == "intervals") {
                Json interval{{"begin", wf->format_time(ready_begin, unit)},
                    {"end", wf->format_time(interval_end, unit)},
                    {"cycle_count", ready_interval_cycles}};
                if (open) interval["open_at_window_end"] = true;
                ready_intervals.push_back(interval);
            }
            in_ready_only = false;
            ready_interval_cycles = 0;
        };

        for (const AnalysisEdge& edge_row :
             collect_analysis_edges(*wf, clock_ref, begin, end, edge)) {
            IWaveformBackend::SampledValue valid_value, ready_value;
            if (!wf->sampled_value_at(valid_ref, edge_row.time_idx, point, valid_value) ||
                !wf->sampled_value_at(ready_ref, edge_row.time_idx, point, ready_value))
                continue;
            ++samples;
            const AnalysisTri v = analysis_truth(valid_value.value);
            const AnalysisTri r = analysis_truth(ready_value.value);
            const bool transfer = v == AnalysisTri::True && r == AnalysisTri::True;
            const bool stall = v == AnalysisTri::True && r == AnalysisTri::False;
            if (transfer) ++transfers;
            if (require_hold && awaiting_handshake) {
                if (transfer) awaiting_handshake = false;
                else if (v != AnalysisTri::True) {
                    ++valid_hold_violations;
                    add_finding({{"type", "valid_dropped_before_handshake"},
                        {"severity", "error"},
                        {"begin", wf->format_time(valid_wait_begin, unit)},
                        {"time", wf->format_time(edge_row.time, unit)},
                        {"observed_valid", analysis_logic_json(valid_value.value,
                                                               valid_info.width, format)},
                        {"reason", v == AnalysisTri::Unknown
                            ? "valid became unknown before a handshake"
                            : "valid deasserted before a handshake"}});
                    awaiting_handshake = false;
                }
            }
            if (require_hold && stall && !awaiting_handshake) {
                awaiting_handshake = true;
                valid_wait_begin = edge_row.time;
            }
            if (r == AnalysisTri::True && v == AnalysisTri::False) {
                ++ready_only_cycles;
                if (!in_ready_only) { in_ready_only = true; ready_begin = edge_row.time; }
                ready_end = edge_row.time;
                ++ready_interval_cycles;
                if (ready_reporting == "all")
                    add_finding({{"type", "ready_without_valid"},
                        {"severity", "info"},
                        {"time", wf->format_time(edge_row.time, unit)}});
            } else finish_ready_interval(ready_end, false);

            std::map<std::string, std::string> current_data;
            for (const DataSignal& data : data_signals) {
                IWaveformBackend::SampledValue value;
                wf->sampled_value_at(data.ref, edge_row.time_idx, point, value);
                current_data[data.alias] = value.value.text;
            }
            if (stall) {
                ++stall_cycles;
                if (!in_stall) {
                    in_stall = true;
                    stall_begin = edge_row.time;
                    stall_data = current_data;
                } else if (check_data) {
                    for (const auto& item : current_data) {
                        if (stall_data[item.first] == item.second) continue;
                        ++data_violations;
                        add_finding({{"type", "data_changed_while_stalled"},
                            {"severity", "warning"},
                            {"begin", wf->format_time(stall_begin, unit)},
                            {"time", wf->format_time(edge_row.time, unit)},
                            {"signal", item.first}});
                    }
                }
            } else if (in_stall) {
                max_stall = std::max(max_stall, stall_cycles);
                if (stall_cycles > max_wait)
                    add_finding({{"type", "long_stall"}, {"severity", "warning"},
                        {"begin", wf->format_time(stall_begin, unit)},
                        {"end", wf->format_time(edge_row.time, unit)},
                        {"cycles", stall_cycles}});
                in_stall = false;
                stall_cycles = 0;
            }
        }
        if (in_stall) {
            max_stall = std::max(max_stall, stall_cycles);
            if (stall_cycles > max_wait)
                add_finding({{"type", "long_stall"}, {"severity", "warning"},
                    {"begin", wf->format_time(stall_begin, unit)},
                    {"end", wf->format_time(end, unit)}, {"cycles", stall_cycles},
                    {"open_at_window_end", true}});
        }
        finish_ready_interval(ready_end, true);
        const bool response_truncated = findings.size() < finding_count;
        Json scopes = Json::array();
        if (response_truncated) scopes.push_back("response_findings");
        Json summary{{"sampling_mode", "clock_edge"}, {"clock", clock},
            {"sample_time_semantics", "time is sample_time"}, {"sample_count", samples},
            {"transfer_count", transfers}, {"max_stall_cycles", max_stall},
            {"ready_without_valid_cycles", ready_only_cycles},
            {"ready_without_valid_reporting", ready_reporting},
            {"ready_without_valid_interval_count", ready_interval_count},
            {"data_stability_violations", data_violations},
            {"require_valid_hold_until_handshake", require_hold},
            {"valid_hold_violations", valid_hold_violations},
            {"valid_wait_open_at_window_end", awaiting_handshake},
            {"scan_complete", true}, {"analysis_complete", true},
            {"response_truncated", response_truncated}, {"total_count", finding_count},
            {"returned_count", findings.size()}, {"truncation_scopes", scopes}};
        Json data{{"findings", findings},
            {"sampling", analysis_sampling_contract(edge, requested_point)}};
        if (ready_reporting == "intervals")
            data["ready_without_valid_intervals"] = ready_intervals;
        return {{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

// ── 5. counter.statistics ──

struct CounterStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "counter.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const std::string clock = args.at("clock");
        Json error;
        uint32_t clock_ref = 0;
        IWaveformBackend::SignalInfo clock_info;
        if (!prepare_analysis_signal(*wf, clock, clock_ref, clock_info, error))
            return analysis_error("CLOCK_NOT_FOUND", "clock signal not found: " + clock);
        uint64_t begin = 0, end = 0;
        if (!parse_analysis_range(*wf, args, begin, end, error)) return error;
        TimeRenderUnit unit;
        if (!parse_analysis_unit(args, unit, error)) return error;
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);
        const std::string edge = args.value("edge", "negedge");
        if (edge != "posedge" && edge != "negedge" && edge != "dual")
            return analysis_error("INVALID_FIELD",
                "args.edge must be posedge, negedge, or dual");
        const std::string requested_point = args.value("sample_point", "");
        const auto point = analysis_observation_point(edge, requested_point);
        const size_t line_limit = args.value("line_limit", 100u);
        const size_t max_samples = args.value(
            "max_samples", std::numeric_limits<size_t>::max());

        std::vector<std::string> counter_paths;
        std::string counter_text = args.at("cnt");
        auto trim = [](std::string text) {
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::string();
            const size_t last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        };
        if (counter_text.size() >= 2 && counter_text.front() == '{' &&
            counter_text.back() == '}') {
            std::string body = counter_text.substr(1, counter_text.size() - 2);
            size_t start = 0;
            while (start <= body.size()) {
                const size_t comma = body.find(',', start);
                std::string path = trim(body.substr(start,
                    comma == std::string::npos ? std::string::npos : comma - start));
                if (path.empty())
                    return analysis_error("INVALID_FIELD", "args.cnt has an empty concatenation item");
                counter_paths.push_back(path);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else counter_paths.push_back(counter_text);

        struct CounterSignal { uint32_t ref; uint32_t width; };
        std::vector<CounterSignal> counters;
        uint32_t counter_width = 0;
        for (const std::string& path : counter_paths) {
            uint32_t ref = 0;
            IWaveformBackend::SignalInfo info;
            if (!prepare_analysis_signal(*wf, path, ref, info, error)) return error;
            if (info.encoding != IWaveformBackend::ValueKind::BitVector)
                return analysis_error("INVALID_SIGNAL_TYPE",
                    "counter signal must be a bit vector: " + path);
            counters.push_back({ref, info.width});
            counter_width += info.width;
        }
        if (counter_width > 64)
            return analysis_error("INVALID_FIELD",
                "args.cnt combined width exceeds the 64-bit counter contract");

        std::unique_ptr<ExprNode> valid_expression;
        uint32_t valid_ref = 0;
        if (args.at("vld").is_string()) {
            IWaveformBackend::SignalInfo info;
            if (!prepare_analysis_signal(*wf, args.at("vld"), valid_ref, info, error))
                return error;
        } else {
            const Json predicate = args.at("vld");
            std::string message;
            valid_expression.reset(parse_expression(predicate.at("expr"), message));
            if (!valid_expression ||
                !bind_expr_aliases(valid_expression.get(), predicate.at("signals"), message))
                return analysis_error("EXPRESSION_INVALID", message);
            for (auto it = predicate.at("signals").begin();
                 it != predicate.at("signals").end(); ++it) {
                uint32_t ref = 0;
                IWaveformBackend::SignalInfo info;
                if (!prepare_analysis_signal(*wf, it.value(), ref, info, error)) return error;
            }
        }

        Json evidence = Json::array();
        size_t evidence_count = 0;
        auto add_evidence = [&](uint64_t time, const char* kind, const Json& value) {
            ++evidence_count;
            if (evidence.size() < line_limit)
                evidence.push_back({{"time", wf->format_time(time, unit)},
                                    {"kind", kind}, {"value", value}});
        };
        size_t samples = 0, valid_count = 0, valid_false = 0, unknown = 0;
        bool scan_complete = true, have_value = false, have_previous = false;
        uint64_t min_value = 0, max_value = 0, previous_value = 0;
        std::string min_bits, max_bits;
        size_t min_count = 0, max_count = 0;
        uint64_t min_first_time = 0, max_first_time = 0;
        long double sum = 0;
        for (const AnalysisEdge& edge_row :
             collect_analysis_edges(*wf, clock_ref, begin, end, edge)) {
            if (samples >= max_samples) { scan_complete = false; break; }
            ++samples;
            AnalysisTri valid_state = AnalysisTri::Unknown;
            if (valid_expression) {
                const LogicValue result = eval_expression(valid_expression.get(), *wf,
                    edge_row.time_idx, nullptr, point);
                valid_state = !result.known ? AnalysisTri::Unknown
                    : result.bits.find('1') == std::string::npos
                        ? AnalysisTri::False : AnalysisTri::True;
            } else {
                IWaveformBackend::SampledValue value;
                if (wf->sampled_value_at(valid_ref, edge_row.time_idx, point, value))
                    valid_state = analysis_truth(value.value);
            }
            if (valid_state == AnalysisTri::False) { ++valid_false; continue; }
            if (valid_state == AnalysisTri::Unknown) {
                ++unknown;
                add_evidence(edge_row.time, "unknown_valid", nullptr);
                continue;
            }
            std::string bits;
            bool known_counter = true;
            for (const CounterSignal& counter : counters) {
                IWaveformBackend::SampledValue value;
                if (!wf->sampled_value_at(counter.ref, edge_row.time_idx, point, value) ||
                    value.value.kind != IWaveformBackend::ValueKind::BitVector) {
                    known_counter = false;
                    break;
                }
                bits += value.value.text;
            }
            uint64_t value = 0;
            if (!known_counter || !bits_to_u64(bits, value)) {
                ++unknown;
                add_evidence(edge_row.time, "unknown_counter", nullptr);
                continue;
            }
            WaveValue wave_value;
            wave_value.kind = IWaveformBackend::ValueKind::BitVector;
            wave_value.text = bits;
            if (!have_previous || value != previous_value) {
                add_evidence(edge_row.time, have_previous ? "value_change" : "initial",
                    analysis_logic_json(wave_value, counter_width, format));
                previous_value = value;
                have_previous = true;
            }
            ++valid_count;
            sum += static_cast<long double>(value);
            if (!have_value) {
                have_value = true;
                min_value = max_value = value;
                min_bits = max_bits = bits;
                min_count = max_count = 1;
                min_first_time = max_first_time = edge_row.time;
            } else {
                if (value < min_value) {
                    min_value = value; min_bits = bits; min_count = 1;
                    min_first_time = edge_row.time;
                } else if (value == min_value) ++min_count;
                if (value > max_value) {
                    max_value = value; max_bits = bits; max_count = 1;
                    max_first_time = edge_row.time;
                } else if (value == max_value) ++max_count;
            }
        }

        const bool response_truncated = evidence.size() < evidence_count;
        Json scopes = Json::array();
        if (!scan_complete) scopes.push_back("analysis_samples");
        if (response_truncated) scopes.push_back("response_evidence");
        Json summary{{"sample_count", samples}, {"valid_count", valid_count},
            {"sampling_mode", "clock_edge"}, {"clock", clock},
            {"sample_time_semantics", "time is sample_time"},
            {"begin", wf->format_time(begin, unit)}, {"end", wf->format_time(end, unit)},
            {"valid_false_count", valid_false}, {"unknown_count", unknown},
            {"scan_complete", scan_complete}, {"analysis_complete", scan_complete},
            {"response_truncated", response_truncated},
            {"total_count", evidence_count}, {"returned_count", evidence.size()},
            {"truncation_scopes", scopes}};
        Json data{{"cnt", args.at("cnt")}, {"vld", args.at("vld")},
            {"evidence", evidence},
            {"sampling", analysis_sampling_contract(edge, requested_point)}};
        if (have_value) {
            WaveValue min_wave, max_wave;
            min_wave.kind = max_wave.kind = IWaveformBackend::ValueKind::BitVector;
            min_wave.text = min_bits;
            max_wave.text = max_bits;
            summary["min_value"] = analysis_logic_json(min_wave, counter_width, format);
            summary["max_value"] = analysis_logic_json(max_wave, counter_width, format);
            summary["average_value"] = decimal_average(
                sum / static_cast<long double>(valid_count));
            data["min_count"] = min_count;
            data["max_count"] = max_count;
            data["min_first_time"] = wf->format_time(min_first_time, unit);
            data["max_first_time"] = wf->format_time(max_first_time, unit);
        }
        return {{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

// ── Factory functions ──

std::unique_ptr<EngineActionHandler> make_clock_point_query_handler() {
    return std::make_unique<ClockPointQueryHandler>();
}
std::unique_ptr<EngineActionHandler> make_expr_eval_at_handler() {
    return std::make_unique<ExprEvalAtHandler>();
}
std::unique_ptr<EngineActionHandler> make_signal_sampled_pulse_inspect_handler() {
    return std::make_unique<SignalSampledPulseInspectHandler>();
}
std::unique_ptr<EngineActionHandler> make_protocol_handshake_inspect_handler() {
    return std::make_unique<ProtocolHandshakeInspectHandler>();
}
std::unique_ptr<EngineActionHandler> make_counter_statistics_handler() {
    return std::make_unique<CounterStatisticsHandler>();
}

} // namespace xdebug_fst
