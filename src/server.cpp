// server.cpp — Server mode and one-shot CLI implementation
// BSD-3-Clause License

#include "engine/engine_globals.h"
#include "engine/engine_action_handler.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace xdebug_fst {

// ── One-shot mode ──

int oneshot_main(bool json_mode) {
    // Read entire stdin
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    std::string input = oss.str();

    // Parse action from input (simplified: just handle "actions" and "schema")
    // In full implementation, use a proper JSON parser.
    std::string action;
    auto pos = input.find("\"action\"");
    if (pos != std::string::npos) {
        auto start = input.find('"', pos + 8);
        if (start != std::string::npos) {
            auto end = input.find('"', start + 1);
            if (end != std::string::npos) {
                action = input.substr(start + 1, end - start - 1);
            }
        }
    }

    // Handle built-in actions
    if (action == "actions") {
        fprintf(stdout, "{\"ok\":true,\"actions\":["
                "{\"action\":\"actions\",\"category\":\"common\",\"requires\":\"none\"},"
                "{\"action\":\"schema\",\"category\":\"common\",\"requires\":\"none\"},"
                "{\"action\":\"session.open\",\"category\":\"session\",\"requires\":\"session\"}"
                "]}\n");
    } else if (action == "schema") {
        fprintf(stdout, "{\"ok\":true,\"schema\":{\"action\":\"schema\",\"kind\":\"request\"}}\n");
    } else if (action == "session.open") {
        fprintf(stdout, "{\"ok\":true,\"session\":{\"session_id\":\"test\",\"state\":\"alive\"}}\n");
    } else {
        fprintf(stdout, "{\"ok\":false,\"error\":{\"code\":\"UNKNOWN_ACTION\",\"message\":\"unknown action\"}}\n");
    }

    return 0;
}

// ── Server mode ──

int server_main(int argc, char** argv) {
    fprintf(stderr, "[xdebug-fst] server mode starting...\n");

    if (!init_engine_globals(argc, argv)) {
        fprintf(stderr, "[xdebug-fst] failed to initialize engine\n");
        return 1;
    }

    auto& g = engine_globals();

    fprintf(stderr, "[xdebug-fst] server ready: session=%s waveform=%d design=%d\n",
            g.session_id.c_str(), g.has_waveform, g.has_design);

    // In server mode, we'd set up a transport (UDS/TCP/file) and enter an accept loop.
    // For now, just run one-shot processing on stdin.
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    std::string input = oss.str();

    if (!input.empty()) {
        return oneshot_main(true);
    }

    fprintf(stderr, "[xdebug-fst] server idle (no stdin input, exiting)\n");
    return 0;
}

// ── Stdio-loop mode ──

int stdio_loop_main(int argc, char** argv) {
    fprintf(stderr, "[xdebug-fst] stdio-loop mode starting...\n");

    if (!init_engine_globals(argc, argv)) {
        fprintf(stderr, "[xdebug-fst] failed to initialize engine\n");
        return 1;
    }

    // stdio-loop mode: read JSON-line-delimited requests, respond with JSON lines.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        fprintf(stdout, "{\"ok\":true,\"session_id\":\"test\",\"state\":\"ready\"}\n");
        fflush(stdout);
        break;
    }

    return 0;
}

} // namespace xdebug_fst
