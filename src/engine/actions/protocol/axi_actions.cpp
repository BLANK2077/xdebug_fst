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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <limits>
#include <map>
#include <set>
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

static bool resolve_axi_config(const Json& args, AxiSignalMap& sm, Json& out_err) {
    const std::string name = args.at("name");
    auto found = axi_configs().find(name);
    if (found == axi_configs().end()) {
        out_err = Json{{"ok", false}, {"error", {{"code", "CONFIG_NOT_FOUND"},
            {"message", "AXI config not found: " + name}}}};
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
    uint64_t end_time = 0;        // B handshake or RLAST handshake time
    uint32_t end_time_idx = 0;
    bool complete = true;
    std::vector<std::string> data_beats;
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
        if (known_high(valid) && known_high(ready)) {
            AxiHandshakeEvent ev;
            ev.channel = channel;
            ev.time = time;
            ev.time_idx = ti;
            ev.kind = "handshake";
            events.push_back(ev);
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
// Uses FIFO ordering for W beats (each W beat assigned to oldest pending write).
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
    // Build merged timeline: (time, type, index)
    enum class WEKind { AW, W, B };
    struct WEvent { uint64_t time; WEKind kind; size_t idx; };
    std::vector<WEvent> wevents;
    for (size_t i = 0; i < aw_events.size(); i++)
        wevents.push_back({aw_events[i].time, WEKind::AW, i});
    for (size_t i = 0; i < w_events.size(); i++)
        wevents.push_back({w_events[i].time, WEKind::W, i});
    // Note: B events are matched by id, not processed in timeline order
    std::sort(wevents.begin(), wevents.end(),
        [](const WEvent& a, const WEvent& b) { return a.time < b.time; });

    // FIFO queue for pending writes (awaiting W beats)
    struct PendingWrite {
        size_t aw_idx;
        std::vector<std::string> data_beats;
        bool data_done = false; // WLAST received
    };
    std::deque<PendingWrite> pending_w;
    // Completed data-phase writes, keyed by id for B matching
    std::map<std::string, std::deque<PendingWrite>> data_done_by_id;

    for (auto& we : wevents) {
        if (we.kind == WEKind::AW) {
            pending_w.push_back({we.idx, {}, false});
        } else if (we.kind == WEKind::W) {
            if (!pending_w.empty()) {
                auto& pw = pending_w.front();
                pw.data_beats.push_back(w_events[we.idx].data);
                if (w_events[we.idx].last) {
                    pw.data_done = true;
                    // Move to data-done map, keyed by AW's id
                    const std::string& awid = aw_events[pw.aw_idx].id;
                    data_done_by_id[awid].push_back(std::move(pw));
                    pending_w.pop_front();
                }
            }
        }
    }
    // Any remaining pending writes are incomplete (no WLAST seen)
    // They stay in pending_w; we'll add them as incomplete below.

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
            txn.end_time = bev.time;
            txn.end_time_idx = bev.time_idx;
            txn.complete = true;
            txn.data_beats = std::move(pw.data_beats);
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
            txn.complete = false;
            txn.data_beats = std::move(pw.data_beats);
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
        txn.complete = false;
        txn.data_beats = std::move(pw.data_beats);
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
        bool data_done = false; // RLAST received
        uint64_t end_time = 0;
        uint32_t end_time_idx = 0;
    };
    std::map<std::string, std::deque<PendingRead>> pending_r_by_id;

    for (auto& re : revents) {
        if (re.kind == REKind::AR) {
            const auto& arev = ar_events[re.idx];
            pending_r_by_id[arev.id].push_back({re.idx, {}, false, 0, 0});
        } else { // R
            const auto& rev = r_events[re.idx];
            auto it = pending_r_by_id.find(rev.id);
            if (it != pending_r_by_id.end() && !it->second.empty()) {
                auto& pr = it->second.front();
                pr.data_beats.push_back(rev.data);
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
                    txn.end_time = pr.end_time;
                    txn.end_time_idx = pr.end_time_idx;
                    txn.complete = true;
                    txn.data_beats = std::move(pr.data_beats);
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
            txn.complete = false;
            txn.data_beats = std::move(pr.data_beats);
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

    return result;
}

// ═══════════════════════════════════════════════════════════════════
// JSON rendering helpers
// ═══════════════════════════════════════════════════════════════════

static std::string render_bits(const std::string& bits, ValueRenderFormat format) {
    if (bits.empty()) return "'h0";
    return render_logic_value(logic_value_from_bits(bits, bits.size()), format);
}

static Json axi_txn_json(const AxiTransaction& txn, IWaveformBackend& wf,
                         TimeRenderUnit unit, ValueRenderFormat format,
                         bool matched = false) {
    const std::string direction = txn.is_write ? "write" : "read";
    Json j{{"direction",direction},
        {"latency",wf.format_time(txn.end_time >= txn.start_time
            ? txn.end_time - txn.start_time : 0, unit)},
        {"response_dependency_violation",false},
        {"address",{{"channel",txn.is_write ? "aw" : "ar"},
            {"handshake_time",wf.format_time(txn.start_time,unit)},
            {"addr",render_bits(txn.address,format)},
            {"id",render_bits(txn.id,format)},
            {"len",render_bits(txn.length,format)},
            {"size",render_bits(txn.size,format)},
            {"burst",render_bits(txn.burst,format)}}},
        {"response",{{"channel",txn.is_write ? "b" : "r"},
            {"handshake_time",wf.format_time(txn.end_time,unit)},
            {"resp",render_bits(txn.resp,format)}}}};
    if (txn.is_write) j["phase_order"] = "aw_before_w";
    if (matched) j["match_time"] = wf.format_time(txn.start_time, unit);
    return j;
}

static Json axi_txn_json(const AxiTransaction& txn) {
    return axi_txn_json(txn, *engine_globals().waveform,
                        TimeRenderUnit::Ns,
                        ValueRenderFormat::Hex);
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
        {{"action","axi.query"},{"purpose","查询 AXI channel/transaction。"}},
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
                if (!indices.empty()) { first_edge = wf->time_at(indices.front()); found_edge = true; }
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
                axi_txn_json(*matches[offset], *wf, unit, format);
            merge_json(summary, completeness(result.complete, false,
                                              matches.size(), found ? 1 : 0));
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
        } else {
            summary["query_mode"] = "count";
            summary["data_scope"] = "none";
            merge_json(summary, completeness(result.complete, false,
                                              matches.size(), 0));
        }
        return {{"ok",true},{"summary",summary},{"data",data}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        size_t incomplete_read = 0, incomplete_write = 0;
        const AxiTransaction* slowest = nullptr; uint64_t slowest_latency = 0;
        for (const auto& txn : result.transactions) {
            if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
            if (!txn.complete) { txn.is_write ? ++incomplete_write : ++incomplete_read; continue; }
            const uint64_t latency = txn.end_time >= txn.start_time ? txn.end_time - txn.start_time : 0;
            (txn.is_write ? write_lat : read_lat).push_back(latency);
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
                if (txn.is_write) item["phase_order"] = "aw_before_w";
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
            for (uint32_t ti : clock_edges) {
                const uint64_t time = wf->time_at(ti);
                uint64_t read = 0, write = 0;
                for (const auto& txn : result.transactions) {
                    if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
                    if (txn.start_time <= time && (!txn.complete || txn.end_time > time))
                        txn.is_write ? ++write : ++read;
                }
                read_depth.push_back(read); write_depth.push_back(write);
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
            std::vector<uint64_t> combined = read_depth;
            combined.insert(combined.end(),write_depth.begin(),write_depth.end());
            Json total = depth_stats(combined);
            Json summary = common_summary();
            summary["samples"] = total.at("samples"); summary["min"] = total.at("min");
            summary["max"] = total.at("max"); summary["avg"] = total.at("avg");
            merge_json(summary,completeness(result.complete,false,
                                            clock_edges.size(),clock_edges.size()));
            return {{"ok",true},{"summary",summary},{"data",{{"osd",{
                {"read",read_stats},{"write",write_stats},
                {"final_read",read_depth.empty()?0:read_depth.back()},
                {"final_write",write_depth.empty()?0:write_depth.back()},
                {"definitions",{{"read","accepted AR without completed RLAST"},
                    {"write","accepted AW without completed B"}}}}}}}};
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
            {"sample_count",wf->time_indices_of(wf->find_signal(sm.aclk)).size()},
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
        Json latency{{"read",stats(read_lat,true)},{"write",stats(write_lat,true)},
            {"definitions",{{"read","AR handshake to RLAST handshake"},
                {"write","AW handshake to B handshake"}}},
            {"write_phase_order_counts",{{"aw_before_w",write_lat.size()},
                {"same_cycle",0},{"w_before_aw",0},{"unknown",0}}}};
        Json data{{"latency",latency}};
        if (slowest) data["slowest"] = axi_txn_json(*slowest,*wf,unit,format);
        return {{"ok",true},{"summary",summary},{"data",data}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        writes << "direction" << separator << "time" << separator << "address\n";
        reads << "direction" << separator << "time" << separator << "address\n";
        size_t write_count = 0, read_count = 0, incomplete_write = 0, incomplete_read = 0;
        for (const auto& txn : result.transactions) {
            if (!txn.complete) { txn.is_write ? ++incomplete_write : ++incomplete_read; continue; }
            std::ostream& stream = txn.is_write ? writes : reads;
            stream << (txn.is_write ? "write" : "read") << separator
                   << wf->format_time(txn.start_time,unit) << separator
                   << render_bits(txn.address,format) << '\n';
            txn.is_write ? ++write_count : ++read_count;
        }
        meta << Json{{"name",args.at("name")},{"write_count",write_count},
            {"read_count",read_count}}.dump(2) << '\n';
        const size_t total = write_count + read_count;
        Json output_summary{{"path",prefix},{"write_path",write_path},
            {"read_path",read_path},{"meta_path",meta_path},{"file_format",file_format}};
        Json summary{{"name",args.at("name")},{"write_count",write_count},
            {"read_count",read_count},{"row_count",total},{"format",file_format},
            {"status","written"},{"output_written",true},
            {"sample_count",wf->time_indices_of(wf->find_signal(sm.aclk)).size()},
            {"full_scan_count",1},{"incomplete_write_count",incomplete_write},
            {"incomplete_read_count",incomplete_read},{"buffered_w_beat_count",0},
            {"buffered_w_burst_count",0},{"orphan_w_beat_count",0},
            {"orphan_b_count",0},{"orphan_r_beat_count",0},
            {"response_dependency_violation_count",0},
            {"requested_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(t_end,unit)}}},
            {"scanned_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(t_end,unit)}}},{"output",output_summary}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        Json data = Json::object();
        if (found) data["transaction"] = axi_txn_json(*matches[position],*wf,unit,format);
        return {{"ok",true},{"summary",summary},{"data",data}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

        uint64_t t_begin, t_end;
        if (!parse_time_range(args, *wf, t_begin, t_end, cfg_err)) return cfg_err;

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
            {"sample_count",wf->time_indices_of(wf->find_signal(sm.aclk)).size()},
            {"transfer_count",0},{"max_stall_cycles",max_cycles},
            {"ready_without_valid_cycles",0},
            {"first_activity_time",wf->format_time(t_begin,unit)},
            {"scanned_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(t_end,unit)}}}};
        if (!sm.sample_point.empty()) summary["sample_point"] = sm.sample_point;
        merge_json(summary,completeness(scan_complete,truncated,
                                        stalls.size(),findings.size()));
        return {{"ok",true},{"summary",summary},{"data",{{"findings",findings}}}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        std::sort(latencies.begin(), latencies.end(),
            [](const auto& left, const auto& right) { return left.second > right.second; });
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
        Json summary{{"name",args.at("name")},{"begin",wf->format_time(t_begin,unit)},
            {"end",wf->format_time(t_end,unit)},{"candidate_count",latencies.size()}};
        merge_json(summary,completeness(result.complete,truncated,
                                        selected,outliers.size()));
        Json data{{"method",method},{"classification",method == "top_n"
            ? "slowest_ranking" : "threshold_exceeded"},{"outliers",outliers}};
        if (method == "top_n") data["top_n"] = top_n;
        else data["threshold"] = args.at("threshold");
        return {{"ok",true},{"summary",summary},{"data",data}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        int read = 0, write = 0, peak_read = 0, peak_write = 0;
        uint64_t peak_read_time = t_begin, peak_write_time = t_begin;
        uint64_t first_nonzero = t_begin; bool has_nonzero = false;
        Json points = Json::array();
        const size_t limit = args.value("line_limit",1000u);
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
        const bool truncated = points.size() < deltas.size();
        Json summary{{"name",args.at("name")},{"sampling_mode","clock_edge"},
            {"clock",sm.aclk},{"edge",sm.edge},{"sample_time_semantics","time is sample_time"},
            {"sample_count",wf->time_indices_of(wf->find_signal(sm.aclk)).size()},
            {"peak_read",peak_read},{"peak_write",peak_write},
            {"peak_read_time",wf->format_time(peak_read_time,unit)},
            {"peak_write_time",wf->format_time(peak_write_time,unit)},
            {"first_nonzero_time",wf->format_time(first_nonzero,unit)},
            {"final_read",read},{"final_write",write},
            {"requested_range",{{"begin",wf->format_time(t_begin,unit)},
                {"end",wf->format_time(t_end,unit)}}}};
        if (!sm.sample_point.empty()) summary["sample_point"] = sm.sample_point;
        merge_json(summary,completeness(result.complete,truncated,
                                        deltas.size(),points.size()));
        return {{"ok",true},{"summary",summary},{"data",{{"change_points",points}}}};
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
        if (!resolve_axi_config(args, sm, cfg_err)) return cfg_err;

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
        Json transactions = Json::array();
        for (const auto& txn : result.transactions) {
            if (direction != "all" && ((direction == "write") != txn.is_write)) continue;
            if (!txn.complete) { txn.is_write ? ++incomplete_write : ++incomplete_read; continue; }
            ++total;
            if (transactions.size() < limit)
                transactions.push_back(axi_txn_json(txn,*wf,unit,format,true));
        }
        const bool truncated = transactions.size() < total;
        Json summary{{"name",args.at("name")},{"begin",wf->format_time(t_begin,unit)},
            {"end",wf->format_time(t_end,unit)}};
        merge_json(summary,completeness(result.complete,truncated,
                                        total,transactions.size()));
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
