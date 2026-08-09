// server.cpp — Server, stdio-loop, and one-shot CLI modes
// BSD-3-Clause License

#include "engine/engine_globals.h"
#include "engine/engine_action_handler.h"
#include "engine/action_registry.h"
#include "api/json_types.h"
#include "protocol/public_catalog.h"
#include "protocol/response.h"
#include "protocol/contract.h"
#include "protocol/xout_renderer.h"
#include "session/session_paths.h"
#include "session/session_registry.h"
#include "session/session_service.h"
#include "session/uds_transport.h"

#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace xdebug_fst {

static bool s_engine_server_context = false;

// ── Helpers ──

static Json error_response(const std::string& code,
                           const std::string& msg,
                           const std::string& layer = "handler",
                           bool recoverable = true) {
    return Json{{"ok", false},
                {"error", {{"code", code},
                           {"message", msg},
                           {"recoverable", recoverable},
                           {"error_layer", layer}}}};
}

static bool valid_internal_control_request(const Json& request,
                                           const std::string& action) {
    if (!request.is_object() || request.size() != 3 ||
        request.value("api_version", std::string()) != "xdebug.internal.v1" ||
        request.value("action", std::string()) != action ||
        !request.contains("args") || !request["args"].is_object() ||
        !request["args"].empty()) {
        return false;
    }
    return action == "server.ping" || action == "server.version" ||
           action == "server.quit";
}

static Json dispatch(const Json& request);

static Json dispatch_handler(const Json& request) {
    std::string action = request.value("action", "");
    if (action.empty()) {
        return error_response("MISSING_ACTION", "request must contain 'action'");
    }

    // Built-in stateless actions
    if (action == "actions") {
        const CatalogResult catalog =
            build_actions_catalog(request.value("args", Json::object()));
        return Json{{"ok", true},
                    {"summary", catalog.summary},
                    {"data", catalog.data}};
    }

    if (action == "schema") {
        const CatalogResult catalog =
            build_schema_catalog(request.value("args", Json::object()));
        if (!catalog.ok) return Json{{"ok", false}, {"error", catalog.error}};
        return Json{{"ok", true},
                    {"summary", catalog.summary},
                    {"data", catalog.data}};
    }

    // ── batch: run multiple requests in one call ──

    if (action == "batch") {
        auto args = request.value("args", Json::object());
        if (!args.contains("requests") || !args["requests"].is_array()) {
            return error_response("MISSING_FIELD", "batch requires args.requests[]");
        }
        Json responses = Json::array();
        for (auto& sub : args["requests"]) {
            if (!sub.is_object()) {
                responses.push_back(error_response("MISSING_ACTION", "batch item must be an object"));
                continue;
            }
            responses.push_back(dispatch(sub));
        }
        return Json{{"ok", true},
                    {"summary", {{"request_count", responses.size()}}},
                    {"data", {{"responses", responses}}}};
    }

    // Dispatch to registered handlers
    auto& reg = ActionRegistry::instance();
    auto* handler = reg.find(action);
    if (!handler) {
        return error_response("UNKNOWN_ACTION", "unknown action: " + action);
    }

    // Pre-flight checks
    auto& g = engine_globals();
    if (handler->needs_design() && !g.has_design) {
        return error_response("DESIGN_NOT_LOADED", "action requires design database: " + action);
    }
    if (handler->needs_waveform() && !g.has_waveform) {
        return error_response("WAVEFORM_NOT_LOADED", "action requires waveform file: " + action);
    }

    try {
        return handler->run(request);
    } catch (const std::exception& e) {
        return error_response("INTERNAL_ERROR", std::string("handler threw: ") + e.what());
    }
}

static Json dispatch(const Json& request) {
    const std::string action = request.is_object() ? request.value("action", "") : "";
    const ContractResult validation = validate_public_request(request);
    if (!validation.ok) return canonical_error(request, action, validation.error);

    if (!s_engine_server_context && is_frontend_session_action(action)) {
        Json response = canonical_response(
            request, action, handle_frontend_session_action(request));
        const ContractResult response_validation =
            validate_public_response(action, response);
        if (!response_validation.ok) {
            return canonical_error(request, action, response_validation.error);
        }
        return response;
    }
    if (s_engine_server_context && is_frontend_session_action(action)) {
        Json response = canonical_error(
            request, action,
            {{"code", "SESSION_ACTION_NOT_ALLOWED"},
             {"message", "session lifecycle actions must be handled by the frontend"},
             {"recoverable", true}, {"error_layer", "session_manager"}});
        const ContractResult response_validation =
            validate_public_response(action, response);
        if (!response_validation.ok) {
            return canonical_error(request, action, response_validation.error);
        }
        return response;
    }
    if (!s_engine_server_context && request_targets_managed_session(request)) {
        Json response = forward_to_managed_session(request);
        if (!response.value("ok", false) ||
            response.value("api_version", std::string()) != "xdebug.v1") {
            response = canonical_response(request, action, response);
        }
        const ContractResult response_validation =
            validate_public_response(action, response);
        if (!response_validation.ok) {
            return canonical_error(request, action, response_validation.error);
        }
        return response;
    }
    Json response = canonical_response(request, action, dispatch_handler(request));
    const ContractResult response_validation =
        validate_public_response(action, response);
    if (!response_validation.ok) {
        return canonical_error(request, action, response_validation.error);
    }
    return response;
}

// ── One-shot mode ──

int oneshot_main(bool json_mode) {
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    std::string input = oss.str();

    Json request;
    try {
        request = Json::parse(input);
    } catch (const std::exception& e) {
        Json error = canonical_error(
            Json::object(), "error",
            Json{{"code", "INVALID_JSON"}, {"message", e.what()},
                 {"recoverable", true}, {"error_layer", "handler"}});
        const std::string output = json_mode ? error.dump(2) + "\n"
                                             : render_xout_response(error);
        fprintf(stdout, "%s", output.c_str());
        return 1;
    }

    Json response = dispatch(request);
    const std::string output = json_mode ? response.dump(2) + "\n"
                                         : render_xout_response(response);
    fprintf(stdout, "%s", output.c_str());
    return response.value("ok", false) ? 0 : 1;
}

// ── Server mode ──

int server_main(int argc, char** argv) {
    fprintf(stderr, "[xdebug-fst] server mode starting...\n");

    std::string socket_path;
    std::string generation;
    std::string session_id;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--socket-path") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (std::strcmp(argv[i], "--generation") == 0 && i + 1 < argc) {
            generation = argv[++i];
        } else if (std::strcmp(argv[i], "--session-id") == 0 && i + 1 < argc) {
            session_id = argv[++i];
        }
    }
    if (socket_path.empty() || generation.empty() || session_id.empty() ||
        !xdebug_design::xdebug_design_generation_matches(session_id, generation)) {
        fprintf(stderr, "[xdebug-fst] invalid or stale UDS engine generation\n");
        return 1;
    }

    if (!init_engine_globals(argc, argv)) {
        fprintf(stderr, "[xdebug-fst] failed to initialize engine\n");
        return 1;
    }

    std::string transport_error;
    const int listener = create_uds_listener(socket_path, transport_error);
    if (listener < 0) {
        fprintf(stderr, "[xdebug-fst] %s\n", transport_error.c_str());
        return 1;
    }

    s_engine_server_context = true;
    xdebug_engine::SessionRegistry registry;
    bool should_quit = false;
    while (!should_quit) {
        Json request;
        int client = -1;
        if (!uds_receive_request(listener, request, client, transport_error)) {
            fprintf(stderr, "[xdebug-fst] %s\n", transport_error.c_str());
            continue;
        }
        Json response;
        const std::string api_version =
            request.value("api_version", std::string());
        const std::string action = request.value("action", std::string());
        if (api_version == "xdebug.internal.v1" &&
            !valid_internal_control_request(request, action)) {
            response = error_response(
                "INVALID_INTERNAL_REQUEST",
                "internal control request must match the strict private envelope",
                "transport", false);
        } else if (api_version == "xdebug.internal.v1" && action == "server.ping") {
            response = {{"ok", true},
                        {"data", {{"pong", true}, {"generation", generation}}}};
        } else if (api_version == "xdebug.internal.v1" &&
                   action == "server.version") {
            response = {{"ok", true},
                        {"data", {{"api_version", "xdebug.internal.v1"},
                                  {"generation", generation}}}};
        } else if (api_version == "xdebug.internal.v1" &&
                   action == "server.quit") {
            response = {{"ok", true}, {"data", Json::object()}};
            should_quit = true;
        } else {
            response = dispatch(request);
        }
        registry.touch_if_generation(session_id, generation, time(nullptr));
        if (!uds_send_response(client, response, transport_error)) {
            fprintf(stderr, "[xdebug-fst] %s\n", transport_error.c_str());
        }
    }
    close(listener);
    unlink(socket_path.c_str());

    fprintf(stderr, "[xdebug-fst] server exiting\n");
    return 0;
}

// ── Stdio-loop mode ──
//
// xdebug-stdio-loop wire protocol (xverif MCP compatible):
//   1. on startup, emit the ready line on stdout
//   2. each request line is a JSON object; the response is an envelope:
//      {"id": <request id>, "ok": bool, "api_version": "xdebug.v1",
//       "action": <action>, "payload_format": "json"|"xout",
//       "json": <response> | "xout": <text>}
//   3. "stdio.quit" terminates the loop

int stdio_loop_main(int argc, char** argv) {
    fprintf(stderr, "[xdebug-fst] stdio-loop mode starting...\n");

    if (!init_engine_globals(argc, argv)) {
        fprintf(stderr, "[xdebug-fst] failed to initialize engine\n");
        return 1;
    }

    bool json_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) json_mode = true;
    }

    // Ready handshake (xdebug-stdio-loop protocol): the launcher waits for
    // this line on stdout before sending session.open.
    fprintf(stdout, "{\"type\":\"ready\",\"protocol\":\"xdebug-stdio-loop\",\"version\":1,\"pid\":%ld}\n",
            static_cast<long>(getpid()));
    fflush(stdout);

    int req_seq = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        Json request;
        try {
            request = Json::parse(line);
        } catch (const std::exception& exception) {
            Json env{{"id", "req-" + std::to_string(++req_seq)},
                     {"ok", false},
                     {"error", {{"code", "INVALID_JSON"},
                                {"message", exception.what()}}}};
            fprintf(stdout, "%s\n", env.dump().c_str());
            fflush(stdout);
            continue;
        }

        std::string action = request.value("action", "");
        ++req_seq;
        std::string rid = request.value("id",
                          request.value("request_id", "req-" + std::to_string(req_seq)));
        if (request.contains("id")) request.erase("id");
        if (request.contains("trace_id")) {
            if (!request["trace_id"].is_string() ||
                request["trace_id"].get<std::string>().empty()) {
                Json env{{"id", rid}, {"ok", false},
                         {"error", {{"code", "INVALID_REQUEST"},
                                    {"message", "stdio-loop trace_id must be a non-empty string"}}}};
                fprintf(stdout, "%s\n", env.dump().c_str());
                fflush(stdout);
                continue;
            }
            // trace_id is transport observability metadata.  It is consumed
            // by the loop boundary and never projected into the strict public
            // xdebug.v1 request envelope.
            request.erase("trace_id");
        }
        bool wants_json = json_mode;
        if (request.contains("payload_format")) {
            if (!request["payload_format"].is_string() ||
                (request["payload_format"] != "json" && request["payload_format"] != "xout")) {
                Json env{{"id", rid}, {"ok", false},
                         {"error", {{"code", "INVALID_REQUEST"},
                                    {"message", "stdio-loop payload_format must be json or xout"}}}};
                fprintf(stdout, "%s\n", env.dump().c_str());
                fflush(stdout);
                continue;
            }
            wants_json = request["payload_format"] == "json";
            request.erase("payload_format");
        }
        if (action == "stdio.quit") {
            Json env{{"id", rid}, {"ok", true}, {"api_version", "xdebug.v1"},
                     {"action", "stdio.quit"}, {"payload_format", "json"},
                     {"json", {{"ok", true}, {"action", "stdio.quit"}}}};
            fprintf(stdout, "%s\n", env.dump().c_str());
            fflush(stdout);
            break;
        }

        Json response = dispatch(request);
        bool ok = response.value("ok", false);

        Json env;
        env["id"] = rid;
        env["ok"] = ok;
        env["api_version"] = "xdebug.v1";
        env["action"] = action;
        if (!ok) {
            env["error"] = response.value("error", Json::object());
            env["json"] = response;
        }
        if (wants_json) {
            env["payload_format"] = "json";
            if (ok) env["json"] = response;
        } else {
            env["payload_format"] = "xout";
            env["xout"] = render_xout_response(response);
        }
        fprintf(stdout, "%s\n", env.dump().c_str());
        fflush(stdout);
    }

    return 0;
}

} // namespace xdebug_fst
