# test_window.py — window.verify / verify.conditions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def test_window_verify_clock_sampling(loop_runner: StdioLoopRunner,
                                      counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "signal": "top.counter_top.count",
        "clock": "top.clk", "edge": "rising",
        "begin": "0", "end": "300"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["check_count"] == 15
    assert s["verdict"] == "pass"
    assert s["fail_count"] == 0


def test_window_verify_with_expect(loop_runner: StdioLoopRunner,
                                   counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "signal": "top.counter_top.count",
        "clock": "top.clk", "edge": "rising",
        "begin": "200", "end": "220", "expect": "8'h07"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["check_count"] == 2
    assert s["pass_count"] == 1  # only t=220 has value 8'h07
    assert s["fail_count"] == 1


def test_window_verify_with_expression(loop_runner: StdioLoopRunner,
                                       counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "signal": "top.counter_top.count",
        "expression": "top.counter_top.count >= 8'h05",
        "clock": "top.clk", "edge": "rising",
        "begin": "0", "end": "200"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["check_count"] == 10
    # reset holds count=0 until t=100 (6 sampled edges), then 1..4 (4 edges)
    assert s["fail_count"] == 8
    assert s["pass_count"] == 2


def test_window_verify_no_clock(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "signal": "top.counter_top.count", "begin": "0", "end": "100"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["check_count"] == 2  # changes at 0 and 100


def test_verify_conditions(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "signal": "top.counter_top.count",
        "conditions": [
            {"time": "300", "expect": "8'h0b"},
            {"time": "500", "expect": "8'h14"},
            {"time": "400", "expect": "8'h00"},
        ]})
    assert rsp.get("ok"), rsp
    results = rsp["data"]["results"]
    assert len(results) == 3
    assert results[0]["pass"] is True
    assert results[1]["pass"] is True
    assert results[2]["pass"] is False
    assert rsp["summary"]["pass_count"] == 2
    assert rsp["summary"]["fail_count"] == 1


def test_verify_conditions_range(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "signal": "top.counter_top.count",
        "conditions": [
            {"begin": "100", "end": "140", "expect": "8'h01"},
        ]})
    assert rsp.get("ok"), rsp
    results = rsp["data"]["results"]
    # changes in [100,140]: 100->1, 120->2, 140->3
    assert len(results) == 3
    assert results[0]["pass"] is True
    assert results[1]["pass"] is False
    assert results[2]["pass"] is False


def test_verify_conditions_missing(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "signal": "top.counter_top.count", "conditions": []})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "MISSING_FIELD"
