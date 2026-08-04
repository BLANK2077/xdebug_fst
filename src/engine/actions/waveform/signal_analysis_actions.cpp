// signal_analysis_actions.cpp — signal.statistics, signal.stability,
// signal.xz_verify, signal.anomaly.inspect handlers (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── Shared helpers ──

static Json render_value_json(const std::string& bits, uint32_t width,
                              ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, static_cast<int>(width));
    return logic_value_json(v, fmt);
}

static bool is_all_zero(const std::string& bits) {
    for (char c : bits)
        if (c != '0') return false;
    return true;
}

static bool is_all_one(const std::string& bits) {
    for (char c : bits)
        if (c != '1') return false;
    return true;
}

static bool has_x_bit(const std::string& bits) {
    for (char c : bits)
        if (c == 'x' || c == 'X') return true;
    return false;
}

static bool has_z_bit(const std::string& bits) {
    for (char c : bits)
        if (c == 'z' || c == 'Z') return true;
    return false;
}

// Parse begin/end from args (accepts string or integer JSON values).
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

// ── signal.statistics ──

struct SignalStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message",
                                    "action requires waveform file: signal.statistics"}}}};
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

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        std::string clock = args.value("clock", "");
        std::string edge = args.value("edge", "posedge");

        Json out;
        out["ok"] = true;

        if (!clock.empty()) {
            // ── Clock-based sampling ──
            uint32_t clk_ref = wf->find_signal(clock);
            if (clk_ref == IWaveformBackend::kInvalidSignalRef) {
                return Json{{"ok", false},
                            {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                       {"message",
                                        "clock signal not found in waveform: " + clock}}}};
            }
            if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});

            bool rising = (edge == "posedge" || edge == "both");
            bool falling = (edge == "negedge" || edge == "both");

            ClockSampleScanner scanner(*wf, clk_ref, ref, rising, falling);
            std::vector<ClockSample> samples;
            scanner.scan(begin_ti, end_ti, samples);

            int sample_count = static_cast<int>(samples.size());
            int zero_count = 0, one_count = 0, x_count = 0, z_count = 0;
            int transitions = 0;
            std::string prev_bits;
            Json data_samples = Json::array();

            for (size_t i = 0; i < samples.size(); ++i) {
                std::string bits = samples[i].middle;
                if (bits.empty()) {
                    x_count++;
                    continue;
                }

                if (has_x_bit(bits))
                    x_count++;
                else if (has_z_bit(bits))
                    z_count++;
                else if (is_all_zero(bits))
                    zero_count++;
                else if (is_all_one(bits))
                    one_count++;

                if (i > 0 && bits != prev_bits) transitions++;
                prev_bits = bits;

                data_samples.push_back(
                    {{"time", samples[i].time},
                     {"time_idx", samples[i].time_idx},
                     {"value", render_value_json(bits, info.width, fmt)}});
            }

            double activity =
                sample_count > 0
                    ? static_cast<double>(transitions) /
                          static_cast<double>(sample_count)
                    : 0.0;

            out["summary"] = {{"signal", sig},
                              {"begin", begin},
                              {"end", end},
                              {"sample_count", sample_count},
                              {"state_counts",
                               {{"zero", zero_count},
                                {"one", one_count},
                                {"x", x_count},
                                {"z", z_count}}},
                              {"transition_count", transitions},
                              {"activity", activity},
                              {"sampling_mode", "clock_edge"},
                              {"clock", clock},
                              {"edge", edge}};
            out["data"] = {{"samples", data_samples}};
        } else {
            // ── Self-change-based sampling ──
            std::vector<uint32_t> indices = wf->time_indices_of(ref);

            int sample_count = 0;
            int zero_count = 0, one_count = 0, x_count = 0, z_count = 0;
            int transitions = 0;
            std::string prev_bits;
            Json data_samples = Json::array();

            for (uint32_t ti : indices) {
                if (ti < begin_ti || ti > end_ti) continue;
                IWaveformBackend::SignalOffset off;
                if (!wf->signal_offset_at(ref, ti, off)) continue;
                if (!off.time_match) continue;
                std::string bits = wf->signal_value_str(ref, off.start, 0);
                if (bits.empty()) continue;

                sample_count++;

                if (has_x_bit(bits))
                    x_count++;
                else if (has_z_bit(bits))
                    z_count++;
                else if (is_all_zero(bits))
                    zero_count++;
                else if (is_all_one(bits))
                    one_count++;

                if (sample_count > 1 && bits != prev_bits) transitions++;
                prev_bits = bits;

                data_samples.push_back(
                    {{"time", wf->time_at(ti)},
                     {"time_idx", ti},
                     {"value", render_value_json(bits, info.width, fmt)}});
            }

            double activity =
                sample_count > 0
                    ? static_cast<double>(transitions) /
                          static_cast<double>(sample_count)
                    : 0.0;

            out["summary"] = {{"signal", sig},
                              {"begin", begin},
                              {"end", end},
                              {"sample_count", sample_count},
                              {"state_counts",
                               {{"zero", zero_count},
                                {"one", one_count},
                                {"x", x_count},
                                {"z", z_count}}},
                              {"transition_count", transitions},
                              {"activity", activity},
                              {"sampling_mode", "value_change"}};
            out["data"] = {{"samples", data_samples}};
        }

        return out;
    }
};

// ── signal.stability ──

struct SignalStabilityHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.stability"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message",
                                    "action requires waveform file: signal.stability"}}}};
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

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        std::vector<uint32_t> indices = wf->time_indices_of(ref);

        bool stable = true;
        std::string first_bits;
        Json changes = Json::array();
        int change_row_count = 0;
        bool includes_initial = false;

        for (uint32_t ti : indices) {
            if (ti < begin_ti || ti > end_ti) continue;
            IWaveformBackend::SignalOffset off;
            if (!wf->signal_offset_at(ref, ti, off)) continue;
            if (!off.time_match) continue;
            std::string bits = wf->signal_value_str(ref, off.start, 0);
            if (bits.empty()) continue;

            change_row_count++;
            if (change_row_count == 1) {
                first_bits = bits;
                includes_initial = true;
            }

            changes.push_back({{"time", wf->time_at(ti)},
                               {"time_idx", ti},
                               {"value",
                                render_value_json(bits, info.width, fmt)}});

            if (change_row_count > 1 && bits != first_bits) {
                stable = false;
                break;
            }
        }

        int actual_transition_count = stable ? 0 : 1;

        Json out;
        out["ok"] = true;
        out["data"] = {
            {"signal", sig},
            {"begin", begin},
            {"end", end},
            {"changes", changes},
            {"summary",
             {{"stable", stable},
              {"change_row_count", change_row_count},
              {"actual_transition_count", actual_transition_count},
              {"scan_stopped_on_first_transition", !stable}}},
            {"includes_initial_value", includes_initial}};
        return out;
    }
};

// ── signal.xz_verify ──

struct SignalXzVerifyHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.xz_verify"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message",
                                    "action requires waveform file: signal.xz_verify"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        std::string expected_state = args.value("expected_state", "");
        std::string match_mode = args.value("match_mode", "exact");

        if (sig.empty() || expected_state.empty()) {
            return Json{{"ok", false},
                        {"error",
                         {{"code", "MISSING_FIELD"},
                          {"message",
                           "args.signal and args.expected_state are required"}}}};
        }
        if (expected_state != "x" && expected_state != "z") {
            return Json{{"ok", false},
                        {"error",
                         {{"code", "INVALID_FIELD"},
                          {"message", "args.expected_state must be x or z"}}}};
        }
        if (match_mode != "exact" && match_mode != "contains") {
            return Json{{"ok", false},
                        {"error",
                         {{"code", "INVALID_FIELD"},
                          {"message",
                           "args.match_mode must be exact or contains"}}}};
        }

        auto* wf = g.waveform.get();
        uint32_t ref = wf->find_signal(sig);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            return Json{{"ok", false},
                        {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                   {"message", "signal not found in waveform: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        std::vector<uint32_t> indices = wf->time_indices_of(ref);

        bool always_matched = true;
        int checked_value_count = 0;
        Json initial_value = nullptr;
        Json first_mismatch = nullptr;
        char expected_bit = expected_state[0];

        for (uint32_t ti : indices) {
            if (ti < begin_ti || ti > end_ti) continue;
            IWaveformBackend::SignalOffset off;
            if (!wf->signal_offset_at(ref, ti, off)) continue;
            if (!off.time_match) continue;
            std::string bits = wf->signal_value_str(ref, off.start, 0);
            if (bits.empty()) continue;

            Json val_json =
                render_value_json(bits, info.width, ValueRenderFormat::Hex);

            if (checked_value_count == 0) {
                initial_value = val_json;
            }
            checked_value_count++;

            bool matched;
            if (match_mode == "exact") {
                matched = true;
                for (char c : bits) {
                    if (c != expected_bit) {
                        matched = false;
                        break;
                    }
                }
            } else {  // contains
                matched = (bits.find(expected_bit) != std::string::npos);
            }

            if (!matched) {
                always_matched = false;
                first_mismatch = {{"sample_time", wf->time_at(ti)},
                                  {"value", val_json}};
                break;
            }
        }

        if (checked_value_count == 0) {
            return Json{
                {"ok", false},
                {"error",
                 {{"code", "VALUE_NOT_AVAILABLE"},
                  {"message",
                   "no waveform values for signal in requested window: " +
                       sig}}}};
        }

        Json out;
        out["ok"] = true;
        out["data"] = {
            {"summary",
             {{"signal", sig},
              {"expected_state", expected_state},
              {"match_mode", match_mode},
              {"verdict", always_matched ? "pass" : "fail"},
              {"always_matched", always_matched},
              {"checked_value_count", checked_value_count},
              {"stop_reason",
               always_matched ? "window_end" : "first_mismatch"}}},
            {"initial_value", initial_value},
            {"first_mismatch", first_mismatch}};
        return out;
    }
};

// ── signal.anomaly.inspect ──

struct SignalAnomalyInspectHandler : public EngineActionHandler {
    const char* action_name() const override {
        return "signal.anomaly.inspect";
    }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{
                {"ok", false},
                {"error",
                 {{"code", "WAVEFORM_NOT_LOADED"},
                  {"message",
                   "action requires waveform file: signal.anomaly.inspect"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        if (sig.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal is required"}}}};
        }

        std::string anomaly_type = args.value("anomaly_type", "xz");
        if (anomaly_type != "x" && anomaly_type != "z" &&
            anomaly_type != "xz" && anomaly_type != "both") {
            anomaly_type = "xz";
        }

        auto* wf = g.waveform.get();
        uint32_t ref = wf->find_signal(sig);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            return Json{{"ok", false},
                        {"error", {{"code", "SIGNAL_NOT_FOUND"},
                                   {"message", "signal not found in waveform: " + sig}}}};
        }
        if (!wf->is_loaded(ref)) wf->load_signals({ref});

        IWaveformBackend::SignalInfo info;
        wf->signal_info(ref, info);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("render_format", "hex"), fmt);

        uint64_t begin = parse_time_arg(args, "begin", 0);
        uint64_t end = parse_time_arg(args, "end", wf->max_time());

        uint32_t begin_ti = wf->time_idx_of(begin);
        uint32_t end_ti = wf->time_idx_of(end);
        if (end_ti < begin_ti) end_ti = begin_ti;

        std::vector<uint32_t> indices = wf->time_indices_of(ref);

        Json anomalies = Json::array();
        int anomaly_count = 0;
        Json first_anomaly_time = nullptr;

        bool in_anomaly = false;
        uint64_t anomaly_begin_time = 0;
        uint32_t anomaly_begin_ti = 0;
        std::string anomaly_value;
        std::string anomaly_kind;

        for (uint32_t ti : indices) {
            if (ti < begin_ti || ti > end_ti) continue;
            IWaveformBackend::SignalOffset off;
            if (!wf->signal_offset_at(ref, ti, off)) continue;
            if (!off.time_match) continue;
            std::string bits = wf->signal_value_str(ref, off.start, 0);
            if (bits.empty()) continue;

            bool is_anomalous = false;
            std::string kind;

            bool hx = has_x_bit(bits);
            bool hz = has_z_bit(bits);

            if (anomaly_type == "x" || anomaly_type == "xz" ||
                anomaly_type == "both") {
                if (hx) {
                    is_anomalous = true;
                    kind = "x";
                }
            }
            if (!is_anomalous &&
                (anomaly_type == "z" || anomaly_type == "xz" ||
                 anomaly_type == "both")) {
                if (hz) {
                    is_anomalous = true;
                    kind = "z";
                }
            }

            if (is_anomalous) {
                if (!in_anomaly) {
                    in_anomaly = true;
                    anomaly_begin_time = wf->time_at(ti);
                    anomaly_begin_ti = ti;
                    anomaly_value = bits;
                    anomaly_kind = kind;
                }
            } else {
                if (in_anomaly) {
                    uint64_t anomaly_end_time = wf->time_at(ti);
                    anomalies.push_back(
                        {{"begin_time", anomaly_begin_time},
                         {"end_time", anomaly_end_time},
                         {"begin_time_idx", anomaly_begin_ti},
                         {"end_time_idx", ti},
                         {"value",
                          render_value_json(anomaly_value, info.width, fmt)},
                         {"kind", anomaly_kind}});
                    anomaly_count++;
                    if (first_anomaly_time.is_null())
                        first_anomaly_time = anomaly_begin_time;
                    in_anomaly = false;
                }
            }
        }

        // Handle open anomaly at end of window
        if (in_anomaly) {
            anomalies.push_back(
                {{"begin_time", anomaly_begin_time},
                 {"end_time", end},
                 {"begin_time_idx", anomaly_begin_ti},
                 {"end_time_idx", end_ti},
                 {"value",
                  render_value_json(anomaly_value, info.width, fmt)},
                 {"kind", anomaly_kind}});
            anomaly_count++;
            if (first_anomaly_time.is_null())
                first_anomaly_time = anomaly_begin_time;
        }

        Json out;
        out["ok"] = true;
        Json summary;
        summary["anomaly_count"] = anomaly_count;
        if (!first_anomaly_time.is_null())
            summary["first_anomaly_time"] = first_anomaly_time;
        out["summary"] = summary;
        out["data"] = {{"anomalies", anomalies}};
        return out;
    }
};

// ── Factory functions ──

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
