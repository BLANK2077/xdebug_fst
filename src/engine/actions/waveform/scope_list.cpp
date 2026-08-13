// scope_list.cpp — scope.list and scope.roots actions (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "protocol/domain_xout_renderer.h"

#include <fnmatch.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

Json failure(const char* code, const std::string& message) {
    return Json{{"ok", false},
                {"error", {{"code", code}, {"message", message}}}};
}

std::string first_component(const std::string& path) {
    const size_t dot = path.find('.');
    return dot == std::string::npos ? path : path.substr(0, dot);
}

std::string local_name(const std::string& path) {
    const size_t dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

std::string public_wave_path(const std::string& path) {
    if (path == "TOP") return "top";
    if (path.rfind("TOP.", 0) == 0) return "top" + path.substr(3);
    return path;
}

Json wave_root(const std::string& path) {
    return Json{{"path", path}, {"name", local_name(path)},
                {"full_name", path}, {"def_name", local_name(path)},
                {"type", 0}, {"queryable", true}};
}

Json design_root(const std::string& path) {
    return Json{{"path", path}, {"name", local_name(path)},
                {"full_name", path}, {"def_name", local_name(path)},
                {"kind", "module"}, {"discovery", "npi_top"},
                {"traceable", true}};
}

struct ScopeRootsHandler final : EngineActionHandler {
    const char* action_name() const override { return "scope.roots"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto& globals = engine_globals();
        const std::string source = req.value("args", Json::object())
                                       .value("source", "auto");
        const bool select_wave = source == "auto" || source == "wave";
        const bool select_design = source == "auto" || source == "design";
        const bool wave_available = globals.has_waveform && globals.waveform;
        const bool design_available = globals.has_design && globals.design;

        std::set<std::string> wave_names;
        Json wave_roots = Json::array();
        if (select_wave && wave_available) {
            for (uint32_t i = 0; i < globals.waveform->root_scope_count(); ++i) {
                const uint32_t ref = globals.waveform->root_scope_at(i);
                const char* name = ref ? globals.waveform->scope_full_name(ref) : nullptr;
                if (!name || !*name) continue;
                const std::string path = public_wave_path(name);
                if (!wave_names.insert(path).second) continue;
                wave_roots.push_back(wave_root(path));
            }
        }

        std::set<std::string> design_names;
        Json design_roots = Json::array();
        if (select_design && design_available) {
            for (int i = 0; i < globals.design->signal_count(); ++i) {
                const char* name = globals.design->signal_name(i);
                if (!name || !*name) continue;
                const std::string root = first_component(name);
                if (!root.empty() && design_names.insert(root).second)
                    design_roots.push_back(design_root(root));
            }
        }

        std::set<std::string> all_names = wave_names;
        all_names.insert(design_names.begin(), design_names.end());
        Json roots = Json::array();
        size_t matched_count = 0;
        for (const std::string& path : all_names) {
            const bool has_wave = wave_names.count(path) != 0;
            const bool has_design = design_names.count(path) != 0;
            if (has_wave && has_design) {
                ++matched_count;
                roots.push_back({{"path", path},
                                 {"sources", Json::array({"design", "wave"})},
                                 {"status", "matched"},
                                 {"wave", wave_root(path)},
                                 {"design", design_root(path)}});
            } else if (has_design) {
                roots.push_back({{"path", path},
                                 {"sources", Json::array({"design"})},
                                 {"status", "design_only"},
                                 {"wave", nullptr},
                                 {"design", design_root(path)}});
            } else {
                roots.push_back({{"path", path},
                                 {"sources", Json::array({"wave"})},
                                 {"status", "wave_only"},
                                 {"wave", wave_root(path)},
                                 {"design", nullptr}});
            }
        }

        std::string reason;
        Json recommended = nullptr;
        if (roots.empty()) {
            reason = "no roots discovered";
        } else if (roots.size() == 1) {
            recommended = roots[0]["path"];
            reason = "unique root";
        } else if (matched_count == 1) {
            for (const auto& root : roots) {
                if (root.at("status") == "matched") recommended = root.at("path");
            }
            reason = "unique matched root";
        } else if (matched_count > 1) {
            reason = "multiple matched roots";
        } else {
            reason = "multiple roots or design/wave mismatch";
        }

        const bool sources_complete =
            (!select_wave || wave_available) && (!select_design || design_available);
        Json truncation_scopes = Json::array();
        if (!sources_complete) truncation_scopes.push_back("analysis_sources");
        Json summary{{"source", source},
                     {"wave_available", wave_available},
                     {"design_available", design_available},
                     {"resource_available", wave_available || design_available},
                     {"root_count", roots.size()},
                     {"wave_count", wave_roots.size()},
                     {"design_count", design_roots.size()},
                     {"matched_count", matched_count},
                     {"recommended_root", recommended},
                     {"recommended_reason", reason},
                     {"scan_complete", sources_complete},
                     {"analysis_complete", sources_complete},
                     {"response_truncated", false},
                     {"total_count", roots.size()},
                     {"returned_count", roots.size()},
                     {"truncation_scopes", truncation_scopes}};
        Json data{{"roots", roots}, {"wave_roots", wave_roots},
                  {"design_roots", design_roots}};
        if (!sources_complete) {
            Json limitations = Json::array();
            if (select_wave && !wave_available)
                limitations.push_back("wave roots unavailable: waveform not loaded");
            if (select_design && !design_available)
                limitations.push_back("design roots unavailable: design not loaded");
            data["limitations"] = limitations;
        }
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_scope_roots_xout(response);
    }
};

bool matches_any(const std::string& name, const Json& patterns) {
    for (const auto& pattern : patterns) {
        if (fnmatch(pattern.get<std::string>().c_str(), name.c_str(), 0) == 0)
            return true;
    }
    return false;
}

bool included(const std::string& name, const Json& includes,
              const Json& excludes) {
    if (matches_any(name, excludes)) return false;
    return includes.empty() || matches_any(name, includes);
}

void collect_depth(IWaveformBackend& waveform, uint32_t scope, int depth,
                   const std::string& prefix,
                   std::vector<std::pair<uint32_t, std::string>>& out) {
    if (depth == 0) {
        out.emplace_back(scope, prefix);
        return;
    }
    for (uint32_t i = 0; i < waveform.scope_child_count(scope); ++i) {
        const uint32_t child = waveform.scope_child_at(scope, i);
        const char* child_name = child ? waveform.scope_name(child) : nullptr;
        if (!child_name || !*child_name) continue;
        const std::string child_prefix = prefix.empty()
            ? child_name : prefix + "." + child_name;
        collect_depth(waveform, child, depth - 1, child_prefix, out);
    }
}

struct ScopeListHandler final : EngineActionHandler {
    const char* action_name() const override { return "scope.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        auto& globals = engine_globals();
        auto& waveform = *globals.waveform;
        const Json& args = req.value("args", Json::object());
        const std::string path = args.value("path", std::string());
        const int level = args.value("level", 0);
        const std::string kind = args.value("kind", "all");
        const Json includes = args.value("include_patterns", Json::array());
        const Json excludes = args.value("exclude_patterns", Json::array());

        std::vector<std::pair<uint32_t, std::string>> selected;
        if (path.empty() || path == ".") {
            if (level == 0) {
                for (uint32_t i = 0; i < waveform.root_scope_count(); ++i) {
                    const uint32_t root = waveform.root_scope_at(i);
                    const char* name = root ? waveform.scope_name(root) : nullptr;
                    if (name && *name) selected.emplace_back(root, name);
                }
            } else {
                for (uint32_t i = 0; i < waveform.root_scope_count(); ++i) {
                    const uint32_t root = waveform.root_scope_at(i);
                    const char* name = root ? waveform.scope_name(root) : nullptr;
                    if (name && *name)
                        collect_depth(waveform, root, level - 1, name, selected);
                }
            }
        } else {
            uint32_t base = 0;
            for (uint32_t i = 0; i < waveform.scope_count(); ++i) {
                const uint32_t ref = waveform.scope_at(i);
                const char* full = ref ? waveform.scope_full_name(ref) : nullptr;
                if (full && (path == full || path == public_wave_path(full))) {
                    base = ref;
                    break;
                }
            }
            if (!base) return failure("SCOPE_NOT_FOUND", "scope not found: " + path);
            collect_depth(waveform, base, level, "", selected);
        }

        Json all_modules = Json::array();
        Json all_ports = Json::array();
        Json all_signals = Json::array();
        for (const auto& [scope_ref, prefix] : selected) {
            for (uint32_t i = 0; i < waveform.scope_child_count(scope_ref); ++i) {
                const uint32_t child = waveform.scope_child_at(scope_ref, i);
                const char* name = child ? waveform.scope_name(child) : nullptr;
                if (!name || !*name) continue;
                const std::string relative = prefix.empty()
                    ? name : prefix + "." + name;
                all_modules.push_back({{"name", relative},
                                       {"module_name", std::string(name)}});
            }
            for (uint32_t i = 0; i < waveform.scope_var_count(scope_ref); ++i) {
                const uint32_t var = waveform.scope_var_at(scope_ref, i);
                const char* name = var ? waveform.var_name(var) : nullptr;
                if (!name || !*name) continue;
                const std::string relative = prefix.empty()
                    ? name : prefix + "." + name;
                uint32_t width = 0;
                waveform.var_encoding(var, &width);
                int direction = 0;
                if (globals.has_design && globals.design) {
                    const char* full = waveform.var_full_name(var);
                    std::string design_name = full ? full : "";
                    const size_t dot = design_name.find('.');
                    if (dot != std::string::npos)
                        design_name = "top" + design_name.substr(dot);
                    const int index = globals.design->resolve(design_name.c_str());
                    if (index >= 0) direction = globals.design->signal_direction(index);
                }
                if (direction != 0) {
                    const char* direction_name = direction == 1 ? "input"
                        : direction == 2 ? "output" : "inout";
                    all_ports.push_back({{"name", relative},
                                         {"direction", direction_name},
                                         {"width", width ? Json(width) : Json(nullptr)}});
                } else {
                    all_signals.push_back({{"name", relative},
                                           {"width", width ? Json(width) : Json(nullptr)}});
                }
            }
        }

        const auto filter_rows = [&](const Json& input) {
            Json output = Json::array();
            for (const auto& row : input) {
                const std::string name = row.at("name");
                if (included(name, includes, excludes)) output.push_back(row);
            }
            return output;
        };
        Json modules = (kind == "all" || kind == "module")
            ? filter_rows(all_modules) : Json::array();
        Json ports = (kind == "all" || kind == "port")
            ? filter_rows(all_ports) : Json::array();
        Json signals = (kind == "all" || kind == "signal")
            ? filter_rows(all_signals) : Json::array();
        const size_t scanned = all_modules.size() + all_ports.size() + all_signals.size();
        const size_t eligible = modules.size() + ports.size() + signals.size();
        const Json& limits = req.value("limits", Json::object());
        const size_t max_rows = limits.contains("max_rows")
            ? limits.at("max_rows").get<size_t>() : eligible;
        size_t remaining = max_rows;
        const auto apply_limit = [&remaining](Json& rows) {
            if (rows.size() > remaining) rows.erase(rows.begin() + remaining, rows.end());
            remaining -= std::min(remaining, rows.size());
        };
        apply_limit(modules);
        apply_limit(ports);
        apply_limit(signals);
        const size_t returned = modules.size() + ports.size() + signals.size();
        const bool truncated = returned < eligible;
        Json summary{{"path", path}, {"level", level}, {"kind", kind},
                     {"include_patterns", includes}, {"exclude_patterns", excludes},
                     {"scanned_row_count", scanned},
                     {"returned_module_count", modules.size()},
                     {"returned_port_count", ports.size()},
                     {"returned_signal_count", signals.size()},
                     {"total_module_count", all_modules.size()},
                     {"total_port_count", all_ports.size()},
                     {"total_signal_count", all_signals.size()},
                     {"scan_complete", true}, {"analysis_complete", true},
                     {"response_truncated", truncated}, {"total_count", scanned},
                     {"returned_count", returned},
                     {"truncation_scopes", truncated
                         ? Json::array({"response_rows"}) : Json::array()}};
        return Json{{"ok", true}, {"summary", summary},
                    {"data", {{"modules", modules}, {"ports", ports},
                              {"signals", signals}}}};
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_scope_list_handler() {
    return std::make_unique<ScopeListHandler>();
}
std::unique_ptr<EngineActionHandler> make_scope_roots_handler() {
    return std::make_unique<ScopeRootsHandler>();
}

}  // namespace xdebug_fst
