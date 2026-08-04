// combined_register.cpp — Register all combined/design/window action handlers (BSD-3-Clause)
#include "engine/action_registry.h"

namespace xdebug_fst {

// Design actions (in design/design_actions.cpp)
std::unique_ptr<EngineActionHandler> make_signal_canonicalize_handler();
std::unique_ptr<EngineActionHandler> make_expr_normalize_handler();

// Combined actions (in combined/combined_actions.cpp)
std::unique_ptr<EngineActionHandler> make_trace_active_driver_handler();
std::unique_ptr<EngineActionHandler> make_trace_active_driver_chain_handler();
std::unique_ptr<EngineActionHandler> make_trace_x_origin_handler();

// Window actions (in combined/window_actions.cpp)
std::unique_ptr<EngineActionHandler> make_window_verify_handler();
std::unique_ptr<EngineActionHandler> make_verify_conditions_handler();

void register_combined_actions(ActionRegistry& r) {
    // Design
    r.add(make_signal_canonicalize_handler());
    r.add(make_expr_normalize_handler());

    // Combined
    r.add(make_trace_active_driver_handler());
    r.add(make_trace_active_driver_chain_handler());
    r.add(make_trace_x_origin_handler());

    // Window
    r.add(make_window_verify_handler());
    r.add(make_verify_conditions_handler());
}

} // namespace xdebug_fst
