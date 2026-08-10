// signal_resolve.cpp — signal.resolve, trace.driver, trace.load (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"

#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

Json failure(const char* code, const std::string& message) {
    return Json{{"ok", false},
                {"error", {{"code", code}, {"message", message}}}};
}

Json completeness_summary(const std::string& signal, const char* mode,
                          size_t total, size_t returned) {
    const bool truncated = returned < total;
    return Json{{"signal", signal},
                {"mode", mode},
                {"scan_complete", true},
                {"analysis_complete", true},
                {"response_truncated", truncated},
                {"total_count", total},
                {"returned_count", returned},
                {"truncation_scopes", truncated
                    ? Json::array({"response_paths"}) : Json::array()}};
}

std::string signal_name(IDesignBackend& design, int index) {
    const char* name = index >= 0 ? design.signal_name(index) : nullptr;
    return name ? name : "";
}

struct SignalResolveHandler final : EngineActionHandler {
    const char* action_name() const override { return "signal.resolve"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto& design = *engine_globals().design;
        const std::string query = req.at("args").at("signal").get<std::string>();
        const int index = design.resolve(query.c_str());
        if (index < 0) {
            return failure("SIGNAL_NOT_FOUND", "signal not found: " + query);
        }
        const std::string resolved = signal_name(design, index);
        Json match{{"signal", resolved},
                   {"type", design.signal_type(index)
                                ? design.signal_type(index) : ""},
                   {"file", design.signal_file(index)
                                ? design.signal_file(index) : ""},
                   {"line", design.signal_line(index)}};
        Json summary{{"status", "found"},
                     {"query", query},
                     {"scan_complete", true},
                     {"analysis_complete", true},
                     {"response_truncated", false},
                     {"total_count", 1},
                     {"returned_count", 1},
                     {"truncation_scopes", Json::array()}};
        return Json{{"ok", true}, {"summary", summary},
                    {"data", {{"matches", Json::array({match})}}}};
    }
};

struct TraceDriverHandler final : EngineActionHandler {
    const char* action_name() const override { return "trace.driver"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto& design = *engine_globals().design;
        const Json& args = req.at("args");
        const std::string query = args.at("signal").get<std::string>();
        const int index = design.resolve(query.c_str());
        if (index < 0) {
            return failure("SIGNAL_NOT_FOUND", "signal not found: " + query);
        }
        const bool no_statement_only = args.value("no_statement_only", false);
        const std::string role_filter = args.value("role", std::string());
        std::vector<IDesignBackend::DriverRecord> records;
        design.trace_driver(index, records);
        Json paths = Json::array();
        for (const auto& record : records) {
            if (no_statement_only && record.src_signal < 0) continue;
            if (!role_filter.empty() && record.dependency_role != role_filter)
                continue;
            Json signal_path = Json::array();
            if (record.src_signal >= 0) {
                const std::string source = signal_name(design, record.src_signal);
                if (!source.empty()) signal_path.push_back(source);
            }
            signal_path.push_back(signal_name(design, index));
            paths.push_back({{"file", record.file},
                             {"line", record.line},
                             {"source_context", Json::array()},
                             {"signal_path", signal_path}});
        }
        const size_t total = paths.size();
        const Json& limits = req.value("limits", Json::object());
        const size_t max_results = limits.contains("max_results")
            ? limits.at("max_results").get<size_t>() : total;
        if (paths.size() > max_results)
            paths.erase(paths.begin() + max_results, paths.end());
        return Json{{"ok", true},
                    {"summary", completeness_summary(
                        query, "driver", total, paths.size())},
                    {"data", {{"paths", paths}}}};
    }
};

struct TraceLoadHandler final : EngineActionHandler {
    const char* action_name() const override { return "trace.load"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto& design = *engine_globals().design;
        const Json& args = req.at("args");
        const std::string query = args.at("signal").get<std::string>();
        const int index = design.resolve(query.c_str());
        if (index < 0) {
            return failure("SIGNAL_NOT_FOUND", "signal not found: " + query);
        }
        const std::string role_filter = args.value("role", std::string());
        std::vector<IDesignBackend::LoadRecord> records;
        design.trace_load(index, records);
        Json paths = Json::array();
        for (const auto& record : records) {
            if (!role_filter.empty() && record.kind != role_filter) continue;
            Json signal_path = Json::array({signal_name(design, index)});
            const std::string consumer = signal_name(design, record.consumer);
            if (!consumer.empty()) signal_path.push_back(consumer);
            paths.push_back({{"file", record.file},
                             {"line", record.line},
                             {"source_context", Json::array()},
                             {"signal_path", signal_path}});
        }
        const size_t total = paths.size();
        const Json& limits = req.value("limits", Json::object());
        const size_t max_results = limits.contains("max_results")
            ? limits.at("max_results").get<size_t>() : total;
        if (paths.size() > max_results)
            paths.erase(paths.begin() + max_results, paths.end());
        return Json{{"ok", true},
                    {"summary", completeness_summary(
                        query, "load", total, paths.size())},
                    {"data", {{"paths", paths}}}};
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_signal_resolve_handler() {
    return std::make_unique<SignalResolveHandler>();
}
std::unique_ptr<EngineActionHandler> make_trace_driver_handler() {
    return std::make_unique<TraceDriverHandler>();
}
std::unique_ptr<EngineActionHandler> make_trace_load_handler() {
    return std::make_unique<TraceLoadHandler>();
}

}  // namespace xdebug_fst
