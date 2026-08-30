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
    assert rsp["summary"]["value_width_complete"] is True
    assert rsp["summary"]["width_diagnostics"] == []
    assert rsp["data"]["expr_value"] is True


def test_expr_eval_at_unsized_zero_uses_value_equality(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    equal = loop_runner.request("expr.eval_at", args={
        "expr": "count == 0", "time": "20ps", "clock": "top.clk",
        "signals": {"count": "top.counter_top.count"}})
    assert equal.get("ok"), equal
    assert equal["summary"]["status"] == "true"
    assert equal["data"]["expr_value"] is True

    unequal = loop_runner.request("expr.eval_at", args={
        "expr": "count != 0", "time": "20ps", "clock": "top.clk",
        "signals": {"count": "top.counter_top.count"}})
    assert unequal.get("ok"), unequal
    assert unequal["summary"]["status"] == "false"
    assert unequal["data"]["expr_value"] is False


def test_expr_eval_at_accepts_unsized_based_literal(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "count == 'h0b", "time": "300ps", "clock": "top.clk",
        "signals": {"count": "top.counter_top.count"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "true"
    assert rsp["data"]["expr_value"] is True


def test_expr_eval_at_signed_sized_literal(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("expr.eval_at", args={
        "expr": "count == 8'sh0b", "time": "300ps", "clock": "top.clk",
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


def test_expr_eval_at_preserves_wildcard_case_unknown_semantics(
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

    inside_concrete = loop_runner.request("expr.eval_at", args={
        "expr": "control ==?i 32'h00000000", "time": "0ps",
        "clock": "GCD.T_13", "signals": {"control": "GCD.y"}})
    assert inside_concrete.get("ok"), inside_concrete
    assert inside_concrete["summary"]["status"] == "false"
    assert inside_concrete["data"]["expr_value"] is False

    inside_wildcard = loop_runner.request("expr.eval_at", args={
        "expr": "control ==?i 32'hxxxxxxxx", "time": "0ps",
        "clock": "GCD.T_13", "signals": {"control": "GCD.y"}})
    assert inside_wildcard.get("ok"), inside_wildcard
    assert inside_wildcard["summary"]["status"] == "true"
    assert inside_wildcard["data"]["expr_value"] is True


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


def test_counter_statistics_resolves_named_cursor_time_range(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    common = {
        "cnt": "top.counter_top.count",
        "clock": "top.clk",
        "vld": {"expr": "!rst", "signals": {"rst": "top.reset"}},
        "edge": "posedge",
        "sample_point": "after",
    }
    direct = loop_runner.request("counter.statistics", args={
        **common, "time_range": {"begin": "100ps", "end": "300ps"},
    })
    assert direct.get("ok"), direct
    for name, time in (("cnt_begin", "100ps"), ("cnt_end", "300ps")):
        cursor = loop_runner.request("waveform.cursor.set", args={
            "name": name, "time": time,
        })
        assert cursor.get("ok"), cursor

    resolved = loop_runner.request("counter.statistics", args={
        **common,
        "time_range": {"begin": "@cnt_begin", "end": "@cnt_end"},
    })
    assert resolved.get("ok"), resolved
    assert resolved["summary"]["begin"] == direct["summary"]["begin"]
    assert resolved["summary"]["end"] == direct["summary"]["end"]
    assert resolved["summary"]["min_value"] == direct["summary"]["min_value"]
    assert resolved["summary"]["max_value"] == direct["summary"]["max_value"]

    missing = loop_runner.request("counter.statistics", args={
        **common, "time_range": {"begin": "@missing", "end": "300ps"},
    })
    assert not missing.get("ok"), missing
    assert missing["error"]["code"] == "INVALID_TIME"
    assert "waveform cursor not found: missing" in missing["error"]["message"]


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


def test_counter_and_sampled_pulse_preserve_x_from_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    time_range = {"begin": "0ps", "end": "20ns"}
    clock = "top.masslav_if.clk"
    unknown = "top.masslav_if.Pslave_err"

    counter = loop_runner.request("counter.statistics", args={
        "cnt": unknown, "clock": clock, "vld": clock,
        "edge": "posedge", "sample_point": "after",
        "time_range": time_range,
    })
    assert counter.get("ok"), counter
    assert counter["summary"]["sample_count"] == 2
    assert counter["summary"]["unknown_count"] == 2

    pulse = loop_runner.request("signal.sampled_pulse.inspect", args={
        "valid": unknown, "clock": clock,
        "payloads": ["top.masslav_if.Pwdata"],
        "edge": "posedge", "sample_point": "after",
        "rules": {"payload_changed_without_sampled_valid": "all"},
        "time_range": time_range,
    })
    assert pulse.get("ok"), pulse
    assert pulse["summary"]["payload_risk_count"] == 1
    value = pulse["data"]["findings"][0]["sampled_payloads"][0]["value"]
    assert value["known"] is False
    assert value["has_x"] is True
    assert "x" in value["bits"]


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


def test_signal_sampled_pulse_line_limit_marks_truncation(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("signal.sampled_pulse.inspect", args={
        "valid": "top.in_valid", "clock": "top.clk",
        "payloads": ["top.in_data", "top.in_ready"],
        "edge": "posedge", "sample_point": "after",
        "rules": {"payload_changed_without_sampled_valid": "all"},
        "time_range": {"begin": "0ps", "end": "500ps"},
        "line_limit": 1,
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] > 1
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["truncation_scopes"] == ["response_findings"]
    assert len(rsp["data"]["findings"]) == 1


def test_sampled_pulse_and_handshake_empty_before_first_edge(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    empty_range = {"begin": "0ps", "end": "5ps"}
    pulse = loop_runner.request("signal.sampled_pulse.inspect", args={
        "valid": "top.in_valid", "clock": "top.clk",
        "edge": "posedge",
        "sample_point": "after", "time_range": empty_range,
        "line_limit": 10,
    })
    assert pulse.get("ok"), pulse
    assert pulse["summary"]["sample_count"] == 0
    assert pulse["summary"]["total_count"] == 0
    assert pulse["data"]["findings"] == []

    handshake = loop_runner.request("protocol.handshake.inspect", args={
        "clock": "top.clk", "valid": "top.in_valid",
        "ready": "top.in_ready", "data": "top.in_data",
        "edge": "posedge", "sample_point": "after",
        "time_range": empty_range,
        "rules": {
            "max_wait_cycles": 1,
            "check_data_stable_when_stalled": True,
            "ready_without_valid": "summary",
        },
    })
    assert handshake.get("ok"), handshake
    assert handshake["summary"]["sample_count"] == 0
    assert handshake["summary"]["total_count"] == 0
    assert handshake["data"]["findings"] == []


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


def test_protocol_handshake_reports_z_valid_from_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_processor_fst) -> None:
    open_session(loop_runner, wellen_processor_fst)
    rsp = loop_runner.request("protocol.handshake.inspect", args={
        "clock": "tb_processor.clk",
        "valid": ("tb_processor.uut.data_block_instantiation."
                  "Instruction_register.data_2_ir"),
        "ready": "tb_processor.rst",
        "edge": "posedge", "sample_point": "after",
        "time_range": {"begin": "40ns", "end": "60ns"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["valid_hold_violations"] == 1
    finding = rsp["data"]["findings"][0]
    assert finding["type"] == "valid_dropped_before_handshake"
    assert finding["reason"] == "valid became unknown before a handshake"
    assert finding["observed_valid"]["known"] is False
    assert finding["observed_valid"]["has_z"] is True
    assert finding["observed_valid"]["bits"] == "zzzzzzzz"


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
