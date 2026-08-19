// engine_globals.h — Global state for xdebug-fst engine
// BSD-3-Clause License
#pragma once

#include "backend/waveform_backend.h"
#include "backend/design_backend.h"
#include "backend/design_query_index.h"

#include <memory>
#include <string>

namespace xdebug_fst {

/// Global state shared by all action handlers.
struct EngineGlobals {
    // Backend instances
    std::unique_ptr<IWaveformBackend> waveform;
    std::unique_ptr<IDesignBackend>   design;
    std::unique_ptr<DesignQueryIndex> design_index;

    // Session info
    std::string session_id;
    std::string waveform_path;   // .fst file path
    std::string design_path;     // resolved DesignDB artifact path
    std::string design_format = "xdd-so";

    // Flags
    bool has_waveform = false;
    bool has_design   = false;
};

/// Singleton accessor.
EngineGlobals& engine_globals();

/// Initialize from command-line arguments.
bool init_engine_globals(int argc, char** argv);

} // namespace xdebug_fst
