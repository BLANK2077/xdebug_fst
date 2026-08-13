#pragma once

#include "api/json_types.h"

#include <string>

namespace xdebug_fst {

std::string render_source_paths_xout(const std::string& action,
                                     const Json& response);
std::string render_active_driver_chain_xout(const Json& response);
std::string render_x_origin_xout(const Json& response);
std::string render_stream_xout(const std::string& action,
                               const Json& response);
std::string render_scope_roots_xout(const Json& response);

}  // namespace xdebug_fst
