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
}

} // namespace xdebug_fst
