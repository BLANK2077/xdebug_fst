# test_protocol.py — APB/AXI protocol actions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import AXI_CONFIG, open_session
from runner import StdioLoopRunner


# ── APB ──

APB_CONFIG = {
    "clock": "top.pclk", "edge": "posedge", "sample_point": "after",
    "reset": {"signal": "top.presetn", "polarity": "active_low"},
    "paddr": "top.paddr", "psel": "top.psel",
    "penable": "top.penable", "pwrite": "top.pwrite",
    "pwdata": "top.pwdata", "prdata": "top.prdata",
    "pready": "top.pready", "pslverr": "top.pslverr",
}

WELLEN_APB_CONFIG = {
    "clock": "top.masslav_if.clk", "edge": "posedge",
    "sample_point": "after",
    "reset": {
        "signal": "top.masslav_if.Pslave_err", "polarity": "active_high",
    },
    "paddr": "top.masslav_if.Paddr",
    "psel": "top.masslav_if.Psel",
    "penable": "top.masslav_if.Penable",
    "pwrite": "top.masslav_if.Pwrite",
    "pwdata": "top.masslav_if.Pwdata",
    "prdata": "top.masslav_if.Prdata",
    "pready": "top.masslav_if.Pready",
    "pslverr": "top.masslav_if.Pslave_err",
}

WELLEN_AXI_CONFIG = {
    "clock": "top.masslav_if.clk", "edge": "posedge",
    "sample_point": "after",
    "reset": {
        "signal": "top.masslav_if.Pslave_err", "polarity": "active_high",
    },
    "awaddr": "top.masslav_if.Paddr",
    "awid": "top.masslav_if.Pslave_err",
    "awlen": "top.masslav_if.Pslave_err",
    "awsize": "top.masslav_if.Pslave_err",
    "awburst": "top.masslav_if.Pslave_err",
    "awvalid": "top.masslav_if.Psel",
    "awready": "top.masslav_if.Pready",
    "wdata": "top.masslav_if.Pwdata",
    "wstrb": "top.masslav_if.Paddr",
    "wlast": "top.masslav_if.Penable",
    "wvalid": "top.masslav_if.Penable",
    "wready": "top.masslav_if.Pready",
    "bid": "top.masslav_if.Pslave_err",
    "bresp": "top.masslav_if.Pslave_err",
    "bvalid": "top.masslav_if.Penable",
    "bready": "top.masslav_if.Pready",
    "araddr": "top.masslav_if.Paddr",
    "arid": "top.masslav_if.Pslave_err",
    "arlen": "top.masslav_if.Pslave_err",
    "arsize": "top.masslav_if.Pslave_err",
    "arburst": "top.masslav_if.Pslave_err",
    "arvalid": "top.masslav_if.Psel",
    "arready": "top.masslav_if.Pready",
    "rid": "top.masslav_if.Pslave_err",
    "rdata": "top.masslav_if.Prdata",
    "rresp": "top.masslav_if.Pslave_err",
    "rlast": "top.masslav_if.Penable",
    "rvalid": "top.masslav_if.Penable",
    "rready": "top.masslav_if.Pready",
}


def load_apb(loop_runner: StdioLoopRunner, apb_fst, name: str = "apb0") -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.config.load", args={
        "name": name, "config": APB_CONFIG})
    assert rsp.get("ok"), rsp

def test_apb_config_list(loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.config.list", args={})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["count"] == 1
    assert rsp["data"]["configs"][0]["name"] == "apb0"
    assert rsp["data"]["configs"][0]["sampling_mode"] == "clock_edge"

    named = loop_runner.request("apb.config.list", args={"name": "apb0"})
    assert named.get("ok"), named
    assert named["summary"] == {"name": "apb0", "status": "found"}
    assert named["data"]["config"]["clock"] == "top.pclk"


def test_apb_config_list_empty(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.config.list", args={})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {"count": 0}
    assert rsp["data"] == {"configs": []}


def test_apb_config_load(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.config.load", args={
        "name": "apb0", "config": APB_CONFIG})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {"name": "apb0", "status": "loaded"}
    assert rsp["data"]["config"]["psel"] == "top.psel"
    assert rsp["data"]["recommended_actions"][1]["action"] == "apb.query"


def test_apb_query(loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.query", args={
        "name": "apb0", "query": {"line_limit": 10},
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    transfers = rsp["data"]["transactions"]
    assert rsp["summary"]["total_count"] == 4
    assert rsp["summary"]["returned_count"] == 4
    # writes: addr=1 data=0xab; addr=3 data=0x5a; reads: addr=1, addr=3
    assert transfers[0]["is_write"] is True
    assert transfers[0]["addr"] == "8'h01"
    assert transfers[0]["data"] == "8'hab"
    assert transfers[1]["is_write"] is True
    assert transfers[1]["addr"] == "8'h03"
    assert transfers[1]["data"] == "8'h5a"
    assert transfers[2]["is_write"] is False
    assert transfers[2]["addr"] == "8'h01"
    assert transfers[3]["is_write"] is False
    assert transfers[3]["addr"] == "8'h03"
    for t in transfers:
        assert t["has_error"] is False


@pytest.mark.parametrize("address", [
    {"mode": "exact", "values": ["8'h01"]},
    {"mode": "range", "begin": "1", "end": "1"},
    {"mode": "mask", "value": "8'h01", "mask": "8'hff"},
])
def test_apb_query_address_modes(loop_runner: StdioLoopRunner, apb_fst,
                                 address) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.query", args={
        "name": "apb0", "direction": "read", "address": address,
        "query": {"line_limit": 10}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["query_mode"] == "list"
    assert rsp["summary"]["total_count"] == 1
    assert rsp["data"]["transactions"][0]["addr"] == "8'h01"


def test_apb_query_and_transfer_window_empty(
        loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    queried = loop_runner.request("apb.query", args={
        "name": "apb0",
        "address": {"mode": "exact", "values": ["8'hff"]},
        "query": {"line_limit": 10},
    })
    assert queried.get("ok"), queried
    assert queried["summary"]["total_count"] == 0
    assert queried["summary"]["returned_count"] == 0
    assert queried["data"]["transactions"] == []
    assert queried["data"]["filter"] == {
        "direction": "all",
        "address": {"mode": "exact", "values": ["8'hff"]},
    }

    window = loop_runner.request("apb.transfer_window", args={
        "name": "apb0",
        "time_range": {"begin": "0ps", "end": "10ps"},
        "line_limit": 10,
    })
    assert window.get("ok"), window
    assert window["summary"]["total_count"] == 0
    assert window["summary"]["returned_count"] == 0
    assert window["data"] == {"transactions": []}


def test_apb_query_count_index_last_and_truncation(
        loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    count = loop_runner.request("apb.query", args={"name": "apb0"})
    assert count.get("ok"), count
    assert count["summary"]["query_mode"] == "count"
    assert count["summary"]["total_count"] == 4
    assert "transactions" not in count["data"]

    indexed = loop_runner.request("apb.query", args={
        "name": "apb0", "query": {"index": 2}})
    assert indexed.get("ok"), indexed
    assert indexed["summary"]["found"] is True
    assert indexed["data"]["transaction"]["addr"] == "8'h03"

    last = loop_runner.request("apb.query", args={
        "name": "apb0", "direction": "read", "last": True})
    assert last.get("ok"), last
    assert last["summary"]["query_mode"] == "last"
    assert last["data"]["transaction"]["addr"] == "8'h03"

    limited = loop_runner.request("apb.query", args={
        "name": "apb0", "query": {"line_limit": 1}})
    assert limited.get("ok"), limited
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["total_count"] == 4
    assert limited["summary"]["returned_count"] == 1


def test_apb_statistics(loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.statistics", args={
        "name": "apb0", "filter": {"direction": "all"}})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["scanned_transaction_count"] == 4
    assert s["matched_transaction_count"] == 4
    assert s["matched_read_count"] == 2
    assert s["matched_write_count"] == 2
    assert s["analysis_quality"] == "complete"


def test_apb_statistics_address_filter(loop_runner: StdioLoopRunner,
                                       apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.statistics", args={
        "name": "apb0", "filter": {"direction": "write", "address": {
            "mode": "exact", "values": ["8'h03"]}}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["matched_transaction_count"] == 1
    assert rsp["summary"]["matched_write_count"] == 1
    assert rsp["summary"]["matched_read_count"] == 0
    assert rsp["summary"]["filter_applied"] is True


def test_apb_statistics_empty(loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.statistics", args={
        "name": "apb0",
        "filter": {
            "direction": "all",
            "address": {"mode": "exact", "values": ["8'hff"]},
        },
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["matched_transaction_count"] == 0
    assert rsp["summary"]["matched_read_count"] == 0
    assert rsp["summary"]["matched_write_count"] == 0


def test_apb_statistics_and_cursor_report_incomplete_raw_fst_scan(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    loaded = loop_runner.request("apb.config.load", args={
        "name": "wellen_apb", "config": WELLEN_APB_CONFIG,
    })
    assert loaded.get("ok"), loaded

    statistics = loop_runner.request("apb.statistics", args={
        "name": "wellen_apb", "filter": {"direction": "all"},
    })
    assert statistics.get("ok"), statistics
    assert statistics["summary"]["matched_transaction_count"] == 10
    assert statistics["summary"]["unresolved_transaction_count"] == 1
    assert statistics["summary"]["scan_complete"] is False
    assert statistics["summary"]["analysis_complete"] is False
    assert statistics["summary"]["truncation_scopes"] == [
        "analysis_transactions"
    ]

    cursor = loop_runner.request("apb.transaction.cursor", args={
        "name": "wellen_apb", "op": "begin", "direction": "all",
    })
    assert cursor.get("ok"), cursor
    assert cursor["summary"]["found"] is True
    assert cursor["summary"]["total_count"] == 10
    assert cursor["summary"]["scan_complete"] is False
    assert cursor["summary"]["analysis_complete"] is False
    assert cursor["summary"]["truncation_scopes"] == [
        "analysis_transactions"
    ]


def test_apb_transaction_cursor(loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.transaction.cursor", args={
        "name": "apb0", "op": "begin", "direction": "write"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["found"] is True
    assert rsp["summary"]["index"] == 1
    assert rsp["summary"]["total_count"] == 2
    assert rsp["data"]["transaction"]["is_write"] is True

    nxt = loop_runner.request("apb.transaction.cursor", args={
        "name": "apb0", "op": "next", "direction": "write"})
    assert nxt.get("ok"), nxt
    assert nxt["summary"]["index"] == 2
    assert nxt["summary"]["at_end"] is True

    end = loop_runner.request("apb.transaction.cursor", args={
        "name": "apb0", "op": "next", "direction": "write"})
    assert end.get("ok"), end
    assert end["summary"]["found"] is False
    assert end["summary"]["index"] is None
    assert end["data"] == {}


def test_apb_transfer_window(loop_runner: StdioLoopRunner, apb_fst) -> None:
    load_apb(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.transfer_window", args={
        "name": "apb0", "time_range": {"begin": "0ps", "end": "500ps"},
        "line_limit": 3})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 4
    assert rsp["summary"]["returned_count"] == 3
    assert rsp["summary"]["response_truncated"] is True
    assert len(rsp["data"]["transactions"]) == 3
    assert rsp["data"]["transactions"][0]["type"] == "WR"


def test_apb_query_no_waveform(cli_runner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "apb.query",
                             "args": {"name": "apb0"}})
    assert not result.ok
    assert result.response["error"]["code"] == "INVALID_REQUEST"


def test_apb_missing_config(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.query", args={"name": "missing"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CONFIG_NOT_FOUND"


# ── AXI ──

def _load_axi(loop_runner: StdioLoopRunner) -> dict:
    rsp = loop_runner.request("axi.config.load",
                              args={"name": "axi0", "config": AXI_CONFIG})
    assert rsp.get("ok"), rsp
    return rsp

def test_axi_config_list(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.config.list")
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["count"] == 0


def test_axi_config_load(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = _load_axi(loop_runner)
    assert rsp["data"]["config"]["channels"]["aw"]["valid"] == "TOP.awvalid"
    assert len(rsp["data"]["validation"]["signals"]) == 31

    rsp = loop_runner.request("axi.config.list", args={"name": "axi0"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["config"]["clock"] == "TOP.aclk"


def test_axi_query(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.query",
                              args={"name": "axi0", "direction": "write",
                                    "query": {"line_limit": 10},
                                    "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    txns = rsp["data"]["transactions"]
    assert rsp["summary"]["total_count"] == 1
    assert len(txns) == 1
    w = txns[0]
    assert w["address"]["addr"] == "8'h10"
    assert w["address"]["id"] == "4'h1"
    assert w["address"]["len"] == "8'h02"
    assert w["address"]["size"] == "3'h2"
    assert w["address"]["burst"] == "2'h1"
    assert w["address"]["handshake_time"] == "60ps"
    assert w["response"]["handshake_time"] == "120ps"

    rsp = loop_runner.request("axi.query",
                              args={"name": "axi0", "direction": "read",
                                    "query": {"line_limit": 10},
                                    "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    r = rsp["data"]["transactions"][0]
    assert r["address"]["addr"] == "8'h10"
    assert r["address"]["id"] == "4'h2"
    assert r["address"]["handshake_time"] == "180ps"
    assert r["response"]["handshake_time"] == "220ps"


def test_axi_query_filters_selectors_and_errors(loop_runner: StdioLoopRunner,
                                                axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.query", args={"name": "axi0",
        "direction": "write", "address": {"mode": "exact", "values": ["8'h10"]}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["query_mode"] == "count"
    assert rsp["summary"]["total_count"] == 1

    rsp = loop_runner.request("axi.query", args={"name": "axi0",
        "direction": "write", "id": {"mode": "range", "begin": "1", "end": "1"},
        "query": {"index": 1}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["found"] is True
    assert rsp["data"]["transaction"]["address"]["id"] == "4'h1"

    rsp = loop_runner.request("axi.query", args={"name": "axi0",
        "direction": "read", "address": {"mode": "mask", "value": "8'h10",
        "mask": "8'hf0"}, "last": True})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["query_mode"] == "last"

    rsp = loop_runner.request("axi.query", args={"name": "missing",
        "direction": "write"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CONFIG_NOT_FOUND"

    rsp = loop_runner.request("axi.query", args={"name": "axi0",
        "direction": "write", "time_range": {"begin": "300ps", "end": "100ps"}})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "TIME_RANGE_INVALID"


def test_axi_query_empty(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.query", args={
        "name": "axi0",
        "direction": "write",
        "address": {"mode": "exact", "values": ["8'hff"]},
        "query": {"line_limit": 10},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 0
    assert rsp["summary"]["returned_count"] == 0
    assert rsp["data"]["transactions"] == []
    assert rsp["data"]["filter"]["direction"] == "write"


def test_axi_query_and_channel_stall_truncate_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    loaded = loop_runner.request("axi.config.load", args={
        "name": "wellen_axi", "config": WELLEN_AXI_CONFIG,
    })
    assert loaded.get("ok"), loaded

    query = loop_runner.request("axi.query", args={
        "name": "wellen_axi", "direction": "read",
        "query": {"line_limit": 1},
    })
    assert query.get("ok"), query
    assert query["summary"]["total_count"] == 10
    assert query["summary"]["returned_count"] == 1
    assert query["summary"]["response_truncated"] is True
    assert query["summary"]["truncation_scopes"] == [
        "response_transactions"
    ]
    assert len(query["data"]["transactions"]) == 1

    channel = loop_runner.request("axi.channel_stall", args={
        "name": "wellen_axi", "channel": "aw", "line_limit": 1,
    })
    assert channel.get("ok"), channel
    assert channel["summary"]["total_count"] == 10
    assert channel["summary"]["returned_count"] == 1
    assert channel["summary"]["response_truncated"] is True
    assert channel["summary"]["truncation_scopes"] == [
        "response_transactions"
    ]
    assert len(channel["data"]["findings"]) == 1


def test_axi_analysis(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.analysis",
                              args={"name": "axi0", "analysis": "latency",
                                    "direction": "all"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["completed_write_count"] == 1
    assert rsp["summary"]["completed_read_count"] == 1
    assert rsp["summary"]["channel_handshakes"] == {
        "aw": 1, "w": 3, "b": 1, "ar": 1, "r": 2}
    assert rsp["data"]["latency"]["write"]["samples"] == 1


def test_axi_analysis_osd_and_pending(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.analysis",
                              args={"name": "axi0", "analysis": "osd",
                                    "direction": "all"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis"] == "osd"
    assert rsp["data"]["osd"]["write"]["max"] >= 1

    rsp = loop_runner.request("axi.analysis",
                              args={"name": "axi0", "analysis": "pending",
                                    "direction": "all", "line_limit": 1})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["pending_transactions"] == []


def test_axi_pending_analysis_truncates_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    pending_config = {
        **WELLEN_AXI_CONFIG,
        "bvalid": "top.masslav_if.Pslave_err",
    }
    loaded = loop_runner.request("axi.config.load", args={
        "name": "wellen_axi_pending", "config": pending_config,
    })
    assert loaded.get("ok"), loaded

    pending = loop_runner.request("axi.analysis", args={
        "name": "wellen_axi_pending", "analysis": "pending",
        "direction": "write", "line_limit": 1,
    })
    assert pending.get("ok"), pending
    assert pending["summary"]["incomplete_write_count"] > 1
    assert pending["summary"]["total_count"] > 1
    assert pending["summary"]["returned_count"] == 1
    assert pending["summary"]["response_truncated"] is True
    assert pending["summary"]["truncation_scopes"] == [
        "response_transactions"
    ]
    assert len(pending["data"]["pending_transactions"]) == 1


def test_axi_statistics(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.statistics",
                              args={"name": "axi0", "filter": {"direction": "all"}})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["matched_transaction_count"] == 2
    assert s["matched_write_count"] == 1
    assert s["matched_read_count"] == 1
    assert s["analysis_quality"] == "complete"

    rsp = loop_runner.request("axi.statistics", args={"name": "axi0",
        "filter": {"direction": "write", "ids": ["1"],
                   "address": {"mode": "range", "begin": "8'h10", "end": "8'h1f"}}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["matched_write_count"] == 1


def test_axi_statistics_empty(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.statistics", args={
        "name": "axi0",
        "filter": {
            "direction": "all",
            "address": {"mode": "exact", "values": ["8'hff"]},
        },
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["matched_transaction_count"] == 0
    assert rsp["summary"]["matched_read_count"] == 0
    assert rsp["summary"]["matched_write_count"] == 0


def test_axi_export(loop_runner: StdioLoopRunner, axi_fst, tmp_path) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    prefix = tmp_path / "axi_transactions"
    rsp = loop_runner.request("axi.export", args={"name": "axi0",
        "time_range": {"begin": "0ps", "end": "500ps"},
        "output": {"path": str(prefix), "file_format": "tsv"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["row_count"] == 2
    assert (tmp_path / "axi_transactions.write.tsv").exists()
    assert (tmp_path / "axi_transactions.read.tsv").exists()
    assert (tmp_path / "axi_transactions.meta.json").exists()


def test_axi_export_empty(loop_runner: StdioLoopRunner,
                          axi_fst, tmp_path) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    output = tmp_path / "empty_axi"
    rsp = loop_runner.request("axi.export", args={
        "name": "axi0",
        "time_range": {"begin": "0ps", "end": "10ps"},
        "output": {"path": str(output), "file_format": "tsv"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "written"
    assert rsp["summary"]["total_count"] == 0
    assert rsp["summary"]["returned_count"] == 0
    assert rsp["summary"]["row_count"] == 0
    assert (tmp_path / "empty_axi.meta.json").is_file()


def test_axi_transaction_cursor(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.transaction.cursor",
                              args={"name": "axi0", "op": "begin"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["found"] is True
    assert rsp["summary"]["index"] == 1
    assert rsp["data"]["transaction"]["direction"] == "write"


def test_axi_transaction_cursor_empty_at_end(
        loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    for op in ("begin", "next"):
        rsp = loop_runner.request(
            "axi.transaction.cursor", args={"name": "axi0", "op": op}
        )
        assert rsp.get("ok"), rsp
        assert rsp["summary"]["found"] is True
    end = loop_runner.request(
        "axi.transaction.cursor", args={"name": "axi0", "op": "next"}
    )
    assert end.get("ok"), end
    assert end["summary"]["found"] is False
    assert end["data"] == {}


def test_axi_channel_stall(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.channel_stall",
                              args={"name": "axi0", "channel": "aw"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["channel"] == "aw"
    assert rsp["summary"]["max_stall_cycles"] >= 0
    for finding in rsp["data"]["findings"]:
        assert finding["type"] == "long_stall"
        assert finding["cycles"] >= 0


def test_axi_latency_outlier(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.latency_outlier",
                              args={"name": "axi0", "direction": "all",
                                    "method": "top_n", "top_n": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["candidate_count"] == 2
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["data"]["classification"] == "slowest_ranking"

    rsp = loop_runner.request("axi.latency_outlier", args={"name": "axi0",
        "direction": "all", "method": "threshold", "threshold": "50ps"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["classification"] == "threshold_exceeded"
    assert rsp["summary"]["total_count"] == 1

    limited = loop_runner.request("axi.latency_outlier", args={
        "name": "axi0", "direction": "all", "method": "top_n",
        "top_n": 2, "line_limit": 1,
    })
    assert limited.get("ok"), limited
    assert limited["summary"]["total_count"] == 2
    assert limited["summary"]["returned_count"] == 1
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["truncation_scopes"] == [
        "response_transactions"
    ]


def test_axi_latency_outlier_empty(loop_runner: StdioLoopRunner,
                                   axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.latency_outlier", args={
        "name": "axi0", "direction": "all",
        "method": "threshold", "threshold": "1us",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 0
    assert rsp["summary"]["returned_count"] == 0
    assert rsp["data"]["outliers"] == []


def test_axi_outstanding_timeline(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.outstanding_timeline",
                              args={"name": "axi0", "direction": "all"})
    assert rsp.get("ok"), rsp
    assert max(rsp["summary"]["peak_read"], rsp["summary"]["peak_write"]) >= 1
    for point in rsp["data"]["change_points"]:
        assert point["read"] >= 0 and point["write"] >= 0

    limited = loop_runner.request("axi.outstanding_timeline", args={
        "name": "axi0", "direction": "all", "line_limit": 1,
    })
    assert limited.get("ok"), limited
    assert limited["summary"]["total_count"] == 4
    assert limited["summary"]["returned_count"] == 1
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["truncation_scopes"] == [
        "response_transactions"
    ]


def test_axi_outstanding_timeline_empty(loop_runner: StdioLoopRunner,
                                        axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.outstanding_timeline", args={
        "name": "axi0", "direction": "all",
        "time_range": {"begin": "0ps", "end": "10ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 0
    assert rsp["data"]["change_points"] == []


def test_axi_request_response_pair(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.request_response_pair",
                              args={"name": "axi0", "direction": "all"})
    assert rsp.get("ok"), rsp
    pairs = rsp["data"]["transactions"]
    assert rsp["summary"]["total_count"] == 2
    for pair in pairs:
        assert pair["latency"] != "0ns"
        assert "address" in pair and "response" in pair

    limited = loop_runner.request("axi.request_response_pair", args={
        "name": "axi0", "direction": "all", "line_limit": 1,
    })
    assert limited.get("ok"), limited
    assert limited["summary"]["total_count"] == 2
    assert limited["summary"]["returned_count"] == 1
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["truncation_scopes"] == [
        "response_transactions"
    ]


def test_axi_request_response_pair_empty(loop_runner: StdioLoopRunner,
                                         axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    _load_axi(loop_runner)
    rsp = loop_runner.request("axi.request_response_pair", args={
        "name": "axi0", "direction": "all",
        "time_range": {"begin": "0ps", "end": "10ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 0
    assert rsp["summary"]["returned_count"] == 0
    assert rsp["data"]["transactions"] == []
