// axi_actions.cpp — AXI protocol analysis handlers (BSD-3-Clause)
// Implements: axi.config.list, axi.config.load, axi.query,
//   axi.analysis, axi.export, axi.statistics, axi.transaction.cursor,
//   axi.channel_stall, axi.latency_outlier, axi.outstanding_timeline,
//   axi.request_response_pair
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "api/json_types.h"
#include "engine/actions/value_source_entries.h"
#include "waveform/clock_sampling.h"
#include "protocol/text_response_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace xdebug_fst {

static Json make_error(const std::string& code, const std::string& message);

// ═══════════════════════════════════════════════════════════════════
// AXI config helpers
// ═══════════════════════════════════════════════════════════════════

struct AxiSignalMap {
    std::string name, aclk, aresetn, reset_polarity;
    std::string edge = "negedge", sample_point;
    // Write Address
    std::string awid, awaddr, awlen, awsize, awburst, awvalid, awready;
    // Write Data
    std::string wdata, wstrb, wlast, wvalid, wready;
    // Write Response
    std::string bid, bresp, bvalid, bready;
    // Read Address
    std::string arid, araddr, arlen, arsize, arburst, arvalid, arready;
    // Read Data
    std::string rid, rdata, rresp, rlast, rvalid, rready;
};

static std::map<std::string, AxiSignalMap>& axi_configs() {
    static std::map<std::string, AxiSignalMap> configs;
    return configs;
}

static std::map<std::string, size_t>& axi_cursors() {
    static std::map<std::string, size_t> cursors;
    return cursors;
}

bool axi_value_source_entries(const std::string& name,
                              std::vector<ValueSourceEntry>& out) {
    auto found = axi_configs().find(name);
    if (found == axi_configs().end()) return false;
    const AxiSignalMap& s = found->second;
    out = {{"clock", s.aclk}, {"reset", s.aresetn},
           {"awid", s.awid}, {"awaddr", s.awaddr}, {"awlen", s.awlen},
           {"awsize", s.awsize}, {"awburst", s.awburst},
           {"awvalid", s.awvalid}, {"awready", s.awready},
           {"wdata", s.wdata}, {"wstrb", s.wstrb}, {"wlast", s.wlast},
           {"wvalid", s.wvalid}, {"wready", s.wready},
           {"bid", s.bid}, {"bresp", s.bresp},
           {"bvalid", s.bvalid}, {"bready", s.bready},
           {"arid", s.arid}, {"araddr", s.araddr}, {"arlen", s.arlen},
           {"arsize", s.arsize}, {"arburst", s.arburst},
           {"arvalid", s.arvalid}, {"arready", s.arready},
           {"rid", s.rid}, {"rdata", s.rdata}, {"rresp", s.rresp},
           {"rlast", s.rlast},
           {"rvalid", s.rvalid}, {"rready", s.rready}};
    return true;
}

static Json axi_signal_map_json(const AxiSignalMap& m) {
    Json result{{"name", m.name}, {"sampling_mode", "clock_edge"},
        {"clock", m.aclk}, {"reset", {{"signal", m.aresetn},
        {"polarity", m.reset_polarity}}}, {"edge", m.edge},
        {"channels", {
            {"aw", {{"addr", m.awaddr}, {"id", m.awid}, {"len", m.awlen},
                {"size", m.awsize}, {"burst", m.awburst},
                {"valid", m.awvalid}, {"ready", m.awready}}},
            {"w", {{"data", m.wdata}, {"strb", m.wstrb}, {"last", m.wlast},
                {"valid", m.wvalid}, {"ready", m.wready}}},
            {"b", {{"id", m.bid}, {"resp", m.bresp},
                {"valid", m.bvalid}, {"ready", m.bready}}},
            {"ar", {{"addr", m.araddr}, {"id", m.arid}, {"len", m.arlen},
                {"size", m.arsize}, {"burst", m.arburst},
                {"valid", m.arvalid}, {"ready", m.arready}}},
            {"r", {{"id", m.rid}, {"data", m.rdata}, {"resp", m.rresp},
                {"last", m.rlast}, {"valid", m.rvalid}, {"ready", m.rready}}}
        }}};
    if (!m.sample_point.empty()) result["sample_point"] = m.sample_point;
    return result;
}

static bool parse_axi_signal_map(const std::string& name, const Json& j,
                                AxiSignalMap& m, std::string& message) {
    m = {};
    m.name = name;
    m.aclk = j.at("clock");
    m.aresetn = j.at("reset").at("signal");
    m.reset_polarity = j.at("reset").at("polarity");
    m.edge = j.value("edge", "negedge");
    m.sample_point = j.value("sample_point", "");
    auto set = [&](const char* key, std::string& field) { field = j.at(key); };
    set("awid", m.awid); set("awaddr", m.awaddr); set("awlen", m.awlen);
    set("awsize", m.awsize); set("awburst", m.awburst);
    set("awvalid", m.awvalid); set("awready", m.awready);
    set("wdata", m.wdata); set("wstrb", m.wstrb); set("wlast", m.wlast);
    set("wvalid", m.wvalid); set("wready", m.wready);
    set("bid", m.bid); set("bresp", m.bresp);
    set("bvalid", m.bvalid); set("bready", m.bready);
    set("arid", m.arid); set("araddr", m.araddr); set("arlen", m.arlen);
    set("arsize", m.arsize); set("arburst", m.arburst);
    set("arvalid", m.arvalid); set("arready", m.arready);
    set("rid", m.rid); set("rdata", m.rdata); set("rresp", m.rresp);
    set("rlast", m.rlast); set("rvalid", m.rvalid); set("rready", m.rready);
    if (m.edge == "negedge" && !m.sample_point.empty()) {
        message = "negedge AXI config must omit sample_point";
        return false;
    }
    if (m.edge != "negedge" && m.sample_point.empty()) m.sample_point = "before";
    return true;
}

static bool resolve_axi_config(const Json& args, AxiSignalMap& sm, Json& out_err,
                               const std::string& action) {
    const std::string name = args.at("name");
    auto found = axi_configs().find(name);
    if (found == axi_configs().end()) {
        Json example_args{{"name","axi0"}};
        if (action == "axi.query") {
            example_args["direction"] = "write";
            example_args["query"] = {{"line_limit",8}};
        }
        out_err = Json{{"ok",false},{"error",{
            {"code","CONFIG_NOT_FOUND"},{"message","axi config not found: " + name},
            {"recoverable",true},{"error_layer","handler"},
            {"invalid_arg","args.name"},
            {"expected","name of a previously loaded axi config"},
            {"missing_name",name},{"missing_resource","axi config"},
            {"correct_example",{{"api_version","xdebug.v1"},{"action",action},
                {"target",{{"session_id","case_a"}}},{"args",example_args}}},
            {"example_note","Example only; choose an existing config name or load this config before using it."},
            {"next_actions",Json::array({
                "Call axi.config.list to inspect loaded configs.",
                "Call axi.config.load before this action."})}}}};
        return false;
    }
    sm = found->second;
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// AXI data structures
// ═══════════════════════════════════════════════════════════════════

struct AxiHandshakeEvent {
    std::string channel;  // "aw","w","b","ar","r"
    uint64_t time = 0;
    uint32_t time_idx = 0;
    uint64_t valid_begin_time = 0;
    std::string kind;     // "handshake"
    // Channel-specific data
    std::string id;
    std::string addr;
    std::string len;
    std::string size;
    std::string burst;
    std::string data;
    std::string strb;
    std::string resp;
    bool last = false;
};

struct AxiTransaction {
    size_t index = 0;
    bool is_write = false;
    std::string id;
    std::string address;
    std::string length;
    std::string size;
    std::string burst;
    uint64_t start_time = 0;      // AW/AR handshake time
    uint32_t start_time_idx = 0;
    uint64_t address_valid_begin_time = 0;
    uint64_t end_time = 0;        // B handshake or RLAST handshake time
    uint32_t end_time_idx = 0;
    bool complete = true;
    std::vector<std::string> data_beats;
    std::vector<AxiHandshakeEvent> data_events;
    std::string resp;
};

// ═══════════════════════════════════════════════════════════════════
// AXI scanner
// ═══════════════════════════════════════════════════════════════════

struct AxiScanResult {
    std::vector<AxiHandshakeEvent> aw_events;
    std::vector<AxiHandshakeEvent> w_events;
    std::vector<AxiHandshakeEvent> b_events;
    std::vector<AxiHandshakeEvent> ar_events;
    std::vector<AxiHandshakeEvent> r_events;
    std::vector<AxiTransaction> transactions;
    // Per-channel signal refs (cached for stall detection etc.)
    uint32_t ref_awvalid = 0, ref_awready = 0;
    uint32_t ref_wvalid = 0, ref_wready = 0;
    uint32_t ref_bvalid = 0, ref_bready = 0;
    uint32_t ref_arvalid = 0, ref_arready = 0;
    uint32_t ref_rvalid = 0, ref_rready = 0;
    bool complete = true;
};

// Helper: read a signal's bit string at a time index
static std::string read_signal_at(IWaveformBackend& wf, uint32_t ref, uint32_t ti) {
    if (ref == IWaveformBackend::kInvalidSignalRef) return "0";
    IWaveformBackend::SignalOffset off;
    if (!wf.signal_offset_at(ref, ti, off)) return "0";
    return wf.signal_value_str(ref, off.start, 0);
}

static IWaveformBackend::ObservationPoint axi_point(const AxiSignalMap& config) {
    if (config.edge == "negedge") return IWaveformBackend::ObservationPoint::Raw;
    return config.sample_point == "after" ? IWaveformBackend::ObservationPoint::After
                                           : IWaveformBackend::ObservationPoint::Before;
}

static std::string read_signal_at(IWaveformBackend& wf, uint32_t ref, uint32_t ti,
                                  IWaveformBackend::ObservationPoint point) {
    IWaveformBackend::SampledValue sampled;
    if (!ref || !wf.sampled_value_at(ref,ti,point,sampled) ||
        sampled.value.kind != IWaveformBackend::ValueKind::BitVector) return "x";
    return sampled.value.text;
}

static bool known_high(const std::string& bits) {
    if (bits.empty()) return false;
    bool one = false;
    for (char bit : bits) {
        if (bit == 'x' || bit == 'X' || bit == 'z' || bit == 'Z') return false;
        if (bit == '1') one = true;
    }
    return one;
}

static bool known_binary(const std::string& bits) {
    if (bits.empty()) return false;
    for (char bit : bits)
        if (bit != '0' && bit != '1') return false;
    return true;
}

static std::vector<uint32_t> selected_clock_edges(IWaveformBackend& wf,
    const AxiSignalMap& config, uint64_t begin, uint64_t end) {
    std::vector<uint32_t> selected;
    const uint32_t ref = wf.find_signal(config.aclk);
    if (!ref) return selected;
    if (!wf.is_loaded(ref)) wf.load_signals({ref});
    uint32_t previous = std::numeric_limits<uint32_t>::max();
    for (uint32_t ti : wf.time_indices_of(ref)) {
        if (ti == previous) continue;
        previous = ti;
        const uint64_t time = wf.time_at(ti);
        if (time < begin || time > end) continue;
        const std::string before = read_signal_at(wf,ref,ti,
            IWaveformBackend::ObservationPoint::Before);
        const std::string raw = read_signal_at(wf,ref,ti,
            IWaveformBackend::ObservationPoint::Raw);
        const bool rising = is_rising_edge(before,raw);
        const bool falling = is_falling_edge(before,raw);
        if ((config.edge == "dual" && (rising || falling)) ||
            (config.edge == "posedge" && rising) ||
            (config.edge == "negedge" && falling)) selected.push_back(ti);
    }
    return selected;
}

// Collect handshake events on a valid/ready pair within time range.
// When valid=1 AND ready=1, record an event at that time.
// Scan a channel for handshake events, sampled at clock edges.
// ref_clk: 0 (kInvalidSignalRef) disables clock-edge gating (fallback).
// multi_beat: true for W/R channels where every clock cycle with valid&&ready
// is a new data beat; false for AW/AR/B (single-beat) where a handshake is
// counted only on the entry edge (valid or ready rising while the other is high).
static void scan_channel_handshakes(
    IWaveformBackend& wf,
    uint32_t ref_valid, uint32_t ref_ready,
    const std::string& channel,
    uint64_t t_begin, uint64_t t_end,
    std::vector<AxiHandshakeEvent>& events,
    uint32_t ref_clk, uint32_t ref_reset,
    const AxiSignalMap& config,
    bool& scan_complete)
{
    if (ref_valid == IWaveformBackend::kInvalidSignalRef ||
        ref_ready == IWaveformBackend::kInvalidSignalRef) return;
    if (ref_clk != IWaveformBackend::kInvalidSignalRef &&
        !wf.is_loaded(ref_clk)) {
        wf.load_signals({ref_clk});
    }

    const auto point = axi_point(config);
    bool valid_active = false;
    uint64_t valid_begin_time = 0;
    uint32_t previous = std::numeric_limits<uint32_t>::max();
    for (uint32_t ti : wf.time_indices_of(ref_clk)) {
        if (ti == previous) continue;
        previous = ti;
        const uint64_t time = wf.time_at(ti);
        if (time < t_begin || time > t_end) continue;
        const std::string before = read_signal_at(wf,ref_clk,ti,
            IWaveformBackend::ObservationPoint::Before);
        const std::string raw = read_signal_at(wf,ref_clk,ti,
            IWaveformBackend::ObservationPoint::Raw);
        const bool rising = is_rising_edge(before,raw);
        const bool falling = is_falling_edge(before,raw);
        if (!((config.edge == "dual" && (rising || falling)) ||
              (config.edge == "posedge" && rising) ||
              (config.edge == "negedge" && falling))) continue;
        const std::string reset = read_signal_at(wf,ref_reset,ti,point);
        if (!known_binary(reset)) {
            scan_complete = false;
            continue;
        }
        const bool reset_asserted = config.reset_polarity == "active_low"
            ? !known_high(reset) : known_high(reset);
        if (reset_asserted) continue;
        const std::string valid = read_signal_at(wf,ref_valid,ti,point);
        const std::string ready = read_signal_at(wf,ref_ready,ti,point);
        if (!known_binary(valid) || !known_binary(ready)) {
            scan_complete = false;
            continue;
        }
        if (known_high(valid)) {
            if (!valid_active) valid_begin_time = time;
            valid_active = true;
        } else {
            valid_active = false;
        }
        if (known_high(valid) && known_high(ready)) {
            AxiHandshakeEvent ev;
            ev.channel = channel;
            ev.time = time;
            ev.time_idx = ti;
            ev.valid_begin_time = valid_begin_time;
            ev.kind = "handshake";
            events.push_back(ev);
            // A channel may transfer a new payload on the immediately next
            // edge without dropping VALID.  Each accepted payload starts a
            // new valid interval, matching the original pin monitor.
            valid_active = false;
        }
    }
}

// Augment handshake events with channel-specific data signals
static void augment_aw_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.awid);
    uint32_t ref_addr = wf.find_signal(sm.awaddr);
    uint32_t ref_len = wf.find_signal(sm.awlen);
    uint32_t ref_size = wf.find_signal(sm.awsize);
    uint32_t ref_burst = wf.find_signal(sm.awburst);
    if (ref_id == IWaveformBackend::kInvalidSignalRef) return;
    wf.load_signals({ref_id, ref_addr, ref_len, ref_size, ref_burst});
    const auto point = axi_point(sm);
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx, point);
        ev.addr = read_signal_at(wf, ref_addr, ev.time_idx, point);
        ev.len = read_signal_at(wf, ref_len, ev.time_idx, point);
        ev.size = read_signal_at(wf, ref_size, ev.time_idx, point);
        ev.burst = read_signal_at(wf, ref_burst, ev.time_idx, point);
    }
}

static void augment_w_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_data = wf.find_signal(sm.wdata);
    uint32_t ref_last = wf.find_signal(sm.wlast);
    uint32_t ref_strb = wf.find_signal(sm.wstrb);
    if (ref_data == IWaveformBackend::kInvalidSignalRef) return;
    wf.load_signals({ref_data, ref_last, ref_strb});
    const auto point = axi_point(sm);
    for (auto& ev : events) {
        ev.data = read_signal_at(wf, ref_data, ev.time_idx, point);
        ev.strb = read_signal_at(wf, ref_strb, ev.time_idx, point);
        if (ref_last) {
            std::string lv = read_signal_at(wf, ref_last, ev.time_idx, point);
            ev.last = (!lv.empty() && lv.back() == '1');
        }
    }
}

static void augment_b_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.bid);
    uint32_t ref_resp = wf.find_signal(sm.bresp);
    if (ref_id == IWaveformBackend::kInvalidSignalRef) return;
    wf.load_signals({ref_id, ref_resp});
    const auto point = axi_point(sm);
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx, point);
        ev.resp = read_signal_at(wf, ref_resp, ev.time_idx, point);
    }
}

static void augment_ar_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.arid);
    uint32_t ref_addr = wf.find_signal(sm.araddr);
    uint32_t ref_len = wf.find_signal(sm.arlen);
    uint32_t ref_size = wf.find_signal(sm.arsize);
    uint32_t ref_burst = wf.find_signal(sm.arburst);
    if (ref_id == IWaveformBackend::kInvalidSignalRef) return;
    wf.load_signals({ref_id, ref_addr, ref_len, ref_size, ref_burst});
    const auto point = axi_point(sm);
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx, point);
        ev.addr = read_signal_at(wf, ref_addr, ev.time_idx, point);
        ev.len = read_signal_at(wf, ref_len, ev.time_idx, point);
        ev.size = read_signal_at(wf, ref_size, ev.time_idx, point);
        ev.burst = read_signal_at(wf, ref_burst, ev.time_idx, point);
    }
}

static void augment_r_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.rid);
    uint32_t ref_data = wf.find_signal(sm.rdata);
    uint32_t ref_last = wf.find_signal(sm.rlast);
    uint32_t ref_resp = wf.find_signal(sm.rresp);
    if (ref_id == IWaveformBackend::kInvalidSignalRef) return;
    wf.load_signals({ref_id, ref_data, ref_last, ref_resp});
    const auto point = axi_point(sm);
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx, point);
        ev.data = read_signal_at(wf, ref_data, ev.time_idx, point);
        ev.resp = read_signal_at(wf, ref_resp, ev.time_idx, point);
        if (ref_last) {
            std::string lv = read_signal_at(wf, ref_last, ev.time_idx, point);
            ev.last = (!lv.empty() && lv.back() == '1');
        }
    }
}

// Match AW+W+B into write transactions and AR+R into read transactions.
// AXI4 has no WID: complete WLAST-delimited bursts bind to AW requests in
// acceptance order, including bursts whose first W handshake precedes AW.
// B responses matched to writes by id.
static void assemble_axi_transactions(
    const std::vector<AxiHandshakeEvent>& aw_events,
    const std::vector<AxiHandshakeEvent>& w_events,
    const std::vector<AxiHandshakeEvent>& b_events,
    const std::vector<AxiHandshakeEvent>& ar_events,
    const std::vector<AxiHandshakeEvent>& r_events,
    std::vector<AxiTransaction>& transactions)
{
    // --- Write transactions ---
    struct PendingWrite {
        size_t aw_idx;
        std::vector<std::string> data_beats;
        std::vector<AxiHandshakeEvent> data_events;
    };
    std::deque<PendingWrite> pending_w;
    std::map<std::string, std::deque<PendingWrite>> data_done_by_id;

    std::vector<std::vector<AxiHandshakeEvent>> w_bursts;
    std::vector<AxiHandshakeEvent> current_w_burst;
    for (const auto& event : w_events) {
        current_w_burst.push_back(event);
        if (event.last) {
            w_bursts.push_back(std::move(current_w_burst));
            current_w_burst.clear();
        }
    }
    const size_t paired_bursts = std::min(aw_events.size(),w_bursts.size());
    for (size_t index = 0; index < paired_bursts; ++index) {
        PendingWrite write{index,{},{}};
        write.data_events = std::move(w_bursts[index]);
        for (const auto& event : write.data_events)
            write.data_beats.push_back(event.data);
        data_done_by_id[aw_events[index].id].push_back(std::move(write));
    }
    for (size_t index = paired_bursts; index < aw_events.size(); ++index)
        pending_w.push_back({index,{},{}});
    if (!current_w_burst.empty() && paired_bursts < aw_events.size()) {
        auto& write = pending_w.front();
        write.data_events = std::move(current_w_burst);
        for (const auto& event : write.data_events)
            write.data_beats.push_back(event.data);
    }

    // Match B events to data-done writes by id, then add unmatched as incomplete
    std::vector<bool> b_used(b_events.size(), false);
    for (size_t bi = 0; bi < b_events.size(); bi++) {
        const auto& bev = b_events[bi];
        auto it = data_done_by_id.find(bev.id);
        if (it != data_done_by_id.end() && !it->second.empty()) {
            auto& pw = it->second.front();
            b_used[bi] = true;

            AxiTransaction txn;
            txn.index = transactions.size();
            txn.is_write = true;
            txn.id = bev.id;
            txn.address = aw_events[pw.aw_idx].addr;
            txn.length = aw_events[pw.aw_idx].len;
            txn.size = aw_events[pw.aw_idx].size;
            txn.burst = aw_events[pw.aw_idx].burst;
            txn.start_time = aw_events[pw.aw_idx].time;
            txn.start_time_idx = aw_events[pw.aw_idx].time_idx;
            txn.address_valid_begin_time =
                aw_events[pw.aw_idx].valid_begin_time;
            txn.end_time = bev.time;
            txn.end_time_idx = bev.time_idx;
            txn.complete = true;
            txn.data_beats = std::move(pw.data_beats);
            txn.data_events = std::move(pw.data_events);
            txn.resp = bev.resp;
            transactions.push_back(txn);

            it->second.pop_front();
        }
    }

    // Data-done writes without B (incomplete)
    for (auto& kv : data_done_by_id) {
        for (auto& pw : kv.second) {
            AxiTransaction txn;
            txn.index = transactions.size();
            txn.is_write = true;
            txn.id = aw_events[pw.aw_idx].id;
            txn.address = aw_events[pw.aw_idx].addr;
            txn.length = aw_events[pw.aw_idx].len;
            txn.size = aw_events[pw.aw_idx].size;
            txn.burst = aw_events[pw.aw_idx].burst;
            txn.start_time = aw_events[pw.aw_idx].time;
            txn.start_time_idx = aw_events[pw.aw_idx].time_idx;
            txn.address_valid_begin_time =
                aw_events[pw.aw_idx].valid_begin_time;
            txn.complete = false;
            txn.data_beats = std::move(pw.data_beats);
            txn.data_events = std::move(pw.data_events);
            transactions.push_back(txn);
        }
    }

    // Pending writes that never got WLAST (incomplete)
    for (auto& pw : pending_w) {
        AxiTransaction txn;
        txn.index = transactions.size();
        txn.is_write = true;
        txn.id = aw_events[pw.aw_idx].id;
        txn.address = aw_events[pw.aw_idx].addr;
        txn.length = aw_events[pw.aw_idx].len;
        txn.size = aw_events[pw.aw_idx].size;
        txn.burst = aw_events[pw.aw_idx].burst;
        txn.start_time = aw_events[pw.aw_idx].time;
        txn.start_time_idx = aw_events[pw.aw_idx].time_idx;
        txn.address_valid_begin_time = aw_events[pw.aw_idx].valid_begin_time;
        txn.complete = false;
        txn.data_beats = std::move(pw.data_beats);
        txn.data_events = std::move(pw.data_events);
        transactions.push_back(txn);
    }

    // Orphan B events (B without matching write)
    for (size_t bi = 0; bi < b_events.size(); bi++) {
        if (!b_used[bi]) {
            AxiTransaction txn;
            txn.index = transactions.size();
            txn.is_write = true;
            txn.id = b_events[bi].id;
            txn.start_time = b_events[bi].time;
            txn.start_time_idx = b_events[bi].time_idx;
            txn.end_time = b_events[bi].time;
            txn.end_time_idx = b_events[bi].time_idx;
            txn.complete = false;
            transactions.push_back(txn);
        }
    }

    // --- Read transactions ---
    // Similar approach: merge AR and R events by time, match by id
    enum class REKind { AR, R };
    struct REvent { uint64_t time; REKind kind; size_t idx; };
    std::vector<REvent> revents;
    for (size_t i = 0; i < ar_events.size(); i++)
        revents.push_back({ar_events[i].time, REKind::AR, i});
    for (size_t i = 0; i < r_events.size(); i++)
        revents.push_back({r_events[i].time, REKind::R, i});
    std::sort(revents.begin(), revents.end(),
        [](const REvent& a, const REvent& b) { return a.time < b.time; });

    // Track pending reads by id: FIFO queue per id
    struct PendingRead {
        size_t ar_idx;
        std::vector<std::string> data_beats;
        std::vector<AxiHandshakeEvent> data_events;
        bool data_done = false; // RLAST received
        uint64_t end_time = 0;
        uint32_t end_time_idx = 0;
    };
    std::map<std::string, std::deque<PendingRead>> pending_r_by_id;

    for (auto& re : revents) {
        if (re.kind == REKind::AR) {
            const auto& arev = ar_events[re.idx];
            pending_r_by_id[arev.id].push_back({re.idx, {}, {}, false, 0, 0});
        } else { // R
            const auto& rev = r_events[re.idx];
            auto it = pending_r_by_id.find(rev.id);
            if (it != pending_r_by_id.end() && !it->second.empty()) {
                auto& pr = it->second.front();
                pr.data_beats.push_back(rev.data);
                pr.data_events.push_back(rev);
                if (rev.last) {
                    pr.data_done = true;
                    pr.end_time = rev.time;
                    pr.end_time_idx = rev.time_idx;
                    // Complete this read transaction
                    const auto& arev = ar_events[pr.ar_idx];
                    AxiTransaction txn;
                    txn.index = transactions.size();
                    txn.is_write = false;
                    txn.id = arev.id;
                    txn.address = arev.addr;
                    txn.length = arev.len;
                    txn.size = arev.size;
                    txn.burst = arev.burst;
                    txn.start_time = arev.time;
                    txn.start_time_idx = arev.time_idx;
                    txn.address_valid_begin_time = arev.valid_begin_time;
                    txn.end_time = pr.end_time;
                    txn.end_time_idx = pr.end_time_idx;
                    txn.complete = true;
                    txn.data_beats = std::move(pr.data_beats);
                    txn.data_events = std::move(pr.data_events);
                    txn.resp = rev.resp;
                    transactions.push_back(txn);
                    it->second.pop_front();
                }
            }
        }
    }

    // Incomplete reads (no RLAST)
    for (auto& kv : pending_r_by_id) {
        for (auto& pr : kv.second) {
            const auto& arev = ar_events[pr.ar_idx];
            AxiTransaction txn;
            txn.index = transactions.size();
            txn.is_write = false;
            txn.id = arev.id;
            txn.address = arev.addr;
            txn.length = arev.len;
            txn.size = arev.size;
            txn.burst = arev.burst;
            txn.start_time = arev.time;
            txn.start_time_idx = arev.time_idx;
            txn.address_valid_begin_time = arev.valid_begin_time;
            txn.complete = false;
            txn.data_beats = std::move(pr.data_beats);
            txn.data_events = std::move(pr.data_events);
            if (!pr.data_beats.empty() && pr.end_time == 0) {
                // Use last data beat time as end
                // (We don't track per-beat times in PendingRead, skip)
            }
            transactions.push_back(txn);
        }
    }
}

static AxiScanResult scan_axi(IWaveformBackend& wf, const AxiSignalMap& sm,
    uint64_t t_begin, uint64_t t_end, Json& out_err)
{
    AxiScanResult result;
    const char* hard_raw = std::getenv("XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES");
    if (hard_raw != nullptr) {
        uint64_t hard_limit = 0;
        try { hard_limit = std::stoull(hard_raw); }
        catch (const std::exception&) { hard_limit = 0; }
        if (hard_limit > 0 && hard_limit < 1024) {
            const std::string material = sm.name + "\n" + sm.aclk + "\n" +
                sm.awaddr + "\n" + sm.araddr;
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char byte : material) {
                hash ^= static_cast<uint64_t>(byte);
                hash *= 1099511628211ULL;
            }
            std::ostringstream key;
            key << std::hex << std::setw(16) << std::setfill('0') << hash;
            out_err = {{"ok",false},{"error",{
                {"code","ANALYSIS_MEMORY_LIMIT_EXCEEDED"},
                {"message","analysis cache build exceeds the configured hard memory limit"},
                {"recoverable",true},{"error_layer","handler"},
                {"protocol","axi"},{"hard_max_bytes",hard_limit},
                {"current_estimated_bytes",0},{"key_summary",key.str()},
                {"next_actions",Json::array({
                    "For stream analysis, explicitly retry with cache_scope=range or a smaller time_range.",
                    "If range analysis still exceeds the limit, use x-npi for one-off offline analysis."})}}}};
            return result;
        }
    }

    // Resolve signal refs
    auto find_sig = [&](const std::string& name, const std::string& role) -> uint32_t {
        uint32_t ref = wf.find_signal(name);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            out_err = Json{{"ok", false},
                {"error", {{"code", "CONFIG_SIGNAL_NOT_FOUND"},
                           {"message", "AXI " + role + " signal not found: " + name}}}};
        }
        return ref;
    };

    uint32_t ref_awvalid = find_sig(sm.awvalid, "awvalid");
    uint32_t ref_awready = find_sig(sm.awready, "awready");
    uint32_t ref_wvalid  = find_sig(sm.wvalid, "wvalid");
    uint32_t ref_wready  = find_sig(sm.wready, "wready");
    uint32_t ref_bvalid  = find_sig(sm.bvalid, "bvalid");
    uint32_t ref_bready  = find_sig(sm.bready, "bready");
    uint32_t ref_arvalid = find_sig(sm.arvalid, "arvalid");
    uint32_t ref_arready = find_sig(sm.arready, "arready");
    uint32_t ref_rvalid  = find_sig(sm.rvalid, "rvalid");
    uint32_t ref_rready  = find_sig(sm.rready, "rready");

    if (!out_err.is_null()) return result;

    // Cache refs for stall detection
    result.ref_awvalid = ref_awvalid; result.ref_awready = ref_awready;
    result.ref_wvalid  = ref_wvalid;  result.ref_wready  = ref_wready;
    result.ref_bvalid  = ref_bvalid;  result.ref_bready  = ref_bready;
    result.ref_arvalid = ref_arvalid; result.ref_arready = ref_arready;
    result.ref_rvalid  = ref_rvalid;  result.ref_rready  = ref_rready;

    // Load all control signals
    wf.load_signals({ref_awvalid, ref_awready, ref_wvalid, ref_wready,
                     ref_bvalid, ref_bready, ref_arvalid, ref_arready,
                     ref_rvalid, ref_rready});

    // Scan each channel for handshakes (sampled at clock edges)
    uint32_t ref_clk = find_sig(sm.aclk,"clock");
    uint32_t ref_reset = find_sig(sm.aresetn,"reset");
    if (!out_err.is_null()) return result;
    wf.load_signals({ref_clk,ref_reset});
    scan_channel_handshakes(wf, ref_awvalid, ref_awready, "aw", t_begin, t_end,
                            result.aw_events, ref_clk, ref_reset, sm,
                            result.complete);
    scan_channel_handshakes(wf, ref_wvalid, ref_wready, "w", t_begin, t_end,
                            result.w_events, ref_clk, ref_reset, sm,
                            result.complete);
    scan_channel_handshakes(wf, ref_bvalid, ref_bready, "b", t_begin, t_end,
                            result.b_events, ref_clk, ref_reset, sm,
                            result.complete);
    scan_channel_handshakes(wf, ref_arvalid, ref_arready, "ar", t_begin, t_end,
                            result.ar_events, ref_clk, ref_reset, sm,
                            result.complete);
    scan_channel_handshakes(wf, ref_rvalid, ref_rready, "r", t_begin, t_end,
                            result.r_events, ref_clk, ref_reset, sm,
                            result.complete);

    // Augment with data payloads
    augment_aw_events(wf, sm, result.aw_events);
    augment_w_events(wf, sm, result.w_events);
    augment_b_events(wf, sm, result.b_events);
    augment_ar_events(wf, sm, result.ar_events);
    augment_r_events(wf, sm, result.r_events);

    // Assemble transactions
    assemble_axi_transactions(result.aw_events, result.w_events, result.b_events,
                              result.ar_events, result.r_events, result.transactions);
    std::stable_sort(result.transactions.begin(), result.transactions.end(),
        [](const AxiTransaction& lhs, const AxiTransaction& rhs) {
            return lhs.start_time < rhs.start_time;
        });
    for (size_t index = 0; index < result.transactions.size(); ++index)
        result.transactions[index].index = index;

    return result;
}

// ═══════════════════════════════════════════════════════════════════
// JSON rendering helpers
// ═══════════════════════════════════════════════════════════════════

static std::string render_bits(const std::string& bits, ValueRenderFormat format) {
    if (bits.empty()) return "'h0";
    return render_logic_value(logic_value_from_bits(bits, bits.size()), format);
}

static std::string render_unwidth_hex(const std::string& bits) {
    const std::string rendered = render_bits(bits,ValueRenderFormat::Hex);
    const size_t apostrophe = rendered.find('\'');
    return apostrophe == std::string::npos ? rendered : rendered.substr(apostrophe);
}

static size_t axi_expected_beat_count(const AxiTransaction& txn) {
    size_t value = 0;
    for (char bit : txn.length) {
        value <<= 1;
        if (bit == '1') ++value;
    }
    return value + 1;
}

static std::string axi_write_phase_order(const AxiTransaction& txn) {
    if (!txn.is_write || txn.data_events.empty()) return "unknown";
    const uint64_t first_w = txn.data_events.front().time;
    if (first_w < txn.start_time) return "w_before_aw";
    if (first_w == txn.start_time) return "same_cycle";
    return "aw_before_w";
}

static Json axi_txn_json(const AxiTransaction& txn, IWaveformBackend& wf,
                         TimeRenderUnit unit, ValueRenderFormat format,
                         bool matched = false, bool include_data = false) {
    const std::string direction = txn.is_write ? "write" : "read";
    Json j{{"direction",direction},
        {"latency",wf.format_time(txn.end_time >= txn.start_time
            ? txn.end_time - txn.start_time : 0, unit)},
        {"response_dependency_violation",false},
        {"address",{{"channel",txn.is_write ? "aw" : "ar"},
            {"handshake_time",wf.format_time(txn.start_time,unit)},
            {"valid_begin_time",wf.format_time(
                txn.address_valid_begin_time,unit)},
            {"addr",render_bits(txn.address,format)},
            {"id",render_bits(txn.id,format)},
            {"len",render_bits(txn.length,format)},
            {"size",render_bits(txn.size,format)},
            {"burst",render_bits(txn.burst,format)}}},
        {"response",{{"channel",txn.is_write ? "b" : "r"},
            {"handshake_time",wf.format_time(txn.end_time,unit)},
            {"resp",render_bits(txn.resp,format)}}}};
    if (txn.is_write) j["phase_order"] = axi_write_phase_order(txn);
    uint64_t length = 0;
    bool known_length = !txn.length.empty() && txn.length.size() <= 64;
    for (char bit : txn.length) {
        length <<= 1;
        if (bit == '1') ++length;
        else if (bit != '0') known_length = false;
    }
    Json data{{"channel",txn.is_write ? "w" : "r"},
        {"beat_count",txn.data_events.size()},
        {"expected_beat_count",known_length ? Json(length + 1) : Json(nullptr)}};
    if (!txn.data_events.empty()) {
        data["valid_begin_time"] = wf.format_time(
            txn.data_events.front().valid_begin_time,unit);
        data["first_handshake_time"] = wf.format_time(
            txn.data_events.front().time,unit);
        data["last_handshake_time"] = wf.format_time(
            txn.data_events.back().time,unit);
    }
    if (include_data) {
        Json beats = Json::array();
        for (size_t index = 0; index < txn.data_events.size(); ++index) {
            const auto& event = txn.data_events[index];
            Json beat{{"index",index + 1},
                {"handshake_time",wf.format_time(event.time,unit)},
                {"data",render_bits(event.data,format)},
                {"last",event.last}};
            if (txn.is_write)
                beat["wstrb"] = render_bits(event.strb,format);
            else
                beat["resp"] = render_bits(event.resp,format);
            beats.push_back(std::move(beat));
        }
        data["beats"] = std::move(beats);
    }
    j["data"] = std::move(data);
    if (matched) j["match_time"] = wf.format_time(txn.start_time, unit);
    return j;
}

static Json axi_txn_json(const AxiTransaction& txn) {
    return axi_txn_json(txn, *engine_globals().waveform,
                        TimeRenderUnit::Ns,
                        ValueRenderFormat::Hex);
}

static std::string axi_xout_scalar(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) ||
        !is_xout_scalar_json(object.at(key))) return std::string();
    return json_to_xout_value(object.at(key));
}

static void emit_axi_scalar_section(
    TextResponseBuilder& out, const std::string& name, const Json& object,
    std::initializer_list<const char*> preferred) {
    if (!object.is_object() || object.empty()) return;
    out.emit_section(name);
    std::set<std::string> emitted;
    for (const char* key : preferred) {
        if (!object.contains(key) || !is_xout_scalar_json(object.at(key))) continue;
        out.emit_kv(key, object.at(key));
        emitted.insert(key);
    }
    for (auto item = object.begin(); item != object.end(); ++item) {
        if (emitted.count(item.key()) || !is_xout_scalar_json(item.value())) continue;
        out.emit_kv(item.key(), item.value());
    }
}

static void emit_axi_transaction_domains(
    TextResponseBuilder& out, const Json& transaction,
    const std::string& prefix) {
    emit_axi_scalar_section(out, prefix + "_address",
        transaction.value("address",Json::object()),
        {"channel","valid_begin_time","handshake_time","addr","id",
         "len","size","burst"});
    const Json data = transaction.value("data",Json::object());
    emit_axi_scalar_section(out, prefix + "_data", data,
        {"channel","valid_begin_time","first_handshake_time",
         "last_handshake_time","beat_count","expected_beat_count"});
    const Json beats = data.value("beats",Json::array());
    if (beats.is_array() && !beats.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& beat : beats) rows.push_back({
            axi_xout_scalar(beat,"index"),
            axi_xout_scalar(beat,"handshake_time"),
            axi_xout_scalar(beat,"data"), axi_xout_scalar(beat,"wstrb"),
            axi_xout_scalar(beat,"resp"), axi_xout_scalar(beat,"last")});
        out.emit_section(prefix + "_beats");
        out.emit_table({"index","handshake_time","data","wstrb","resp","last"},
                       rows);
    }
    emit_axi_scalar_section(out, prefix + "_response",
        transaction.value("response",Json::object()),
        {"channel","handshake_time","resp"});
}

static std::string render_axi_query_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.query");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","direction","data_scope","query_mode",
             "found","scan_complete","analysis_complete",
             "response_truncated","total_count","returned_count"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    for (const char* key : {"truncation_scopes","value_width_complete",
                            "width_diagnostics"}) {
        if (!summary.contains(key)) continue;
        if (summary.at(key).is_array() && summary.at(key).empty())
            out.emit_kv(key,"[empty]");
        else out.emit_kv(key,summary.at(key));
    }
    const Json data = response.value("data",Json::object());
    emit_axi_scalar_section(out,"filter",data.value("filter",Json::object()),
                            {"direction"});
    const Json transaction = data.value("transaction",Json());
    if (transaction.is_object() && !transaction.empty()) {
        emit_axi_scalar_section(out,"transaction",transaction,
            {"direction","phase_order","latency",
             "response_dependency_violation","match_time"});
        emit_axi_transaction_domains(out,transaction,"transaction");
    }
    return out.str();
}

static std::string render_axi_statistics_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.statistics");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","scanned_transaction_count",
             "matched_transaction_count","matched_read_count",
             "matched_write_count","unresolved_transaction_count",
             "filter_applied","analysis_quality","full_scan_count",
             "scan_complete","analysis_complete","response_truncated",
             "total_count","returned_count"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    const Json data = response.value("data",Json::object());
    emit_axi_scalar_section(out,"filter",data.value("filter",Json::object()),
                            {"direction"});
    emit_axi_scalar_section(out,"notes",data.value("notes",Json::object()),
                            {"unresolved_transaction_count"});
    return out.str();
}

static std::string render_axi_analysis_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.analysis");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","analysis","direction","sample_count",
             "full_scan_count","completed_read_count","completed_write_count",
             "incomplete_read_count","incomplete_write_count",
             "buffered_w_beat_count","buffered_w_burst_count",
             "orphan_w_beat_count","orphan_b_count","orphan_r_beat_count",
             "response_dependency_violation_count","samples","min","max",
             "avg","p50","p95","p99","scan_complete",
             "analysis_complete","response_truncated","total_count",
             "returned_count","value_width_complete"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    const Json data = response.value("data",Json::object());
    const Json latency = data.value("latency",Json::object());
    emit_axi_scalar_section(out,"latency.read",latency.value("read",Json::object()),
                            {"samples","min","max","avg","p50","p95","p99"});
    emit_axi_scalar_section(out,"latency.write",latency.value("write",Json::object()),
                            {"samples","min","max","avg","p50","p95","p99"});
    emit_axi_scalar_section(out,"latency.definitions",
        latency.value("definitions",Json::object()),{"read","write"});
    emit_axi_scalar_section(out,"latency.write_phase_order_counts",
        latency.value("write_phase_order_counts",Json::object()),
        {"aw_before_w","same_cycle","w_before_aw","unknown"});
    const Json slowest = data.value("slowest",Json());
    if (slowest.is_object() && !slowest.empty()) {
        emit_axi_scalar_section(out,"slowest",slowest,
            {"direction","phase_order","latency",
             "response_dependency_violation"});
        emit_axi_scalar_section(out,"slowest.address",
            slowest.value("address",Json::object()),
            {"channel","valid_begin_time","handshake_time","addr","id",
             "len","size","burst"});
        emit_axi_scalar_section(out,"slowest.data",
            slowest.value("data",Json::object()),
            {"channel","valid_begin_time","first_handshake_time",
             "last_handshake_time","beat_count","expected_beat_count"});
        emit_axi_scalar_section(out,"slowest.response",
            slowest.value("response",Json::object()),
            {"channel","handshake_time","resp"});
    }
    return out.str();
}

static std::string render_axi_latency_outlier_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.latency_outlier");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","begin","end","candidate_count",
             "scan_complete","analysis_complete","response_truncated",
             "total_count","returned_count","value_width_complete"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    const Json data = response.value("data",Json::object());
    const Json rows_json = data.value("outliers",Json::array());
    if (rows_json.is_array() && !rows_json.empty()) {
        const bool include_phase_order = std::any_of(
            rows_json.begin(),rows_json.end(),[](const Json& transaction) {
                return transaction.contains("phase_order");
            });
        std::vector<std::vector<std::string>> rows;
        for (const auto& txn : rows_json) {
            const Json address = txn.value("address",Json::object());
            const Json payload = txn.value("data",Json::object());
            const Json response_data = txn.value("response",Json::object());
            std::vector<std::string> row{axi_xout_scalar(txn,"direction")};
            if (include_phase_order)
                row.push_back(axi_xout_scalar(txn,"phase_order"));
            const std::vector<std::string> suffix{
                axi_xout_scalar(txn,"latency"),
                axi_xout_scalar(txn,"response_dependency_violation"),
                axi_xout_scalar(address,"channel"),axi_xout_scalar(address,"valid_begin_time"),
                axi_xout_scalar(address,"handshake_time"),axi_xout_scalar(address,"addr"),
                axi_xout_scalar(address,"id"),axi_xout_scalar(address,"len"),
                axi_xout_scalar(address,"size"),axi_xout_scalar(address,"burst"),
                axi_xout_scalar(payload,"channel"),axi_xout_scalar(payload,"valid_begin_time"),
                axi_xout_scalar(payload,"first_handshake_time"),
                axi_xout_scalar(payload,"last_handshake_time"),
                axi_xout_scalar(payload,"beat_count"),
                axi_xout_scalar(payload,"expected_beat_count"),
                axi_xout_scalar(response_data,"channel"),
                axi_xout_scalar(response_data,"handshake_time"),
                axi_xout_scalar(response_data,"resp"),axi_xout_scalar(txn,"match_time")};
            row.insert(row.end(),suffix.begin(),suffix.end());
            rows.push_back(std::move(row));
        }
        out.emit_section("outliers");
        std::vector<std::string> headers{"direction"};
        if (include_phase_order) headers.push_back("phase_order");
        const std::vector<std::string> suffix_headers{"latency",
            "response_dependency_violation","address.channel",
            "address.valid_begin_time","address.handshake_time","address.addr",
            "address.id","address.len","address.size","address.burst",
            "data.channel","data.valid_begin_time","data.first_handshake_time",
            "data.last_handshake_time","data.beat_count","data.expected_beat_count",
            "response.channel","response.handshake_time","response.resp","match_time"};
        headers.insert(headers.end(),suffix_headers.begin(),suffix_headers.end());
        out.emit_table(headers,rows);
        for (const char* key : {"method","classification","top_n","threshold"})
            if (data.contains(key)) out.emit_kv(key,data.at(key));
    }
    return out.str();
}

static std::string render_axi_outstanding_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.outstanding_timeline");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","sampling_mode","clock","edge",
             "sample_time_semantics","sample_count","peak_read","peak_write",
             "peak_read_time","peak_write_time","first_nonzero_time",
             "final_read","final_write","scan_complete","analysis_complete",
             "response_truncated","total_count","returned_count","sample_point"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    const Json points = response.value("data",Json::object())
        .value("change_points",Json::array());
    if (points.is_array() && !points.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& point_row : points) rows.push_back({
            axi_xout_scalar(point_row,"time"),axi_xout_scalar(point_row,"read"),
            axi_xout_scalar(point_row,"write"),axi_xout_scalar(point_row,"read_delta"),
            axi_xout_scalar(point_row,"read_event"),
            axi_xout_scalar(point_row,"write_delta"),
            axi_xout_scalar(point_row,"write_event")});
        out.emit_section("change_points");
        out.emit_table({"time","read","write","read_delta","read_event",
                        "write_delta","write_event"},rows);
    }
    return out.str();
}

static std::string render_axi_stall_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.channel_stall");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","channel","sampling_mode","clock","edge",
             "sample_time_semantics","sample_count","transfer_count",
             "max_stall_cycles","ready_without_valid_cycles",
             "first_activity_time","scan_complete","analysis_complete",
             "response_truncated","total_count","returned_count","sample_point"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    out.emit_section("data");
    const Json findings = response.value("data",Json::object())
        .value("findings",Json::array());
    if (findings.empty()) out.emit_kv("findings","[empty]");
    else out.emit_json_table(findings,static_cast<int>(findings.size()));
    return out.str();
}

static std::string render_axi_cursor_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("axi.transaction.cursor");
    const Json summary = response.value("summary",Json::object());
    out.emit_section("summary");
    for (const char* key : {"name","op","direction","found","index",
             "index_base","at_begin","at_end","scan_complete",
             "analysis_complete","response_truncated","total_count",
             "returned_count","value_width_complete"})
        if (summary.contains(key)) out.emit_kv(key,summary.at(key));
    const Json txn = response.value("data",Json::object())
        .value("transaction",Json());
    if (txn.is_object() && !txn.empty()) {
        emit_axi_scalar_section(out,"transaction",txn,
            {"direction","phase_order","latency",
             "response_dependency_violation"});
        emit_axi_scalar_section(out,"transaction.address",
            txn.value("address",Json::object()),
            {"channel","valid_begin_time","handshake_time","addr","id",
             "len","size","burst"});
        emit_axi_scalar_section(out,"transaction.data",
            txn.value("data",Json::object()),
            {"channel","valid_begin_time","first_handshake_time",
             "last_handshake_time","beat_count","expected_beat_count"});
        emit_axi_scalar_section(out,"transaction.response",
            txn.value("response",Json::object()),
            {"channel","handshake_time","resp"});
    }
    return out.str();
}

static Json axi_event_json(const AxiHandshakeEvent& ev) {
    Json j;
    j["time"] = ev.time;
    j["time_idx"] = ev.time_idx;
    j["kind"] = ev.kind;
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
    return Json{};
}

static bool parse_time_range(const Json& args, IWaveformBackend& wf,
    uint64_t& t_begin, uint64_t& t_end, Json& error)
{
    t_begin = wf.min_time();
    t_end = wf.max_time();
    const Json range = args.value("time_range", Json::object());
    std::string message;
    if (range.contains("begin") &&
        !wf.parse_time(range.at("begin"), t_begin, message)) {
        error = make_error("INVALID_TIME", message);
        return false;
    }
    if (range.contains("end") &&
        !wf.parse_time(range.at("end"), t_end, message, true)) {
        error = make_error("INVALID_TIME", message);
        return false;
    }
    if (t_begin > t_end) {
        error = make_error("TIME_RANGE_INVALID", "end is before begin");
        return false;
    }
    return true;
}

static std::vector<std::pair<std::string, std::string>> axi_signal_fields(
    const AxiSignalMap& m) {
    return {{"clock",m.aclk},{"reset",m.aresetn},{"awvalid",m.awvalid},
        {"awready",m.awready},{"awaddr",m.awaddr},{"awid",m.awid},
        {"awlen",m.awlen},{"awsize",m.awsize},{"awburst",m.awburst},
        {"wvalid",m.wvalid},{"wready",m.wready},{"wdata",m.wdata},
        {"wstrb",m.wstrb},{"wlast",m.wlast},{"bvalid",m.bvalid},
        {"bready",m.bready},{"bid",m.bid},{"bresp",m.bresp},
        {"arvalid",m.arvalid},{"arready",m.arready},{"araddr",m.araddr},
        {"arid",m.arid},{"arlen",m.arlen},{"arsize",m.arsize},
        {"arburst",m.arburst},{"rvalid",m.rvalid},{"rready",m.rready},
        {"rdata",m.rdata},{"rid",m.rid},{"rresp",m.rresp},{"rlast",m.rlast}};
}

static Json recommended_actions() {
    return Json::array({
        {{"action","value.at"},{"purpose","按一个或多个指定时间读取单信号、命名信号列表或接口配置维护的值。"}},
        {{"action","axi.query"},{"purpose","按通道握手，或按方向、地址、ID 与地址握手时间查询重建后的 AXI transaction。"}},
        {{"action","axi.transaction.cursor"},{"purpose","在 AXI transfer 间移动游标。"}},
        {{"action","axi.analysis"},{"purpose","汇总 AXI 行为。"}},
        {{"action","axi.statistics"},{"purpose","按方向、ID 和地址过滤统计已完成 AXI 事务。"}},
        {{"action","axi.export"},{"purpose","导出 AXI 数据。"}},
        {{"action","axi.channel_stall"},{"purpose","实验性 AXI stall 分析。"}},
        {{"action","axi.latency_outlier"},{"purpose","实验性 AXI latency 异常。"}},
        {{"action","axi.outstanding_timeline"},{"purpose","实验性 AXI outstanding 时间线。"}},
        {{"action","axi.request_response_pair"},{"purpose","实验性 AXI 请求响应配对。"}}
    });
}

static bool parse_render(const Json& args, TimeRenderUnit& unit,
                         ValueRenderFormat& format, Json& error) {
    std::string message;
    if (!parse_time_render_unit(args.value("render_time_unit", "ns"), unit, message)) {
        error = make_error("INVALID_TIME_UNIT", message);
        return false;
    }
    if (!parse_value_render_format(args.value("value_format", "hex"), format)) {
        error = make_error("INVALID_FIELD", "invalid value_format");
        return false;
    }
    return true;
}

static Json completeness(bool complete, bool truncated,
                         size_t total, size_t returned) {
    Json scopes = Json::array();
    if (!complete) scopes.push_back("analysis_transactions");
    if (truncated) scopes.push_back("response_transactions");
    return {{"scan_complete",complete},{"analysis_complete",complete},
        {"response_truncated",truncated},{"total_count",total},
        {"returned_count",returned},{"truncation_scopes",scopes}};
}

static void merge_json(Json& target, const Json& source) {
    for (auto it = source.begin(); it != source.end(); ++it)
        target[it.key()] = it.value();
}

static bool logic_u64(const std::string& text, uint64_t& value) {
    LogicValue logic;
    if (!parse_sv_literal(text,logic) || !logic.known || logic.bits.size() > 64)
        return false;
    value = 0;
    for (char bit : logic.bits) { value <<= 1; if (bit == '1') ++value; }
    return true;
}

enum class AxiFilterResult { No, Yes, Unresolved };

static AxiFilterResult field_matches(const std::string& bits, const Json& filter) {
    if (filter.is_null() || filter.empty()) return AxiFilterResult::Yes;
    if (bits.size() > 64) return AxiFilterResult::Unresolved;
    uint64_t actual = 0;
    for (char bit : bits) {
        actual <<= 1;
        if (bit == '1') ++actual;
        else if (bit != '0') return AxiFilterResult::Unresolved;
    }
    const std::string mode = filter.at("mode");
    if (mode == "exact") {
        for (const auto& candidate : filter.at("values")) {
            uint64_t expected = 0;
            if (logic_u64(candidate,expected) && expected == actual)
                return AxiFilterResult::Yes;
        }
        return AxiFilterResult::No;
    }
    if (mode == "range") {
        uint64_t begin = 0, end = 0;
        if (!logic_u64(filter.at("begin"),begin) ||
            !logic_u64(filter.at("end"),end)) return AxiFilterResult::Unresolved;
        return actual >= begin && actual <= end ? AxiFilterResult::Yes
                                                 : AxiFilterResult::No;
    }
    uint64_t expected = 0, mask = 0;
    if (!logic_u64(filter.at("value"),expected) ||
        !logic_u64(filter.at("mask"),mask)) return AxiFilterResult::Unresolved;
    return (actual & mask) == (expected & mask) ? AxiFilterResult::Yes
                                                 : AxiFilterResult::No;
}

// ═══════════════════════════════════════════════════════════════════
// 7. axi.config.list
// ═══════════════════════════════════════════════════════════════════
struct AxiConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        const Json args = req.value("args", Json::object());
        if (args.contains("name")) {
            const std::string name = args.at("name");
            auto found = axi_configs().find(name);
            if (found == axi_configs().end())
                return make_error("CONFIG_NOT_FOUND", "AXI config not found: " + name);
            return {{"ok",true},{"summary",{{"name",name},{"status","found"}}},
                {"data",{{"config",axi_signal_map_json(found->second)}}}};
        }
        Json configs = Json::array();
        for (const auto& item : axi_configs())
            configs.push_back(axi_signal_map_json(item.second));
        return {{"ok",true},{"summary",{{"count",configs.size()}}},
            {"data",{{"configs",configs}}}};
    }
};

// ═══════════════════════════════════════════════════════════════════
// 8. axi.config.load
// ═══════════════════════════════════════════════════════════════════
struct AxiConfigLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        const Json args = req.at("args");
        Json source;
        if (args.contains("config")) source = args.at("config");
        else {
            std::ifstream stream(args.at("config_path").get<std::string>());
            if (!stream) return make_error("INVALID_FIELD", "cannot read AXI config_path");
            try { stream >> source; }
            catch (const std::exception& exception) {
                return make_error("INVALID_FIELD",
                    std::string("invalid AXI config JSON: ") + exception.what());
            }
        }
        AxiSignalMap config;
        std::string message;
        if (!parse_axi_signal_map(args.at("name"), source, config, message))
            return make_error("INVALID_FIELD", message);
        auto* wf = engine_globals().waveform.get();
        Json signals = Json::array();
        uint64_t first_edge = wf->max_time();
        bool found_edge = false;
        for (const auto& field : axi_signal_fields(config)) {
            const uint32_t ref = wf->find_signal(field.second);
            if (!ref) return make_error("CONFIG_SIGNAL_NOT_FOUND",
                "AXI " + field.first + " signal not found: " + field.second);
            if (!wf->is_loaded(ref) && wf->load_signals({ref}) != 1)
                return make_error("VALUE_NOT_AVAILABLE", "failed to load AXI signal");
            IWaveformBackend::SignalInfo info;
            if (!wf->signal_info(ref, info))
                return make_error("VALUE_NOT_AVAILABLE", "AXI signal metadata unavailable");
            if ((field.first == "clock" || field.first == "reset" ||
                 field.first == "awvalid" || field.first == "awready" ||
                 field.first == "wvalid" || field.first == "wready" ||
                 field.first == "wlast" || field.first == "bvalid" ||
                 field.first == "bready" || field.first == "arvalid" ||
                 field.first == "arready" || field.first == "rvalid" ||
                 field.first == "rready" || field.first == "rlast") && info.width != 1)
                return make_error("INVALID_FIELD", "AXI control signal must resolve to one bit");
            signals.push_back({{"field",field.first},{"requested_path",field.second},
                {"resolved_path",field.second},{"width",info.width},{"status","ok"}});
            if (field.first == "clock") {
                if (!wf->is_loaded(ref)) wf->load_signals({ref});
                const auto indices = wf->time_indices_of(ref);
                if (!indices.empty()) {
                    size_t first_change = 0;
                    // Native FST records the time-zero initialization as a
                    // change while FSDB starts at the first real transition.
                    // Keep config.load's public first_edge format-neutral.
                    if (indices.size() > 1 &&
                        wf->time_at(indices.front()) == wf->min_time()) {
                        first_change = 1;
                    }
                    first_edge = wf->time_at(indices[first_change]);
                    found_edge = true;
                }
            }
        }
        if (!found_edge) return make_error("VALUE_NOT_AVAILABLE", "AXI clock has no edges");
        axi_configs()[config.name] = config;
        axi_cursors().erase(config.name + ":all");
        axi_cursors().erase(config.name + ":read");
        axi_cursors().erase(config.name + ":write");
        return {{"ok",true},{"summary",{{"name",config.name},{"status","loaded"}}},
            {"data",{{"config",axi_signal_map_json(config)},
                {"validation",{{"status","ok"},{"clock",{{"status","ok"},
                    {"edge",config.edge},{"first_edge",first_edge}}},{"signals",signals}}},
                {"recommended_actions",recommended_actions()}}}};
    }
};

// ═══════════════════════════════════════════════════════════════════
// 9. axi.query
// ═══════════════════════════════════════════════════════════════════
struct AxiQueryHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.query"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        TimeRenderUnit unit;
        ValueRenderFormat format;
        if (!parse_render(args, unit, format, scan_err)) return scan_err;
        const std::string direction = args.value("direction", "write");
        std::vector<const AxiTransaction*> matches;
        const Json address_filter = args.value("address",Json());
        const Json id_filter = args.value("id",Json());
        for (const auto& txn : result.transactions)
            if (txn.complete && ((direction == "write") == txn.is_write) &&
                field_matches(txn.address,address_filter) == AxiFilterResult::Yes &&
                field_matches(txn.id,id_filter) == AxiFilterResult::Yes)
                matches.push_back(&txn);
        const Json query = args.value("query", Json::object());
        const bool include_data = args.value("output", Json::object())
            .value("include_data", false);
        const bool last = args.value("last", false);
        const int index = query.value("index", -1);
        const int requested_limit = query.value("line_limit", -1);
        Json filter{{"direction",direction}};
        if (args.contains("address")) filter["address"] = args.at("address");
        if (args.contains("id")) filter["id"] = args.at("id");
        if (args.contains("time_range")) filter["time_range"] = {
            {"begin",wf->format_time(t_begin,unit)},
            {"end",wf->format_time(t_end,unit)}};
        Json summary{{"name",args.at("name")},{"direction",direction},
            {"data_scope","first_beat_each_with_first_transaction_full"}};
        Json data{{"filter",filter}};
        if (last || (index > 0 && requested_limit < 0)) {
            const size_t offset = last ? (matches.empty() ? 0 : matches.size() - 1)
                : static_cast<size_t>(index - 1);
            const bool found = !matches.empty() && offset < matches.size();
            summary["query_mode"] = last ? "last" : "index";
            summary["data_scope"] = "all_returned_transactions_full";
            summary["found"] = found;
            if (found) data["transaction"] =
                axi_txn_json(*matches[offset], *wf, unit, format,
                             false, include_data);
            merge_json(summary, completeness(result.complete, false,
                                              matches.size(), found ? 1 : 0));
            summary["value_width_complete"] = true;
            summary["width_diagnostics"] = Json::array();
        } else if (requested_limit > 0) {
            Json transactions = Json::array();
            const size_t begin = index > 0 ? static_cast<size_t>(index - 1) : 0;
            for (size_t i = begin; i < matches.size() &&
                 transactions.size() < static_cast<size_t>(requested_limit); ++i)
                transactions.push_back(axi_txn_json(*matches[i], *wf, unit, format));
            const bool truncated = begin + transactions.size() < matches.size();
            summary["query_mode"] = "list";
            merge_json(summary, completeness(result.complete, truncated,
                                              matches.size(), transactions.size()));
            data["transactions"] = std::move(transactions);
            summary["value_width_complete"] = true;
            summary["width_diagnostics"] = Json::array();
        } else {
            summary["query_mode"] = "count";
            summary["data_scope"] = "none";
            merge_json(summary, completeness(result.complete, false,
                                              matches.size(), 0));
        }
        return {{"ok",true},{"summary",summary},{"data",data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_query_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 10. axi.analysis
// ═══════════════════════════════════════════════════════════════════
struct AxiAnalysisHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.analysis"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args,unit,format,scan_err)) return scan_err;
        const std::string analysis = args.value("analysis","latency");
        const std::string direction = args.value("direction","all");
        std::vector<uint64_t> read_lat, write_lat;
        std::map<std::string,size_t> write_phase_counts{
            {"aw_before_w",0},{"same_cycle",0},{"w_before_aw",0},{"unknown",0}};
        size_t incomplete_read = 0, incomplete_write = 0;
        const AxiTransaction* slowest = nullptr; uint64_t slowest_latency = 0;
        for (const auto& txn : result.transactions) {
            if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
            if (!txn.complete) { txn.is_write ? ++incomplete_write : ++incomplete_read; continue; }
            const uint64_t latency = txn.end_time >= txn.start_time ? txn.end_time - txn.start_time : 0;
            (txn.is_write ? write_lat : read_lat).push_back(latency);
            if (txn.is_write) ++write_phase_counts[axi_write_phase_order(txn)];
            if (!slowest || latency > slowest_latency) { slowest = &txn; slowest_latency = latency; }
        }
        const auto clock_edges = selected_clock_edges(*wf,sm,t_begin,t_end);
        auto common_summary = [&]() {
            return Json{{"name",args.at("name")},{"analysis",analysis},
                {"direction",direction},{"sample_count",clock_edges.size()},
                {"full_scan_count",1},{"scanned_range",{{"begin",wf->format_time(t_begin,unit)},
                    {"end",wf->format_time(t_end,unit)}}},
                {"completed_read_count",read_lat.size()},{"completed_write_count",write_lat.size()},
                {"incomplete_read_count",incomplete_read},{"incomplete_write_count",incomplete_write},
                {"buffered_w_beat_count",0},{"buffered_w_burst_count",0},
                {"orphan_w_beat_count",0},{"orphan_b_count",0},{"orphan_r_beat_count",0},
                {"response_dependency_violation_count",0},
                {"channel_handshakes",{{"aw",result.aw_events.size()},
                    {"w",result.w_events.size()},{"b",result.b_events.size()},
                    {"ar",result.ar_events.size()},{"r",result.r_events.size()}}}};
        };
        if (analysis == "pending") {
            const size_t limit = args.value("line_limit",1000u);
            size_t total = 0;
            Json pending = Json::array();
            for (const auto& txn : result.transactions) {
                if (txn.complete || (direction != "all" &&
                    ((direction == "write") != txn.is_write))) continue;
                ++total;
                if (pending.size() >= limit) continue;
                uint64_t expected = 0;
                bool known_length = txn.length.size() <= 64;
                for (char bit : txn.length) {
                    expected <<= 1;
                    if (bit == '1') ++expected;
                    else if (bit != '0') known_length = false;
                }
                Json item{{"direction",txn.is_write ? "write" : "read"},
                    {"request_time",wf->format_time(txn.start_time,unit)},
                    {"age",wf->format_time(t_end >= txn.start_time ? t_end-txn.start_time : 0,unit)},
                    {"addr",render_bits(txn.address,format)},{"id",render_bits(txn.id,format)},
                    {"len",render_bits(txn.length,format)},
                    {"expected_beat_count",known_length ? Json(expected+1) : Json(nullptr)},
                    {"observed_beat_count",txn.data_beats.size()},
                    {"data_complete",false}};
                if (txn.is_write) item["phase_order"] = axi_write_phase_order(txn);
                pending.push_back(std::move(item));
            }
            Json summary = common_summary();
            merge_json(summary,completeness(result.complete,pending.size()<total,
                                            total,pending.size()));
            return {{"ok",true},{"summary",summary},
                {"data",{{"pending_transactions",pending}}}};
        }
        if (analysis == "osd") {
            std::vector<uint64_t> read_depth, write_depth;
            const uint32_t reset_ref = wf->find_signal(sm.aresetn);
            const auto point = axi_point(sm);
            // Sweep transaction boundaries once; rescanning every transaction at
            // every clock edge is quadratic on long, high-outstanding traces.
            struct DepthEvent { uint64_t time; int read; int write; };
            std::vector<DepthEvent> events;
            events.reserve(result.transactions.size() * 2);
            for (const auto& txn : result.transactions) {
                if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
                if (txn.complete && txn.end_time <= txn.start_time) continue;
                events.push_back({txn.start_time, txn.is_write ? 0 : 1, txn.is_write ? 1 : 0});
                if (txn.complete)
                    events.push_back({txn.end_time, txn.is_write ? 0 : -1, txn.is_write ? -1 : 0});
            }
            std::sort(events.begin(), events.end(), [](const DepthEvent& a, const DepthEvent& b) {
                return a.time < b.time;
            });
            size_t event_index = 0;
            int64_t active_read = 0, active_write = 0;
            for (uint32_t ti : clock_edges) {
                const std::string reset = read_signal_at(*wf,reset_ref,ti,point);
                if (!known_binary(reset) ||
                    (sm.reset_polarity == "active_low"
                        ? !known_high(reset) : known_high(reset))) continue;
                const uint64_t time = wf->time_at(ti);
                while (event_index < events.size() && events[event_index].time <= time) {
                    active_read += events[event_index].read;
                    active_write += events[event_index].write;
                    ++event_index;
                }
                read_depth.push_back(static_cast<uint64_t>(active_read));
                write_depth.push_back(static_cast<uint64_t>(active_write));
            }
            auto depth_stats = [](const std::vector<uint64_t>& values) {
                uint64_t min = values.empty() ? 0 : *std::min_element(values.begin(),values.end());
                uint64_t max = values.empty() ? 0 : *std::max_element(values.begin(),values.end());
                uint64_t sum = 0; for (uint64_t value : values) sum += value;
                Json result{{"samples",values.size()},{"min",min},{"max",max},
                    {"avg",values.empty() ? 0.0 : static_cast<double>(sum)/values.size()}};
                if (values.empty()) result["status"] = "empty";
                return result;
            };
            Json read_stats = depth_stats(read_depth), write_stats = depth_stats(write_depth);
            std::vector<uint64_t> combined;
            combined.reserve(read_depth.size());
            for (size_t index = 0; index < read_depth.size(); ++index)
                combined.push_back(read_depth[index] + write_depth[index]);
            Json total = depth_stats(combined);
            Json summary = common_summary();
            summary["samples"] = total.at("samples"); summary["min"] = total.at("min");
            summary["max"] = total.at("max"); summary["avg"] = total.at("avg");
            merge_json(summary,completeness(result.complete,false,
                                            combined.size(),combined.size()));
            return {{"ok",true},{"summary",summary},{"data",{{"osd",{
                {"read",read_stats},{"write",write_stats},
                {"final_read",read_depth.empty()?0:read_depth.back()},
                {"final_write",write_depth.empty()?0:write_depth.back()},
                {"definitions",{{"read","increment on AR handshake, decrement on RLAST handshake"},
                    {"write","increment on AW handshake, decrement on B handshake"}}}}}}}};
        }
        auto stats = [&](std::vector<uint64_t> values, bool percentile) {
            std::sort(values.begin(),values.end());
            uint64_t min = values.empty() ? 0 : values.front();
            uint64_t max = values.empty() ? 0 : values.back();
            uint64_t sum = 0; for (uint64_t value : values) sum += value;
            Json s{{"samples",values.size()},{"min",wf->format_time(min,unit)},
                {"max",wf->format_time(max,unit)},
                {"avg",wf->format_time(values.empty() ? 0 : sum / values.size(),unit)}};
            if (percentile) {
                auto pct = [&](size_t numerator) { return values.empty() ? 0 :
                    values[std::min(values.size()-1,(values.size()*numerator+99)/100-1)]; };
                s["p50"] = wf->format_time(pct(50),unit);
                s["p95"] = wf->format_time(pct(95),unit);
                s["p99"] = wf->format_time(pct(99),unit);
            }
            return s;
        };
        std::vector<uint64_t> all = read_lat; all.insert(all.end(),write_lat.begin(),write_lat.end());
        Json total_stats = stats(all,true);
        Json summary{{"name",args.at("name")},{"analysis",analysis},{"direction",direction},
            {"sample_count",clock_edges.size()},
            {"full_scan_count",1},{"scanned_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(t_end,unit)}}},
            {"completed_read_count",read_lat.size()},{"completed_write_count",write_lat.size()},
            {"incomplete_read_count",incomplete_read},{"incomplete_write_count",incomplete_write},
            {"buffered_w_beat_count",0},{"buffered_w_burst_count",0},
            {"orphan_w_beat_count",0},{"orphan_b_count",0},{"orphan_r_beat_count",0},
            {"response_dependency_violation_count",0},
            {"channel_handshakes",{{"aw",result.aw_events.size()},{"w",result.w_events.size()},
                {"b",result.b_events.size()},{"ar",result.ar_events.size()},
                {"r",result.r_events.size()}}},
            {"min",total_stats.at("min")},{"avg",total_stats.at("avg")},
            {"max",total_stats.at("max")},{"p50",total_stats.at("p50")},
            {"p95",total_stats.at("p95")},{"p99",total_stats.at("p99")},
            {"samples",all.size()}};
        merge_json(summary,completeness(result.complete,false,all.size(),all.size()));
        summary["value_width_complete"] = true;
        summary["width_diagnostics"] = Json::array();
        Json latency{{"read",stats(read_lat,true)},{"write",stats(write_lat,true)},
            {"definitions",{{"read","AR handshake to RLAST handshake"},
                {"write","AW handshake to B handshake"}}},
            {"write_phase_order_counts",write_phase_counts}};
        Json data{{"latency",latency}};
        if (slowest) data["slowest"] = axi_txn_json(*slowest,*wf,unit,format);
        return {{"ok",true},{"summary",summary},{"data",data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_analysis_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 11. axi.export
// ═══════════════════════════════════════════════════════════════════
struct AxiExportHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.export"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args,unit,format,scan_err)) return scan_err;
        if (!args.contains("output") || !args.at("output").contains("path"))
            return make_error("INVALID_FIELD","axi.export requires output.path");
        const Json output = args.at("output");
        const std::string prefix = output.at("path");
        const std::string file_format = output.value("file_format","tsv");
        const std::string extension = "." + file_format;
        const std::string write_path = prefix + ".write" + extension;
        const std::string read_path = prefix + ".read" + extension;
        const std::string meta_path = prefix + ".meta.json";
        std::ofstream writes(write_path), reads(read_path), meta(meta_path);
        if (!writes || !reads || !meta)
            return make_error("OUTPUT_WRITE_FAILED","cannot open AXI export output");
        const char separator = file_format == "csv" ? ',' : '\t';
        auto write_header = [&](std::ostream& stream) {
            stream << "seq" << separator << "completion_time" << separator
                << "addr_time" << separator << "first_data_time" << separator
                << "last_data_time" << separator << "latency" << separator
                << "phase_order" << separator
                << "response_dependency_violation" << separator << "id"
                << separator << "addr" << separator << "len" << separator
                << "size" << separator << "burst" << separator << "resp"
                << separator << "beat_count" << separator
                << "expected_beat_count\n";
        };
        write_header(writes); write_header(reads);
        size_t write_count = 0, read_count = 0, incomplete_write = 0, incomplete_read = 0;
        std::map<std::string,int> write_count_by_id, read_count_by_id;
        std::map<std::string,int> burst_histogram;
        for (const auto& txn : result.transactions) {
            if (!txn.complete) { txn.is_write ? ++incomplete_write : ++incomplete_read; continue; }
            std::ostream& stream = txn.is_write ? writes : reads;
            const uint64_t first_data = txn.data_events.empty()
                ? txn.start_time : txn.data_events.front().time;
            const uint64_t last_data = txn.data_events.empty()
                ? txn.start_time : txn.data_events.back().time;
            stream << txn.index + 1 << separator
                << wf->format_time(txn.end_time,unit) << separator
                << wf->format_time(txn.start_time,unit) << separator
                << wf->format_time(first_data,unit) << separator
                << wf->format_time(last_data,unit) << separator
                << wf->format_time(txn.end_time - txn.start_time,unit) << separator
                << (txn.is_write ? axi_write_phase_order(txn) : "") << separator
                << "false" << separator << render_unwidth_hex(txn.id) << separator
                << render_unwidth_hex(txn.address) << separator
                << render_unwidth_hex(txn.length) << separator
                << render_unwidth_hex(txn.size) << separator
                << render_unwidth_hex(txn.burst) << separator
                << render_unwidth_hex(txn.resp) << separator
                << txn.data_events.size() << separator
                << axi_expected_beat_count(txn) << '\n';
            ++(txn.is_write ? write_count_by_id : read_count_by_id)[
                render_unwidth_hex(txn.id)];
            ++burst_histogram[render_unwidth_hex(txn.burst)];
            txn.is_write ? ++write_count : ++read_count;
        }
        auto counts_json = [](const std::map<std::string,int>& counts) {
            Json object = Json::object();
            for (const auto& item : counts) object[item.first] = item.second;
            return object;
        };
        auto ids_json = [](const std::map<std::string,int>& counts) {
            Json array = Json::array();
            for (const auto& item : counts) array.push_back(item.first);
            return array;
        };
        std::map<std::string,int> current_write_by_id, current_read_by_id;
        std::map<std::string,int> max_write_by_id, max_read_by_id;
        int current_write = 0, current_read = 0;
        int max_total_write = 0, max_total_read = 0;
        struct OutstandingEvent {
            uint64_t time;
            bool start;
            bool write;
            std::string id;
        };
        std::vector<OutstandingEvent> outstanding_events;
        for (const auto& txn : result.transactions) {
            if (!txn.complete) continue;
            const std::string id = render_unwidth_hex(txn.id);
            outstanding_events.push_back({txn.start_time,true,txn.is_write,id});
            outstanding_events.push_back({txn.end_time,false,txn.is_write,id});
        }
        std::stable_sort(outstanding_events.begin(),outstanding_events.end(),
            [](const OutstandingEvent& left, const OutstandingEvent& right) {
                if (left.time != right.time) return left.time < right.time;
                return left.start > right.start;
            });
        for (const auto& event : outstanding_events) {
            int& current = event.write ? current_write : current_read;
            auto& current_by_id = event.write ? current_write_by_id : current_read_by_id;
            auto& maximum_by_id = event.write ? max_write_by_id : max_read_by_id;
            if (event.start) {
                ++current;
                maximum_by_id[event.id] = std::max(maximum_by_id[event.id],
                                                    ++current_by_id[event.id]);
                if (event.write) max_total_write = std::max(max_total_write,current);
                else max_total_read = std::max(max_total_read,current);
            } else {
                if (current > 0) --current;
                if (current_by_id[event.id] > 0) --current_by_id[event.id];
            }
        }
        auto max_by_id = [](const std::map<std::string,int>& counts) {
            Json object = Json::object();
            for (const auto& item : counts) object[item.first] = item.second;
            return object;
        };
        const size_t sample_count = selected_clock_edges(*wf,sm,t_begin,t_end).size();
        const uint64_t scan_end = std::min(t_end,wf->max_time());
        meta << Json{{"name",args.at("name")},{"format",file_format},
            {"begin",wf->format_time(t_begin,unit)},{"end",wf->format_time(t_end,unit)},
            {"scan_begin",wf->format_time(t_begin,unit)},
            {"scan_end",wf->format_time(scan_end,unit)},
            {"sample_count",sample_count},{"full_scan_count",1},
            {"analysis_complete",result.complete},{"write_file",write_path},
            {"read_file",read_path},{"meta_file",meta_path},
            {"write_count",write_count},{"read_count",read_count},
            {"total_count",write_count + read_count},
            {"unique_write_ids",ids_json(write_count_by_id)},
            {"unique_read_ids",ids_json(read_count_by_id)},
            {"write_count_by_id",counts_json(write_count_by_id)},
            {"read_count_by_id",counts_json(read_count_by_id)},
            {"max_write_outstanding_by_id",max_by_id(max_write_by_id)},
            {"max_read_outstanding_by_id",max_by_id(max_read_by_id)},
            {"max_total_write_outstanding",max_total_write},
            {"max_total_read_outstanding",max_total_read},
            {"burst_histogram",counts_json(burst_histogram)},
            {"beat_count_mismatch_count",0},{"incomplete_write_count",incomplete_write},
            {"incomplete_read_count",incomplete_read},{"buffered_w_beat_count",0},
            {"buffered_w_burst_count",0},{"orphan_w_beat_count",0},
            {"orphan_b_count",0},{"orphan_r_beat_count",0},
            {"response_dependency_violation_count",0},
            {"reset_cleared_write_count",0},{"reset_cleared_read_count",0}}.dump(2)
             << '\n';
        const size_t total = write_count + read_count;
        Json output_summary{{"path",prefix},{"write_path",write_path},
            {"read_path",read_path},{"meta_path",meta_path},{"file_format",file_format}};
        Json summary{{"name",args.at("name")},{"write_count",write_count},
            {"read_count",read_count},{"row_count",total},{"format",file_format},
            {"status","written"},{"output_written",true},
            {"sample_count",selected_clock_edges(*wf,sm,t_begin,t_end).size()},
            {"full_scan_count",1},{"incomplete_write_count",incomplete_write},
            {"incomplete_read_count",incomplete_read},{"buffered_w_beat_count",0},
            {"buffered_w_burst_count",0},{"orphan_w_beat_count",0},
            {"orphan_b_count",0},{"orphan_r_beat_count",0},
            {"response_dependency_violation_count",0},
            {"requested_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(t_end,unit)}}},
            {"scanned_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(std::min(t_end,wf->max_time()),unit)}}},
            {"output",output_summary}};
        merge_json(summary,completeness(result.complete,false,total,total));
        return {{"ok",true},{"summary",summary},{"data",Json::object()}};
    }
};

// ═══════════════════════════════════════════════════════════════════
// 12. axi.statistics
// ═══════════════════════════════════════════════════════════════════
struct AxiStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        size_t read_count = 0, write_count = 0;
        size_t error_count = 0;
        double sum_read_lat = 0, sum_write_lat = 0;
        uint64_t min_lat = UINT64_MAX, max_lat = 0;
        size_t read_lat_count = 0, write_lat_count = 0;

        for (auto& txn : result.transactions) {
            if (!txn.complete) { error_count++; continue; }
            uint64_t lat = (txn.end_time > txn.start_time) ? (txn.end_time - txn.start_time) : 0;
            if (txn.is_write) {
                write_count++;
                sum_write_lat += lat;
                write_lat_count++;
            } else {
                read_count++;
                sum_read_lat += lat;
                read_lat_count++;
            }
            if (lat < min_lat) min_lat = lat;
            if (lat > max_lat) max_lat = lat;
        }
        if (result.transactions.empty() || (read_lat_count + write_lat_count) == 0) {
            min_lat = 0;
        }

        double avg_read_lat = read_lat_count ? sum_read_lat / read_lat_count : 0;
        double avg_write_lat = write_lat_count ? sum_write_lat / write_lat_count : 0;

        // Compute outstanding max: walk through all start/end events
        size_t outstanding_max = 0;
        {
            std::vector<std::pair<uint64_t, int>> events; // +1 start, -1 end
            for (auto& txn : result.transactions) {
                events.emplace_back(txn.start_time, 1);
                if (txn.complete && txn.end_time > 0)
                    events.emplace_back(txn.end_time, -1);
            }
            std::sort(events.begin(), events.end());
            size_t cur = 0;
            for (auto& p : events) {
                cur += p.second;
                if (cur > outstanding_max) outstanding_max = cur;
            }
        }

        (void)avg_read_lat; (void)avg_write_lat; (void)min_lat;
        (void)max_lat; (void)outstanding_max; (void)error_count;
        const Json input_filter = args.value("filter", Json::object());
        const std::string direction = input_filter.value("direction", "all");
        const Json address_filter = input_filter.value("address",Json());
        Json id_filter;
        if (input_filter.contains("ids"))
            id_filter = {{"mode","exact"},{"values",input_filter.at("ids")}};
        size_t matched_read = 0, matched_write = 0, unresolved = 0;
        for (const auto& txn : result.transactions) {
            if (!txn.complete) continue;
            if (!(direction == "all" || (direction == "write" && txn.is_write) ||
                (direction == "read" && !txn.is_write))) continue;
            const auto address_match = field_matches(txn.address,address_filter);
            const auto id_match = field_matches(txn.id,id_filter);
            if (address_match == AxiFilterResult::Unresolved ||
                id_match == AxiFilterResult::Unresolved) { ++unresolved; continue; }
            if (address_match == AxiFilterResult::Yes && id_match == AxiFilterResult::Yes)
                txn.is_write ? ++matched_write : ++matched_read;
        }
        Json filter{{"direction",direction}};
        if (input_filter.contains("address")) filter["address"] = input_filter.at("address");
        if (input_filter.contains("ids")) filter["ids"] = input_filter.at("ids");
        const size_t matched = matched_read + matched_write;
        Json summary{{"name",args.at("name")},
            {"scanned_transaction_count",result.transactions.size()},
            {"matched_transaction_count",matched},{"matched_read_count",matched_read},
            {"matched_write_count",matched_write},{"unresolved_transaction_count",unresolved},
            {"filter_applied",!input_filter.empty()},
            {"analysis_quality",(unresolved || !result.complete)
                ? "ambiguous" : "complete"},
            {"full_scan_count",1}};
        merge_json(summary, completeness(result.complete && unresolved == 0,
                                         false,matched,matched));
        return {{"ok",true},{"summary",summary},{"data",{{"filter",filter},
            {"notes",{{"unresolved_transaction_count",
            "因被引用的 address/ID 含 X/Z 或不可解析，导致无法判断是否匹配过滤条件的已完成事务数。"}}}}}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_statistics_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 13. axi.transaction.cursor
// ═══════════════════════════════════════════════════════════════════
struct AxiTransactionCursorHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.transaction.cursor"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;
        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args, unit, format, scan_err)) return scan_err;
        const std::string direction = args.value("direction", "all");
        std::vector<const AxiTransaction*> matches;
        for (const auto& txn : result.transactions)
            if (txn.complete && (direction == "all" ||
                (direction == "write" && txn.is_write) ||
                (direction == "read" && !txn.is_write))) matches.push_back(&txn);
        const std::string key = args.at("name").get<std::string>() + ":" + direction;
        size_t& position = axi_cursors()[key];
        const std::string op = args.at("op");
        bool found = !matches.empty();
        if (found) {
            if (op == "begin") position = 0;
            else if (op == "last") position = matches.size() - 1;
            else if (op == "next") {
                if (position + 1 < matches.size()) ++position; else found = false;
            } else {
                if (position > 0) --position; else found = false;
            }
        }
        Json summary{{"name",args.at("name")},{"op",op},{"direction",direction},
            {"found",found},{"index",found ? Json(position + 1) : Json(nullptr)},
            {"index_base",1},{"at_begin",found && position == 0},
            {"at_end",found && position + 1 == matches.size()}};
        merge_json(summary, completeness(result.complete,false,
                                         matches.size(),found ? 1 : 0));
        summary["value_width_complete"] = true;
        summary["width_diagnostics"] = Json::array();
        Json data = Json::object();
        if (found) data["transaction"] = axi_txn_json(*matches[position],*wf,unit,format);
        return {{"ok",true},{"summary",summary},{"data",data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_cursor_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 14. axi.channel_stall
// ═══════════════════════════════════════════════════════════════════
struct AxiChannelStallHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.channel_stall"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        if (!parse_time_range(args, *wf, t_begin, t_end, cfg_err)) return cfg_err;
        Json scan_error;
        const auto protocol_scan = scan_axi(*wf,sm,t_begin,t_end,scan_error);
        if (!scan_error.is_null()) return scan_error;

        // Resolve all valid/ready pairs
        struct ChanPair { std::string name; uint32_t ref_v; uint32_t ref_r; };
        std::vector<ChanPair> channels;
        auto add_chan = [&](const std::string& name, const std::string& sig_v, const std::string& sig_r) {
            uint32_t rv = wf->find_signal(sig_v);
            uint32_t rr = wf->find_signal(sig_r);
            if (!rv || !rr) return;
            channels.push_back({name, rv, rr});
            wf->load_signals({rv, rr});
        };
        const std::string selected_channel = args.value("channel", "aw");
        if (selected_channel == "aw") add_chan("aw", sm.awvalid, sm.awready);
        if (selected_channel == "w") add_chan("w", sm.wvalid, sm.wready);
        if (selected_channel == "b") add_chan("b", sm.bvalid, sm.bready);
        if (selected_channel == "ar") add_chan("ar", sm.arvalid, sm.arready);
        if (selected_channel == "r") add_chan("r", sm.rvalid, sm.rready);

        Json stalls = Json::array();
        bool scan_complete = true;
        for (auto& ch : channels) {
            // Merge change points
            std::set<uint32_t> ti_set;
            for (uint32_t ti : wf->time_indices_of(ch.ref_v)) {
                uint64_t t = wf->time_at(ti);
                if (t >= t_begin && t <= t_end) ti_set.insert(ti);
            }
            for (uint32_t ti : wf->time_indices_of(ch.ref_r)) {
                uint64_t t = wf->time_at(ti);
                if (t >= t_begin && t <= t_end) ti_set.insert(ti);
            }
            uint32_t ti_begin = wf->time_idx_of(t_begin);
            if (ti_begin > 0) ti_set.insert(ti_begin);

            std::vector<uint32_t> tis(ti_set.begin(), ti_set.end());

            bool in_stall = false;
            uint64_t stall_start = 0;
            uint32_t stall_start_ti = 0;

            for (uint32_t ti : tis) {
                std::string vv = read_signal_at(*wf, ch.ref_v, ti);
                std::string rv = read_signal_at(*wf, ch.ref_r, ti);
                if (!known_binary(vv) || !known_binary(rv)) {
                    scan_complete = false;
                    continue;
                }
                bool valid_high = known_high(vv);
                bool ready_high = known_high(rv);

                if (valid_high && !ready_high) {
                    if (!in_stall) {
                        in_stall = true;
                        stall_start = wf->time_at(ti);
                        stall_start_ti = ti;
                    }
                } else if (in_stall) {
                    // Stall ends
                    uint64_t end_t = wf->time_at(ti);
                    stalls.push_back({
                        {"channel", ch.name},
                        {"start_time", stall_start},
                        {"end_time", end_t},
                        {"duration", (end_t > stall_start) ? (end_t - stall_start) : 0ULL},
                        {"start_time_idx", stall_start_ti},
                        {"end_time_idx", ti}
                    });
                    in_stall = false;
                }
            }
            if (in_stall) {
                stalls.push_back({
                    {"channel", ch.name},
                    {"start_time", stall_start},
                    {"end_time", t_end},
                    {"duration", (t_end > stall_start) ? (t_end - stall_start) : 0ULL},
                    {"start_time_idx", stall_start_ti},
                    {"end_time_idx", wf->time_idx_of(t_end)}
                });
            }
        }

        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args, unit, format, cfg_err)) return cfg_err;
        const auto clock_edges = selected_clock_edges(*wf,sm,t_begin,t_end);
        uint64_t first_activity = t_end;
        std::vector<std::pair<uint32_t,uint32_t>> activity_channels{
            {protocol_scan.ref_awvalid,protocol_scan.ref_awready},
            {protocol_scan.ref_wvalid,protocol_scan.ref_wready},
            {protocol_scan.ref_bvalid,protocol_scan.ref_bready},
            {protocol_scan.ref_arvalid,protocol_scan.ref_arready},
            {protocol_scan.ref_rvalid,protocol_scan.ref_rready}};
        const auto activity_point = axi_point(sm);
        std::vector<std::pair<std::string,std::string>> previous_values(
            activity_channels.size());
        bool have_activity_baseline = false;
        for (uint32_t ti : clock_edges) {
            bool changed = false;
            for (size_t index = 0; index < activity_channels.size(); ++index) {
                const auto [valid_ref,ready_ref] = activity_channels[index];
                const std::pair<std::string,std::string> current{
                    read_signal_at(*wf,valid_ref,ti,activity_point),
                    read_signal_at(*wf,ready_ref,ti,activity_point)};
                if (have_activity_baseline && current != previous_values[index])
                    changed = true;
                previous_values[index] = current;
            }
            if (have_activity_baseline && changed) {
                first_activity = wf->time_at(ti);
                break;
            }
            have_activity_baseline = true;
        }
        size_t transfer_count = 0;
        if (selected_channel == "aw") transfer_count = protocol_scan.aw_events.size();
        if (selected_channel == "w") transfer_count = protocol_scan.w_events.size();
        if (selected_channel == "b") transfer_count = protocol_scan.b_events.size();
        if (selected_channel == "ar") transfer_count = protocol_scan.ar_events.size();
        if (selected_channel == "r") transfer_count = protocol_scan.r_events.size();
        size_t ready_without_valid = 0;
        if (!channels.empty()) {
            const auto point = axi_point(sm);
            for (uint32_t ti : clock_edges) {
                if (wf->time_at(ti) < first_activity) continue;
                const std::string valid = read_signal_at(
                    *wf,channels.front().ref_v,ti,point);
                const std::string ready = read_signal_at(
                    *wf,channels.front().ref_r,ti,point);
                if (known_binary(valid) && known_binary(ready) &&
                    !known_high(valid) && known_high(ready)) ++ready_without_valid;
            }
        }
        const size_t limit = args.value("line_limit", 1000u);
        Json findings = Json::array();
        size_t max_cycles = 0;
        for (const auto& stall : stalls) {
            const uint64_t duration = stall.at("duration");
            const size_t cycles = static_cast<size_t>(duration / 10);
            max_cycles = std::max(max_cycles, cycles);
            if (findings.size() < limit) findings.push_back({{"type","long_stall"},
                {"severity","warning"},{"begin",wf->format_time(stall.at("start_time"),unit)},
                {"end",wf->format_time(stall.at("end_time"),unit)},{"cycles",cycles}});
        }
        const bool truncated = findings.size() < stalls.size();
        Json summary{{"name",args.at("name")},{"channel",selected_channel},
            {"sampling_mode","clock_edge"},{"clock",sm.aclk},{"edge",sm.edge},
            {"sample_time_semantics","time is sample_time"},
            {"sample_count",clock_edges.size()},
            {"transfer_count",transfer_count},{"max_stall_cycles",max_cycles},
            {"ready_without_valid_cycles",ready_without_valid},
            {"first_activity_time",wf->format_time(first_activity,unit)},
            {"scanned_range",{{"begin",args.value("time_range",Json::object())
                    .contains("begin") ? wf->format_time(t_begin,unit) : std::string("0ns")},
                {"end",args.value("time_range",Json::object()).contains("end")
                    ? wf->format_time(t_end,unit) : std::string("max")}}}};
        if (!sm.sample_point.empty()) summary["sample_point"] = sm.sample_point;
        merge_json(summary,completeness(scan_complete,truncated,
                                        stalls.size(),findings.size()));
        return {{"ok",true},{"summary",summary},{"data",{{"findings",findings}}}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_stall_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 15. axi.latency_outlier
// ═══════════════════════════════════════════════════════════════════
struct AxiLatencyOutlierHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.latency_outlier"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args, unit, format, scan_err)) return scan_err;
        const std::string direction = args.value("direction", "all");
        const std::string method = args.value("method", "top_n");
        std::vector<std::pair<const AxiTransaction*, uint64_t>> latencies;
        for (size_t i = 0; i < result.transactions.size(); i++) {
            auto& txn = result.transactions[i];
            if (!txn.complete || (direction != "all" &&
                ((direction == "write") != txn.is_write))) continue;
            uint64_t lat = (txn.end_time > txn.start_time) ? (txn.end_time - txn.start_time) : 0;
            latencies.emplace_back(&txn, lat);
        }
        std::stable_sort(latencies.begin(), latencies.end(),
            [](const auto& left, const auto& right) {
                if (left.second != right.second) return left.second > right.second;
                return left.first->start_time < right.first->start_time;
            });
        uint64_t threshold_ticks = 0;
        if (method == "threshold") {
            std::string message;
            if (!wf->parse_time(args.at("threshold"),threshold_ticks,message))
                return make_error("INVALID_TIME",message);
        }
        const size_t top_n = args.value("top_n",10u);
        const size_t line_limit = args.value("line_limit",1000u);
        Json outliers = Json::array();
        size_t selected = 0;
        for (const auto& item : latencies) {
            if (method == "threshold" && item.second <= threshold_ticks) continue;
            if (method == "top_n" && selected >= top_n) break;
            ++selected;
            if (outliers.size() < line_limit)
                outliers.push_back(axi_txn_json(*item.first,*wf,unit,format,true));
        }
        const bool truncated = outliers.size() < selected;
        const Json requested_range = args.value("time_range",Json::object());
        Json summary{{"name",args.at("name")},
            {"begin",requested_range.contains("begin")
                ? wf->format_time(t_begin,unit) : std::string("0ns")},
            {"end",requested_range.contains("end")
                ? wf->format_time(t_end,unit) : std::string("max")},
            {"candidate_count",latencies.size()}};
        merge_json(summary,completeness(result.complete,truncated,
                                        selected,outliers.size()));
        summary["value_width_complete"] = true;
        summary["width_diagnostics"] = Json::array();
        Json data{{"method",method},{"classification",method == "top_n"
            ? "slowest_ranking" : "threshold_exceeded"},{"outliers",outliers}};
        if (method == "top_n") data["top_n"] = top_n;
        else data["threshold"] = args.at("threshold");
        return {{"ok",true},{"summary",summary},{"data",data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_latency_outlier_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 16. axi.outstanding_timeline
// ═══════════════════════════════════════════════════════════════════
struct AxiOutstandingTimelineHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.outstanding_timeline"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args,unit,format,scan_err)) return scan_err;
        const std::string direction = args.value("direction","all");
        struct Delta { int read = 0; int write = 0; };
        std::map<uint64_t,Delta> deltas;
        for (const auto& txn : result.transactions) {
            if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
            (txn.is_write ? deltas[txn.start_time].write : deltas[txn.start_time].read)++;
            if (txn.complete)
                (txn.is_write ? deltas[txn.end_time].write : deltas[txn.end_time].read)--;
        }
        for (auto iterator = deltas.begin(); iterator != deltas.end();) {
            if (iterator->second.read == 0 && iterator->second.write == 0)
                iterator = deltas.erase(iterator);
            else
                ++iterator;
        }
        int read = 0, write = 0, peak_read = 0, peak_write = 0;
        uint64_t peak_read_time = t_begin, peak_write_time = t_begin;
        uint64_t first_nonzero = t_begin; bool has_nonzero = false;
        Json points = Json::array();
        const size_t limit = args.value("line_limit",1000u);
        const auto clock_edges = selected_clock_edges(*wf,sm,t_begin,t_end);
        size_t active_sample_count = 0;
        uint64_t first_active_time = t_begin;
        const uint32_t reset_ref = wf->find_signal(sm.aresetn);
        const auto point = axi_point(sm);
        for (uint32_t ti : clock_edges) {
            const std::string reset = read_signal_at(*wf,reset_ref,ti,point);
            if (!known_binary(reset) ||
                (sm.reset_polarity == "active_low"
                    ? !known_high(reset) : known_high(reset))) continue;
            if (active_sample_count++ == 0) first_active_time = wf->time_at(ti);
        }
        // The original xdebug exposes an initial zero-valued point for the
        // XAMBA "before" sampling contract, but not for the legacy "after"
        // contract.  An empty requested range must remain an empty timeline.
        const bool include_initial_point =
            sm.sample_point == "before" && !deltas.empty();
        if (include_initial_point && points.size() < limit) points.push_back({
            {"time",wf->format_time(first_active_time,unit)},
            {"read",0},{"write",0},{"read_delta",0},{"write_delta",0},
            {"read_event","none"},{"write_event","none"}});
        for (const auto& item : deltas) {
            read += item.second.read; write += item.second.write;
            if (!has_nonzero && (read || write)) { first_nonzero = item.first; has_nonzero = true; }
            if (read > peak_read) { peak_read = read; peak_read_time = item.first; }
            if (write > peak_write) { peak_write = write; peak_write_time = item.first; }
            if (points.size() < limit) points.push_back({{"time",wf->format_time(item.first,unit)},
                {"read",read},{"write",write},{"read_delta",item.second.read},
                {"write_delta",item.second.write},{"read_event",item.second.read > 0
                    ? "ar_handshake" : item.second.read < 0 ? "rlast_handshake" : "none"},
                {"write_event",item.second.write > 0 ? "aw_handshake"
                    : item.second.write < 0 ? "b_handshake" : "none"}});
        }
        const size_t total_points = deltas.size() + (include_initial_point ? 1 : 0);
        const bool truncated = points.size() < total_points;
        const Json requested_time = args.value("time_range",Json::object());
        Json summary{{"name",args.at("name")},{"sampling_mode","clock_edge"},
            {"clock",sm.aclk},{"edge",sm.edge},{"sample_time_semantics","time is sample_time"},
            {"sample_count",active_sample_count},
            {"peak_read",peak_read},{"peak_write",peak_write},
            {"peak_read_time",wf->format_time(peak_read_time,unit)},
            {"peak_write_time",wf->format_time(peak_write_time,unit)},
            {"first_nonzero_time",wf->format_time(first_nonzero,unit)},
            {"final_read",read},{"final_write",write},
            {"requested_range",{{"begin",requested_time.contains("begin")
                    ? wf->format_time(t_begin,unit) : std::string("0ns")},
                {"end",requested_time.contains("end")
                    ? wf->format_time(t_end,unit) : std::string("max")}}}};
        if (!sm.sample_point.empty()) summary["sample_point"] = sm.sample_point;
        merge_json(summary,completeness(result.complete,truncated,
                                        total_points,points.size()));
        if (truncated)
            summary["truncation_scopes"] = Json::array({
                sm.sample_point == "before"
                    ? "response_change_points" : "response_transactions"});
        return {{"ok",true},{"summary",summary},{"data",{{"change_points",points}}}};
    }

    std::string render_xout(const Json& response) const override {
        return render_axi_outstanding_xout(response);
    }
};

// ═══════════════════════════════════════════════════════════════════
// 17. axi.request_response_pair
// ═══════════════════════════════════════════════════════════════════
struct AxiRequestResponsePairHandler : public EngineActionHandler {
    const char* action_name() const override { return "axi.request_response_pair"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = require_waveform(action_name());
        if (!err.is_null()) return err;

        auto args = req.value("args", Json::object());
        auto* wf = engine_globals().waveform.get();

        AxiSignalMap sm;
        Json cfg_err;
        if (!resolve_axi_config(args, sm, cfg_err, action_name())) return cfg_err;

        uint64_t t_begin, t_end;
        Json scan_err;
        if (!parse_time_range(args, *wf, t_begin, t_end, scan_err)) return scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;
        TimeRenderUnit unit; ValueRenderFormat format;
        if (!parse_render(args,unit,format,scan_err)) return scan_err;
        const std::string direction = args.value("direction","all");
        const size_t limit = args.value("line_limit",1000u);
        size_t total = 0, incomplete_write = 0, incomplete_read = 0;
        std::vector<const AxiTransaction*> ordered;
        ordered.reserve(result.transactions.size());
        for (const auto& txn : result.transactions) ordered.push_back(&txn);
        // Match the original range-query projection: canonical transactions
        // enter in address/sequence order, then the context list is sorted by
        // match_time only.  Equal-time ordering is therefore the deterministic
        // std::sort tie permutation, not an invented direction preference.
        std::sort(ordered.begin(),ordered.end(),
            [](const AxiTransaction* left, const AxiTransaction* right) {
                return left->start_time < right->start_time;
            });
        Json transactions = Json::array();
        for (const AxiTransaction* txn_pointer : ordered) {
            const auto& txn = *txn_pointer;
            if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
            if (!txn.complete) { txn.is_write ? ++incomplete_write : ++incomplete_read; continue; }
            ++total;
            if (transactions.size() < limit)
                transactions.push_back(axi_txn_json(txn,*wf,unit,format,true));
        }
        const bool truncated = transactions.size() < total;
        const Json requested_range = args.value("time_range",Json::object());
        Json summary{{"name",args.at("name")},
            {"begin",requested_range.contains("begin")
                ? wf->format_time(t_begin,unit) : std::string("0ns")},
            {"end",requested_range.contains("end")
                ? wf->format_time(t_end,unit) : std::string("max")}};
        merge_json(summary,completeness(result.complete,truncated,
                                        total,transactions.size()));
        summary["value_width_complete"] = true;
        summary["width_diagnostics"] = Json::array();
        Json diagnostics{{"full_scan_count",1},{"incomplete_write_count",incomplete_write},
            {"incomplete_read_count",incomplete_read},{"buffered_w_beat_count",0},
            {"buffered_w_burst_count",0},{"orphan_w_beat_count",0},{"orphan_b_count",0},
            {"orphan_r_beat_count",0},{"response_dependency_violation_count",0}};
        Json pairing{{"write_data","AXI4 W bursts bind in AW acceptance order"},
            {"write_response","BID binds to the oldest data-complete AW with the same ID"},
            {"read_response","RID binds to the oldest AR with the same ID"}};
        return {{"ok",true},{"summary",summary},{"data",{{"pairing_rule",pairing},
            {"diagnostics",diagnostics},{"transactions",transactions}}}};
    }
};

// ═══════════════════════════════════════════════════════════════════
// Factory functions
// ═══════════════════════════════════════════════════════════════════
std::unique_ptr<EngineActionHandler> make_axi_config_list_handler() {
    return std::make_unique<AxiConfigListHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_config_load_handler() {
    return std::make_unique<AxiConfigLoadHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_query_handler() {
    return std::make_unique<AxiQueryHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_analysis_handler() {
    return std::make_unique<AxiAnalysisHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_export_handler() {
    return std::make_unique<AxiExportHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_statistics_handler() {
    return std::make_unique<AxiStatisticsHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_transaction_cursor_handler() {
    return std::make_unique<AxiTransactionCursorHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_channel_stall_handler() {
    return std::make_unique<AxiChannelStallHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_latency_outlier_handler() {
    return std::make_unique<AxiLatencyOutlierHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_outstanding_timeline_handler() {
    return std::make_unique<AxiOutstandingTimelineHandler>();
}
std::unique_ptr<EngineActionHandler> make_axi_request_response_pair_handler() {
    return std::make_unique<AxiRequestResponsePairHandler>();
}

} // namespace xdebug_fst
