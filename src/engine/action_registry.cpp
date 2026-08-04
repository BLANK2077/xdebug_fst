// action_registry.cpp — Action handler registry implementation
#include "action_registry.h"

namespace xdebug_fst {

void ActionRegistry::add(std::unique_ptr<EngineActionHandler> h) {
    action_names_.push_back(h->action_name());
    handlers_[h->action_name()] = std::move(h);
}

EngineActionHandler* ActionRegistry::find(const std::string& name) const {
    auto it = handlers_.find(name);
    return (it != handlers_.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> ActionRegistry::list_actions() const {
    return action_names_;
}

ActionRegistry& ActionRegistry::instance() {
    static ActionRegistry reg;
    static bool initialized = false;
    if (!initialized) {
        register_all_actions(reg);
        initialized = true;
    }
    return reg;
}

} // namespace xdebug_fst
