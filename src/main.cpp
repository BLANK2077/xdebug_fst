// main.cpp — xdebug-fst entry point
// BSD-3-Clause License
//
// CLI interface (compatible with xdebug):
//   xdebug-fst --json -                    # one-shot JSON on stdin
//   xdebug-fst -                           # one-shot XOUT on stdin
//   xdebug-fst --stdio-loop --json         # persistent session loop
//   xdebug-fst --server <id> -fsdb <path>  # engine server mode

#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include "build_version.h"

namespace xdebug_fst {
    int server_main(int argc, char** argv);
    int stdio_loop_main(int argc, char** argv);
    int oneshot_main(bool json_mode);
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 ||
                      std::strcmp(argv[1], "-h") == 0)) {
        std::cout << "xdebug-fst — FST waveform and DesignDB queries\n"
                  << "Usage: xdebug-fst [--json] [-]\n"
                  << "       xdebug-fst --stdio-loop [--json]\n"
                  << "       xdebug-fst --version [--json]\n"
                  << "Requests: xdebug.v1 JSON on stdin; default output: XOUT.\n"
                  << "Managed sessions use local UDS. Input waveform format: FST only.\n";
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
        if (argc == 3 && std::strcmp(argv[2], "--json") == 0) {
            std::cout << "{\"name\":\"xdebug-fst\",\"version\":\"" XDEBUG_RELEASE_VERSION
                "\",\"git_revision\":\"" XDEBUG_GIT_REVISION
                "\",\"schema_revision\":\"" XDEBUG_SCHEMA_REVISION
                "\",\"compat_runtime_revision\":\"" XDEBUG_COMPAT_RUNTIME "\"}\n";
        } else if (argc == 2) {
            std::cout << "xdebug-fst " XDEBUG_RELEASE_VERSION " (" XDEBUG_GIT_REVISION ")\n";
        } else {
            std::cerr << "Invalid --version arguments\n";
            return 2;
        }
        return 0;
    }
    // Server arguments are parsed by server_main; public CLI accepts only these flags.
    bool has_server = false;
    for (int i = 1; i < argc; ++i) has_server |= std::strcmp(argv[i], "--server") == 0;
    if (!has_server) {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") && std::strcmp(argv[i], "-") &&
                std::strcmp(argv[i], "--stdio-loop")) {
                std::cerr << "Unknown argument: " << argv[i] << "; use --help\n";
                return 2;
            }
        }
    }
    // Detect mode
    bool server_mode = false;
    bool stdio_loop  = false;
    bool json_mode   = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0) {
            server_mode = true;
            break;
        } else if (std::strcmp(argv[i], "--stdio-loop") == 0) {
            stdio_loop = true;
            break;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        }
    }

    if (server_mode) {
        return xdebug_fst::server_main(argc, argv);
    } else if (stdio_loop) {
        return xdebug_fst::stdio_loop_main(argc, argv);
    } else {
        return xdebug_fst::oneshot_main(json_mode);
    }
}
