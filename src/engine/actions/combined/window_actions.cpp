// window_actions.cpp — window.verify, verify.conditions (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"
#include "core/value/logic_value.h"
#include "waveform/expr/expr_eval.h"
#include "waveform/clock_sampling.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── shared helpers ──

static Json render_val_json(const std::string& bits, int width, ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, width);
    return logic_value_json(v, fmt);
}

static bool logic_value_truthy(const LogicValue& v) {
    if (!v.known) return false;
    // Any non-zero bit makes it truthy
    for (char c : v.bits) {
        if (c == '1') return true;
        if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') return false;
    }
    return false;
}

// ── window.verify ──

struct WindowVerifyHandler : public EngineActionHandler {
    const char* action_name() const override { return "window.verify"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: window.verify"}}}};
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
                                   {"message", "signal not found: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        uint64_t begin_t = 0;
        uint64_t end_t = wf->max_time();
        if (args.contains("begin")) {
            try { begin_t = std::stoull(args["begin"].get<std::string>()); }
            catch (...) { return Json{{"ok", false}, {"error", {{"code", "INVALID_TIME"}, {"message", "args.begin must be an integer"}}}}; }
        }
        if (args.contains("end")) {
            try { end_t = std::stoull(args["end"].get<std::string>()); }
            catch (...) { return Json{{"ok", false}, {"error", {{"code", "INVALID_TIME"}, {"message", "args.end must be an integer"}}}}; }
        }

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        // Parse optional expression
        std::string expr_str = args.value("expression", "");
        ExprNode* expr_root = nullptr;
        std::vector<uint32_t> expr_sig_refs;
        if (!expr_str.empty()) {
            std::string parse_err;
            expr_root = parse_expression(expr_str, parse_err);
            if (!expr_root) {
                return Json{{"ok", false},
                            {"error", {{"code", "PARSE_ERROR"},
                                       {"message", parse_err.empty() ? "expression parse error" : parse_err}}}};
            }
            // Pre-load expression signals
            std::vector<std::string> esigs = expression_signals(expr_root);
            for (auto& es : esigs) {
                uint32_t er = wf->find_signal(es);
                if (er && !wf->is_loaded(er)) {
                    wf->load_signals({er});
                    expr_sig_refs.push_back(er);
                }
            }
        }

        // Optional expected value
        std::string expect_str = args.value("expect", "");

        // Collect sample points
        std::string clock_sig = args.value("clock", "");
        std::vector<std::pair<uint32_t, uint64_t>> sample_points; // (time_idx, time)

        if (!clock_sig.empty()) {
            // Use clock edges
            uint32_t clk_ref = wf->find_signal(clock_sig);
            if (clk_ref == IWaveformBackend::kInvalidSignalRef) {
                delete expr_root;
                return Json{{"ok", false},
                            {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                       {"message", "clock signal not found: " + clock_sig}}}};
            }
            if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});

            bool rising = true;
            bool falling = false;
            std::string edge_str = args.value("edge", "rising");
            if (edge_str == "falling") { rising = false; falling = true; }
            else if (edge_str == "both") { rising = true; falling = true; }

            ClockSampleScanner scanner(*wf, clk_ref, ref, rising, falling);
            std::vector<ClockSample> samples;
            uint32_t begin_ti = wf->time_idx_of(begin_t);
            uint32_t end_ti = wf->time_idx_of(end_t);
            scanner.scan(begin_ti, end_ti, samples);

            for (auto& s : samples) {
                sample_points.push_back({s.time_idx, s.time});
            }
        } else {
            // No clock: sample at every change point within window
            std::vector<uint32_t> ti_vec = wf->time_indices_of(ref);
            for (uint32_t ti : ti_vec) {
                uint64_t tt = wf->time_at(ti);
                if (tt >= begin_t && tt <= end_t) {
                    // Only include points where the signal actually changed
                    IWaveformBackend::SignalOffset off;
                    if (wf->signal_offset_at(ref, ti, off) && off.time_match) {
                        sample_points.push_back({ti, tt});
                    }
                }
            }
        }

        Json checks = Json::array();
        int pass_count = 0;
        int fail_count = 0;

        for (auto& sp : sample_points) {
            uint32_t ti = sp.first;
            uint64_t tt = sp.second;

            IWaveformBackend::SignalOffset off;
            if (!wf->signal_offset_at(ref, ti, off)) continue;
            std::string bits = wf->signal_value_str(ref, off.start, 0);

            Json check;
            check["time"] = tt;
            check["time_idx"] = ti;
            check["value"] = render_val_json(bits, static_cast<int>(info.width), fmt);

            bool pass = true;
            bool has_check = false;

            // Evaluate expression if provided
            if (expr_root) {
                LogicValue ev = eval_expression(expr_root, *wf, ti);
                bool expr_pass = logic_value_truthy(ev);
                check["expression_result"] = logic_value_json(ev, fmt);
                pass = pass && expr_pass;
                has_check = true;
            }

            // Compare with expected value if provided
            if (!expect_str.empty()) {
                bool val_match = false;
                LogicValue exp_v;
                if (parse_sv_literal(expect_str, exp_v)) {
                    val_match = (bits == exp_v.bits);
                } else {
                    val_match = (bits == expect_str);
                }
                check["expected"] = expect_str;
                pass = pass && val_match;
                has_check = true;
            }

            check["pass"] = has_check ? pass : true;  // if no checks, default pass
            if (check["pass"].get<bool>()) pass_count++;
            else fail_count++;

            checks.push_back(check);
        }

        delete expr_root;

        return Json{
            {"ok", true},
            {"summary", {
                {"check_count", checks.size()},
                {"pass_count", pass_count},
                {"fail_count", fail_count},
                {"verdict", fail_count == 0 ? "pass" : "fail"}
            }},
            {"data", {{"checks", checks}}}
        };
    }
};

// ── verify.conditions ──

struct VerifyConditionsHandler : public EngineActionHandler {
    const char* action_name() const override { return "verify.conditions"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: verify.conditions"}}}};
        }

        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        auto conditions = args.value("conditions", Json::array());

        if (sig.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal is required"}}}};
        }
        if (!conditions.is_array() || conditions.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.conditions must be a non-empty array"}}}};
        }

        auto* wf = g.waveform.get();
        uint32_t ref = wf->find_signal(sig);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            return Json{{"ok", false},
                        {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                   {"message", "signal not found: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        Json results = Json::array();
        int pass_count = 0;
        int fail_count = 0;

        int cond_idx = 0;
        for (auto& cond : conditions) {
            if (!cond.is_object()) {
                cond_idx++;
                continue;
            }

            std::string expect = cond.value("expect", "");
            // Get the time or range for this condition
            std::vector<std::pair<uint32_t, uint64_t>> check_points; // (time_idx, time)

            if (cond.contains("time") && cond["time"].is_string()) {
                uint64_t ct = 0;
                try { ct = std::stoull(cond["time"].get<std::string>()); }
                catch (...) { cond_idx++; continue; }
                uint32_t cti = wf->time_idx_of(ct);
                check_points.push_back({cti, ct});
            } else if (cond.contains("begin") || cond.contains("end")) {
                uint64_t cb = 0, ce = wf->max_time();
                if (cond.contains("begin")) {
                    try { cb = std::stoull(cond["begin"].get<std::string>()); }
                    catch (...) { cond_idx++; continue; }
                }
                if (cond.contains("end")) {
                    try { ce = std::stoull(cond["end"].get<std::string>()); }
                    catch (...) { cond_idx++; continue; }
                }
                // Sample at every change point within range
                std::vector<uint32_t> ti_vec = wf->time_indices_of(ref);
                for (uint32_t ti : ti_vec) {
                    uint64_t tt = wf->time_at(ti);
                    if (tt >= cb && tt <= ce) {
                        IWaveformBackend::SignalOffset off;
                        if (wf->signal_offset_at(ref, ti, off) && off.time_match) {
                            check_points.push_back({ti, tt});
                        }
                    }
                }
            } else {
                // No time specified — skip this condition
                cond_idx++;
                continue;
            }

            ValueRenderFormat cf = fmt;
            if (cond.contains("render_format")) {
                parse_value_render_format(cond["render_format"].get<std::string>(), cf);
            }

            for (auto& cp : check_points) {
                uint32_t ti = cp.first;
                uint64_t tt = cp.second;

                IWaveformBackend::SignalOffset off;
                if (!wf->signal_offset_at(ref, ti, off)) continue;
                std::string bits = wf->signal_value_str(ref, off.start, 0);

                // Compare with expected
                bool pass = false;
                if (!expect.empty()) {
                    LogicValue exp_v;
                    if (parse_sv_literal(expect, exp_v)) {
                        pass = (bits == exp_v.bits);
                    } else {
                        pass = (bits == expect);
                    }
                }

                Json r;
                r["condition_index"] = cond_idx;
                r["time"] = tt;
                r["time_idx"] = ti;
                r["actual"] = render_val_json(bits, static_cast<int>(info.width), cf);
                r["expected"] = expect;
                r["pass"] = pass;

                if (pass) pass_count++;
                else fail_count++;

                results.push_back(r);
            }
            cond_idx++;
        }

        return Json{
            {"ok", true},
            {"summary", {
                {"pass_count", pass_count},
                {"fail_count", fail_count}
            }},
            {"data", {{"results", results}}}
        };
    }
};

std::unique_ptr<EngineActionHandler> make_window_verify_handler() {
    return std::make_unique<WindowVerifyHandler>();
}
std::unique_ptr<EngineActionHandler> make_verify_conditions_handler() {
    return std::make_unique<VerifyConditionsHandler>();
}

} // namespace xdebug_fst
