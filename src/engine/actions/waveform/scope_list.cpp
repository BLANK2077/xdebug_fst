// scope_list.cpp — scope.list and scope.roots actions (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "engine/trace_source_context.h"
#include "protocol/domain_xout_renderer.h"

#include <fnmatch.h>
#include <algorithm>
#include <cctype>
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

bool matches_scope_path(const std::string& requested,
                        const std::string& waveform_path) {
    if (requested == waveform_path || requested == public_wave_path(waveform_path))
        return true;
    if (waveform_path.rfind("TOP.", 0) == 0)
        return requested == waveform_path.substr(4);
    if (waveform_path.rfind("top.", 0) == 0)
        return requested == waveform_path.substr(4);
    return false;
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

std::string source_module_name(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(
               static_cast<unsigned char>(text[begin]))) ++begin;
    static const std::string keyword = "module";
    if (text.compare(begin, keyword.size(), keyword) != 0) return {};
    begin += keyword.size();
    if (begin >= text.size() || !std::isspace(
            static_cast<unsigned char>(text[begin]))) return {};
    while (begin < text.size() && std::isspace(
               static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = begin;
    while (end < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[end]);
        if (!(std::isalnum(byte) || text[end] == '_' || text[end] == '$' ||
              text[end] == '\\')) break;
        ++end;
    }
    return text.substr(begin, end - begin);
}

std::string design_scope_component(IDesignBackend& design,
                                   const std::string& waveform_scope) {
    std::string scope = public_wave_path(waveform_scope);
    if (scope.rfind("top.", 0) != 0) scope = "top." + scope;
    const std::string prefix = scope + ".";
    for (int index = 0; index < design.signal_count(); ++index) {
        const char* name = design.signal_name(index);
        if (!name || std::string(name).rfind(prefix, 0) != 0) continue;
        const char* file = design.signal_file(index);
        const int line = design.signal_line(index);
        if (!file || !*file || line <= 0) continue;
        for (const auto& row : trace_source_context(file, line)) {
            const std::string module = source_module_name(
                row.value("text", std::string()));
            if (!module.empty()) return module;
        }
    }
    return {};
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

        // Verilator FST places an implementation-only `top` wrapper above a
        // testbench module.  The original FSDB/NPI view exposes that unique
        // stateful child as the public root.  Collapse the wrapper only when
        // the DesignDB proves the child contains internal (non-port) state;
        // a port-only DUT such as counter_top keeps the existing `top` root.
        std::map<std::string, std::string> public_root_overrides;
        if (wave_available && design_available) {
            for (uint32_t i = 0; i < globals.waveform->root_scope_count(); ++i) {
                const uint32_t root = globals.waveform->root_scope_at(i);
                const char* root_full = root
                    ? globals.waveform->scope_full_name(root) : nullptr;
                if (!root_full || public_wave_path(root_full) != "top" ||
                    globals.waveform->scope_child_count(root) != 1) {
                    continue;
                }
                const uint32_t child = globals.waveform->scope_child_at(root, 0);
                const char* child_name_ptr = child
                    ? globals.waveform->scope_name(child) : nullptr;
                if (!child_name_ptr || !*child_name_ptr) continue;
                const std::string child_name = child_name_ptr;
                const std::string design_prefix = "top." + child_name + ".";
                bool has_internal_state = false;
                for (int signal = 0; signal < globals.design->signal_count(); ++signal) {
                    const char* name = globals.design->signal_name(signal);
                    if (name && std::string(name).rfind(design_prefix, 0) == 0 &&
                        globals.design->signal_direction(signal) == 0) {
                        has_internal_state = true;
                        break;
                    }
                }
                if (has_internal_state)
                    public_root_overrides["top"] = child_name;
            }
        }

        std::set<std::string> wave_names;
        Json wave_roots = Json::array();
        if (select_wave && wave_available) {
            for (uint32_t i = 0; i < globals.waveform->root_scope_count(); ++i) {
                const uint32_t ref = globals.waveform->root_scope_at(i);
                const char* name = ref ? globals.waveform->scope_full_name(ref) : nullptr;
                if (!name || !*name) continue;
                std::string path = public_wave_path(name);
                const auto override = public_root_overrides.find(path);
                if (override != public_root_overrides.end())
                    path = override->second;
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
                std::string root = first_component(name);
                const auto override = public_root_overrides.find(root);
                if (override != public_root_overrides.end())
                    root = override->second;
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
                if (full && matches_scope_path(path, full)) {
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
                if (waveform.scope_kind(child)==
                        IWaveformBackend::ScopeKind::Interface) {
                    const char* full = waveform.scope_full_name(child);
                    std::string design_name = full ? public_wave_path(full) : "";
                    if (!design_name.empty()&&design_name.rfind("top.",0)!=0)
                        design_name="top."+design_name;
                    const int design_index=globals.has_design&&globals.design
                        ?globals.design->resolve(design_name.c_str()):-1;
                    if (design_index>=0&&
                        globals.design->signal_direction(design_index)!=0) {
                        all_ports.push_back({{"name",relative},
                            {"direction","interface"},{"width",nullptr}});
                    } else {
                        all_signals.push_back({{"name",relative},
                            {"width",nullptr}});
                    }
                } else {
                    const char* component = waveform.scope_component(child);
                    std::string module_name = component && *component
                        ? std::string(component) : std::string();
                    if (module_name.empty() && globals.has_design && globals.design) {
                        const char* full = waveform.scope_full_name(child);
                        if (full && *full)
                            module_name = design_scope_component(
                                *globals.design, full);
                    }
                    if (module_name.empty()) module_name = name;
                    all_modules.push_back({{"name", relative},
                                           {"module_name", module_name}});
                }
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
