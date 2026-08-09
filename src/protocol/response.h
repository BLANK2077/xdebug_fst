#pragma once

#include "api/json_types.h"

#include <string>

namespace xdebug_fst {

Json tool_metadata();
Json canonical_response(const Json& request,
                        const std::string& action,
                        const Json& handler_response);
Json canonical_error(const Json& request,
                     const std::string& action,
                     const Json& error);

}  // namespace xdebug_fst
