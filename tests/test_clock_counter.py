# test_clock_counter.py — clock/counter/expr/pulse/handshake actions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def test_noncanonical_clock_point_query_is_not_public(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("clock_point_query", args={
        "signal": "top.counter_top.count", "clock": "top.clk",
        "time": "300", "sample_point": "middle"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "UNKNOWN_ACTION"


def test_expr_eval_at_equal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "count == 8'h0b", "time": "300ps", "clock": "top.clk",
        "signals": {"count": "top.counter_top.count"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "true"
    assert rsp["data"]["expr_value"] is True


def test_expr_eval_at_arith(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "count + 1", "time": "300ps", "clock": "top.clk",
        "signals": {"count": "top.counter_top.count"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "true"
    assert rsp["data"]["operands"][0]["value"]["value"] == "8'h0b"


def test_expr_eval_at_slice(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "count[3:0]", "time": "300ps", "clock": "top.clk",
        "signals": {"count": "top.counter_top.count"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "true"


def test_expr_eval_at_parse_error(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "clk +*", "time": "300ps", "clock": "top.clk",
        "signals": {"clk": "top.clk"}})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "PARSE_ERROR"


def test_counter_statistics(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("counter.statistics", args={
        "signal": "top.counter_top.count", "clock": "top.clk",
        "begin": "100", "end": "400"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["sample_count"] == 16
    assert s["min_value"] == 1
    assert s["max_value"] == 16
    assert rsp["data"]["direction"] == "up"


def test_signal_sampled_pulse_inspect(loop_runner: StdioLoopRunner,
                                      counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.sampled_pulse.inspect", args={
        "signal": "top.counter_top.count", "clock": "top.clk",
        "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["pulse_count"] >= 0
    if rsp["summary"]["pulse_count"] > 0:
        for p in rsp["data"]["pulses"]:
            assert "begin_time" in p and "end_time" in p


def test_protocol_handshake_inspect(loop_runner: StdioLoopRunner,
                                    counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={
        "req_signal": "top.clk", "ack_signal": "top.clk",
        "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["handshake_count"] == 25
    assert rsp["summary"]["min_latency"] == 0


def test_protocol_handshake_inspect_missing_fields(loop_runner: StdioLoopRunner,
                                                   counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "MISSING_FIELD"
