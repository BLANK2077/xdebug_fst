// signal_resolve.cpp — signal.resolve, trace.driver, trace.load (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"

namespace xdebug_fst {

struct SignalResolveHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.resolve"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design||!g.design) return Json{{"ok",false},{"error",{{"code","DESIGN_NOT_LOADED"}}}};
        std::string sig = req.value("args",Json::object()).value("signal","");
        if (sig.empty()) return Json{{"ok",false},{"error",{{"code","MISSING_FIELD"}}}};
        int idx = g.design->resolve(sig.c_str());
        if (idx<0) return Json{{"ok",false},{"error",{{"code","SIGNAL_NOT_FOUND"}}}};
        return Json{{"ok",true},{"data",{{"signal",sig},{"index",idx},{"name",g.design->signal_name(idx)?g.design->signal_name(idx):""},{"type",g.design->signal_type(idx)?g.design->signal_type(idx):""},{"width",g.design->signal_width(idx)}}}};
    }
};

struct TraceDriverHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.driver"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design||!g.design) return Json{{"ok",false},{"error",{{"code","DESIGN_NOT_LOADED"}}}};
        std::string sig = req.value("args",Json::object()).value("signal","");
        int idx = g.design->resolve(sig.c_str());
        if (idx<0) return Json{{"ok",false},{"error",{{"code","SIGNAL_NOT_FOUND"}}}};
        std::vector<IDesignBackend::DriverRecord> drivers;
        g.design->trace_driver(idx, drivers);
        Json arr = Json::array();
        for (auto& d:drivers) arr.push_back({{"src_signal",d.src_signal>=0?g.design->signal_name(d.src_signal):nullptr},{"kind",d.kind},{"file",d.file},{"line",d.line}});
        return Json{{"ok",true},{"summary",{{"signal",sig},{"driver_count",drivers.size()}}},{"data",{{"drivers",arr}}}};
    }
};

struct TraceLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.load"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design||!g.design) return Json{{"ok",false},{"error",{{"code","DESIGN_NOT_LOADED"}}}};
        std::string sig = req.value("args",Json::object()).value("signal","");
        int idx = g.design->resolve(sig.c_str());
        if (idx<0) return Json{{"ok",false},{"error",{{"code","SIGNAL_NOT_FOUND"}}}};
        std::vector<IDesignBackend::LoadRecord> loads;
        g.design->trace_load(idx, loads);
        Json arr = Json::array();
        for (auto& l:loads) arr.push_back({{"consumer",l.consumer>=0?g.design->signal_name(l.consumer):nullptr},{"kind",l.kind},{"file",l.file},{"line",l.line}});
        return Json{{"ok",true},{"summary",{{"signal",sig},{"load_count",loads.size()}}},{"data",{{"loads",arr}}}};
    }
};

std::unique_ptr<EngineActionHandler> make_signal_resolve_handler() { return std::make_unique<SignalResolveHandler>(); }
std::unique_ptr<EngineActionHandler> make_trace_driver_handler() { return std::make_unique<TraceDriverHandler>(); }
std::unique_ptr<EngineActionHandler> make_trace_load_handler() { return std::make_unique<TraceLoadHandler>(); }

} // namespace xdebug_fst
