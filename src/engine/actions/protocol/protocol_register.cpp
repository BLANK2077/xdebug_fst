// protocol_register.cpp — Register all protocol action handlers (BSD-3-Clause)
#include "engine/action_registry.h"

namespace xdebug_fst {

// APB actions
std::unique_ptr<EngineActionHandler> make_apb_config_list_handler();
std::unique_ptr<EngineActionHandler> make_apb_config_load_handler();
std::unique_ptr<EngineActionHandler> make_apb_query_handler();
std::unique_ptr<EngineActionHandler> make_apb_statistics_handler();
std::unique_ptr<EngineActionHandler> make_apb_transaction_cursor_handler();
std::unique_ptr<EngineActionHandler> make_apb_transfer_window_handler();

// AXI actions
std::unique_ptr<EngineActionHandler> make_axi_config_list_handler();
std::unique_ptr<EngineActionHandler> make_axi_config_load_handler();
std::unique_ptr<EngineActionHandler> make_axi_query_handler();
std::unique_ptr<EngineActionHandler> make_axi_analysis_handler();
std::unique_ptr<EngineActionHandler> make_axi_export_handler();
std::unique_ptr<EngineActionHandler> make_axi_statistics_handler();
std::unique_ptr<EngineActionHandler> make_axi_transaction_cursor_handler();
std::unique_ptr<EngineActionHandler> make_axi_channel_stall_handler();
std::unique_ptr<EngineActionHandler> make_axi_latency_outlier_handler();
std::unique_ptr<EngineActionHandler> make_axi_outstanding_timeline_handler();
std::unique_ptr<EngineActionHandler> make_axi_request_response_pair_handler();

void register_protocol_actions(ActionRegistry& r) {
    // APB
    r.add(make_apb_config_list_handler());
    r.add(make_apb_config_load_handler());
    r.add(make_apb_query_handler());
    r.add(make_apb_statistics_handler());
    r.add(make_apb_transaction_cursor_handler());
    r.add(make_apb_transfer_window_handler());

    // AXI
    r.add(make_axi_config_list_handler());
    r.add(make_axi_config_load_handler());
    r.add(make_axi_query_handler());
    r.add(make_axi_analysis_handler());
    r.add(make_axi_export_handler());
    r.add(make_axi_statistics_handler());
    r.add(make_axi_transaction_cursor_handler());
    r.add(make_axi_channel_stall_handler());
    r.add(make_axi_latency_outlier_handler());
    r.add(make_axi_outstanding_timeline_handler());
    r.add(make_axi_request_response_pair_handler());
}

} // namespace xdebug_fst
