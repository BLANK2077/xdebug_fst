// register_all.cpp — Register all action handlers
// BSD-3-Clause License
#include "engine/action_registry.h"

namespace xdebug_fst {

// Waveform actions
std::unique_ptr<EngineActionHandler> make_scope_list_handler();
std::unique_ptr<EngineActionHandler> make_scope_roots_handler();
std::unique_ptr<EngineActionHandler> make_value_at_handler();
std::unique_ptr<EngineActionHandler> make_signal_changes_handler();

// Design actions
std::unique_ptr<EngineActionHandler> make_signal_resolve_handler();
std::unique_ptr<EngineActionHandler> make_trace_driver_handler();
std::unique_ptr<EngineActionHandler> make_trace_load_handler();

// Group registration functions (parallel-implemented action groups)
void register_signal_analysis_actions(ActionRegistry& r);
void register_clock_counter_actions(ActionRegistry& r);
void register_list_event_cursor_actions(ActionRegistry& r);
void register_protocol_actions(ActionRegistry& r);
void register_stream_actions(ActionRegistry& r);
void register_combined_actions(ActionRegistry& r);

void register_all_actions(ActionRegistry& r) {
    // Waveform
    r.add(make_scope_list_handler());
    r.add(make_scope_roots_handler());
    r.add(make_value_at_handler());
    r.add(make_signal_changes_handler());

    // Design
    r.add(make_signal_resolve_handler());
    r.add(make_trace_driver_handler());
    r.add(make_trace_load_handler());

    // Group registrations (order matters for the action list output)
    register_signal_analysis_actions(r);
    register_clock_counter_actions(r);
    register_list_event_cursor_actions(r);
    register_protocol_actions(r);
    register_stream_actions(r);
    register_combined_actions(r);
}

} // namespace xdebug_fst
