#pragma once

#include "api/json_types.h"

#include <string>

namespace xdebug_fst {

bool is_frontend_session_action(const std::string& action);
bool request_targets_managed_session(const Json& request);
Json handle_frontend_session_action(const Json& request);
Json forward_to_managed_session(const Json& request);

}  // namespace xdebug_fst
