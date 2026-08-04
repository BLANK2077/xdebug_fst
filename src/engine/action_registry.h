// action_registry.h — Action handler registry
#pragma once
#include "engine/engine_action_handler.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xdebug_fst {

class ActionRegistry {
public:
    void add(std::unique_ptr<EngineActionHandler> handler);
    EngineActionHandler* find(const std::string& action_name) const;
    std::vector<std::string> list_actions() const;

    static ActionRegistry& instance();

private:
    ActionRegistry() = default;
    std::unordered_map<std::string, std::unique_ptr<EngineActionHandler>> handlers_;
    std::vector<std::string> action_names_; // insertion order
};

void register_all_actions(ActionRegistry& r);

} // namespace xdebug_fst
