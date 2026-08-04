// axi_actions.cpp — AXI protocol analysis handlers (BSD-3-Clause)
// Implements: axi.config.list, axi.config.load, axi.query,
//   axi.analysis, axi.export, axi.statistics, axi.transaction.cursor,
//   axi.channel_stall, axi.latency_outlier, axi.outstanding_timeline,
//   axi.request_response_pair
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "api/json_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {

// ═══════════════════════════════════════════════════════════════════
// AXI config helpers
// ═══════════════════════════════════════════════════════════════════

struct AxiSignalMap {
    std::string aclk, aresetn;
    // Write Address
    std::string awid, awaddr, awlen, awvalid, awready;
    // Write Data
    std::string wdata, wlast, wvalid, wready;
    // Write Response
    std::string bid, bvalid, bready;
    // Read Address
    std::string arid, araddr, arlen, arvalid, arready;
    // Read Data
    std::string rid, rdata, rlast, rvalid, rready;
};

static AxiSignalMap default_axi_signals() {
    return {
        "TOP.clk", "TOP.rst_n",
        "TOP.awid", "TOP.awaddr", "TOP.awlen", "TOP.awvalid", "TOP.awready",
        "TOP.wdata", "TOP.wlast", "TOP.wvalid", "TOP.wready",
        "TOP.bid", "TOP.bvalid", "TOP.bready",
        "TOP.arid", "TOP.araddr", "TOP.arlen", "TOP.arvalid", "TOP.arready",
        "TOP.rid", "TOP.rdata", "TOP.rlast", "TOP.rvalid", "TOP.rready"
    };
}

static Json axi_signal_map_json(const AxiSignalMap& m) {
    return Json{
        {"aclk", m.aclk}, {"aresetn", m.aresetn},
        {"awid", m.awid}, {"awaddr", m.awaddr}, {"awlen", m.awlen},
        {"awvalid", m.awvalid}, {"awready", m.awready},
        {"wdata", m.wdata}, {"wlast", m.wlast},
        {"wvalid", m.wvalid}, {"wready", m.wready},
        {"bid", m.bid}, {"bvalid", m.bvalid}, {"bready", m.bready},
        {"arid", m.arid}, {"araddr", m.araddr}, {"arlen", m.arlen},
        {"arvalid", m.arvalid}, {"arready", m.arready},
        {"rid", m.rid}, {"rdata", m.rdata}, {"rlast", m.rlast},
        {"rvalid", m.rvalid}, {"rready", m.rready}
    };
}

static AxiSignalMap parse_axi_signal_map(const Json& j, const AxiSignalMap& fallback) {
    AxiSignalMap m = fallback;
    if (!j.is_object()) return m;
    auto set_str = [&](const char* key, std::string& field) {
        if (j.contains(key)) field = j[key].get<std::string>();
    };
    set_str("aclk", m.aclk); set_str("aresetn", m.aresetn);
    set_str("awid", m.awid); set_str("awaddr", m.awaddr); set_str("awlen", m.awlen);
    set_str("awvalid", m.awvalid); set_str("awready", m.awready);
    set_str("wdata", m.wdata); set_str("wlast", m.wlast);
    set_str("wvalid", m.wvalid); set_str("wready", m.wready);
    set_str("bid", m.bid); set_str("bvalid", m.bvalid); set_str("bready", m.bready);
    set_str("arid", m.arid); set_str("araddr", m.araddr); set_str("arlen", m.arlen);
    set_str("arvalid", m.arvalid); set_str("arready", m.arready);
    set_str("rid", m.rid); set_str("rdata", m.rdata); set_str("rlast", m.rlast);
    set_str("rvalid", m.rvalid); set_str("rready", m.rready);
    return m;
}

static bool resolve_axi_config(const Json& args, AxiSignalMap& sm, Json& out_err) {
    sm = default_axi_signals();
    if (args.contains("config")) {
        if (args["config"].is_string()) {
            std::string name = args["config"].get<std::string>();
            if (name != "default") {
                out_err = Json{{"ok", false},
                    {"error", {{"code", "CONFIG_NOT_FOUND"},
                               {"message", "AXI config not found: " + name}}}};
                return false;
            }
        } else if (args["config"].is_object()) {
            sm = parse_axi_signal_map(args["config"], sm);
        }
    }
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
    std::string data;
    std::string resp;
    bool last = false;
};

struct AxiTransaction {
    size_t index = 0;
    bool is_write = false;
    std::string id;
    std::string address;
    std::string length;
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
};

// Helper: read a signal's bit string at a time index
static std::string read_signal_at(IWaveformBackend& wf, uint32_t ref, uint32_t ti) {
    if (ref == IWaveformBackend::kInvalidSignalRef) return "0";
    IWaveformBackend::SignalOffset off;
    if (!wf.signal_offset_at(ref, ti, off)) return "0";
    return wf.signal_value_str(ref, off.start, 0);
}

// Collect handshake events on a valid/ready pair within time range.
// When valid=1 AND ready=1, record an event at that time.
static void scan_channel_handshakes(
    IWaveformBackend& wf,
    uint32_t ref_valid, uint32_t ref_ready,
    const std::string& channel,
    uint64_t t_begin, uint64_t t_end,
    std::vector<AxiHandshakeEvent>& events)
{
    if (!ref_valid || !ref_ready) return;

    // Merge change points from valid and ready
    std::set<uint32_t> ti_set;
    for (uint32_t ti : wf.time_indices_of(ref_valid)) {
        uint64_t t = wf.time_at(ti);
        if (t >= t_begin && t <= t_end) ti_set.insert(ti);
    }
    for (uint32_t ti : wf.time_indices_of(ref_ready)) {
        uint64_t t = wf.time_at(ti);
        if (t >= t_begin && t <= t_end) ti_set.insert(ti);
    }
    // Add boundaries
    uint32_t ti_begin = wf.time_idx_of(t_begin);
    if (ti_begin > 0) ti_set.insert(ti_begin);

    std::vector<uint32_t> tis(ti_set.begin(), ti_set.end());
    for (uint32_t ti : tis) {
        std::string v = read_signal_at(wf, ref_valid, ti);
        std::string r = read_signal_at(wf, ref_ready, ti);
        bool valid_high = (!v.empty() && v.back() == '1');
        bool ready_high = (!r.empty() && r.back() == '1');
        if (valid_high && ready_high) {
            AxiHandshakeEvent ev;
            ev.channel = channel;
            ev.time = wf.time_at(ti);
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
    if (!ref_id) return;
    wf.load_signals({ref_id, ref_addr, ref_len});
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx);
        ev.addr = read_signal_at(wf, ref_addr, ev.time_idx);
        ev.len = read_signal_at(wf, ref_len, ev.time_idx);
    }
}

static void augment_w_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_data = wf.find_signal(sm.wdata);
    uint32_t ref_last = wf.find_signal(sm.wlast);
    if (!ref_data) return;
    wf.load_signals({ref_data, ref_last});
    for (auto& ev : events) {
        ev.data = read_signal_at(wf, ref_data, ev.time_idx);
        if (ref_last) {
            std::string lv = read_signal_at(wf, ref_last, ev.time_idx);
            ev.last = (!lv.empty() && lv.back() == '1');
        }
    }
}

static void augment_b_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.bid);
    if (ref_id == IWaveformBackend::kInvalidSignalRef) return;
    wf.load_signals({ref_id});
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx);
    }
}

static void augment_ar_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.arid);
    uint32_t ref_addr = wf.find_signal(sm.araddr);
    uint32_t ref_len = wf.find_signal(sm.arlen);
    if (!ref_id) return;
    wf.load_signals({ref_id, ref_addr, ref_len});
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx);
        ev.addr = read_signal_at(wf, ref_addr, ev.time_idx);
        ev.len = read_signal_at(wf, ref_len, ev.time_idx);
    }
}

static void augment_r_events(IWaveformBackend& wf,
    const AxiSignalMap& sm, std::vector<AxiHandshakeEvent>& events)
{
    uint32_t ref_id = wf.find_signal(sm.rid);
    uint32_t ref_data = wf.find_signal(sm.rdata);
    uint32_t ref_last = wf.find_signal(sm.rlast);
    if (!ref_id) return;
    wf.load_signals({ref_id, ref_data, ref_last});
    for (auto& ev : events) {
        ev.id = read_signal_at(wf, ref_id, ev.time_idx);
        ev.data = read_signal_at(wf, ref_data, ev.time_idx);
        if (ref_last) {
            std::string lv = read_signal_at(wf, ref_last, ev.time_idx);
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
            txn.start_time = aw_events[pw.aw_idx].time;
            txn.start_time_idx = aw_events[pw.aw_idx].time_idx;
            txn.end_time = bev.time;
            txn.end_time_idx = bev.time_idx;
            txn.complete = true;
            txn.data_beats = std::move(pw.data_beats);
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
                    txn.start_time = arev.time;
                    txn.start_time_idx = arev.time_idx;
                    txn.end_time = pr.end_time;
                    txn.end_time_idx = pr.end_time_idx;
                    txn.complete = true;
                    txn.data_beats = std::move(pr.data_beats);
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

    // Scan each channel for handshakes
    scan_channel_handshakes(wf, ref_awvalid, ref_awready, "aw", t_begin, t_end, result.aw_events);
    scan_channel_handshakes(wf, ref_wvalid, ref_wready, "w", t_begin, t_end, result.w_events);
    scan_channel_handshakes(wf, ref_bvalid, ref_bready, "b", t_begin, t_end, result.b_events);
    scan_channel_handshakes(wf, ref_arvalid, ref_arready, "ar", t_begin, t_end, result.ar_events);
    scan_channel_handshakes(wf, ref_rvalid, ref_rready, "r", t_begin, t_end, result.r_events);

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

static Json axi_txn_json(const AxiTransaction& txn) {
    Json j;
    j["index"] = txn.index;
    j["direction"] = txn.is_write ? "write" : "read";

    // Render id/address/length using logic_value_json (consistent with APB pattern)
    if (!txn.id.empty()) {
        LogicValue id_lv = logic_value_from_bits(txn.id, static_cast<int>(txn.id.size()));
        j["id"] = logic_value_json(id_lv, ValueRenderFormat::Hex);
    } else {
        j["id"] = Json(nullptr);
    }
    if (!txn.address.empty()) {
        LogicValue addr_lv = logic_value_from_bits(txn.address, static_cast<int>(txn.address.size()));
        j["address"] = logic_value_json(addr_lv, ValueRenderFormat::Hex);
    } else {
        j["address"] = Json(nullptr);
    }
    if (!txn.length.empty()) {
        LogicValue len_lv = logic_value_from_bits(txn.length, static_cast<int>(txn.length.size()));
        j["length"] = logic_value_json(len_lv, ValueRenderFormat::Hex);
    } else {
        j["length"] = Json(nullptr);
    }

    j["start_time"] = txn.start_time;
    j["end_time"] = txn.end_time;
    j["start_time_idx"] = txn.start_time_idx;
    j["end_time_idx"] = txn.end_time_idx;
    j["burst_status"] = txn.complete ? "ok" : "incomplete";
    return j;
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

static void parse_time_range(const Json& args, IWaveformBackend& wf,
    uint64_t& t_begin, uint64_t& t_end)
{
    t_begin = 0;
    t_end = wf.max_time();
    if (args.contains("begin")) {
        auto& b = args["begin"];
        t_begin = b.is_number() ? b.get<uint64_t>() : std::stoull(b.get<std::string>());
    }
    if (args.contains("end")) {
        auto& e = args["end"];
        t_end = e.is_number() ? e.get<uint64_t>() : std::stoull(e.get<std::string>());
    }
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

        Json configs = Json::array();
        configs.push_back({
            {"name", "default"},
            {"signal_map", axi_signal_map_json(default_axi_signals())}
        });
        return Json{
            {"ok", true},
            {"data", {{"configs", configs}}}
        };
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

        auto args = req.value("args", Json::object());
        std::string name = args.value("name", "default");
        if (name == "default") {
            return Json{
                {"ok", true},
                {"data", {
                    {"name", "default"},
                    {"signal_map", axi_signal_map_json(default_axi_signals())}
                }}
            };
        }
        return make_error("CONFIG_NOT_FOUND", "AXI config not found: " + name);
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
        parse_time_range(args, *wf, t_begin, t_end);

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        Json tjson = Json::array();
        for (auto& txn : result.transactions) {
            tjson.push_back(axi_txn_json(txn));
        }

        return Json{
            {"ok", true},
            {"summary", {{"transaction_count", result.transactions.size()}}},
            {"data", {{"transactions", tjson}}}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        auto ev_to_json = [](const std::vector<AxiHandshakeEvent>& events) {
            Json arr = Json::array();
            for (auto& ev : events) arr.push_back(axi_event_json(ev));
            return arr;
        };

        Json txn_arr = Json::array();
        for (auto& txn : result.transactions) {
            txn_arr.push_back(axi_txn_json(txn));
        }

        return Json{
            {"ok", true},
            {"summary", {{"transaction_count", result.transactions.size()}}},
            {"data", {
                {"channels", {
                    {"aw", ev_to_json(result.aw_events)},
                    {"w", ev_to_json(result.w_events)},
                    {"b", ev_to_json(result.b_events)},
                    {"ar", ev_to_json(result.ar_events)},
                    {"r", ev_to_json(result.r_events)}
                }},
                {"transactions", txn_arr}
            }}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        Json tjson = Json::array();
        for (auto& txn : result.transactions) {
            tjson.push_back(axi_txn_json(txn));
        }

        return Json{
            {"ok", true},
            {"summary", {{"transaction_count", result.transactions.size()}}},
            {"data", {{"transactions", tjson}}}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        Json scan_err;
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

        return Json{
            {"ok", true},
            {"summary", {
                {"transaction_count", result.transactions.size()},
                {"read_count", read_count},
                {"write_count", write_count},
                {"avg_read_latency", avg_read_lat},
                {"avg_write_latency", avg_write_lat},
                {"min_latency", min_lat},
                {"max_latency", max_lat},
                {"outstanding_max", outstanding_max},
                {"error_count", error_count}
            }}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        size_t cursor = 0;
        if (args.contains("cursor")) {
            auto& c = args["cursor"];
            cursor = c.is_number() ? c.get<size_t>() : static_cast<size_t>(std::stoull(c.get<std::string>()));
        }

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        const size_t page_size = 50;
        Json tjson = Json::array();
        size_t i = cursor;
        while (i < result.transactions.size() && tjson.size() < page_size) {
            tjson.push_back(axi_txn_json(result.transactions[i]));
            i++;
        }
        bool has_more = (i < result.transactions.size());
        Json next_cursor = has_more ? Json(static_cast<uint64_t>(i)) : Json(nullptr);

        return Json{
            {"ok", true},
            {"summary", {{"transaction_count", result.transactions.size()}}},
            {"data", {
                {"transactions", tjson},
                {"next_cursor", next_cursor},
                {"has_more", has_more}
            }}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

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
        add_chan("aw", sm.awvalid, sm.awready);
        add_chan("w",  sm.wvalid,  sm.wready);
        add_chan("b",  sm.bvalid,  sm.bready);
        add_chan("ar", sm.arvalid, sm.arready);
        add_chan("r",  sm.rvalid,  sm.rready);

        Json stalls = Json::array();
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
                bool valid_high = (!vv.empty() && vv.back() == '1');
                bool ready_high = (!rv.empty() && rv.back() == '1');

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

        return Json{
            {"ok", true},
            {"summary", {{"stall_count", stalls.size()}}},
            {"data", {{"stalls", stalls}}}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        double threshold = args.value("threshold", 3.0);

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        // Compute latencies for complete transactions
        std::vector<std::pair<size_t, uint64_t>> latencies; // (txn_index, latency)
        double sum_lat = 0;
        for (size_t i = 0; i < result.transactions.size(); i++) {
            auto& txn = result.transactions[i];
            if (!txn.complete) continue;
            uint64_t lat = (txn.end_time > txn.start_time) ? (txn.end_time - txn.start_time) : 0;
            latencies.emplace_back(i, lat);
            sum_lat += lat;
        }
        double avg_latency = latencies.empty() ? 0 : sum_lat / latencies.size();
        double cutoff = avg_latency * threshold;

        Json outliers = Json::array();
        for (auto& p : latencies) {
            if (p.second > cutoff) {
                auto& txn = result.transactions[p.first];
                Json oj;
                oj["index"] = txn.index;
                oj["direction"] = txn.is_write ? "write" : "read";
                if (!txn.id.empty()) {
                    LogicValue id_lv = logic_value_from_bits(txn.id, static_cast<int>(txn.id.size()));
                    oj["id"] = logic_value_json(id_lv, ValueRenderFormat::Hex);
                } else {
                    oj["id"] = Json(nullptr);
                }
                if (!txn.address.empty()) {
                    LogicValue addr_lv = logic_value_from_bits(txn.address, static_cast<int>(txn.address.size()));
                    oj["address"] = logic_value_json(addr_lv, ValueRenderFormat::Hex);
                } else {
                    oj["address"] = Json(nullptr);
                }
                oj["latency"] = p.second;
                oj["avg_latency"] = avg_latency;
                outliers.push_back(oj);
            }
        }

        return Json{
            {"ok", true},
            {"summary", {
                {"outlier_count", outliers.size()},
                {"avg_latency", avg_latency}
            }},
            {"data", {{"outliers", outliers}}}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        // Build timeline: events at start (+1) and end (-1) of each transaction
        struct OSEvent { uint64_t time; uint32_t time_idx; int delta; };
        std::vector<OSEvent> os_events;
        for (auto& txn : result.transactions) {
            os_events.push_back({txn.start_time, txn.start_time_idx, 1});
            if (txn.complete && txn.end_time > 0)
                os_events.push_back({txn.end_time, txn.end_time_idx, -1});
        }
        std::sort(os_events.begin(), os_events.end(),
            [](const OSEvent& a, const OSEvent& b) { return a.time < b.time; });

        Json timeline = Json::array();
        size_t cur = 0;
        size_t max_os = 0;
        double sum_os = 0;
        size_t sample_count = 0;
        for (auto& ev : os_events) {
            cur += ev.delta;
            timeline.push_back({
                {"time", ev.time},
                {"time_idx", ev.time_idx},
                {"outstanding", cur}
            });
            if (cur > max_os) max_os = cur;
            sum_os += cur;
            sample_count++;
        }
        double avg_os = sample_count ? sum_os / sample_count : 0;

        return Json{
            {"ok", true},
            {"summary", {
                {"max_outstanding", max_os},
                {"avg_outstanding", avg_os}
            }},
            {"data", {{"timeline", timeline}}}
        };
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
        parse_time_range(args, *wf, t_begin, t_end);

        std::string filter_id;
        if (args.contains("id")) {
            filter_id = args["id"].get<std::string>();
        }

        Json scan_err;
        auto result = scan_axi(*wf, sm, t_begin, t_end, scan_err);
        if (!scan_err.is_null()) return scan_err;

        Json pairs = Json::array();
        for (auto& txn : result.transactions) {
            if (!txn.complete) continue;
            if (!filter_id.empty() && txn.id != filter_id) continue;

            Json req_j;
            req_j["direction"] = txn.is_write ? "write" : "read";
            if (!txn.id.empty()) {
                LogicValue id_lv = logic_value_from_bits(txn.id, static_cast<int>(txn.id.size()));
                req_j["id"] = logic_value_json(id_lv, ValueRenderFormat::Hex);
            } else {
                req_j["id"] = Json(nullptr);
            }
            if (!txn.address.empty()) {
                LogicValue addr_lv = logic_value_from_bits(txn.address, static_cast<int>(txn.address.size()));
                req_j["address"] = logic_value_json(addr_lv, ValueRenderFormat::Hex);
            } else {
                req_j["address"] = Json(nullptr);
            }
            if (!txn.length.empty()) {
                LogicValue len_lv = logic_value_from_bits(txn.length, static_cast<int>(txn.length.size()));
                req_j["length"] = logic_value_json(len_lv, ValueRenderFormat::Hex);
            } else {
                req_j["length"] = Json(nullptr);
            }
            req_j["time"] = txn.start_time;
            req_j["time_idx"] = txn.start_time_idx;

            Json resp_j;
            resp_j["time"] = txn.end_time;
            resp_j["time_idx"] = txn.end_time_idx;

            uint64_t lat = (txn.end_time > txn.start_time) ? (txn.end_time - txn.start_time) : 0;
            pairs.push_back({
                {"request", req_j},
                {"response", resp_j},
                {"latency", lat}
            });
        }

        return Json{
            {"ok", true},
            {"summary", {{"pair_count", pairs.size()}}},
            {"data", {{"pairs", pairs}}}
        };
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
