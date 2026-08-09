# test_combined.py — trace.active_driver / active_driver_chain / x_origin (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def test_trace_active_driver(loop_runner: StdioLoopRunner, counter_fst,
                             counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["signal"] == "top.counter_top.count"
    assert rsp["summary"]["time"] == "300ps"
    assert rsp["summary"]["termination"] in ("assignment", "no_driver")
    assert rsp["summary"]["scan_complete"] is True
    paths = rsp["data"]["paths"]
    assert paths
    assert paths[0]["signal_path"][-1] == "top.counter_top.count"
    assert paths[0]["file"].endswith("counter_top.sv")


def test_trace_active_driver_requires_both(loop_runner: StdioLoopRunner,
                                           counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.clk", "time": "100ps"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "DESIGN_NOT_LOADED"


def test_trace_active_driver_chain(loop_runner: StdioLoopRunner, counter_fst,
                                   counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "render_time_unit": "ps", "value_format": "hex"},
        target=None)
    assert rsp.get("ok"), rsp
    hops = rsp["data"]["hops"]
    assert hops
    assert hops[0]["relation"] == "root"
    assert hops[0]["signal"] == "top.counter_top.count"
    assert hops[0]["value"] == "8'h0b"


def test_trace_x_origin_not_x(loop_runner: StdioLoopRunner, counter_fst,
                              counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "not_x_at_query_time"
    assert rsp["summary"]["evidence_status"] == "proven"
    assert rsp["data"]["query"]["value"]["value"] == "8'h0b"
    assert rsp["data"]["chains"] == []


def test_trace_x_origin_x_propagation(loop_runner: StdioLoopRunner,
                                      gcd_xorigin_fst,
                                      gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.io_z", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] in ("origin_found", "partial")
    assert rsp["summary"]["origin_count"] >= 1
    chains = rsp["data"]["chains"]
    assert [hop["signal"] for hop in chains[0]["hops"]] == [
        "GCD.io_z", "GCD.x", "GCD.io_a"]
    assert chains[0]["origin"]["signal"] == "GCD.io_a"
    assert "x" in chains[0]["origin"]["reason"]


def test_trace_x_origin_not_x_late(loop_runner: StdioLoopRunner, xprop_fst,
                                   xprop_design_db) -> None:
    open_session(loop_runner, xprop_fst, xprop_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "top.out", "time": "200ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "not_x_at_query_time"


def test_trace_x_origin_missing_signal(loop_runner: StdioLoopRunner, xprop_fst,
                                       xprop_design_db) -> None:
    open_session(loop_runner, xprop_fst, xprop_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "nope", "time": "20ps"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"
