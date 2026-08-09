#include "protocol/response.h"

#include "core/diagnostic_error.h"

namespace xdebug_fst {

Json tool_metadata() {
    return {
        {"name", "xdebug"},
        {"version", "0.1.0"},
        {"build_id", "8eecf71271cc-c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"},
        {"git_revision", "8eecf71271cc"},
        {"schema_revision", "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"}
    };
}

Json canonical_error(const Json& request,
                     const std::string& action,
                     const Json& error) {
    const Json normalized = xdebug_core::normalize_diagnostic_error(error, "handler");
    Json response;
    response["api_version"] = "xdebug.v1";
    if (request.is_object() && request.contains("request_id") &&
        request["request_id"].is_string() && !request["request_id"].get<std::string>().empty()) {
        response["request_id"] = request["request_id"];
    }
    response["ok"] = false;
    response["action"] = action.empty() ? "error" : action;
    response["tool"] = tool_metadata();
    response["session"] = nullptr;
    response["summary"] = {
        {"status", "error"},
        {"error_code", normalized.value("code", "ACTION_FAILED")}
    };
    response["data"] = nullptr;
    response["error"] = normalized;
    return response;
}

Json canonical_response(const Json& request,
                        const std::string& action,
                        const Json& handler_response) {
    if (!handler_response.value("ok", false)) {
        return canonical_error(
            request,
            action,
            handler_response.value("error", Json::object()));
    }

    Json response;
    response["api_version"] = "xdebug.v1";
    if (request.is_object() && request.contains("request_id") &&
        request["request_id"].is_string() && !request["request_id"].get<std::string>().empty()) {
        response["request_id"] = request["request_id"];
    }
    response["ok"] = true;
    response["action"] = action;
    response["tool"] = tool_metadata();
    response["session"] = handler_response.value("session", Json(nullptr));
    response["summary"] = handler_response.value("summary", Json::object());
    response["data"] = handler_response.value("data", Json::object());
    for (const char* field : {
             "findings", "warnings", "suggested_next_actions", "advisories"}) {
        if (handler_response.contains(field)) response[field] = handler_response[field];
    }
    response["error"] = nullptr;
    return response;
}

}  // namespace xdebug_fst
