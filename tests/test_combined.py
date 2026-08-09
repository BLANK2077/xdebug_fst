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
        "signal": "top.counter_top.count", "time": "305ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["time"] == "305ps"
    assert rsp["summary"]["active_time"] == "300ps"


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
    assert origins == {"GCD.io_a", "GCD.T_13"}
    control_chain = next(chain for chain in rsp["data"]["chains"]
                         if chain["origin"]["signal"] == "GCD.T_13")
    assert control_chain["hops"][-1]["relation"] == "control"
    assert control_chain["hops"][-1]["value"]["width"] == 1
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
        "signal": "GCD.T_13", "relation": "control",
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
    assert event["pending_x_dependencies"][0]["signal"] == "GCD.T_13"


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
