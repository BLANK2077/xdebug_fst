# test_design.py — design actions: resolve/canonicalize/driver/load/normalize (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def test_signal_resolve(loop_runner: StdioLoopRunner, counter_fst,
                        counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("signal.resolve", args={"signal": "top.count"})
    assert rsp.get("ok"), rsp
    d = rsp["data"]
    assert d["name"] == "top.count"
    assert d["type"] == "port"
    assert d["width"] == 8
    assert d["index"] >= 0


def test_signal_resolve_not_found(loop_runner: StdioLoopRunner, counter_fst,
                                  counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("signal.resolve", args={"signal": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_signal_resolve_requires_design(loop_runner: StdioLoopRunner,
                                        counter_fst) -> None:
    open_session(loop_runner, counter_fst)  # waveform only
    rsp = loop_runner.request("signal.resolve", args={"signal": "top.clk"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "DESIGN_NOT_LOADED"


def test_signal_canonicalize(loop_runner: StdioLoopRunner, counter_fst,
                             counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("signal.canonicalize", args={"signal": "top.count"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["canonical"] == "top.count"
    assert rsp["data"]["resolved"] is True


def test_trace_driver(loop_runner: StdioLoopRunner, counter_fst,
                      counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.driver",
                              args={"signal": "top.counter_top.count"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["driver_count"] >= 1
    for drv in rsp["data"]["drivers"]:
        assert drv["kind"] in ("nba", "proc_assign", "cont_assign")
        assert drv["file"] == "counter_top.sv"


def test_trace_driver_not_found(loop_runner: StdioLoopRunner, counter_fst,
                                counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.driver", args={"signal": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_trace_load(loop_runner: StdioLoopRunner, counter_fst,
                    counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.load", args={"signal": "top.clk"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["load_count"] >= 1
    for ld in rsp["data"]["loads"]:
        assert ld["consumer"] is not None
        assert ld["kind"] == "rhs_use"


def test_expr_normalize(loop_runner: StdioLoopRunner) -> None:
    rsp = loop_runner.request("expr.normalize", args={
        "expression": "count + 8'h01 == 8'h0b"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["normalized"] == "((count + 8'h01) == 8'h0b)"


def test_expr_normalize_parse_error(loop_runner: StdioLoopRunner) -> None:
    rsp = loop_runner.request("expr.normalize", args={"expression": "a +* b"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "PARSE_ERROR"
