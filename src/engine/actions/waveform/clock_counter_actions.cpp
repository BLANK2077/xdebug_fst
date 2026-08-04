// clock_counter_actions.cpp — clock_point_query, expr.eval_at,
// signal.sampled_pulse.inspect, protocol.handshake.inspect,
// counter.statistics handlers (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/expr/expr_eval.h"
#include "api/json_types.h"

#include <algorithm>
#include <cmath>
#include <map>
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
        auto args = req.value("args", Json::object());
        std::string expr = args.value("expression", args.value("expr", ""));
        std::string ts = args.value("time", args.value("at", ""));

        if (expr.empty() || ts.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.expression and args.time are required"}}}};
        }

        uint64_t t = 0;
        try { t = std::stoull(ts); }
        catch (...) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_TIME"}, {"message", "time must be an integer"}}}};
        }

        auto* wf = g.waveform.get();

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        // Parse the expression first to detect syntax errors
        std::string parse_error;
        ExprNode* root = parse_expression(expr, parse_error);
        if (!root) {
            return Json{{"ok", false},
                        {"error", {{"code", "PARSE_ERROR"},
                                   {"message", "expression parse error: " + parse_error}}}};
        }

        // Pre-load all referenced signals
        std::vector<std::string> sig_names = expression_signals(root);
        for (const auto& name : sig_names) {
            uint32_t ref = wf->find_signal(name);
            if (ref && !wf->is_loaded(ref)) wf->load_signals({ref});
        }

        uint32_t ti = wf->time_idx_of(t);

        LogicValue result;
        std::string eval_error;
        if (!expr_eval_at(expr, *wf, ti, result, eval_error)) {
            delete root;
            return Json{{"ok", false},
                        {"error", {{"code", "EVAL_ERROR"},
                                   {"message", eval_error}}}};
        }
        delete root;

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"expression", expr},
            {"time", t},
        };
        out["data"] = {
            {"expression", expr},
            {"time", t},
            {"value", logic_value_json(result, fmt)},
        };
        return out;
    }
};

// ── 3. signal.sampled_pulse.inspect ──

struct SignalSampledPulseInspectHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.sampled_pulse.inspect"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: signal.sampled_pulse.inspect"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string clk_name = args.value("clock", "");

        if (sig.empty() || clk_name.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal and args.clock are required"}}}};
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

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());
        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        double pulse_width_tolerance = -1.0;
        if (args.contains("pulse_width_tolerance")) {
            const auto& pwt = args["pulse_width_tolerance"];
            if (pwt.is_number()) pulse_width_tolerance = pwt.get<double>();
            else if (pwt.is_string()) {
                try { pulse_width_tolerance = std::stod(pwt.get<std::string>()); }
                catch (...) { pulse_width_tolerance = -1.0; }
            }
        }

        ClockSampleScanner scanner(*wf, clk_ref, sig_ref, rising, falling);
        std::vector<ClockSample> samples;
        scanner.scan(begin_ti, end_ti, samples);

        Json pulses = Json::array();
        int pulse_count = 0;

        // Walk through samples; a pulse is when middle differs from
        // previous middle but after returns to the original value.
        for (size_t i = 1; i < samples.size(); ++i) {
            const auto& prev = samples[i - 1];
            const auto& cur = samples[i];

            std::string prev_mid = prev.middle;
            std::string cur_mid = cur.middle;
            std::string cur_after = cur.after;

            if (prev_mid.empty() || cur_mid.empty()) continue;

            // Check if middle changed (potential pulse)
            if (prev_mid == cur_mid) continue;

            // Check if after returns to previous middle value
            bool after_returns = !cur_after.empty() && cur_after == prev_mid;

            if (!after_returns) continue;

            // Pulse candidate found
            uint64_t pulse_begin_time = cur.time;
            uint64_t pulse_end_time = cur.time;
            uint64_t pulse_width = 0;

            // Width: time distance to the next edge (or use previous-next edge gap)
            if (i + 1 < samples.size()) {
                pulse_width = samples[i + 1].time - cur.time;
            } else {
                pulse_width = end - cur.time;
            }

            // Apply width tolerance filter
            if (pulse_width_tolerance >= 0.0 && pulse_width > 0) {
                // Interpret tolerance in time units (same as waveform timestamps)
                double width_d = static_cast<double>(pulse_width);
                if (width_d > pulse_width_tolerance) continue;
            }

            pulse_count++;
            pulses.push_back({
                {"begin_time", pulse_begin_time},
                {"end_time", pulse_end_time},
                {"width", pulse_width},
                {"edge_time", cur.time},
            });
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"signal", sig},
            {"clock", clk_name},
            {"edge", edge_str},
            {"begin", begin},
            {"end", end},
            {"pulse_count", pulse_count},
        };
        out["data"] = {{"pulses", pulses}};
        return out;
    }
};

// ── 4. protocol.handshake.inspect ──

struct ProtocolHandshakeInspectHandler : public EngineActionHandler {
    const char* action_name() const override { return "protocol.handshake.inspect"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: protocol.handshake.inspect"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string req_sig = args.value("req_signal", "");
        std::string ack_sig = args.value("ack_signal", "");
        std::string clk_name = args.value("clock", "");

        if (req_sig.empty() || ack_sig.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.req_signal and args.ack_signal are required"}}}};
        }

        auto* wf = g.waveform.get();

        Json err;
        uint32_t req_ref = load_signal(req_sig, wf, err);
        if (req_ref == IWaveformBackend::kInvalidSignalRef) return err;
        uint32_t ack_ref = load_signal(ack_sig, wf, err);
        if (ack_ref == IWaveformBackend::kInvalidSignalRef) return err;

        // If clock is provided, use clock-based sampling
        uint32_t clk_ref = 0;
        if (!clk_name.empty()) {
            clk_ref = load_signal(clk_name, wf, err);
            if (clk_ref == IWaveformBackend::kInvalidSignalRef) return err;
        }

        bool rising = true, falling = false;
        std::string edge_str = args.value("edge", "rising");
        if (!parse_edge(edge_str, rising, falling)) {
            return Json{{"ok", false},
                        {"error", {{"code", "INVALID_FIELD"},
                                   {"message", "edge must be rising, falling, or both"}}}};
        }

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());
        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        Json handshakes = Json::array();
        int handshake_count = 0;
        uint64_t min_latency = UINT64_MAX;
        uint64_t max_latency = 0;
        double sum_latency = 0.0;

        if (clk_ref) {
            // Clock-based sampling: sample req and ack at each clock edge
            // We need to track both signals at clock edges.
            // Strategy: use ClockSampleScanner for req, and separately track ack.
            // But for simplicity, let's use separate scanners for each.

            ClockSampleScanner req_scanner(*wf, clk_ref, req_ref, rising, falling);
            std::vector<ClockSample> req_samples;
            req_scanner.scan(begin_ti, end_ti, req_samples);

            ClockSampleScanner ack_scanner(*wf, clk_ref, ack_ref, rising, falling);
            std::vector<ClockSample> ack_samples;
            ack_scanner.scan(begin_ti, end_ti, ack_samples);

            // Walk req samples to find 0→1 transitions
            // Walk ack samples to find 0→1 transitions
            // Match: for each req rising, find the next ack rising

            // Build simplified state tracking
            std::string prev_req_val;
            bool req_pending = false;
            uint64_t req_time = 0;
            size_t ack_pos = 0;

            for (size_t i = 0; i < req_samples.size(); ++i) {
                const auto& rs = req_samples[i];
                std::string cur_req_mid = rs.middle;

                if (prev_req_val.empty()) {
                    prev_req_val = cur_req_mid;
                    continue;
                }

                // Detect req rising edge (0→1)
                if (prev_req_val == "0" && cur_req_mid == "1") {
                    req_pending = true;
                    req_time = rs.time;
                }

                // Check for ack response: find ack rising at/after this edge
                if (req_pending) {
                    // Advance ack_pos to find ack at or after this time
                    while (ack_pos < ack_samples.size() &&
                           ack_samples[ack_pos].time < rs.time) {
                        ack_pos++;
                    }

                    // Scan ack from ack_pos for rising edge
                    for (size_t j = ack_pos; j < ack_samples.size(); ++j) {
                        std::string ack_mid = ack_samples[j].middle;
                        // Check ack transition: ack going high
                        if (j > 0) {
                            std::string prev_ack_mid = ack_samples[j - 1].middle;
                            if (prev_ack_mid == "0" && ack_mid == "1") {
                                // Handshake found
                                uint64_t ack_time = ack_samples[j].time;
                                uint64_t latency = (ack_time > req_time)
                                    ? (ack_time - req_time) : 0;

                                handshakes.push_back({
                                    {"req_time", req_time},
                                    {"ack_time", ack_time},
                                    {"latency", latency},
                                });

                                handshake_count++;
                                if (latency < min_latency) min_latency = latency;
                                if (latency > max_latency) max_latency = latency;
                                sum_latency += static_cast<double>(latency);

                                req_pending = false;
                                ack_pos = j + 1;
                                break;
                            }
                        }
                        // Also check simple: ack already high?
                        if (ack_mid == "1") {
                            uint64_t ack_time = ack_samples[j].time;
                            uint64_t latency = (ack_time > req_time)
                                ? (ack_time - req_time) : 0;

                            handshakes.push_back({
                                {"req_time", req_time},
                                {"ack_time", ack_time},
                                {"latency", latency},
                            });

                            handshake_count++;
                            if (latency < min_latency) min_latency = latency;
                            if (latency > max_latency) max_latency = latency;
                            sum_latency += static_cast<double>(latency);

                            req_pending = false;
                            ack_pos = j + 1;
                            break;
                        }
                    }
                }

                prev_req_val = cur_req_mid;
            }

        } else {
            // No clock: use change-based approach
            // Sample req and ack at their own change points
            std::vector<uint32_t> req_indices = wf->time_indices_of(req_ref);
            std::vector<uint32_t> ack_indices = wf->time_indices_of(ack_ref);

            bool req_pending = false;
            uint64_t req_time = 0;
            size_t ack_idx = 0;

            for (size_t i = 0; i < req_indices.size(); ++i) {
                uint32_t ti = req_indices[i];
                if (ti < begin_ti || ti > end_ti) continue;

                IWaveformBackend::SignalOffset off;
                if (!wf->signal_offset_at(req_ref, ti, off) || !off.time_match) continue;
                std::string val = wf->signal_value_str(req_ref, off.start, 0);
                if (val.empty()) continue;

                uint64_t cur_time = wf->time_at(ti);

                if (val == "1" && !req_pending) {
                    req_pending = true;
                    req_time = cur_time;
                }

                if (req_pending) {
                    // Look for ack going high
                    while (ack_idx < ack_indices.size() &&
                           wf->time_at(ack_indices[ack_idx]) < cur_time) {
                        ack_idx++;
                    }

                    for (size_t j = ack_idx; j < ack_indices.size(); ++j) {
                        uint32_t ati = ack_indices[j];
                        if (ati < begin_ti || ati > end_ti) continue;

                        IWaveformBackend::SignalOffset aoff;
                        if (!wf->signal_offset_at(ack_ref, ati, aoff) || !aoff.time_match) continue;
                        std::string aval = wf->signal_value_str(ack_ref, aoff.start, 0);

                        if (aval == "1") {
                            uint64_t ack_time = wf->time_at(ati);
                            uint64_t latency = (ack_time > req_time)
                                ? (ack_time - req_time) : 0;

                            handshakes.push_back({
                                {"req_time", req_time},
                                {"ack_time", ack_time},
                                {"latency", latency},
                            });

                            handshake_count++;
                            if (latency < min_latency) min_latency = latency;
                            if (latency > max_latency) max_latency = latency;
                            sum_latency += static_cast<double>(latency);

                            req_pending = false;
                            ack_idx = j + 1;
                            break;
                        }
                    }
                }
            }
        }

        double avg_latency = handshake_count > 0
            ? sum_latency / static_cast<double>(handshake_count) : 0.0;

        if (handshake_count == 0) {
            min_latency = 0;
        }

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"handshake_count", handshake_count},
            {"min_latency", min_latency},
            {"max_latency", max_latency},
            {"avg_latency", avg_latency},
        };
        out["data"] = {
            {"req_signal", req_sig},
            {"ack_signal", ack_sig},
            {"begin", begin},
            {"end", end},
            {"handshakes", handshakes},
        };
        return out;
    }
};

// ── 5. counter.statistics ──

struct CounterStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "counter.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: counter.statistics"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string clk_name = args.value("clock", "");

        if (sig.empty() || clk_name.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal and args.clock are required"}}}};
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

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());
        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        ClockSampleScanner scanner(*wf, clk_ref, sig_ref, rising, falling);
        std::vector<ClockSample> samples;
        scanner.scan(begin_ti, end_ti, samples);

        Json values = Json::array();
        int sample_count = 0;
        uint64_t min_val = UINT64_MAX;
        uint64_t max_val = 0;
        int up_count = 0;
        int down_count = 0;
        int wrap_count = 0;
        uint64_t prev_val = 0;
        bool have_prev = false;

        // Determine max value for wrap detection
        uint64_t max_possible = (sig_info.width >= 64)
            ? UINT64_MAX
            : ((1ULL << sig_info.width) - 1);

        for (size_t i = 0; i < samples.size(); ++i) {
            std::string bits = samples[i].middle;
            if (bits.empty()) continue;

            uint64_t cur_val = 0;
            if (!bits_to_u64(bits, cur_val)) continue; // skip x/z

            sample_count++;

            values.push_back({
                {"time", samples[i].time},
                {"value", render_value_json(bits, sig_info.width, fmt)},
            });

            if (cur_val < min_val) min_val = cur_val;
            if (cur_val > max_val) max_val = cur_val;

            if (have_prev) {
                if (cur_val > prev_val) {
                    up_count++;
                } else if (cur_val < prev_val) {
                    down_count++;
                    // Check wrap: large jump down might be a wrap
                    if (prev_val > max_possible / 2 && cur_val < max_possible / 2) {
                        wrap_count++;
                    }
                }
            }

            prev_val = cur_val;
            have_prev = true;
        }

        if (!have_prev) {
            min_val = 0;
            max_val = 0;
        }

        std::string direction;
        if (up_count > 0 && down_count == 0) direction = "up";
        else if (down_count > 0 && up_count == 0) direction = "down";
        else if (up_count > 0 || down_count > 0) direction = "mixed";
        else direction = "constant";

        Json out;
        out["ok"] = true;
        out["summary"] = {
            {"sample_count", sample_count},
            {"min_value", min_val},
            {"max_value", max_val},
        };
        out["data"] = {
            {"signal", sig},
            {"clock", clk_name},
            {"edge", edge_str},
            {"begin", begin},
            {"end", end},
            {"direction", direction},
            {"wrap_count", wrap_count},
            {"values", values},
        };
        return out;
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
