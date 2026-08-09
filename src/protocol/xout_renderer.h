#pragma once

#include "api/json_types.h"

#include <string>

namespace xdebug_fst {

std::string render_xout_response(const Json& response);
std::string render_xout_response(const Json& response,
                                 const std::string& handler_xout);
std::string render_xout_transport_payload(const Json& response);
std::string render_xout_transport_payload(const Json& response,
                                          const std::string& handler_xout);

} // namespace xdebug_fst
