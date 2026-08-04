// scope_list.cpp — scope.list and scope.roots actions (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"

namespace xdebug_fst {

struct ScopeListHandler : public EngineActionHandler {
    const char* action_name() const override { return "scope.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        auto& wf = *g.waveform;
        Json scopes = Json::array();
        for (uint32_t i = 0, n = wf.scope_count(); i < n && i < 200; ++i) {
            uint32_t sr = wf.scope_at(i);
            if (!sr) continue;
            Json s;
            s["name"] = wf.scope_name(sr) ? wf.scope_name(sr) : "";
            s["var_count"] = wf.scope_var_count(sr);
            s["child_count"] = wf.scope_child_count(sr);
            scopes.push_back(s);
        }
        return Json{{"ok",true},{"summary",{{"scope_count",scopes.size()}}},{"data",{{"scopes",scopes}}}};
    }
};

struct ScopeRootsHandler : public EngineActionHandler {
    const char* action_name() const override { return "scope.roots"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json&) override {
        auto& wf = *engine_globals().waveform;
        Json roots = Json::array();
        for (uint32_t i = 0, n = wf.scope_count(); i < n; ++i) {
            uint32_t sr = wf.scope_at(i);
            if (!sr) continue;
            roots.push_back({{"name",wf.scope_name(sr)?wf.scope_name(sr):""}});
        }
        return Json{{"ok",true},{"summary",{{"root_count",roots.size()}}},{"data",{{"roots",roots}}}};
    }
};

std::unique_ptr<EngineActionHandler> make_scope_list_handler() { return std::make_unique<ScopeListHandler>(); }
std::unique_ptr<EngineActionHandler> make_scope_roots_handler() { return std::make_unique<ScopeRootsHandler>(); }

} // namespace xdebug_fst
