# test_combined.py — trace.active_driver / active_driver_chain / x_origin (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def test_trace_active_driver(loop_runner: StdioLoopRunner, counter_fst,
                             counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.counter_top.count", "time": "300"})
    assert rsp.get("ok"), rsp
    chain = rsp["data"]["chain"]
    assert len(chain) >= 1
    assert rsp["summary"]["depth"] >= 1
    assert rsp["summary"]["termination_reason"] in (
        "input_port", "primary_input", "constant", "no_driver", "max_depth")
    first = chain[0]
    assert first["signal"] == "top.counter_top.count"
    assert first["value"]["value"] == "8'h0b"


def test_trace_active_driver_requires_both(cli_runner) -> None:
    # one-shot without a session: neither backend is loaded
    result = cli_runner.run({"api_version": "xdebug.v1",
                             "action": "trace.active_driver",
                             "args": {"signal": "top.clk", "time": "100"}})
    assert not result.ok
    assert result.response["error"]["code"] in ("WAVEFORM_NOT_LOADED",
                                                "DESIGN_NOT_LOADED")


def test_trace_active_driver_chain(loop_runner: StdioLoopRunner, counter_fst,
                                   counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.counter_top.count", "time": "300"})
    assert rsp.get("ok"), rsp
    chain = rsp["data"]["chain"]
    assert len(chain) >= 1
    for hop in chain:
        assert "signal" in hop
        assert "value" in hop


def test_trace_x_origin_not_x(loop_runner: StdioLoopRunner, counter_fst,
                              counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "top.counter_top.count", "time": "300"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["is_x"] is False
    assert rsp["data"]["value"]["value"] == "8'h0b"


def test_trace_x_origin_x_propagation(loop_runner: StdioLoopRunner, xprop_vcd,
                                      xprop_design_db) -> None:
    open_session(loop_runner, xprop_vcd, xprop_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "top.out", "time": "20"})
    assert rsp.get("ok"), rsp
    d = rsp["data"]
    assert d["is_x"] is True
    assert d["origin_time"] == 0
    chain = d["propagation_chain"]
    assert chain[0]["signal"] == "top.out"
    # X propagates out -> xprop_top.y -> xprop_top.a
    assert chain[-1]["signal"] == "top.xprop_top.a"
    assert "x" in chain[-1]["value"]["value"]
    assert rsp["summary"]["termination_reason"] in (
        "driver_x", "primary_input", "no_driver", "max_depth")


def test_trace_x_origin_not_x_late(loop_runner: StdioLoopRunner, xprop_vcd,
                                   xprop_design_db) -> None:
    open_session(loop_runner, xprop_vcd, xprop_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "top.out", "time": "200"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["is_x"] is False


def test_trace_x_origin_missing_signal(loop_runner: StdioLoopRunner, xprop_vcd,
                                       xprop_design_db) -> None:
    open_session(loop_runner, xprop_vcd, xprop_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "nope", "time": "20"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"
