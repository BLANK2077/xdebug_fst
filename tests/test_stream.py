# test_stream.py — stream actions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


STREAM_CFG = {
    "name": "fifo",
    "clock": "top.clk",
    "valid": "top.in_valid",
    "ready": "top.in_ready",
    "data": "top.in_data",
}


def test_stream_config_list(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.list")
    assert rsp.get("ok"), rsp
    assert rsp["data"]["configs"][0]["name"] == "default"


def test_stream_config_get_default(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.get", args={"name": "default"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["config"]["name"] == "default"


def test_stream_config_get_unknown(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.get", args={"name": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CONFIG_NOT_FOUND"


def test_stream_config_load(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.load",
                              args={"config": STREAM_CFG})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["config"]["name"] == "fifo"


def test_stream_config_load_invalid(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.load",
                              args={"config": {"valid": "x"}})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "VALIDATION_FAILED"


def test_stream_describe(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CFG})
    rsp = loop_runner.request("stream.describe", args={"name": "fifo"})
    assert rsp.get("ok"), rsp
    desc = rsp["data"]["description"]
    assert desc["handshake_count"] == 4
    assert desc["data_width"] == 8
    assert desc["valid_high_count"] >= 4


def test_stream_query(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CFG})
    rsp = loop_runner.request("stream.query", args={
        "name": "fifo", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    hs = rsp["data"]["handshakes"]
    assert rsp["summary"]["handshake_count"] == 4
    assert [h["data"]["value"] for h in hs] == [
        "8'haa", "8'hbb", "8'hcc", "8'hdd"]
    assert [h["time"] for h in hs] == [40, 60, 80, 120]


def test_stream_query_max_rows(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CFG})
    rsp = loop_runner.request("stream.query", args={
        "name": "fifo", "max_rows": 2})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["handshakes"]) == 2
    assert rsp["summary"]["truncated"] is True


def test_stream_export(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CFG})
    rsp = loop_runner.request("stream.export", args={
        "name": "fifo", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["handshakes"]) == 4


def test_stream_validate(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CFG})
    rsp = loop_runner.request("stream.validate", args={"name": "fifo"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["valid"] is True
    assert rsp["data"]["missing_signals"] == []


def test_stream_validate_bad_config(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={
        "config": {"name": "bad", "clock": "top.clk",
                   "valid": "no.such", "ready": "top.in_ready"}})
    rsp = loop_runner.request("stream.validate", args={"name": "bad"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["valid"] is False
    assert rsp["data"]["missing_signals"] == ["no.such"]
