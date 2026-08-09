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


def test_expr_eval_at_parenthesized_logical_expression(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "(count == 8'h0b && reset == 1'b0)", "time": "300ps",
        "clock": "top.clk", "signals": {
            "count": "top.counter_top.count", "reset": "top.reset"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "true"
    assert rsp["data"]["expr_value"] is True


def test_expr_eval_at_logical_not_preserves_unknown(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst) -> None:
    open_session(loop_runner, gcd_xorigin_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "!control", "time": "0ps", "clock": "GCD.T_13",
        "signals": {"control": "GCD.y"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "unknown"
    assert rsp["summary"]["known"] is False
    assert rsp["data"]["expr_value"] is None


def test_expr_eval_at_preserves_casez_and_casex_unknown_semantics(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst) -> None:
    open_session(loop_runner, gcd_xorigin_fst)
    casex = loop_runner.request("expr.eval_at", args={
        "expr": "control ==?x 32'h00000000", "time": "0ps",
        "clock": "GCD.T_13", "signals": {"control": "GCD.y"}})
    assert casex.get("ok"), casex
    assert casex["summary"]["status"] == "true"
    assert casex["data"]["expr_value"] is True

    casez = loop_runner.request("expr.eval_at", args={
        "expr": "control ==?z 32'h00000000", "time": "0ps",
        "clock": "GCD.T_13", "signals": {"control": "GCD.y"}})
    assert casez.get("ok"), casez
    assert casez["summary"]["status"] == "false"
    assert casez["data"]["expr_value"] is False


def test_counter_statistics(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("counter.statistics", args={
        "cnt": "top.counter_top.count", "clock": "top.clk",
        "vld": {"expr": "!rst", "signals": {"rst": "top.reset"}},
        "edge": "posedge", "sample_point": "after",
        "time_range": {"begin": "0ps", "end": "490ps"}})
    assert rsp.get("ok"), rsp
    s = rsp["summary"]
    assert s["sampling_mode"] == "clock_edge"
    assert s["sample_count"] == 24
    assert s["valid_count"] > 0
    assert s["min_value"]["known"] is True
    assert s["max_value"]["known"] is True
    assert s["scan_complete"] is True
    assert rsp["data"]["sampling"]["effective"]["sample_point"] == "after"
    assert rsp["data"]["evidence"][0]["kind"] == "initial"


def test_counter_statistics_sample_and_evidence_limits(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("counter.statistics", args={
        "cnt": "top.count", "clock": "top.clk", "vld": "top.clk",
        "edge": "posedge", "sample_point": "after",
        "time_range": {"begin": "100ps", "end": "490ps"},
        "max_samples": 3, "line_limit": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["sample_count"] == 3
    assert rsp["summary"]["scan_complete"] is False
    assert "analysis_samples" in rsp["summary"]["truncation_scopes"]
    assert rsp["summary"]["returned_count"] <= 1
    assert rsp["summary"]["response_truncated"] is True
    assert "response_evidence" in rsp["summary"]["truncation_scopes"]


def test_counter_statistics_without_valid_counter_value(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("counter.statistics", args={
        "cnt": "{top.count,top.overflow}", "clock": "top.clk",
        "vld": "top.overflow", "edge": "negedge",
        "time_range": {"begin": "0ps", "end": "490ps"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["valid_count"] == 0
    assert "min_value" not in rsp["summary"]
    assert "max_value" not in rsp["summary"]
    assert "average_value" not in rsp["summary"]
    assert rsp["data"]["sampling"]["effective"]["sample_point"] is None


def test_counter_statistics_rejects_bad_time_range(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("counter.statistics", args={
        "cnt": "top.count", "clock": "top.clk", "vld": "top.reset",
        "time_range": {"begin": "400ps", "end": "100ps"}})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "TIME_RANGE_INVALID"


def test_signal_sampled_pulse_inspect(loop_runner: StdioLoopRunner,
                                      stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("signal.sampled_pulse.inspect", args={
        "valid": "top.in_valid", "clock": "top.clk",
        "payloads": ["top.in_data"], "edge": "posedge",
        "sample_point": "after",
        "rules": {"payload_changed_without_sampled_valid": "all"},
        "time_range": {"begin": "0ps", "end": "500ps"},
        "line_limit": 2})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["sampling_mode"] == "clock_edge"
    assert rsp["summary"]["sample_count"] > 0
    assert rsp["summary"]["payload_changed_without_sampled_valid_reporting"] == "all"
    assert rsp["data"]["valid"] == "top.in_valid"
    assert rsp["data"]["payloads"] == [
        {"alias": "payload0", "signal": "top.in_data"}]
    assert rsp["summary"]["returned_count"] == len(rsp["data"]["findings"])


def test_signal_sampled_pulse_rule_requires_payloads(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("signal.sampled_pulse.inspect", args={
        "valid": "top.in_valid", "clock": "top.clk",
        "rules": {"payload_changed_without_sampled_valid": "summary"}})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"


def test_protocol_handshake_inspect(loop_runner: StdioLoopRunner,
                                    stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={
        "clock": "top.clk", "valid": "top.in_valid",
        "ready": "top.in_ready", "data": "top.in_data",
        "edge": "posedge", "sample_point": "after",
        "rules": {"max_wait_cycles": 1,
                  "check_data_stable_when_stalled": True,
                  "ready_without_valid": "intervals"},
        "time_range": {"begin": "0ps", "end": "500ps"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["sampling_mode"] == "clock_edge"
    assert rsp["summary"]["sample_count"] > 0
    assert rsp["summary"]["transfer_count"] > 0
    assert rsp["summary"]["ready_without_valid_reporting"] == "intervals"
    assert "ready_without_valid_intervals" in rsp["data"]
    assert rsp["data"]["sampling"]["effective"]["sample_point"] == "after"


def test_protocol_handshake_data_rule_is_symmetric(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={
        "clock": "top.clk", "valid": "top.in_valid",
        "ready": "top.in_ready", "data": "top.in_data"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"


def test_protocol_handshake_all_reporting_and_line_limit(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={
        "clock": "top.clk", "valid": "top.in_valid",
        "ready": "top.in_ready", "edge": "posedge",
        "sample_point": "after", "line_limit": 1,
        "rules": {"ready_without_valid": "all"},
        "time_range": {"begin": "0ps", "end": "500ps"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["ready_without_valid_cycles"] > 1
    assert rsp["summary"]["total_count"] > 1
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["summary"]["response_truncated"] is True
    assert "response_findings" in rsp["summary"]["truncation_scopes"]


def test_protocol_handshake_inspect_missing_fields(loop_runner: StdioLoopRunner,
                                                   counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"
