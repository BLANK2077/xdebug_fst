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

namespace xdebug_fst {
    int server_main(int argc, char** argv);
    int stdio_loop_main(int argc, char** argv);
    int oneshot_main(bool json_mode);
}

int main(int argc, char** argv) {
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
