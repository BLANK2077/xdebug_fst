// server.cpp — Server, stdio-loop, and one-shot CLI modes
// BSD-3-Clause License

#include "engine/engine_globals.h"
#include "engine/engine_action_handler.h"
#include "engine/action_registry.h"
#include "backend/wellen_fst_backend.h"
#include "backend/xdd_design_backend.h"
#include "waveform/list/list_manager.h"
#include "waveform/cursor/cursor_manager.h"
#include "api/json_types.h"
#include "protocol/public_catalog.h"
#include "protocol/response.h"
#include "protocol/contract.h"

#include <cstdio>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace xdebug_fst {

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

    if (action == "session.open") {
        auto target = request.value("target", Json::object());
        // session_id comes from args.name (xverif MCP convention) or
        // target.session_id (xdebug-fst native convention)
        auto args = request.value("args", Json::object());
        std::string session_id = args.value("name",
            target.value("session_id", "default"));
        std::string fsdb_path  = target.value("fsdb", "");
        std::string design_db  = target.value("daidir", "");

        auto& g = engine_globals();
        g.session_id = session_id;

        // Auto-detect the Verilator DesignDB .so next to the waveform when
        // design_db was not given explicitly (xverif MCP only passes fsdb).
        // Verilator fixtures keep the .so either beside the .fst or under an
        // obj_dir/ subdirectory.
        if (design_db.empty() && !fsdb_path.empty()) {
            size_t slash = fsdb_path.find_last_of('/');
            std::string dir = (slash == std::string::npos)
                ? "." : fsdb_path.substr(0, slash);
            std::string candidates[2] = {dir, dir + "/obj_dir"};
            for (const auto& cand : candidates) {
                DIR* d = opendir(cand.c_str());
                if (!d) continue;
                struct dirent* e;
                while ((e = readdir(d)) != nullptr) {
                    std::string name = e->d_name;
                    if (name.size() > 13 &&
                        name.rfind("libV", 0) == 0 &&
                        name.rfind("__DesignDb.so") == name.size() - 13) {
                        design_db = cand + "/" + name;
                        break;
                    }
                }
                closedir(d);
                if (!design_db.empty()) break;
            }
        }

        // Session-scoped state is reset for the new session
        ListManager::instance().clear();
        CursorManager::instance().clear();
        extern void clear_stream_configs();
        clear_stream_configs();

        // A new session.open replaces any previous session resources
        // (xdebug session semantics: open creates a fresh session).
        if (!fsdb_path.empty() || !design_db.empty()) {
            if (g.waveform) {
                g.waveform->close();
                g.waveform.reset();
                g.has_waveform = false;
            }
            if (g.design) {
                g.design->close();
                g.design.reset();
                g.has_design = false;
            }
        }
        if (!fsdb_path.empty()) {
            g.waveform = std::make_unique<WellenFstBackend>();
            g.has_waveform = g.waveform->open(fsdb_path);
            g.waveform_path = fsdb_path;
            if (!g.has_waveform) {
                g.waveform.reset();
                return Json{{"ok", false},
                            {"error", {{"code", "WAVEFORM_OPEN_FAILED"},
                                       {"message", "failed to open waveform: " + fsdb_path}}}};
            }
        }

        if (!design_db.empty()) {
            g.design = std::make_unique<XddDesignBackend>();
            g.has_design = g.design->open(design_db);
            g.design_path = design_db;
            if (!g.has_design) {
                g.design.reset();
                return Json{{"ok", false},
                            {"error", {{"code", "DESIGN_OPEN_FAILED"},
                                       {"message", "failed to open design db: " + design_db}}}};
            }
        }

        return Json{
            {"ok", true},
            {"session", {
                {"session_id", session_id},
                {"state", "alive"},
                {"has_waveform", g.has_waveform},
                {"has_design", g.has_design},
            }}
        };
    }

    if (action == "session.close") {
        auto& g = engine_globals();
        if (g.waveform) g.waveform->close();
        if (g.design) g.design->close();
        g.has_waveform = false;
        g.has_design = false;
        return Json{{"ok", true}, {"session", {{"state", "closed"}}}};
    }

    // ── Session management (single-session process semantics) ──

    if (action == "session.list") {
        auto& g = engine_globals();
        Json sessions = Json::array();
        sessions.push_back({
            {"session_id", g.session_id},
            {"state", g.has_waveform ? "alive" : "idle"},
            {"has_waveform", g.has_waveform},
            {"has_design", g.has_design},
            {"waveform_path", g.waveform_path},
            {"design_path", g.design_path},
        });
        return Json{{"ok", true},
                    {"summary", {{"session_count", sessions.size()}}},
                    {"data", {{"sessions", sessions}}}};
    }

    if (action == "session.doctor") {
        auto& g = engine_globals();
        Json checks = Json::array();
        auto add_check = [&](const std::string& name, bool ok, const std::string& detail) {
            checks.push_back({{"check", name}, {"ok", ok}, {"detail", detail}});
        };
        add_check("waveform_loaded", g.has_waveform,
                  g.has_waveform ? g.waveform_path : "no waveform file loaded");
        add_check("design_loaded", g.has_design,
                  g.has_design ? g.design_path : "no design db loaded");
        bool healthy = g.has_waveform || true;  // waveform optional for design-only sessions
        return Json{{"ok", true},
                    {"summary", {{"session_id", g.session_id}, {"healthy", healthy}}},
                    {"data", {{"checks", checks}}}};
    }

    if (action == "session.gc" || action == "session.kill") {
        // Single-session process: nothing else to collect. Kill closes the
        // session (same as session.close for this process model).
        auto& g = engine_globals();
        if (action == "session.kill" && g.waveform) g.waveform->close();
        if (action == "session.kill" && g.design) g.design->close();
        if (action == "session.kill") {
            g.has_waveform = false;
            g.has_design = false;
        }
        return Json{{"ok", true},
                    {"summary", {{"action", action},
                                 {"reclaimed", 0},
                                 {"session_id", g.session_id}}}};
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
    return canonical_response(request, action, dispatch_handler(request));
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
        fprintf(stdout, "{\"ok\":false,\"error\":{\"code\":\"PARSE_ERROR\",\"message\":\"%s\"}}\n", e.what());
        return 1;
    }

    Json response = dispatch(request);
    fprintf(stdout, "%s\n", response.dump().c_str());
    return 0;
}

// ── Server mode ──

int server_main(int argc, char** argv) {
    fprintf(stderr, "[xdebug-fst] server mode starting...\n");

    if (!init_engine_globals(argc, argv)) {
        fprintf(stderr, "[xdebug-fst] failed to initialize engine\n");
        return 1;
    }

    // In full implementation: set up UDS/TCP transport, accept loop.
    // For now: process stdin as one-shot.
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    std::string input = oss.str();

    if (!input.empty()) {
        Json request = Json::parse(input);
        Json response = dispatch(request);
        fprintf(stdout, "%s\n", response.dump().c_str());
    }

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

static std::string simple_xout(const std::string& action, const Json& response) {
    // Compact xout rendering: header line + pretty JSON body.
    std::string out = "@xdebug." + action + ".v1\n";
    out += response.dump(2);
    out += "\n";
    return out;
}

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
        } catch (...) {
            Json err = error_response("PARSE_ERROR", "failed to parse JSON request line");
            Json env{{"id", "req-" + std::to_string(++req_seq)},
                     {"ok", false},
                     {"api_version", "xdebug.v1"},
                     {"action", ""},
                     {"payload_format", "json"},
                     {"json", err}};
            fprintf(stdout, "%s\n", env.dump().c_str());
            fflush(stdout);
            continue;
        }

        std::string action = request.value("action", "");
        if (action == "stdio.quit") break;

        std::string rid = request.value("request_id",
                          request.value("id", "req-" + std::to_string(++req_seq)));

        Json response = dispatch(request);
        bool ok = response.value("ok", false);

        Json env;
        env["id"] = rid;
        env["ok"] = ok;
        env["api_version"] = "xdebug.v1";
        env["action"] = action;
        if (json_mode) {
            env["payload_format"] = "json";
            env["json"] = response;
        } else {
            env["payload_format"] = "xout";
            env["xout"] = simple_xout(action, response);
        }
        fprintf(stdout, "%s\n", env.dump().c_str());
        fflush(stdout);
    }

    return 0;
}

} // namespace xdebug_fst
