// engine_action_handler.h — Abstract action handler interface (BSD-3-Clause)
#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace xdebug_fst {

using Json = nlohmann::json;

class EngineActionHandler {
public:
    virtual ~EngineActionHandler() = default;
    virtual const char* action_name() const = 0;
    virtual bool needs_design() const = 0;
    virtual bool needs_waveform() const = 0;
    virtual Json run(const Json& request) = 0;
    // Return an action-specific human-readable projection when the generic
    // renderer cannot faithfully express the response shape.  An empty
    // string deliberately selects the generic renderer.
    virtual std::string render_xout(const Json&) const { return std::string(); }
};

} // namespace xdebug_fst
