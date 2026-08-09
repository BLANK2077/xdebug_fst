#include "protocol/public_action_registry.h"

namespace xdebug_fst {

bool PublicActionRegistry::register_spec(const ActionSpec& spec) {
    if (spec.name.empty()) return false;
    return specs_.emplace(spec.name, spec).second;
}

const ActionSpec* PublicActionRegistry::find(const std::string& name) const {
    auto found = specs_.find(name);
    return found == specs_.end() ? nullptr : &found->second;
}

std::vector<ActionSpec> PublicActionRegistry::list() const {
    std::vector<ActionSpec> result;
    result.reserve(specs_.size());
    for (const auto& entry : specs_) result.push_back(entry.second);
    return result;
}

const PublicActionRegistry& PublicActionRegistry::instance() {
    static const PublicActionRegistry registry = [] {
        PublicActionRegistry registry;
#include "protocol/generated_action_metadata.inc"
        return registry;
    }();
    return registry;
}

}  // namespace xdebug_fst
