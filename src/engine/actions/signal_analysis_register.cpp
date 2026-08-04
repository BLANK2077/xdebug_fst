// signal_analysis_register.cpp — Register waveform signal analysis handlers
// BSD-3-Clause License
#include "engine/action_registry.h"

namespace xdebug_fst {

// Forward declarations
std::unique_ptr<EngineActionHandler> make_signal_statistics_handler();
std::unique_ptr<EngineActionHandler> make_signal_stability_handler();
std::unique_ptr<EngineActionHandler> make_signal_xz_verify_handler();
std::unique_ptr<EngineActionHandler> make_signal_anomaly_inspect_handler();

void register_signal_analysis_actions(ActionRegistry& r) {
    r.add(make_signal_statistics_handler());
    r.add(make_signal_stability_handler());
    r.add(make_signal_xz_verify_handler());
    r.add(make_signal_anomaly_inspect_handler());
}

}  // namespace xdebug_fst
