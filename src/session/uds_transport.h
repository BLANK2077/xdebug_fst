#pragma once

#include "api/json_types.h"

#include <string>

namespace xdebug_fst {

int create_uds_listener(const std::string& socket_path, std::string& error);
bool uds_receive_request(int listener_fd, Json& request, int& client_fd,
                         std::string& error);
bool uds_send_response(int client_fd, const Json& response, std::string& error);
bool uds_request(const std::string& socket_path, const Json& request,
                 Json& response, int timeout_ms, std::string& error);

}  // namespace xdebug_fst
