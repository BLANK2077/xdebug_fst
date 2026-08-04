// stream_register.cpp — Register all stream action handlers (BSD-3-Clause)
#include "engine/action_registry.h"

namespace xdebug_fst {

// Forward declarations
std::unique_ptr<EngineActionHandler> make_stream_config_list_handler();
std::unique_ptr<EngineActionHandler> make_stream_config_get_handler();
std::unique_ptr<EngineActionHandler> make_stream_config_load_handler();
std::unique_ptr<EngineActionHandler> make_stream_describe_handler();
std::unique_ptr<EngineActionHandler> make_stream_query_handler();
std::unique_ptr<EngineActionHandler> make_stream_export_handler();
std::unique_ptr<EngineActionHandler> make_stream_validate_handler();

void register_stream_actions(ActionRegistry& r) {
    r.add(make_stream_config_list_handler());
    r.add(make_stream_config_get_handler());
    r.add(make_stream_config_load_handler());
    r.add(make_stream_describe_handler());
    r.add(make_stream_query_handler());
    r.add(make_stream_export_handler());
    r.add(make_stream_validate_handler());
}

} // namespace xdebug_fst
