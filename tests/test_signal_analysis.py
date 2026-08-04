# test_signal_analysis.py — signal statistics/stability/xz/anomaly (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import open_session
from runner import StdioLoopRunner


def test_signal_statistics(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.clk", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["sample_count"] == 50
    assert s["transition_count"] >= 1
    assert 0 < s["activity"] <= 1.0
    assert s["state_counts"]["zero"] + s["state_counts"]["one"] == s["sample_count"]


def test_signal_statistics_bus(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.counter_top.count", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["sample_count"] == 21
    assert rsp["summary"]["transition_count"] == 20


def test_signal_stability_stable(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    # reset stays 1 for the whole waveform? reset: 1 until t=100, then 0.
    rsp = loop_runner.request("signal.stability", args={
        "signal": "top.counter_top.count", "begin": "400", "end": "490"})
    assert rsp.get("ok"), rsp
    # count changes every cycle in 400-490 (4 changes: 14,15,16,17)
    assert rsp["data"]["summary"]["stable"] is False


def test_signal_stability_unstable(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.stability", args={
        "signal": "top.counter_top.count", "begin": "100", "end": "100"})
    assert rsp.get("ok"), rsp
    # single point: only one change row -> stable
    assert rsp["data"]["summary"]["stable"] is True


def test_signal_xz_verify_pass(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    # count is never x -> expect x should fail with first mismatch
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "top.counter_top.count", "expected_state": "x",
        "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["summary"]["verdict"] == "fail"
    assert rsp["data"]["summary"]["always_matched"] is False


def test_signal_xz_verify_x_present(loop_runner: StdioLoopRunner, xprop_vcd,
                                    xprop_design_db) -> None:
    open_session(loop_runner, xprop_vcd, xprop_design_db)
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "top.xprop_top.a", "expected_state": "x",
        "begin": "0", "end": "50"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["summary"]["verdict"] == "pass"
    assert rsp["data"]["summary"]["always_matched"] is True


def test_signal_xz_verify_invalid_state(loop_runner: StdioLoopRunner,
                                        counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "top.clk", "expected_state": "q", "begin": "0", "end": "100"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_FIELD"


def test_signal_anomaly_inspect_no_anomaly(loop_runner: StdioLoopRunner,
                                           counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.anomaly.inspect", args={
        "signal": "top.counter_top.count", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["anomalies"] == []
    assert rsp["summary"]["anomaly_count"] == 0


def test_signal_anomaly_inspect_x(loop_runner: StdioLoopRunner, xprop_vcd,
                                  xprop_design_db) -> None:
    open_session(loop_runner, xprop_vcd, xprop_design_db)
    rsp = loop_runner.request("signal.anomaly.inspect", args={
        "signal": "top.xprop_top.a", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["anomaly_count"] >= 1
    assert rsp["data"]["anomalies"][0]["kind"] == "x"


def test_signal_analysis_missing_signal(loop_runner: StdioLoopRunner,
                                        counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    for action in ("signal.statistics", "signal.stability",
                   "signal.anomaly.inspect"):
        rsp = loop_runner.request(action, args={"signal": "nope",
                                                "begin": "0", "end": "100"})
        assert not rsp.get("ok"), action
        assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND", action
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "nope", "expected_state": "x", "begin": "0", "end": "100"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"
