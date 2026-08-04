// server.cpp — Server, stdio-loop, and one-shot CLI modes
// BSD-3-Clause License

#include "engine/engine_globals.h"
#include "engine/engine_action_handler.h"
#include "engine/action_registry.h"
#include "backend/wellen_fst_backend.h"
#include "backend/xdd_design_backend.h"
#include "api/json_types.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace xdebug_fst {

// ── Helpers ──

static Json error_response(const std::string& code, const std::string& msg) {
    return Json{{"ok", false}, {"error", {{"code", code}, {"message", msg}}}};
}

static Json dispatch(const Json& request) {
    std::string action = request.value("action", "");
    if (action.empty()) {
        return error_response("MISSING_ACTION", "request must contain 'action'");
    }

    // Built-in stateless actions
    if (action == "actions") {
        auto& reg = ActionRegistry::instance();
        Json list = Json::array();
        for (auto& name : reg.list_actions()) {
            auto* h = reg.find(name);
            Json item;
            item["action"] = name;
            item["category"] = "waveform";
            item["requires"] = "waveform";
            if (h) {
                if (h->needs_design()) item["requires"] = "design+waveform";
                else if (!h->needs_waveform()) item["requires"] = "design";
                else if (!h->needs_design()) item["requires"] = "waveform";
            }
            list.push_back(item);
        }
        // Add meta actions
        list.push_back({{"action", "actions"}, {"category", "common"}, {"requires", "none"}});
        list.push_back({{"action", "schema"}, {"category", "common"}, {"requires", "none"}});
        list.push_back({{"action", "session.open"}, {"category", "session"}, {"requires", "session"}});
        list.push_back({{"action", "session.close"}, {"category", "session"}, {"requires", "session"}});
        return Json{{"ok", true}, {"actions", list}};
    }

    if (action == "schema") {
        std::string schema_action = request.value("args", Json::object()).value("action", "");
        return Json{
            {"ok", true},
            {"schema", {
                {"action", schema_action},
                {"kind", request.value("args", Json::object()).value("kind", "request")},
            }}
        };
    }

    if (action == "session.open") {
        auto target = request.value("target", Json::object());
        std::string session_id = target.value("session_id", "default");
        std::string fsdb_path  = target.value("fsdb", "");
        std::string design_db  = target.value("design_db", "");

        auto& g = engine_globals();
        g.session_id = session_id;

        if (!fsdb_path.empty() && !g.has_waveform) {
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

        if (!design_db.empty() && !g.has_design) {
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

int stdio_loop_main(int argc, char** argv) {
    fprintf(stderr, "[xdebug-fst] stdio-loop mode starting...\n");

    if (!init_engine_globals(argc, argv)) {
        fprintf(stderr, "[xdebug-fst] failed to initialize engine\n");
        return 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        Json request;
        try {
            request = Json::parse(line);
        } catch (...) {
            fprintf(stdout, "{\"ok\":false,\"error\":{\"code\":\"PARSE_ERROR\"}}\n");
            fflush(stdout);
            continue;
        }

        Json response = dispatch(request);
        fprintf(stdout, "%s\n", response.dump().c_str());
        fflush(stdout);
    }

    return 0;
}

} // namespace xdebug_fst
