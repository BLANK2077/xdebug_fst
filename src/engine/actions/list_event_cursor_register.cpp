// list_event_cursor_register.cpp — Register list, event, cursor, and nwave.rc.generate handlers
#include "engine/action_registry.h"

namespace xdebug_fst {

// Forward declarations
std::unique_ptr<EngineActionHandler> make_list_create_handler();
std::unique_ptr<EngineActionHandler> make_list_add_handler();
std::unique_ptr<EngineActionHandler> make_list_delete_handler();
std::unique_ptr<EngineActionHandler> make_list_load_handler();
std::unique_ptr<EngineActionHandler> make_list_show_handler();
std::unique_ptr<EngineActionHandler> make_list_validate_handler();
std::unique_ptr<EngineActionHandler> make_list_export_handler();
std::unique_ptr<EngineActionHandler> make_list_first_change_handler();
std::unique_ptr<EngineActionHandler> make_event_config_list_handler();
std::unique_ptr<EngineActionHandler> make_event_config_load_handler();
std::unique_ptr<EngineActionHandler> make_event_find_handler();
std::unique_ptr<EngineActionHandler> make_event_export_handler();
std::unique_ptr<EngineActionHandler> make_cursor_set_handler();
std::unique_ptr<EngineActionHandler> make_cursor_get_handler();
std::unique_ptr<EngineActionHandler> make_cursor_list_handler();
std::unique_ptr<EngineActionHandler> make_cursor_delete_handler();
std::unique_ptr<EngineActionHandler> make_cursor_use_handler();
std::unique_ptr<EngineActionHandler> make_nwave_rc_generate_handler();

void register_list_event_cursor_actions(ActionRegistry& r) {
    // List actions
    r.add(make_list_create_handler());
    r.add(make_list_add_handler());
    r.add(make_list_delete_handler());
    r.add(make_list_load_handler());
    r.add(make_list_show_handler());
    r.add(make_list_validate_handler());
    r.add(make_list_export_handler());
    r.add(make_list_first_change_handler());

    // Event actions
    r.add(make_event_config_list_handler());
    r.add(make_event_config_load_handler());
    r.add(make_event_find_handler());
    r.add(make_event_export_handler());

    // Cursor actions
    r.add(make_cursor_set_handler());
    r.add(make_cursor_get_handler());
    r.add(make_cursor_list_handler());
    r.add(make_cursor_delete_handler());
    r.add(make_cursor_use_handler());

    // nwave.rc.generate
    r.add(make_nwave_rc_generate_handler());
}

} // namespace xdebug_fst
