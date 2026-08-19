// engine_globals.cpp — Global state implementation
// BSD-3-Clause License

#include "engine_globals.h"
#include "backend/binary_design_backend.h"
#include "backend/wellen_fst_backend.h"
#include "backend/xdd_design_backend.h"

#include <chrono>
#include <cstring>
#include <cstdio>

namespace xdebug_fst {

static EngineGlobals s_globals;

EngineGlobals& engine_globals() {
    return s_globals;
}

bool init_engine_globals(int argc, char** argv) {
    auto& g = s_globals;

    // Parse command-line arguments (mirrors xdebug server args)
    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "-fsdb") == 0 ||
             std::strcmp(argv[i], "-fst") == 0) &&
            i + 1 < argc) {
            g.waveform_path = argv[++i];
        } else if (std::strcmp(argv[i], "-dbdir") == 0 && i + 1 < argc) {
            g.design_path = argv[++i];
        } else if (std::strcmp(argv[i], "--design-db-format") == 0 && i + 1 < argc) {
            g.design_format = argv[++i];
        } else if (std::strcmp(argv[i], "--session-id") == 0 && i + 1 < argc) {
            g.session_id = argv[++i];
        } else if (i == 1 && argv[i][0] != '-') {
            // First positional argument is session_id
            g.session_id = argv[i];
        }
    }

    // Open waveform backend if specified
    if (!g.waveform_path.empty()) {
        g.waveform = std::make_unique<WellenFstBackend>();
        if (g.waveform->open(g.waveform_path)) {
            g.has_waveform = true;
            fprintf(stderr, "[engine] opened waveform: %s (%u time points)\n",
                    g.waveform_path.c_str(), g.waveform->time_count());
        } else {
            fprintf(stderr, "[engine] ERROR: failed to open waveform: %s\n",
                    g.waveform_path.c_str());
            return false;
        }
    }

    // Open design backend if specified
    if (!g.design_path.empty()) {
        if (g.design_format == "xdd-so") {
            g.design = std::make_unique<XddDesignBackend>();
        } else if (g.design_format == "binary-v1") {
            g.design = std::make_unique<BinaryDesignBackend>();
        } else {
            fprintf(stderr, "[engine] ERROR: unsupported design db format: %s\n",
                    g.design_format.c_str());
            return false;
        }
        if (g.design->open(g.design_path)) {
            g.has_design = true;
            const auto index_started = std::chrono::steady_clock::now();
            g.design_index = std::make_unique<DesignQueryIndex>(*g.design);
            const double index_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - index_started).count();
            fprintf(stderr, "[engine] opened design db: %s (%d signals)\n",
                    g.design_path.c_str(), g.design->signal_count());
            fprintf(stderr,
                    "[engine] design query index: build_ms=%.3f estimated_bytes=%zu "
                    "signals_scanned=%llu port_records_scanned=%llu\n",
                    index_ms, g.design_index->estimated_bytes(),
                    static_cast<unsigned long long>(
                        g.design_index->signals_scanned()),
                    static_cast<unsigned long long>(
                        g.design_index->port_records_scanned()));
        } else {
            fprintf(stderr, "[engine] ERROR: failed to open design db: %s\n",
                    g.design_path.c_str());
            return false;
        }
    }

    fprintf(stderr, "[engine] session=%s waveform=%d design=%d\n",
            g.session_id.c_str(), g.has_waveform, g.has_design);
    return true;
}

} // namespace xdebug_fst
