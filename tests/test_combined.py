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


def test_trace_active_driver_preserves_query_and_active_time(
        loop_runner: StdioLoopRunner, counter_fst, counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.count", "time": "305ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["time"] == "305ps"
    assert rsp["summary"]["active_time"] == "300ps"


def test_trace_active_driver_selects_runtime_else_branch(
        loop_runner: StdioLoopRunner, counter_fst, counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.count", "time": "305ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "assignment"
    assert rsp["summary"]["total_count"] == 1
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["data"]["paths"][0]["line"] == 12
    assert rsp["data"]["paths"][0]["signal_path"] == ["top.reset", "top.count"]


def test_trace_active_driver_selects_nested_apb_read_branch(
        loop_runner: StdioLoopRunner, apb_fst, apb_design_db) -> None:
    open_session(loop_runner, apb_fst, apb_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.prdata", "time": "280ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["active_time"] == "260ps"
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["total_count"] == 1
    assert rsp["data"]["paths"] == [{
        "file": "apb_top.sv",
        "line": 25,
        "signal_path": ["top.paddr", "top.prdata"],
        "source_context": [],
    }]


def test_trace_active_driver_selects_case_item_and_default(
        loop_runner: StdioLoopRunner, case_fst, case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    selected = loop_runner.request("trace.active_driver", args={
        "signal": "top.out", "time": "45ps",
        "render_time_unit": "ps"})
    assert selected.get("ok"), selected
    assert selected["summary"]["analysis_complete"] is True
    assert selected["summary"]["total_count"] == 1
    assert selected["data"]["paths"][0]["line"] == 16

    default = loop_runner.request("trace.active_driver", args={
        "signal": "top.out", "time": "65ps",
        "render_time_unit": "ps"})
    assert default.get("ok"), default
    assert default["summary"]["analysis_complete"] is True
    assert default["summary"]["total_count"] == 1
    assert default["data"]["paths"][0]["line"] == 17


def test_trace_active_driver_selects_casez_and_casex_wildcards(
        loop_runner: StdioLoopRunner, case_fst, case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    casez = loop_runner.request("trace.active_driver", args={
        "signal": "top.out_casez", "time": "65ps",
        "render_time_unit": "ps"})
    assert casez.get("ok"), casez
    assert casez["summary"]["analysis_complete"] is True
    assert casez["summary"]["total_count"] == 1
    assert casez["data"]["paths"][0]["line"] == 28

    casex = loop_runner.request("trace.active_driver", args={
        "signal": "top.out_casex", "time": "45ps",
        "render_time_unit": "ps"})
    assert casex.get("ok"), casex
    assert casex["summary"]["analysis_complete"] is True
    assert casex["summary"]["total_count"] == 1
    assert casex["data"]["paths"][0]["line"] == 32


def test_trace_active_driver_selects_exact_case_matches_item_and_default(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    selected = loop_runner.request("trace.active_driver", args={
        "signal": "top.out", "time": "45ps",
        "render_time_unit": "ps"})
    assert selected.get("ok"), selected
    assert selected["summary"]["analysis_complete"] is True
    assert selected["summary"]["total_count"] == 1
    assert selected["data"]["paths"][0]["line"] == 15

    default = loop_runner.request("trace.active_driver", args={
        "signal": "top.out", "time": "65ps",
        "render_time_unit": "ps"})
    assert default.get("ok"), default
    assert default["summary"]["analysis_complete"] is True
    assert default["summary"]["total_count"] == 1
    assert default["data"]["paths"][0]["line"] == 16


def test_trace_active_driver_selects_standalone_matches_predicates(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    exact = loop_runner.request("trace.active_driver", args={
        "signal": "top.matches_top.exact_match_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert exact.get("ok"), exact
    assert exact["summary"]["analysis_complete"] is True
    assert exact["summary"]["total_count"] == 1
    assert exact["data"]["paths"][0]["signal_path"] == [
        "top.data", "top.matches_top.exact_match_out"]

    exact_default = loop_runner.request("trace.active_driver", args={
        "signal": "top.matches_top.exact_match_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert exact_default.get("ok"), exact_default
    assert exact_default["summary"]["analysis_complete"] is True
    assert exact_default["summary"]["total_count"] == 1
    assert exact_default["data"]["paths"][0]["line"] == 77

    wildcard = loop_runner.request("trace.active_driver", args={
        "signal": "top.matches_top.wildcard_match_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert wildcard.get("ok"), wildcard
    assert wildcard["summary"]["analysis_complete"] is True
    assert wildcard["summary"]["total_count"] == 1
    assert wildcard["data"]["paths"][0]["signal_path"] == [
        "top.data", "top.matches_top.wildcard_match_out"]


def test_trace_active_driver_selects_default_only_case_matches(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    for query_time in ("45ps", "65ps"):
        rsp = loop_runner.request("trace.active_driver", args={
            "signal": "top.matches_top.default_only_out",
            "time": query_time, "render_time_unit": "ps"})
        assert rsp.get("ok"), rsp
        assert rsp["summary"]["analysis_complete"] is True
        assert rsp["summary"]["termination"] == "assignment"
        assert rsp["summary"]["returned_count"] == 1
        path = rsp["data"]["paths"][0]
        assert path["line"] == 95
        assert path["signal_path"] == [
            "top.data", "top.matches_top.default_only_out"]


def test_trace_active_driver_chain_skips_nba_self_hold_event(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.hold_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.matches_top.hold_out",
        "top.matches_top.hold_q",
        "top.data",
    ]
    assert rsp["data"]["hops"][0]["active_time"] == "40ps"
    assert rsp["data"]["hops"][1]["active_time"] == "40ps"
    assert rsp["data"]["hops"][1]["line"] == 104


def test_trace_active_driver_chain_skips_inactive_gated_nba_event(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.gated_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.matches_top.gated_out",
        "top.matches_top.gated_q",
        "top.data",
    ]
    assert rsp["data"]["hops"][0]["active_time"] == "40ps"
    assert rsp["data"]["hops"][1]["active_time"] == "40ps"
    assert rsp["data"]["hops"][1]["line"] == 114


def test_trace_active_driver_chain_skips_same_line_ternary_self_hold(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.ternary_hold_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.matches_top.ternary_hold_out",
        "top.matches_top.ternary_hold_q",
        "top.data",
    ]
    assert rsp["data"]["hops"][0]["active_time"] == "20ps"
    assert rsp["data"]["hops"][1]["active_time"] == "20ps"
    assert rsp["data"]["hops"][1]["line"] == 121


def test_trace_active_driver_chain_propagates_through_nba_active_time(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.temporal_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    hops = rsp["data"]["hops"]
    assert [hop["signal"] for hop in hops] == [
        "top.matches_top.temporal_out",
        "top.matches_top.temporal_q",
        "top.data",
    ]
    assert hops[0]["time"] == "65ps"
    assert hops[0]["active_time"] == "60ps"
    assert hops[1]["time"] == "60ps"
    assert hops[1]["active_time"] == "60ps"


def test_trace_active_driver_chain_propagates_nba_time_through_alias_chain(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.temporal_deep", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    hops = rsp["data"]["hops"]
    assert [hop["signal"] for hop in hops] == [
        "top.matches_top.temporal_deep",
        "top.matches_top.temporal_mid",
        "top.matches_top.temporal_q",
        "top.data",
    ]
    assert [hop["active_time"] for hop in hops[:3]] == [
        "60ps", "60ps", "60ps",
    ]
    assert hops[1]["time"] == "60ps"
    assert hops[2]["time"] == "60ps"


def test_trace_active_driver_chain_uses_async_reset_event_time(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.async_out", "time": "35ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "assignment"
    assert rsp["summary"]["termination_detail"] == \
        "constant_or_no_rhs_signal"
    hops = rsp["data"]["hops"]
    assert [hop["signal"] for hop in hops] == [
        "top.matches_top.async_out",
        "top.matches_top.async_q",
    ]
    assert hops[0]["time"] == "35ps"
    assert hops[0]["active_time"] == "30ps"
    assert hops[1]["time"] == "30ps"
    assert hops[1]["active_time"] == "30ps"


def test_trace_active_driver_chain_uses_changed_event_time(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.changed_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    hops = rsp["data"]["hops"]
    assert [hop["signal"] for hop in hops] == [
        "top.matches_top.changed_out",
        "top.matches_top.changed_q",
        "top.data",
    ]
    assert [hop["active_time"] for hop in hops[:2]] == ["60ps", "60ps"]
    assert hops[1]["time"] == "60ps"


def test_trace_active_driver_chain_prefers_unique_nba_over_blocking_write(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.mixed_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    hops = rsp["data"]["hops"]
    assert [hop["signal"] for hop in hops] == [
        "top.matches_top.mixed_out",
        "top.matches_top.mixed_q",
        "top.data",
    ]
    assert hops[1]["line"] == 68
    assert [hop["active_time"] for hop in hops[:2]] == ["60ps", "60ps"]


def test_trace_active_driver_chain_keeps_two_active_nbas_ambiguous(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.double_nba_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == \
        "multiple_active_candidates"
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["statement_count"] == 2
    assert {statement["line"] for statement in evidence["statements"]} == {
        88, 89}


def test_trace_active_driver_chain_stops_at_force(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.forced_out", "time": "35ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "force"
    assert rsp["summary"]["termination_detail"] == "force"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.matches_top.forced_out",
        "top.matches_top.forced_q",
    ]

    released = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.matches_top.forced_out", "time": "55ps",
        "render_time_unit": "ps"})
    assert released.get("ok"), released
    assert released["summary"]["analysis_complete"] is True
    assert released["summary"]["termination"] == "assignment"
    assert released["summary"]["termination_detail"] == \
        "constant_or_no_rhs_signal"
    assert [hop["signal"] for hop in released["data"]["hops"]] == [
        "top.matches_top.forced_out",
        "top.matches_top.forced_q",
    ]


def test_trace_active_driver_reports_force_as_resolved_driver(
        loop_runner: StdioLoopRunner, matches_fst,
        matches_design_db) -> None:
    open_session(loop_runner, matches_fst, matches_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.matches_top.forced_q", "time": "35ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "force"
    assert rsp["summary"]["termination_detail"] == "force"
    assert rsp["summary"]["total_count"] == 1
    assert rsp["data"]["paths"] == [{
        "file": "testdata/fixtures/matches/matches_top.sv",
        "line": 58,
        "source_context": [],
        "signal_path": [
            "top.data",
            "top.matches_top.forced_q",
        ],
    }]


def test_trace_active_driver_preserves_lowered_nested_if_identity(
        loop_runner: StdioLoopRunner, case_fst, case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.nested_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["total_count"] == 1
    assert rsp["data"]["paths"][0]["line"] == 45


def test_trace_active_driver_keeps_same_line_ternary_branches_separate(
        loop_runner: StdioLoopRunner, case_fst, case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    signal_branch = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.ternary_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert signal_branch.get("ok"), signal_branch
    assert signal_branch["summary"]["analysis_complete"] is True
    assert signal_branch["summary"]["total_count"] == 1
    assert signal_branch["data"]["paths"][0]["signal_path"] == [
        "top.data", "top.case_top.ternary_out"]

    constant_branch = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.ternary_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert constant_branch.get("ok"), constant_branch
    assert constant_branch["summary"]["analysis_complete"] is True
    assert constant_branch["summary"]["total_count"] == 1
    assert constant_branch["data"]["paths"][0]["signal_path"] == [
        "top.sel", "top.case_top.ternary_out"]


def test_trace_active_driver_counts_before_response_limit(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "GCD.GEN_1", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_results": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 2
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["truncation_scopes"] == ["response_paths"]


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


def test_trace_active_driver_chain_distinguishes_internal_zero_evidence_from_primary_input(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    internal = loop_runner.request("trace.active_driver_chain", args={
        "signal": "GCD.T_13", "time": "0ps",
        "render_time_unit": "ps"})
    assert internal.get("ok"), internal
    assert internal["summary"]["termination"] == "unresolved"
    assert internal["summary"]["termination_detail"] == "unresolved"
    assert internal["summary"]["returned_count"] == 1
    assert internal["data"]["hops"][0]["signal"] == "GCD.T_13"

    primary = loop_runner.request("trace.active_driver_chain", args={
        "signal": "GCD.io_a", "time": "0ps",
        "render_time_unit": "ps"})
    assert primary.get("ok"), primary
    assert primary["summary"]["termination"] == "primary_input"
    assert primary["summary"]["termination_detail"] == "primary_input"
    assert primary["summary"]["returned_count"] == 1
    assert primary["data"]["hops"][0]["signal"] == "GCD.io_a"


def test_trace_active_driver_chain_stops_at_parent_primary_input_alias(
        loop_runner: StdioLoopRunner, counter_fst,
        counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.counter_top.clk", "time": "305ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.counter_top.clk", "top.clk"]


def test_trace_active_driver_chain_classifies_nba_self_rhs_as_assignment(
        loop_runner: StdioLoopRunner, counter_fst,
        counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.count", "time": "305ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "assignment"
    assert rsp["summary"]["termination_detail"] == \
        "constant_or_no_rhs_signal"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == ["top.count"]


def test_trace_active_driver_chain_reports_real_multiple_active_drivers(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.multiple_driver_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == \
        "multiple_active_candidates"
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["kind"] == "multiple_active_candidates"
    assert evidence["statement_count"] == 2
    assert evidence["rhs_signal_count"] == 2
    assert {statement["line"] for statement in evidence["statements"]} == {
        54, 55}


def test_trace_active_driver_selects_case_inside_pattern_and_range(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    pattern = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.inside_out", "time": "65ps",
        "render_time_unit": "ps"})
    assert pattern.get("ok"), pattern
    assert pattern["summary"]["analysis_complete"] is True
    assert {path["line"] for path in pattern["data"]["paths"]} == {60}

    range_item = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.inside_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert range_item.get("ok"), range_item
    assert range_item["summary"]["analysis_complete"] is True
    assert {path["line"] for path in range_item["data"]["paths"]} == {61}


def test_trace_active_driver_chain_traverses_inout_alias_toward_parent_driver(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.u_inout.bus", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.case_top.u_inout.bus", "top.case_top.inout_bus",
        "top.case_top.data", "top.data"]


def test_trace_active_driver_chain_traverses_two_inout_boundaries_parentward(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.u_inout_mid.u_leaf.bus", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.case_top.u_inout_mid.u_leaf.bus",
        "top.case_top.u_inout_mid.leaf_bus",
        "top.case_top.u_inout_mid.bus",
        "top.case_top.nested_inout_bus",
        "top.case_top.data", "top.data"]


def test_trace_active_driver_chain_enters_child_output_from_parent_net(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.child_output_bus", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.case_top.child_output_bus",
        "top.case_top.u_output.data_o",
        "top.case_top.u_output.data_i",
        "top.case_top.data", "top.data"]


def test_trace_active_driver_selects_one_conditional_procedural_driver(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.procedural_multi_out", "time": "25ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["total_count"] == 1
    assert rsp["data"]["paths"][0]["line"] == 83


def test_trace_active_driver_chain_reports_two_active_procedural_drivers(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.procedural_multi_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == \
        "multiple_active_candidates"
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["statement_count"] == 2
    assert {statement["line"] for statement in evidence["statements"]} == {
        83, 87}


def test_trace_active_driver_chain_reports_cross_instance_output_drivers(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.cross_instance_multi_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == \
        "multiple_active_candidates"
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["kind"] == "multiple_active_candidates"
    assert evidence["statement_count"] == 2
    assert evidence["rhs_signal_count"] == 2


def test_trace_active_driver_selects_nested_nba_constant_and_signal_leaves(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    reset_constant = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.constant_nba_out", "time": "5ps",
        "render_time_unit": "ps"})
    assert reset_constant.get("ok"), reset_constant
    assert reset_constant["summary"]["analysis_complete"] is True
    assert reset_constant["summary"]["termination"] == "assignment"
    assert reset_constant["summary"]["termination_detail"] == \
        "constant_or_no_rhs_signal"
    assert reset_constant["data"]["hops"][0]["line"] == 103

    else_constant = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.constant_nba_out", "time": "25ps",
        "render_time_unit": "ps"})
    assert else_constant.get("ok"), else_constant
    assert else_constant["summary"]["analysis_complete"] is True
    assert else_constant["summary"]["termination_detail"] == \
        "constant_or_no_rhs_signal"
    assert else_constant["data"]["hops"][0]["line"] == 107

    signal_leaf = loop_runner.request("trace.active_driver", args={
        "signal": "top.case_top.constant_nba_out", "time": "45ps",
        "render_time_unit": "ps"})
    assert signal_leaf.get("ok"), signal_leaf
    assert signal_leaf["summary"]["analysis_complete"] is True
    assert signal_leaf["summary"]["total_count"] == 1
    assert signal_leaf["data"]["paths"][0]["line"] == 105
    assert signal_leaf["data"]["paths"][0]["signal_path"] == [
        "top.data", "top.case_top.constant_nba_out"]


def test_trace_active_driver_chain_enters_child_output_before_rhs_ambiguity(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.child_output_expr_bus", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == "multiple_rhs_sources"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.case_top.child_output_expr_bus",
        "top.case_top.u_output_expr.data_o"]
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["statement_count"] == 1
    assert evidence["rhs_signal_count"] == 2
    assert {
        sample["signal"]
        for sample in evidence["statements"][0]["rhs_samples"]
    } == {
        "top.case_top.u_output_expr.data_i",
        "top.case_top.u_output_expr.sel_i",
    }


def test_trace_active_driver_chain_reports_same_instance_output_pair(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.paired_output_bus", "time": "45ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == \
        "multiple_active_candidates"
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["statement_count"] == 2
    assert evidence["rhs_signal_count"] == 2
    assert {statement["line"] for statement in evidence["statements"]} == {
        167, 168}


def test_trace_active_driver_chain_selects_conditional_child_output(
        loop_runner: StdioLoopRunner, case_fst,
        case_design_db) -> None:
    open_session(loop_runner, case_fst, case_design_db)
    signal_branch = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.conditional_output_bus", "time": "45ps",
        "render_time_unit": "ps"})
    assert signal_branch.get("ok"), signal_branch
    assert signal_branch["summary"]["analysis_complete"] is True
    assert signal_branch["summary"]["termination"] == "primary_input"
    assert [hop["signal"] for hop in signal_branch["data"]["hops"]] == [
        "top.case_top.conditional_output_bus",
        "top.case_top.u_output_cond.data_o",
        "top.case_top.u_output_cond.data_i",
        "top.case_top.data",
        "top.data",
    ]

    constant_branch = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.case_top.conditional_output_bus", "time": "25ps",
        "render_time_unit": "ps"})
    assert constant_branch.get("ok"), constant_branch
    assert constant_branch["summary"]["analysis_complete"] is True
    assert constant_branch["summary"]["termination"] == "assignment"
    assert constant_branch["summary"]["termination_detail"] == \
        "constant_or_no_rhs_signal"
    assert [hop["signal"] for hop in constant_branch["data"]["hops"]] == [
        "top.case_top.conditional_output_bus",
        "top.case_top.u_output_cond.data_o",
    ]
    assert constant_branch["data"]["hops"][-1]["line"] == 180


def test_trace_active_driver_chain_preserves_parent_and_child_output_drivers(
        loop_runner: StdioLoopRunner, output_mixed_fst,
        output_mixed_design_db) -> None:
    open_session(loop_runner, output_mixed_fst, output_mixed_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "top.output_mixed_top.mixed_bus", "time": "5ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == \
        "multiple_active_candidates"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "top.output_mixed_top.mixed_bus"]
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["statement_count"] == 2
    assert evidence["rhs_signal_count"] == 2
    assert {statement["line"] for statement in evidence["statements"]} == {
        13, 21}


def test_trace_active_driver_chain_honors_max_nodes(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "GCD.io_z", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_nodes": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "limit"
    assert rsp["summary"]["termination_detail"] == "max_nodes"
    assert rsp["summary"]["analysis_complete"] is False
    assert rsp["summary"]["truncation_scopes"] == ["analysis_trace"]
    assert len(rsp["data"]["hops"]) == 1


def test_trace_active_driver_chain_reports_multi_rhs_ambiguity(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "GCD.GEN_0", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "ambiguous"
    assert rsp["summary"]["termination_detail"] == "multiple_rhs_sources"
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["statement_count"] == 1
    assert evidence["rhs_signal_count"] == 2
    assert [sample["signal"] for sample in evidence["statements"][0]["rhs_samples"]] == [
        "GCD.x", "GCD.y"]


def test_trace_active_driver_chain_depth_frontier(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "GCD.io_z", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_depth": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "limit"
    assert rsp["summary"]["termination_detail"] == "max_depth"
    assert [hop["signal"] for hop in rsp["data"]["hops"]] == [
        "GCD.io_z", "GCD.x"]
    assert rsp["data"]["depth_frontiers"] == [{
        "chain_id": "c0", "signal": "GCD.io_a", "time": "0ps",
        "value": "32'hxxxxxxxx", "stopped_after_depth": 1}]
    assert [item["reason"] for item in rsp["data"]["suggested_next_actions"]] == [
        "continue_from_depth_frontier", "rerun_from_root_with_higher_depth"]


def test_trace_active_driver_chain_ambiguity_sampling_limit(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.active_driver_chain", args={
        "signal": "GCD.GEN_0", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_trace_signals": 1})
    assert rsp.get("ok"), rsp
    evidence = rsp["data"]["ambiguity_evidence"]
    assert evidence["rhs_signal_count"] == 2
    assert evidence["returned_rhs_signal_count"] == 1
    assert evidence["omitted_rhs_signal_count"] == 1
    assert evidence["analysis_complete"] is False
    assert rsp["summary"]["analysis_complete"] is False
    assert rsp["summary"]["truncation_scopes"] == ["ambiguity_rhs_samples"]


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


def test_trace_x_origin_branches_on_x_control_and_x_rhs(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xpredicate_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xpredicate_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.T_14", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["chain_count"] == 2
    relation_paths = [
        {hop["relation"] for hop in chain["hops"]}
        for chain in rsp["data"]["chains"]
    ]
    assert any("control" in relations for relations in relation_paths)
    assert any("rhs" in relations for relations in relation_paths)
    assert {chain["current"]["signal"]
            for chain in rsp["data"]["chains"]} == {
        "GCD.io_a",
        "GCD.y",
    }


def test_trace_x_origin_opaque_predicate_is_pending(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_unresolved_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_unresolved_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.T_14", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "pending"
    assert rsp["summary"]["evidence_status"] == "unresolved"
    assert rsp["summary"]["analysis_complete"] is False
    assert rsp["summary"]["chain_count"] == 1
    chain = rsp["data"]["chains"][0]
    assert chain["status"] == "unresolved"
    assert chain["termination_detail"] == "predicate_unresolved"
    assert chain["complete"] is False


def test_trace_x_origin_stops_at_force_x(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.y", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "origin_found"
    assert rsp["summary"]["origin_count"] == 1
    chain = rsp["data"]["chains"][0]
    assert chain["status"] == "origin_found"
    assert chain["termination_detail"] == "force_x"
    assert [hop["signal"] for hop in chain["hops"]] == ["GCD.y"]
    assert chain["origin"] == {
        "signal": "GCD.y",
        "x_onset_time": "0ps",
        "kind": "force",
        "reason": "force_x",
        "evidence_status": "proven",
        "file": "gcd_xorigin.sv",
        "line": 10,
    }


def test_trace_x_origin_branch_chain_ids_are_consistent(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.GEN_0", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["chain_count"] == 2
    assert {chain["origin"]["signal"] for chain in rsp["data"]["chains"]} == {
        "GCD.io_a", "GCD.y"}
    for chain in rsp["data"]["chains"]:
        assert all(hop["chain_id"] == chain["chain_id"] for hop in chain["hops"])


def test_trace_x_origin_tracks_control_and_per_signal_width(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.T_14", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    origins = {chain["origin"]["signal"] for chain in rsp["data"]["chains"]}
    assert origins == {"GCD.io_a", "GCD.y"}
    control_chain = next(chain for chain in rsp["data"]["chains"]
                         if chain["origin"]["signal"] == "GCD.y")
    assert control_chain["hops"][-1]["relation"] == "control"
    assert control_chain["hops"][-1]["value"]["width"] == 32
    assert rsp["data"]["query"]["value"]["width"] == 33


def test_trace_x_origin_depth_frontier(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.io_z", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_depth": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "limit"
    assert [hop["signal"] for hop in rsp["data"]["chains"][0]["hops"]] == [
        "GCD.io_z", "GCD.x"]
    assert rsp["data"]["depth_frontiers"][0]["signal"] == "GCD.io_a"
    assert rsp["data"]["depth_frontiers"][0]["continue_time"] == "0ps"
    assert [item["reason"] for item in rsp["data"]["suggested_next_actions"]] == [
        "continue_from_depth_frontier", "rerun_from_root_with_higher_depth"]


def test_trace_x_origin_time_step_limit_counts_distinct_x_onsets(
        loop_runner: StdioLoopRunner, wide_xz_fst,
        xorigin_time_design_db) -> None:
    open_session(loop_runner, wide_xz_fst, xorigin_time_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": (
            "AXI_top_tb_from_compiled.dut.a_regex_coprocessor.genblk1."
            "a_topology.genblk1[0].genblk1[0].engine_and_station_i.anEngine."
            "anEngine.g.aregex_cpu.EXE2_Instr"),
        "time": "55215000ps", "render_time_unit": "ps"},
        limits={"max_time_steps": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "limit"
    assert rsp["summary"]["analysis_complete"] is False
    assert rsp["summary"]["chain_count"] == 1
    assert rsp["data"]["chains"][0]["termination_detail"] == "max_time_steps"
    assert "trace truncated by limits.max_time_steps" in rsp["data"]["limitations"]
    assert rsp["data"]["chains"][0]["current"]["x_onset_time"] == "55015000ps"


def test_trace_x_origin_node_limit_stops_at_pending_dependency(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.io_z", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_nodes": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "limit"
    chain = rsp["data"]["chains"][0]
    assert [hop["signal"] for hop in chain["hops"]] == ["GCD.io_z"]
    assert chain["current"]["signal"] == "GCD.x"
    assert chain["termination_detail"] == "max_nodes"


def test_trace_x_origin_dependency_sampling_limit_is_explicit(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.T_14", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_trace_signals": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["termination"] == "limit"
    assert rsp["summary"]["analysis_complete"] is False
    chain = rsp["data"]["chains"][0]
    assert chain["complete"] is False
    assert chain["pending_x_dependencies"] == [{
        "signal": "GCD.y", "relation": "control",
        "reason": "max_trace_signals"}]


def test_trace_x_origin_chain_limit_preserves_omitted_branch(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.T_14", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_chains": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["chain_count"] == 1
    assert rsp["summary"]["termination"] == "limit"
    chain = rsp["data"]["chains"][0]
    assert chain["complete"] is False
    event = chain["branch_events"][0]
    assert event["reason"] == "max_chains"
    assert event["x_dependency_count"] == 2
    assert event["returned_x_dependency_count"] == 1
    assert event["omitted_x_dependency_count"] == 1
    assert event["pending_x_dependencies"][0]["signal"] == "GCD.y"


def test_trace_x_origin_coalesces_port_aliases_before_chain_limit(
        loop_runner: StdioLoopRunner, gcd_xorigin_fst,
        xorigin_alias_design_db) -> None:
    open_session(loop_runner, gcd_xorigin_fst, xorigin_alias_design_db)
    rsp = loop_runner.request("trace.x_origin", args={
        "signal": "GCD.T_14", "time": "0ps",
        "render_time_unit": "ps"}, limits={"max_chains": 2})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["chain_count"] == 2
    assert rsp["summary"]["completed_chain_count"] == 2
    assert rsp["summary"]["limited_chain_count"] == 0
    assert rsp["summary"]["termination"] == "origin_found"
    assert rsp["summary"]["analysis_complete"] is True
    assert {chain["current"]["signal"] for chain in rsp["data"]["chains"]} == {
        "GCD.io_a", "GCD.y"}
    assert all(chain["complete"] is True for chain in rsp["data"]["chains"])
    assert any(hop["relation"] == "port"
               for chain in rsp["data"]["chains"]
               for hop in chain["hops"])


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
