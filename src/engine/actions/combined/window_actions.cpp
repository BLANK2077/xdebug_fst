// window_actions.cpp — verify.conditions and window.verify (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/expr/expr_eval.h"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

Json action_error(const std::string& code, const std::string& message) {
    return Json{{"ok", false},
                {"error", {{"code", code}, {"message", message}}}};
}

bool truthy(const LogicValue& value) {
    return value.known &&
        value.bits.find('1') != std::string::npos;
}

bool bind_aliases(ExprNode* node, const Json& aliases, std::string& error) {
    if (!node) return false;
    if (node->kind == ExprNode::Kind::Signal ||
        node->kind == ExprNode::Kind::Slice) {
        if (node->signal.find('.') != std::string::npos) {
            error = "expression operands must be aliases, not direct signal paths: " +
                    node->signal + "; put real signal paths in args.signals";
            return false;
        }
        if (!aliases.contains(node->signal)) {
            error = "condition references undeclared signal alias: " + node->signal;
            return false;
        }
        node->signal = aliases.at(node->signal).get<std::string>();
    }
    return (!node->left || bind_aliases(node->left, aliases, error)) &&
           (!node->right || bind_aliases(node->right, aliases, error));
}

std::set<std::string> expression_alias_set(const ExprNode* node) {
    std::set<std::string> result;
    for (const std::string& signal : expression_signals(node))
        result.insert(signal);
    return result;
}

bool parse_sampling(const Json& args, std::string& edge,
                    std::string& requested_point, Json& error) {
    edge = args.value("edge", "negedge");
    if (edge != "posedge" && edge != "negedge" && edge != "dual") {
        error = action_error("INVALID_FIELD",
                             "args.edge must be posedge, negedge, or dual");
        return false;
    }
    requested_point = args.value("sample_point", std::string());
    return true;
}

IWaveformBackend::ObservationPoint observation_point(
    const std::string& edge, const std::string& requested_point) {
    if (edge == "negedge") return IWaveformBackend::ObservationPoint::Raw;
    return requested_point == "after"
        ? IWaveformBackend::ObservationPoint::After
        : IWaveformBackend::ObservationPoint::Before;
}

Json sampling_contract(const std::string& edge,
                       const std::string& requested_point) {
    const bool negedge = edge == "negedge";
    const std::string effective_point = negedge ? std::string()
        : (requested_point.empty() ? "before" : requested_point);
    Json output{
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
        output["sample_point_not_applied_reason"] =
            "negedge keeps the established current-value sampling semantics";
    return output;
}

bool selected_edge(const std::string& edge, bool rising, bool falling) {
    return edge == "dual" ? rising || falling
        : edge == "posedge" ? rising : falling;
}

struct EdgeRecord {
    uint32_t time_idx = 0;
    uint64_t time = 0;
    std::string kind;
};

std::vector<EdgeRecord> clock_edges(IWaveformBackend& wf, uint32_t clock_ref) {
    std::vector<EdgeRecord> result;
    uint32_t previous_ti = std::numeric_limits<uint32_t>::max();
    for (uint32_t ti : wf.time_indices_of(clock_ref)) {
        if (ti == previous_ti) continue;
        previous_ti = ti;
        IWaveformBackend::SampledValue before, raw;
        if (!wf.sampled_value_at(clock_ref, ti,
                IWaveformBackend::ObservationPoint::Before, before) ||
            !wf.sampled_value_at(clock_ref, ti,
                IWaveformBackend::ObservationPoint::Raw, raw)) continue;
        const bool rising = is_rising_edge(before.value.text, raw.value.text);
        const bool falling = is_falling_edge(before.value.text, raw.value.text);
        if (!rising && !falling) continue;
        result.push_back({ti, wf.time_at(ti), rising ? "posedge" : "negedge"});
    }
    return result;
}

Json clock_context(IWaveformBackend& wf, const std::string& clock,
                   const std::vector<EdgeRecord>& edges, uint64_t time,
                   const std::string& edge, const std::string& requested_point,
                   TimeRenderUnit unit) {
    Json previous = nullptr, next = nullptr;
    std::string exact_kind;
    for (const EdgeRecord& item : edges) {
        if (item.time == time) exact_kind = item.kind;
        const bool selected = edge == "dual" || edge == item.kind;
        if (!selected) continue;
        if (item.time < time) previous = wf.format_time(item.time, unit);
        else if (item.time > time && next.is_null())
            next = wf.format_time(item.time, unit);
    }
    Json sampling = sampling_contract(edge, requested_point);
    Json output{{"clock", clock},
        {"requested_sampling", sampling.at("requested")},
        {"effective_sampling", sampling.at("effective")},
        {"sample_point_applied", sampling.at("sample_point_applied")},
        {"sample_point_ignored_for_negedge",
         sampling.at("sample_point_ignored_for_negedge")},
        {"requested_time", wf.format_time(time, unit)},
        {"requested_any_edge_hit", !exact_kind.empty()},
        {"clock_edge_kind", exact_kind.empty() ? Json(nullptr)
                                                : Json(exact_kind)},
        {"requested_target_edge_hit", !exact_kind.empty() &&
            (edge == "dual" || edge == exact_kind)},
        {"previous_sample_time", previous}, {"next_sample_time", next},
        {"bracket_complete", !previous.is_null() && !next.is_null()}};
    if (sampling.contains("sample_point_not_applied_reason"))
        output["sample_point_not_applied_reason"] =
            sampling.at("sample_point_not_applied_reason");
    return output;
}

bool prepare_inputs(IWaveformBackend& wf, const Json& aliases,
                    std::map<std::string, uint32_t>& refs,
                    std::map<std::string, uint32_t>& widths, Json& error) {
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        const std::string path = it.value();
        const uint32_t ref = wf.find_signal(path);
        if (!ref) {
            error = action_error("SIGNAL_NOT_FOUND",
                                 "signal not found in waveform: " + path);
            return false;
        }
        if (!wf.is_loaded(ref) && wf.load_signals({ref}) != 1) {
            error = action_error("VALUE_NOT_AVAILABLE",
                                 "failed to load waveform signal: " + path);
            return false;
        }
        IWaveformBackend::SignalInfo info;
        if (!wf.signal_info(ref, info) ||
            info.encoding != IWaveformBackend::ValueKind::BitVector) {
            error = action_error("INVALID_SIGNAL_TYPE",
                "condition operands must resolve to bit-vector signals: " + path);
            return false;
        }
        refs[it.key()] = ref;
        widths[it.key()] = info.width;
    }
    return true;
}

Json rendered_values(IWaveformBackend& wf, const Json& aliases,
                     const std::map<std::string, uint32_t>& refs,
                     const std::map<std::string, uint32_t>& widths,
                     uint32_t time_idx,
                     IWaveformBackend::ObservationPoint point,
                     ValueRenderFormat format) {
    Json output = Json::object();
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        IWaveformBackend::SampledValue sampled;
        if (!wf.sampled_value_at(refs.at(it.key()), time_idx, point, sampled))
            continue;
        LogicValue value = logic_value_from_bits(sampled.value.text,
                                                 widths.at(it.key()));
        output[it.key()] = logic_value_json(value, format);
    }
    return output;
}

}  // namespace

struct VerifyConditionsHandler : public EngineActionHandler {
    const char* action_name() const override { return "verify.conditions"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const Json aliases = args.at("signals");
        Json error;
        std::string edge, requested_point;
        if (!parse_sampling(args, edge, requested_point, error)) return error;
        uint64_t time = 0;
        std::string message;
        if (!wf->parse_time(args.at("time"), time, message))
            return action_error("INVALID_TIME", message);
        TimeRenderUnit unit;
        if (!parse_time_render_unit(args.value("render_time_unit", "ns"),
                                    unit, message))
            return action_error("INVALID_TIME_UNIT", message);
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);

        std::map<std::string, uint32_t> refs, widths;
        if (!prepare_inputs(*wf, aliases, refs, widths, error)) return error;
        const std::string clock = args.at("clock");
        const uint32_t clock_ref = wf->find_signal(clock);
        if (!clock_ref)
            return action_error("CLOCK_NOT_FOUND", "clock signal not found: " + clock);
        wf->load_signals({clock_ref});
        const std::vector<EdgeRecord> edges = clock_edges(*wf, clock_ref);
        const uint32_t time_idx = wf->time_idx_of(time);
        const auto point = observation_point(edge, requested_point);

        struct Parsed {
            Json source;
            std::unique_ptr<ExprNode> root;
        };
        std::vector<Parsed> parsed;
        std::set<std::string> all_referenced;
        for (const Json& condition : args.at("conditions")) {
            std::string parse_error;
            std::unique_ptr<ExprNode> root(
                parse_expression(condition.at("expr"), parse_error));
            if (!root)
                return action_error("PARSE_ERROR", parse_error);
            const std::set<std::string> referenced =
                expression_alias_set(root.get());
            all_referenced.insert(referenced.begin(), referenced.end());
            if (!bind_aliases(root.get(), aliases, parse_error))
                return action_error("INVALID_FIELD", parse_error);
            parsed.push_back({condition, std::move(root)});
        }
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            if (!all_referenced.count(it.key()))
                return action_error("INVALID_FIELD",
                    "signal alias is unused by every condition: " + it.key());
        }

        Json checks = Json::array();
        size_t passed = 0, failed = 0, unknown = 0;
        for (const Parsed& item : parsed) {
            const LogicValue evaluated = eval_expression(
                item.root.get(), *wf, time_idx, nullptr, point);
            Json check{{"time", wf->format_time(time, unit)},
                       {"expr", item.source.at("expr")}};
            if (item.source.contains("name")) check["name"] = item.source.at("name");
            if (!evaluated.known) {
                check["known"] = false;
                check["status"] = "unknown";
                check["pass"] = nullptr;
                check["value"] = logic_value_json(evaluated, format);
                ++unknown;
            } else {
                const bool pass = truthy(evaluated);
                check["known"] = true;
                check["status"] = pass ? "pass" : "fail";
                check["pass"] = pass;
                check["value"] = logic_value_json(evaluated, format);
                if (pass) ++passed; else ++failed;
            }
            checks.push_back(check);
        }
        const bool all_passed = failed == 0 && unknown == 0;
        Json summary{{"time", wf->format_time(time, unit)},
            {"execution_ok", true}, {"verdict", all_passed ? "pass" : "fail"},
            {"condition_count", checks.size()}, {"all_passed", all_passed},
            {"passed", passed}, {"failed", failed}, {"unknown", unknown}};
        Json data{{"checks", checks},
            {"clock_context", clock_context(*wf, clock, edges, time, edge,
                                             requested_point, unit)}};
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

struct WindowVerifyHandler : public EngineActionHandler {
    const char* action_name() const override { return "window.verify"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto* wf = engine_globals().waveform.get();
        const Json args = req.at("args");
        const Json aliases = args.at("signals");
        Json error;
        std::string edge, requested_point;
        if (!parse_sampling(args, edge, requested_point, error)) return error;
        TimeRenderUnit unit;
        std::string message;
        if (!parse_time_render_unit(args.value("render_time_unit", "ns"),
                                    unit, message))
            return action_error("INVALID_TIME_UNIT", message);
        ValueRenderFormat format = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), format);
        uint64_t begin = wf->min_time(), end = wf->max_time();
        const Json range = args.value("time_range", Json::object());
        if (range.contains("begin") &&
            !wf->parse_time(range.at("begin"), begin, message))
            return action_error("INVALID_TIME", message);
        if (range.contains("end") &&
            !wf->parse_time(range.at("end"), end, message, true))
            return action_error("INVALID_TIME", message);
        if (begin > end)
            return action_error("TIME_RANGE_INVALID", "end is before begin");

        std::map<std::string, uint32_t> refs, widths;
        if (!prepare_inputs(*wf, aliases, refs, widths, error)) return error;
        const std::string clock = args.at("clock");
        const uint32_t clock_ref = wf->find_signal(clock);
        if (!clock_ref)
            return action_error("CLOCK_NOT_FOUND", "clock signal not found: " + clock);
        wf->load_signals({clock_ref});
        const std::vector<EdgeRecord> edges = clock_edges(*wf, clock_ref);
        const auto point = observation_point(edge, requested_point);

        struct State {
            std::string expr;
            std::string mode;
            std::unique_ptr<ExprNode> root;
            size_t passed = 0;
            size_t failed = 0;
            size_t unknown = 0;
        };
        std::vector<State> states;
        std::set<std::string> all_referenced;
        for (const Json& condition : args.at("conditions")) {
            std::string parse_error;
            std::unique_ptr<ExprNode> root(
                parse_expression(condition.at("expr"), parse_error));
            if (!root) return action_error("PARSE_ERROR", parse_error);
            const std::set<std::string> referenced =
                expression_alias_set(root.get());
            all_referenced.insert(referenced.begin(), referenced.end());
            if (!bind_aliases(root.get(), aliases, parse_error))
                return action_error("INVALID_FIELD", parse_error);
            states.push_back({condition.at("expr"),
                condition.value("mode", "always"), std::move(root), 0, 0, 0});
        }
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            if (!all_referenced.count(it.key()))
                return action_error("INVALID_FIELD",
                    "signal alias is unused by every condition: " + it.key());
        }

        const size_t evidence_limit = args.value("line_limit", 100u);
        const size_t sample_limit = args.value(
            "max_samples", std::numeric_limits<size_t>::max());
        size_t samples = 0, finding_count = 0;
        bool truncated = false, decisive = false, have_sample = false;
        uint64_t first_sample = 0, last_sample = 0;
        Json findings = Json::array();
        for (const EdgeRecord& clock_edge : edges) {
            if (clock_edge.time < begin || clock_edge.time > end) continue;
            const bool rising = clock_edge.kind == "posedge";
            if (!selected_edge(edge, rising, !rising)) continue;
            if (samples >= sample_limit) {
                truncated = true;
                break;
            }
            ++samples;
            if (!have_sample) {
                first_sample = clock_edge.time;
                have_sample = true;
            }
            last_sample = clock_edge.time;
            bool has_eventually = false;
            bool all_eventually_seen = true;
            for (State& state : states) {
                const LogicValue value = eval_expression(
                    state.root.get(), *wf, clock_edge.time_idx, nullptr, point);
                const bool unknown = !value.known;
                const bool raw_truth = truthy(value);
                const bool pass = state.mode == "never" ? !raw_truth : raw_truth;
                if (unknown) ++state.unknown;
                else if (pass) ++state.passed;
                else ++state.failed;
                if (unknown || !pass) {
                    ++finding_count;
                    if (findings.size() < evidence_limit)
                        findings.push_back({
                            {"time", wf->format_time(clock_edge.time, unit)},
                            {"expr", state.expr}, {"mode", state.mode},
                            {"status", unknown ? "unknown" : "fail"},
                            {"signals", rendered_values(*wf, aliases, refs,
                                widths, clock_edge.time_idx, point, format)}});
                }
                if (state.mode == "eventually") {
                    has_eventually = true;
                    if (state.passed == 0) all_eventually_seen = false;
                } else if (unknown || !pass) {
                    decisive = true;
                    break;
                }
            }
            if (decisive) break;
            if (has_eventually && all_eventually_seen) {
                decisive = true;
                break;
            }
        }

        Json condition_results = Json::array();
        bool all_passed = true;
        size_t failed_samples = 0, unknown_samples = 0;
        for (const State& state : states) {
            const bool passed = state.mode == "eventually"
                ? state.passed > 0
                : state.failed == 0 && state.unknown == 0;
            if (!passed) all_passed = false;
            failed_samples += state.failed;
            unknown_samples += state.unknown;
            condition_results.push_back({{"expr", state.expr},
                {"mode", state.mode}, {"passed", passed},
                {"pass_samples", state.passed},
                {"failed_samples", state.failed},
                {"unknown_samples", state.unknown}});
        }
        const bool scan_complete = !truncated;
        const bool analysis_complete = decisive || scan_complete;
        const bool response_truncated = findings.size() < finding_count;
        Json scopes = Json::array();
        if (truncated) scopes.push_back("analysis_samples");
        if (response_truncated) scopes.push_back("response_findings");
        Json summary{{"execution_ok", true},
            {"verdict", !analysis_complete ? "inconclusive"
                : all_passed ? "pass" : "fail"},
            {"all_passed", analysis_complete ? Json(all_passed) : Json(nullptr)},
            {"sample_count", samples}, {"failed_samples", failed_samples},
            {"unknown_samples", unknown_samples},
            {"proof_begin", wf->format_time(begin, unit)},
            {"proof_end", wf->format_time(end, unit)},
            {"scanned_range", {{"begin", have_sample
                    ? Json(wf->format_time(first_sample, unit)) : Json(nullptr)},
                               {"end", have_sample
                    ? Json(wf->format_time(last_sample, unit)) : Json(nullptr)}}},
            {"stop_reason", decisive ? "decisive_result"
                : truncated ? "max_samples" : "window_end"},
            {"sampling_mode", "clock_edge"}, {"clock", clock},
            {"sample_time_semantics", "time is sample_time"},
            {"scan_complete", scan_complete},
            {"analysis_complete", analysis_complete},
            {"response_truncated", response_truncated},
            {"total_count", finding_count},
            {"returned_count", findings.size()},
            {"truncation_scopes", scopes}};
        Json data{{"conditions", condition_results}, {"findings", findings},
                  {"sampling", sampling_contract(edge, requested_point)}};
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

std::unique_ptr<EngineActionHandler> make_window_verify_handler() {
    return std::make_unique<WindowVerifyHandler>();
}
std::unique_ptr<EngineActionHandler> make_verify_conditions_handler() {
    return std::make_unique<VerifyConditionsHandler>();
}

}  // namespace xdebug_fst
