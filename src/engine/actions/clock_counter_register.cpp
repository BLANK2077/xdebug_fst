// clock_counter_register.cpp — 注册 waveform 时钟/计数器组所有 handler
// BSD-3-Clause License
#include "engine/action_registry.h"

namespace xdebug_fst {

// 前向声明
std::unique_ptr<EngineActionHandler> make_clock_point_query_handler();
std::unique_ptr<EngineActionHandler> make_expr_eval_at_handler();
std::unique_ptr<EngineActionHandler> make_signal_sampled_pulse_inspect_handler();
std::unique_ptr<EngineActionHandler> make_protocol_handshake_inspect_handler();
std::unique_ptr<EngineActionHandler> make_counter_statistics_handler();

void register_clock_counter_actions(ActionRegistry& r) {
    r.add(make_clock_point_query_handler());
    r.add(make_expr_eval_at_handler());
    r.add(make_signal_sampled_pulse_inspect_handler());
    r.add(make_protocol_handshake_inspect_handler());
    r.add(make_counter_statistics_handler());
}

} // namespace xdebug_fst
