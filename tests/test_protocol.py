# test_protocol.py — APB/AXI protocol actions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
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

def test_axi_config_list(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.config.list")
    assert rsp.get("ok"), rsp
    assert rsp["data"]["configs"][0]["name"] == "default"


def test_axi_config_load(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.config.load", args={"name": "default"})
    assert rsp.get("ok"), rsp
    assert "awvalid" in rsp["data"]["signal_map"]


def test_axi_query(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.query", args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    txns = rsp["data"]["transactions"]
    assert rsp["summary"]["transaction_count"] == 2
    write_txns = [t for t in txns if t["direction"] == "write"]
    read_txns = [t for t in txns if t["direction"] == "read"]
    assert len(write_txns) == 1
    assert len(read_txns) == 1
    w = write_txns[0]
    assert w["address"]["value"] == "8'h10"
    assert w["id"]["value"] == "4'h1"
    assert w["length"]["value"] == "8'h02"
    assert w["burst_status"] == "ok"
    assert w["start_time"] == 60
    assert w["end_time"] == 120
    r = read_txns[0]
    assert r["address"]["value"] == "8'h10"
    assert r["id"]["value"] == "4'h2"
    assert r["burst_status"] == "ok"
    assert r["start_time"] == 180
    assert r["end_time"] == 220


def test_axi_analysis(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.analysis", args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    channels = rsp["data"]["channels"]
    assert len(channels["aw"]) == 1
    assert len(channels["b"]) == 1
    assert len(channels["ar"]) == 1
    assert len(channels["w"]) == 3  # 3 data beats
    assert len(channels["r"]) == 2  # 2 read beats


def test_axi_statistics(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.statistics", args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["transaction_count"] == 2
    assert s["write_count"] == 1
    assert s["read_count"] == 1
    assert s["error_count"] == 0
    assert s["max_latency"] > 0


def test_axi_export(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.export", args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["transactions"]) == 2


def test_axi_transaction_cursor(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.transaction.cursor",
                              args={"begin": "0", "end": "500", "cursor": 0})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["transactions"]) == 2
    assert rsp["data"]["has_more"] is False


def test_axi_channel_stall(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.channel_stall",
                              args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["stall_count"] >= 0
    for stall in rsp["data"]["stalls"]:
        assert "channel" in stall
        assert stall["duration"] >= 0


def test_axi_latency_outlier(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.latency_outlier",
                              args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["outlier_count"] >= 0
    assert rsp["summary"]["avg_latency"] > 0


def test_axi_outstanding_timeline(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.outstanding_timeline",
                              args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["max_outstanding"] >= 1
    for point in rsp["data"]["timeline"]:
        assert point["outstanding"] >= 0


def test_axi_request_response_pair(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.request_response_pair",
                              args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    pairs = rsp["data"]["pairs"]
    assert rsp["summary"]["pair_count"] == 2
    for p in pairs:
        assert p["latency"] > 0
        assert "request" in p and "response" in p
