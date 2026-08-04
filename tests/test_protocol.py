# test_protocol.py — APB/AXI protocol actions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


# ── APB ──

def test_apb_config_list(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.config.list")
    assert rsp.get("ok"), rsp
    assert rsp["data"]["configs"][0]["name"] == "default"


def test_apb_config_load(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.config.load", args={"name": "default"})
    assert rsp.get("ok"), rsp
    assert "psel" in rsp["data"]["signal_map"]


def test_apb_query(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.query", args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    transfers = rsp["data"]["transfers"]
    assert rsp["summary"]["transfer_count"] == 4
    # writes: addr=1 data=0xab; addr=3 data=0x5a; reads: addr=1, addr=3
    assert transfers[0]["direction"] == "write"
    assert transfers[0]["address"]["value"] == "8'h01"
    assert transfers[0]["write_data"]["value"] == "8'hab"
    assert transfers[1]["direction"] == "write"
    assert transfers[1]["address"]["value"] == "8'h03"
    assert transfers[1]["write_data"]["value"] == "8'h5a"
    assert transfers[2]["direction"] == "read"
    assert transfers[2]["address"]["value"] == "8'h01"
    assert transfers[3]["direction"] == "read"
    assert transfers[3]["address"]["value"] == "8'h03"
    for t in transfers:
        assert t["status"] == "ok"


def test_apb_statistics(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.statistics", args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["transfer_count"] == 4
    assert s["read_count"] == 2
    assert s["write_count"] == 2
    assert s["error_count"] == 0


def test_apb_transaction_cursor(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.transaction.cursor",
                              args={"begin": "0", "end": "500", "cursor": 0})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["transactions"]) == 4
    assert rsp["data"]["has_more"] is False


def test_apb_transfer_window(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    rsp = loop_runner.request("apb.transfer_window",
                              args={"begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["transfers"]) == 4


def test_apb_query_no_waveform(cli_runner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "apb.query"})
    assert not result.ok
    assert result.response["error"]["code"] == "WAVEFORM_NOT_LOADED"


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
