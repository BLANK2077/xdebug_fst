#pragma once

#include "protocol/action_spec.h"

#include <map>
#include <string>
#include <vector>

namespace xdebug_fst {

class PublicActionRegistry {
public:
    bool register_spec(const ActionSpec& spec);
    const ActionSpec* find(const std::string& name) const;
    std::vector<ActionSpec> list() const;

    static const PublicActionRegistry& instance();

private:
    std::map<std::string, ActionSpec> specs_;
};

}  // namespace xdebug_fst
