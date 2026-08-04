# test_waveform.py — waveform basic actions (BSD-3-Clause)
"""value.at, signal.changes, scope.list, scope.roots."""
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def _value(loop, signal, time):
    rsp = loop.request("value.at", args={"signal": signal, "time": str(time)})
    assert rsp.get("ok"), rsp
    return rsp


def test_value_at_bit_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "top.clk", 300)
    assert rsp["summary"]["time"] == 300
    assert rsp["summary"]["time_match"] is True
    v = rsp["data"]
    assert v["width"] == 1
    assert v["known"] is True
    assert v["value"] in ("1'h0", "1'h1")


def test_value_at_bus_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "top.counter_top.count", 300)
    v = rsp["data"]
    assert v["width"] == 8
    assert v["value"] == "8'h0b"
    assert v["bits"] == "00001011"


def test_value_at_case_insensitive_and_top_prefix(loop_runner: StdioLoopRunner,
                                                  counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "TOP.counter_top.count", 300)
    assert rsp["data"]["value"] == "8'h0b"


def test_value_at_unknown_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={"signal": "nope.sig", "time": "100"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_value_at_missing_fields(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={"signal": "top.clk"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "MISSING_FIELD"


def test_value_at_invalid_time(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at",
                              args={"signal": "top.clk", "time": "abc"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_TIME"


def test_value_at_render_formats(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "top.counter_top.count", 300)
    rsp_bin = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "300",
        "render_format": "bin"})
    assert rsp_bin["data"]["value"] == "8'b00001011"
    rsp_dec = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "300",
        "render_format": "dec"})
    assert rsp_dec["data"]["value"] == "8'd11"


def test_signal_changes(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count", "begin": "0", "end": "490"})
    assert rsp.get("ok")
    changes = rsp["data"]["changes"]
    assert len(changes) == 21
    assert changes[0]["time"] == 0
    assert changes[0]["value"]["value"] == "8'h00"
    assert changes[1]["time"] == 100
    assert changes[1]["value"]["value"] == "8'h01"
    assert rsp["summary"]["change_count"] == 21


def test_signal_changes_window(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count", "begin": "200", "end": "300"})
    assert rsp.get("ok")
    changes = rsp["data"]["changes"]
    assert changes[0]["time"] == 200
    assert changes[-1]["time"] == 300
    assert len(changes) == 6


def test_signal_changes_missing_signal(loop_runner: StdioLoopRunner,
                                       counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={"signal": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_scope_roots(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("scope.roots")
    assert rsp.get("ok")
    names = [r["name"] for r in rsp["data"]["roots"]]
    assert "counter_top" in names or "TOP" in names


def test_scope_list(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("scope.list")
    assert rsp.get("ok")
    assert rsp["summary"]["scope_count"] >= 1
    for s in rsp["data"]["scopes"]:
        assert "name" in s
        assert "var_count" in s


def test_waveform_not_loaded_error(cli_runner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "value.at",
                             "args": {"signal": "top.clk", "time": "10"}})
    assert not result.ok
    assert result.response["error"]["code"] == "WAVEFORM_NOT_LOADED"
