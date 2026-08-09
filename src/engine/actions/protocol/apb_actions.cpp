// apb_actions.cpp — APB protocol analysis handlers (BSD-3-Clause)
// Implements: apb.config.list, apb.config.load, apb.query,
//   apb.statistics, apb.transaction.cursor, apb.transfer_window
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "api/json_types.h"
#include "engine/actions/value_source_entries.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {

// ═══════════════════════════════════════════════════════════════════
// APB config helpers
// ═══════════════════════════════════════════════════════════════════

struct ApbSignalMap {
    std::string pclk;
    std::string psel;
    std::string penable;
    std::string pwrite;
    std::string paddr;
    std::string pwdata;
    std::string prdata;
    std::string pready;
    std::string pslverr;
};

static ApbSignalMap default_apb_signals() {
    return {
        "TOP.pclk", "TOP.psel", "TOP.penable", "TOP.pwrite",
        "TOP.paddr", "TOP.pwdata", "TOP.prdata", "TOP.pready", "TOP.pslverr"
    };
}

bool apb_value_source_entries(const std::string& name,
                              std::vector<ValueSourceEntry>& out) {
    if (name != "default") return false;
    const ApbSignalMap signals = default_apb_signals();
    out = {{"pclk", signals.pclk}, {"psel", signals.psel},
           {"penable", signals.penable}, {"pwrite", signals.pwrite},
           {"paddr", signals.paddr}, {"pwdata", signals.pwdata},
           {"prdata", signals.prdata}, {"pready", signals.pready},
           {"pslverr", signals.pslverr}};
    return true;
}

static Json apb_signal_map_json(const ApbSignalMap& m) {
    return Json{
        {"pclk", m.pclk}, {"psel", m.psel}, {"penable", m.penable},
        {"pwrite", m.pwrite}, {"paddr", m.paddr}, {"pwdata", m.pwdata},
        {"prdata", m.prdata}, {"pready", m.pready}, {"pslverr", m.pslverr}
    };
}

static ApbSignalMap parse_apb_signal_map(const Json& j, const ApbSignalMap& fallback) {
    ApbSignalMap m = fallback;
    if (j.is_object()) {
        if (j.contains("pclk")) m.pclk = j["pclk"].get<std::string>();
        if (j.contains("psel")) m.psel = j["psel"].get<std::string>();
        if (j.contains("penable")) m.penable = j["penable"].get<std::string>();
        if (j.contains("pwrite")) m.pwrite = j["pwrite"].get<std::string>();
        if (j.contains("paddr")) m.paddr = j["paddr"].get<std::string>();
        if (j.contains("pwdata")) m.pwdata = j["pwdata"].get<std::string>();
        if (j.contains("prdata")) m.prdata = j["prdata"].get<std::string>();
        if (j.contains("pready")) m.pready = j["pready"].get<std::string>();
        if (j.contains("pslverr")) m.pslverr = j["pslverr"].get<std::string>();
    }
    return m;
}

// Resolve APB config from args. Returns the signal map and sets error if any
// required signal is not found. On error, out_err is set.
static bool resolve_apb_config(const Json& args, ApbSignalMap& sm, Json& out_err) {
    sm = default_apb_signals();
    if (args.contains("config")) {
        if (args["config"].is_string()) {
            std::string name = args["config"].get<std::string>();
            if (name == "default") {
                // use defaults
            } else {
                out_err = Json{{"ok", false},
                    {"error", {{"code", "CONFIG_NOT_FOUND"},
                               {"message", "APB config not found: " + name}}}};
                return false;
            }
        } else if (args["config"].is_object()) {
            sm = parse_apb_signal_map(args["config"], sm);
        }
    }
    return true;
}

struct ApbTransfer {
    size_t index = 0;
    uint64_t start_time = 0;
    uint64_t end_time = 0;
    uint32_t start_time_idx = 0;
    uint32_t end_time_idx = 0;
    bool is_write = false;
    std::string address_bits;
    std::string write_data_bits;
    std::string read_data_bits;
    bool slave_error = false;
};

// Scan APB transfers from the waveform using merged change points of PSEL/PENABLE.
static std::vector<ApbTransfer> scan_apb_transfers(
    IWaveformBackend& wf,
    const ApbSignalMap& sm,
    uint64_t t_begin, uint64_t t_end,
    int max_transfers,
    Json& out_err)
{
    std::vector<ApbTransfer> transfers;

    // Find signals
    uint32_t ref_psel    = wf.find_signal(sm.psel);
    uint32_t ref_penable = wf.find_signal(sm.penable);
    uint32_t ref_pwrite  = wf.find_signal(sm.pwrite);
    uint32_t ref_paddr   = wf.find_signal(sm.paddr);
    uint32_t ref_pwdata  = wf.find_signal(sm.pwdata);
    uint32_t ref_prdata  = wf.find_signal(sm.prdata);
    uint32_t ref_pslverr = wf.find_signal(sm.pslverr);

    // Validate required signals
    auto check_sig = [&](uint32_t ref, const std::string& name, const std::string& role) -> bool {
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            out_err = Json{{"ok", false},
                {"error", {{"code", "CONFIG_SIGNAL_NOT_FOUND"},
                           {"message", "APB " + role + " signal not found: " + name}}}};
            return false;
        }
        return true;
    };
    if (!check_sig(ref_psel, sm.psel, "psel")) return transfers;
    if (!check_sig(ref_penable, sm.penable, "penable")) return transfers;
    if (!check_sig(ref_pwrite, sm.pwrite, "pwrite")) return transfers;
    if (!check_sig(ref_paddr, sm.paddr, "paddr")) return transfers;
    if (!check_sig(ref_pwdata, sm.pwdata, "pwdata")) return transfers;
    if (!check_sig(ref_prdata, sm.prdata, "prdata")) return transfers;
    // pslverr is optional — default to 0 if not found

    // Load signals
    wf.load_signals({ref_psel, ref_penable, ref_pwrite, ref_paddr,
                     ref_pwdata, ref_prdata});
    if (ref_pslverr) wf.load_signals({ref_pslverr});

    // Collect merged time index list from psel and penable changes
    std::set<uint32_t> ti_set;
    for (uint32_t ti : wf.time_indices_of(ref_psel)) {
        uint64_t t = wf.time_at(ti);
        if (t >= t_begin && t <= t_end) ti_set.insert(ti);
    }
    for (uint32_t ti : wf.time_indices_of(ref_penable)) {
        uint64_t t = wf.time_at(ti);
        if (t >= t_begin && t <= t_end) ti_set.insert(ti);
    }
    // Also add the begin time index
    uint32_t ti_begin = wf.time_idx_of(t_begin);
    uint32_t ti_end = wf.time_idx_of(t_end);
    if (ti_begin > 0) ti_set.insert(ti_begin);
    ti_set.insert(ti_end);

    std::vector<uint32_t> time_indices(ti_set.begin(), ti_set.end());

    // Helper: get bit value at time index
    auto bit_at = [&](uint32_t ref, uint32_t ti) -> std::string {
        IWaveformBackend::SignalOffset off;
        if (!wf.signal_offset_at(ref, ti, off)) return "0";
        return wf.signal_value_str(ref, off.start, 0);
    };

    // Scan for transfers: PSEL=1 AND PENABLE=1
    bool in_transfer = false;
    ApbTransfer cur;
    for (size_t i = 0; i < time_indices.size(); i++) {
        uint32_t ti = time_indices[i];
        std::string psel_val = bit_at(ref_psel, ti);
        std::string penable_val = bit_at(ref_penable, ti);
        bool psel_high = (!psel_val.empty() && psel_val.back() == '1');
        bool penable_high = (!penable_val.empty() && penable_val.back() == '1');

        if (psel_high && penable_high) {
            if (!in_transfer) {
                // Start of transfer
                in_transfer = true;
                cur = ApbTransfer{};
                cur.index = transfers.size();
                cur.start_time = wf.time_at(ti);
                cur.start_time_idx = ti;

                // Capture address and direction
                IWaveformBackend::SignalOffset off;
                if (wf.signal_offset_at(ref_pwrite, ti, off))
                    cur.is_write = (wf.signal_value_str(ref_pwrite, off.start, 0).back() == '1');
                if (wf.signal_offset_at(ref_paddr, ti, off))
                    cur.address_bits = wf.signal_value_str(ref_paddr, off.start, 0);
                if (cur.is_write) {
                    if (wf.signal_offset_at(ref_pwdata, ti, off))
                        cur.write_data_bits = wf.signal_value_str(ref_pwdata, off.start, 0);
                }
                if (ref_pslverr) {
                    if (wf.signal_offset_at(ref_pslverr, ti, off))
                        cur.slave_error = (wf.signal_value_str(ref_pslverr, off.start, 0).back() == '1');
                }
            }
        } else if (in_transfer) {
            // End of transfer at previous time index
            uint32_t prev_ti = (i > 0) ? time_indices[i - 1] : ti;
            cur.end_time = wf.time_at(prev_ti);
            cur.end_time_idx = prev_ti;
            // For reads, capture read data at end of transfer
            if (!cur.is_write) {
                IWaveformBackend::SignalOffset off;
                if (wf.signal_offset_at(ref_prdata, prev_ti, off))
                    cur.read_data_bits = wf.signal_value_str(ref_prdata, off.start, 0);
            }
            transfers.push_back(cur);
            in_transfer = false;
            if (max_transfers > 0 && (int)transfers.size() >= max_transfers) break;
        }
    }
    // If still in transfer at end of range, close it
    if (in_transfer) {
        cur.end_time = t_end;
        cur.end_time_idx = ti_end;
        if (!cur.is_write) {
            IWaveformBackend::SignalOffset off;
            if (wf.signal_offset_at(ref_prdata, ti_end, off))
                cur.read_data_bits = wf.signal_value_str(ref_prdata, off.start, 0);
        }
        transfers.push_back(cur);
    }

    return transfers;
}

static Json apb_transfer_json(const ApbTransfer& txn) {
    Json j;
    j["index"] = txn.index;
    j["start_time"] = txn.start_time;
    j["end_time"] = txn.end_time;
    j["start_time_idx"] = txn.start_time_idx;
    j["end_time_idx"] = txn.end_time_idx;
    j["direction"] = txn.is_write ? "write" : "read";

    // Render address as hex
    LogicValue addr_lv = logic_value_from_bits(txn.address_bits,
        static_cast<int>(txn.address_bits.size()));
    j["address"] = logic_value_json(addr_lv, ValueRenderFormat::Hex);

    if (txn.is_write) {
        LogicValue wd_lv = logic_value_from_bits(txn.write_data_bits,
            static_cast<int>(txn.write_data_bits.size()));
        j["write_data"] = logic_value_json(wd_lv, ValueRenderFormat::Hex);
    } else {
        LogicValue rd_lv = logic_value_from_bits(txn.read_data_bits,
            static_cast<int>(txn.read_data_bits.size()));
        j["read_data"] = logic_value_json(rd_lv, ValueRenderFormat::Hex);
    }

    j["status"] = txn.slave_error ? "slave_error" : "ok";
    return j;
}

static Json make_error(const std::string& code, const std::string& message) {
    return Json{{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}

static Json require_waveform(const char* action_name) {
    auto& g = engine_globals();
    if (!g.has_waveform || !g.waveform) {
        return Json{{"ok", false},
            {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                       {"message", std::string("action requires waveform file: ") + action_name}}}};
    }
    return Json{}; // empty = OK
}

// ═══════════════════════════════════════════════════════════════════
// 1. apb.config.list
// ═══════════════════════════════════════════════════════════════════
struct ApbConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        Json configs = Json::array();
        configs.push_back({
            {"name", "default"},
            {"signal_map", apb_signal_map_json(default_apb_signals())}
        });

        return Json{
            {"ok", true},
            {"data", {{"configs", configs}}}
        };
    }
};

// ═══════════════════════════════════════════════════════════════════
// 2. apb.config.load
// ═══════════════════════════════════════════════════════════════════
struct ApbConfigLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "default");

        if (name == "default") {
            return Json{
                {"ok", true},
                {"data", {
                    {"name", "default"},
                    {"signal_map", apb_signal_map_json(default_apb_signals())}
                }}
            };
        }
        return make_error("CONFIG_NOT_FOUND", "APB config not found: " + name);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 3. apb.query
// ═══════════════════════════════════════════════════════════════════
struct ApbQueryHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.query"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        ApbSignalMap sm;
        Json cfg_err;
        if (!resolve_apb_config(args, sm, cfg_err)) return cfg_err;

        uint64_t t_begin = 0;
        uint64_t t_end = wf->max_time();
        if (args.contains("begin")) {
            auto& b = args["begin"];
            t_begin = b.is_number() ? b.get<uint64_t>() : std::stoull(b.get<std::string>());
        }
        if (args.contains("end")) {
            auto& e = args["end"];
            t_end = e.is_number() ? e.get<uint64_t>() : std::stoull(e.get<std::string>());
        }
        int max_transfers = args.value("max_transfers", -1);

        Json scan_err;
        auto transfers = scan_apb_transfers(*wf, sm, t_begin, t_end, max_transfers, scan_err);
        if (!scan_err.is_null()) return scan_err;

        Json tjson = Json::array();
        for (auto& txn : transfers) {
            tjson.push_back(apb_transfer_json(txn));
        }

        return Json{
            {"ok", true},
            {"summary", {{"transfer_count", transfers.size()}}},
            {"data", {{"transfers", tjson}}}
        };
    }
};

// ═══════════════════════════════════════════════════════════════════
// 4. apb.statistics
// ═══════════════════════════════════════════════════════════════════
struct ApbStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        ApbSignalMap sm;
        Json cfg_err;
        if (!resolve_apb_config(args, sm, cfg_err)) return cfg_err;

        uint64_t t_begin = 0;
        uint64_t t_end = wf->max_time();
        if (args.contains("begin")) {
            auto& b = args["begin"];
            t_begin = b.is_number() ? b.get<uint64_t>() : std::stoull(b.get<std::string>());
        }
        if (args.contains("end")) {
            auto& e = args["end"];
            t_end = e.is_number() ? e.get<uint64_t>() : std::stoull(e.get<std::string>());
        }

        Json scan_err;
        auto transfers = scan_apb_transfers(*wf, sm, t_begin, t_end, -1, scan_err);
        if (!scan_err.is_null()) return scan_err;

        size_t read_count = 0, write_count = 0, error_count = 0;
        double sum_latency = 0;
        uint64_t min_latency = UINT64_MAX, max_latency = 0;

        for (auto& txn : transfers) {
            if (txn.is_write) write_count++; else read_count++;
            if (txn.slave_error) error_count++;
            uint64_t lat = (txn.end_time > txn.start_time) ? (txn.end_time - txn.start_time) : 0;
            sum_latency += lat;
            if (lat < min_latency) min_latency = lat;
            if (lat > max_latency) max_latency = lat;
        }

        double avg_latency = transfers.empty() ? 0 : sum_latency / transfers.size();
        if (transfers.empty()) { min_latency = 0; }

        return Json{
            {"ok", true},
            {"summary", {
                {"transfer_count", transfers.size()},
                {"read_count", read_count},
                {"write_count", write_count},
                {"avg_latency", avg_latency},
                {"min_latency", min_latency},
                {"max_latency", max_latency},
                {"error_count", error_count}
            }}
        };
    }
};

// ═══════════════════════════════════════════════════════════════════
// 5. apb.transaction.cursor
// ═══════════════════════════════════════════════════════════════════
struct ApbTransactionCursorHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.transaction.cursor"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        ApbSignalMap sm;
        Json cfg_err;
        if (!resolve_apb_config(args, sm, cfg_err)) return cfg_err;

        uint64_t t_begin = 0;
        uint64_t t_end = wf->max_time();
        if (args.contains("begin")) {
            auto& b = args["begin"];
            t_begin = b.is_number() ? b.get<uint64_t>() : std::stoull(b.get<std::string>());
        }
        if (args.contains("end")) {
            auto& e = args["end"];
            t_end = e.is_number() ? e.get<uint64_t>() : std::stoull(e.get<std::string>());
        }

        size_t cursor = 0;
        if (args.contains("cursor")) {
            auto& c = args["cursor"];
            cursor = c.is_number() ? c.get<size_t>() : static_cast<size_t>(std::stoull(c.get<std::string>()));
        }

        Json scan_err;
        auto transfers = scan_apb_transfers(*wf, sm, t_begin, t_end, -1, scan_err);
        if (!scan_err.is_null()) return scan_err;

        const size_t page_size = 50;
        Json tjson = Json::array();
        size_t i = cursor;
        while (i < transfers.size() && tjson.size() < page_size) {
            tjson.push_back(apb_transfer_json(transfers[i]));
            i++;
        }
        bool has_more = (i < transfers.size());
        Json next_cursor = has_more ? Json(static_cast<uint64_t>(i)) : Json(nullptr);

        return Json{
            {"ok", true},
            {"summary", {{"transfer_count", transfers.size()}}},
            {"data", {
                {"transactions", tjson},
                {"next_cursor", next_cursor},
                {"has_more", has_more}
            }}
        };
    }
};

// ═══════════════════════════════════════════════════════════════════
// 6. apb.transfer_window
// ═══════════════════════════════════════════════════════════════════
struct ApbTransferWindowHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.transfer_window"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        ApbSignalMap sm;
        Json cfg_err;
        if (!resolve_apb_config(args, sm, cfg_err)) return cfg_err;

        uint64_t t_begin = 0;
        uint64_t t_end = wf->max_time();
        if (args.contains("begin")) {
            auto& b = args["begin"];
            t_begin = b.is_number() ? b.get<uint64_t>() : std::stoull(b.get<std::string>());
        }
        if (args.contains("end")) {
            auto& e = args["end"];
            t_end = e.is_number() ? e.get<uint64_t>() : std::stoull(e.get<std::string>());
        }
        int max_transfers = -1;
        if (args.contains("window")) {
            auto& w = args["window"];
            max_transfers = w.is_number() ? w.get<int>() : std::stoi(w.get<std::string>());
        }

        Json scan_err;
        auto transfers = scan_apb_transfers(*wf, sm, t_begin, t_end, max_transfers, scan_err);
        if (!scan_err.is_null()) return scan_err;

        Json tjson = Json::array();
        for (auto& txn : transfers) {
            tjson.push_back(apb_transfer_json(txn));
        }

        return Json{
            {"ok", true},
            {"summary", {{"transfer_count", transfers.size()}}},
            {"data", {{"transfers", tjson}}}
        };
    }
};

// ═══════════════════════════════════════════════════════════════════
// Factory functions
// ═══════════════════════════════════════════════════════════════════
std::unique_ptr<EngineActionHandler> make_apb_config_list_handler() {
    return std::make_unique<ApbConfigListHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_config_load_handler() {
    return std::make_unique<ApbConfigLoadHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_query_handler() {
    return std::make_unique<ApbQueryHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_statistics_handler() {
    return std::make_unique<ApbStatisticsHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_transaction_cursor_handler() {
    return std::make_unique<ApbTransactionCursorHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_transfer_window_handler() {
    return std::make_unique<ApbTransferWindowHandler>();
}

} // namespace xdebug_fst
