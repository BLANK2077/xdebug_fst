// value_at.cpp — value.at and signal.changes actions (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "api/json_types.h"
#include "waveform/clock_sampling.h"
#include "waveform/list/list_manager.h"
#include "engine/actions/value_source_entries.h"
#include <algorithm>
#include <set>
#include <vector>

namespace xdebug_fst {

// ── Shared helpers ──

// Render a raw wellen bit string as a canonical logic value JSON.
static Json render_value_json(const std::string& bits, uint32_t width,
                              ValueRenderFormat fmt) {
    LogicValue v = logic_value_from_bits(bits, static_cast<int>(width));
    return logic_value_json(v, fmt);
}

// ── value.at ──

struct ValueEntry {
    std::string key;
    std::string path;
};

static Json value_error(const std::string& code, const std::string& message) {
    return Json{{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}

static bool source_entries(const Json& args, std::string& kind,
                           std::string& name, std::vector<ValueEntry>& out,
                           Json& error) {
    if (args.contains("signal")) {
        kind = "signal";
        name = args.at("signal").get<std::string>();
        out.push_back({name, name});
        return true;
    }
    if (args.contains("list")) {
        kind = "list";
        name = args.at("list").get<std::string>();
        const SignalList* list = ListManager::instance().get(name);
        if (!list) {
            error = value_error("LIST_NOT_FOUND", "list not found: " + name);
            return false;
        }
        for (const std::string& signal : list->signals)
            out.push_back({signal, signal});
        if (out.empty()) {
            error = value_error("EMPTY_VALUE_SOURCE", "list has no entries: " + name);
            return false;
        }
        return true;
    }
    std::vector<ValueSourceEntry> configured;
    if (args.contains("apb")) {
        kind = "apb"; name = args.at("apb").get<std::string>();
        if (!apb_value_source_entries(name, configured)) {
            error = value_error("CONFIG_NOT_FOUND", "APB config not found: " + name);
            return false;
        }
        for (const auto& [key, path] : configured) out.push_back({key, path});
        return true;
    }
    if (args.contains("stream")) {
        kind = "stream"; name = args.at("stream").get<std::string>();
        if (!stream_value_source_entries(name, configured)) {
            error = value_error("CONFIG_NOT_FOUND", "stream config not found: " + name);
            return false;
        }
        for (const auto& [key, path] : configured) out.push_back({key, path});
        return true;
    }
    kind = "axi"; name = args.at("axi").get<std::string>();
    if (!axi_value_source_entries(name, configured)) {
        error = value_error("CONFIG_NOT_FOUND", "AXI config not found: " + name);
        return false;
    }
    for (const auto& [key, path] : configured) out.push_back({key, path});
    return true;
}

static Json typed_logic_value(const IWaveformBackend::WaveformValue& value,
                              uint32_t width, ValueRenderFormat format) {
    if (value.kind == IWaveformBackend::ValueKind::BitVector)
        return render_value_json(value.text, width, format);
    if (value.kind == IWaveformBackend::ValueKind::Real)
        return Json{{"value", std::to_string(value.real)}, {"known", true}};
    return Json{{"value", value.kind == IWaveformBackend::ValueKind::Event
                              ? "event" : value.text}, {"known", true}};
}

struct ValueAtHandler : public EngineActionHandler {
    const char* action_name() const override { return "value.at"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        auto* wf = g.waveform.get();
        const Json args = req.at("args");
        std::string source_kind, source_name;
        std::vector<ValueEntry> entries;
        Json error;
        if (!source_entries(args, source_kind, source_name, entries, error)) return error;
        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);
        TimeRenderUnit render_unit = TimeRenderUnit::Ns;
        std::string unit_error;
        if (!parse_time_render_unit(args.value("render_time_unit", "ns"),
                                    render_unit, unit_error))
            return value_error("INVALID_TIME_UNIT", unit_error);
        std::vector<std::string> requested_times;
        if (args.contains("time")) requested_times.push_back(args.at("time"));
        else for (const auto& item : args.at("times")) requested_times.push_back(item);
        std::vector<uint64_t> ticks;
        for (const std::string& text : requested_times) {
            uint64_t tick = 0; std::string parse_error;
            if (!wf->parse_time(text, tick, parse_error))
                return value_error("INVALID_TIME", parse_error);
            ticks.push_back(tick);
        }
        Json entry_json = Json::array();
        std::vector<uint32_t> refs;
        for (const auto& entry : entries) {
            entry_json.push_back({{"key", entry.key}, {"kind", "signal"},
                                  {"path", entry.path}});
            const uint32_t ref = wf->find_signal(entry.path);
            refs.push_back(ref);
            if (ref) wf->load_signals({ref});
        }
        bool value_width_complete = true;
        Json width_diagnostics = Json::array();
        for (size_t i = 0; i < refs.size(); ++i) {
            IWaveformBackend::SignalInfo info;
            if (!refs[i] || !wf->signal_info(refs[i], info) ||
                (info.encoding == IWaveformBackend::ValueKind::BitVector &&
                 info.width == 0)) {
                value_width_complete = false;
                width_diagnostics.push_back({{"signal", entries[i].path},
                    {"role", entries[i].key},
                    {"reason", "npi_range_size_unavailable"}});
            }
        }
        const bool clocked = args.contains("clock");
        uint32_t clock_ref = IWaveformBackend::kInvalidSignalRef;
        if (clocked) {
            clock_ref = wf->find_signal(args.at("clock").get<std::string>());
            if (!clock_ref)
                return value_error("CLOCK_NOT_FOUND", "clock signal not found: " +
                                   args.at("clock").get<std::string>());
            wf->load_signals({clock_ref});
        }
        Json samples = Json::array();
        for (size_t time_i = 0; time_i < ticks.size(); ++time_i) {
            const uint32_t ti = wf->time_idx_of(ticks[time_i]);
            bool target_edge_hit = !clocked;
            Json clock_context;
            IWaveformBackend::ObservationPoint clock_point =
                IWaveformBackend::ObservationPoint::Raw;
            if (clocked) {
                const std::string edge = args.value("edge", "negedge");
                const bool negedge = edge == "negedge";
                const std::string requested_point =
                    args.value("sample_point", std::string());
                const std::string effective_point = negedge ? ""
                    : (requested_point.empty() ? "before" : requested_point);
                if (effective_point == "before")
                    clock_point = IWaveformBackend::ObservationPoint::Before;
                else if (effective_point == "after")
                    clock_point = IWaveformBackend::ObservationPoint::After;

                Json previous = nullptr, next = nullptr;
                std::string exact_kind;
                const auto clock_indices = wf->time_indices_of(clock_ref);
                for (uint32_t edge_ti : clock_indices) {
                    IWaveformBackend::SampledValue before_value, raw_value;
                    if (!wf->sampled_value_at(clock_ref, edge_ti,
                            IWaveformBackend::ObservationPoint::Before,
                            before_value) ||
                        !wf->sampled_value_at(clock_ref, edge_ti,
                            IWaveformBackend::ObservationPoint::Raw,
                            raw_value)) continue;
                    const bool rise = is_rising_edge(before_value.value.text,
                                                     raw_value.value.text);
                    const bool fall = is_falling_edge(before_value.value.text,
                                                      raw_value.value.text);
                    if (!rise && !fall) continue;
                    const std::string kind = rise ? "posedge" : "negedge";
                    const bool selected = edge == "dual" || edge == kind;
                    const uint64_t edge_time = wf->time_at(edge_ti);
                    if (edge_time == ticks[time_i]) exact_kind = kind;
                    if (!selected) continue;
                    if (edge_time < ticks[time_i])
                        previous = wf->format_time(edge_time, render_unit);
                    else if (edge_time > ticks[time_i] && next.is_null())
                        next = wf->format_time(edge_time, render_unit);
                }
                target_edge_hit = !exact_kind.empty() &&
                    (edge == "dual" || edge == exact_kind);
                const Json requested{{"edge", edge},
                    {"sample_point", requested_point.empty()
                        ? Json(nullptr) : Json(requested_point)}};
                const Json effective{{"edge", edge},
                    {"sample_point", effective_point.empty()
                        ? Json(nullptr) : Json(effective_point)}};
                clock_context = {
                    {"clock", args.at("clock")},
                    {"requested_sampling", requested},
                    {"effective_sampling", effective},
                    {"sample_point_applied", !negedge},
                    {"sample_point_ignored_for_negedge",
                        negedge && !requested_point.empty()},
                    {"requested_time", wf->format_time(ticks[time_i], render_unit)},
                    {"requested_any_edge_hit", !exact_kind.empty()},
                    {"clock_edge_kind", exact_kind.empty()
                        ? Json(nullptr) : Json(exact_kind)},
                    {"requested_target_edge_hit", target_edge_hit},
                    {"previous_sample_time", previous},
                    {"next_sample_time", next},
                    {"bracket_complete", !previous.is_null() && !next.is_null()}};
                if (negedge && !requested_point.empty())
                    clock_context["sample_point_not_applied_reason"] =
                        "negedge keeps the established current-value sampling semantics";
            }
            Json values = Json::array();
            for (size_t i = 0; i < entries.size(); ++i) {
                Json row{{"key", entries[i].key}};
                if (!refs[i]) { row["status"] = "signal_not_found"; values.push_back(row); continue; }
                if (clocked && !target_edge_hit) {
                    row["status"] = "missing_value";
                    values.push_back(row);
                    continue;
                }
                IWaveformBackend::SignalInfo info;
                wf->signal_info(refs[i], info);
                IWaveformBackend::SampledValue sampled;
                IWaveformBackend::ObservationPoint point = clocked ? clock_point
                    : IWaveformBackend::ObservationPoint::Raw;
                if (!wf->sampled_value_at(refs[i], ti, point, sampled)) {
                    row["status"] = "missing_value";
                } else {
                    row["status"] = "ok";
                    row["value"] = typed_logic_value(sampled.value, info.width, fmt);
                    if (args.contains("slice_hint") && info.width > 0) {
                        const uint32_t chunk = args.at("slice_hint").at("chunk_width");
                        const uint32_t requested_count =
                            args.at("slice_hint").value("count", 1u);
                        Json slices = Json::array(), commands = Json::array();
                        const std::string raw_value = row.at("value").at("value");
                        for (uint32_t slice = 0; slice < requested_count; ++slice) {
                            const uint32_t lsb = slice * chunk;
                            if (lsb >= info.width) break;
                            const uint32_t msb = std::min(info.width - 1,
                                                          lsb + chunk - 1);
                            slices.push_back({{"index", slice},
                                              {"range", "[" + std::to_string(msb) +
                                                        ":" + std::to_string(lsb) + "]"}});
                            commands.push_back("tools/xbit slice \"" + raw_value +
                                               "\" " + std::to_string(msb) + " " +
                                               std::to_string(lsb) + " --json");
                        }
                        row["xbit_hints"] = {{"status", "ready"},
                            {"signal", entries[i].path}, {"raw_value", raw_value},
                            {"chunk_width", chunk}, {"count", slices.size()},
                            {"slices", slices}, {"commands", commands}};
                    }
                }
                values.push_back(row);
            }
            Json sample{{"time", wf->format_time(ticks[time_i], render_unit)},
                        {"sampling_mode", clocked ? "clock_sampled" : "raw_time"},
                        {"values", values}};
            if (clocked) {
                sample["clock_context"] = clock_context;
            }
            samples.push_back(sample);
        }
        Json summary{{"source_kind", source_kind}, {"source_name", source_name},
                     {"sampling_mode", clocked ? "clock_sampled" : "raw_time"},
                     {"time_count", ticks.size()}, {"entry_count", entries.size()},
                     {"value_width_complete", value_width_complete},
                     {"width_diagnostics", width_diagnostics}};
        return Json{{"ok", true}, {"summary", summary},
                    {"data", {{"entries", entry_json}, {"samples", samples}}}};
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
