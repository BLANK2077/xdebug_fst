// engine_action_handler.h — Abstract action handler interface
// BSD-3-Clause License
// Compatible with xdebug EngineActionHandler contract.
#pragma once

#include <string>
#include <memory>

namespace xdebug_fst {

// Minimal JSON type (nlohmann::json compatible subset)
// For now, use a simple string-based approach; full JSON integration later.
using Json = std::string; // placeholder — will be replaced with nlohmann::json

/// Abstract base for all action handlers.
/// Mirrors xdebug's EngineActionHandler interface for drop-in compatibility.
class EngineActionHandler {
public:
    virtual ~EngineActionHandler() = default;

    /// Action name for dispatch (e.g. "value.at", "trace.driver").
    virtual const char* action_name() const = 0;

    /// Whether this action requires a design database.
    virtual bool needs_design() const = 0;

    /// Whether this action requires a waveform file.
    virtual bool needs_waveform() const = 0;

    /// Execute the action. Receives a JSON request string, returns a JSON
    /// response string.
    virtual std::string run(const std::string& request_json) = 0;

    /// Render the response as human-readable XOUT text (optional).
    virtual std::string render_xout(const std::string& response_json) const {
        return response_json;
    }
};

} // namespace xdebug_fst
