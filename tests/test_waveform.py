# test_waveform.py — waveform basic actions (BSD-3-Clause)
"""value.at, signal.changes, scope.list, scope.roots."""
from __future__ import annotations

import pytest

from conftest import AXI_CONFIG, STREAM_CONFIG, open_session
from runner import StdioLoopRunner


def _value(loop, signal, time):
    rsp = loop.request("value.at", args={"signal": signal, "time": f"{time}ps"})
    assert rsp.get("ok"), rsp
    return rsp


def test_value_at_bit_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "top.clk", 300)
    assert rsp["summary"] == {
        "source_kind": "signal", "source_name": "top.clk",
        "sampling_mode": "raw_time", "time_count": 1, "entry_count": 1,
        "value_width_complete": True, "width_diagnostics": [],
    }
    assert rsp["data"]["entries"] == [
        {"key": "top.clk", "kind": "signal", "path": "top.clk"}]
    assert rsp["data"]["samples"][0]["time"] == "0.3ns"
    v = rsp["data"]["samples"][0]["values"][0]["value"]
    assert v["width"] == 1
    assert v["known"] is True
    assert v["value"] in ("1'h0", "1'h1")


def test_value_at_bus_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "top.counter_top.count", 300)
    v = rsp["data"]["samples"][0]["values"][0]["value"]
    assert v["width"] == 8
    assert v["value"] == "8'h0b"
    assert v["bits"] == "00001011"


def test_value_at_case_insensitive_and_top_prefix(loop_runner: StdioLoopRunner,
                                                  counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "TOP.counter_top.count", 300)
    assert rsp["data"]["samples"][0]["values"][0]["value"]["value"] == "8'h0b"


def test_value_at_unknown_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={"signal": "nope.sig", "time": "100"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["samples"][0]["values"] == [
        {"key": "nope.sig", "status": "signal_not_found"}]


def test_value_at_missing_fields(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={"signal": "top.clk"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"
    assert rsp["error"]["error_layer"] == "schema"


def test_value_at_invalid_time(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at",
                              args={"signal": "top.clk", "time": "abc"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_TIME"


def test_value_at_render_formats(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _value(loop_runner, "top.counter_top.count", 300)
    rsp_bin = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "value_format": "bin"})
    assert rsp_bin["data"]["samples"][0]["values"][0]["value"]["value"] == \
        "8'b00001011"
    rsp_dec = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "value_format": "dec"})
    assert rsp_dec["data"]["samples"][0]["values"][0]["value"]["value"] == \
        "8'd11"


def test_value_at_times_and_render_unit(loop_runner: StdioLoopRunner,
                                        counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "times": ["200ps", "300ps"],
        "render_time_unit": "us",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["time_count"] == 2
    assert [sample["time"] for sample in rsp["data"]["samples"]] == [
        "0.0002us", "0.0003us"]
    assert [sample["values"][0]["value"]["value"]
            for sample in rsp["data"]["samples"]] == ["8'h06", "8'h0b"]


def test_value_at_clock_context(loop_runner: StdioLoopRunner,
                                counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "clock": "top.clk", "edge": "dual", "sample_point": "after",
    })
    assert rsp.get("ok"), rsp
    sample = rsp["data"]["samples"][0]
    assert sample["sampling_mode"] == "clock_sampled"
    context = sample["clock_context"]
    assert context["requested_sampling"] == {
        "edge": "dual", "sample_point": "after"}
    assert context["effective_sampling"] == {
        "edge": "dual", "sample_point": "after"}
    assert context["requested_any_edge_hit"] is True
    assert context["requested_target_edge_hit"] is True
    assert context["clock_edge_kind"] in ("posedge", "negedge")


def test_value_at_xbit_slice_hint(loop_runner: StdioLoopRunner,
                                  counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "300ps",
        "slice_hint": {"chunk_width": 4, "count": 2},
    })
    assert rsp.get("ok"), rsp
    hints = rsp["data"]["samples"][0]["values"][0]["xbit_hints"]
    assert hints["status"] == "ready"
    assert hints["raw_value"] == "8'h0b"
    assert hints["slices"] == [
        {"index": 0, "range": "[3:0]"},
        {"index": 1, "range": "[7:4]"},
    ]


def test_value_at_clock_miss_reports_missing_value(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "top.counter_top.count", "time": "301ps",
        "clock": "top.clk", "edge": "posedge",
    })
    assert rsp.get("ok"), rsp
    sample = rsp["data"]["samples"][0]
    assert sample["values"][0]["status"] == "missing_value"
    assert sample["clock_context"]["requested_any_edge_hit"] is False
    assert sample["clock_context"]["requested_target_edge_hit"] is False


def test_value_at_apb_source(loop_runner: StdioLoopRunner, apb_fst) -> None:
    open_session(loop_runner, apb_fst)
    loaded = loop_runner.request("apb.config.load", args={
        "name": "apb0", "config": {
            "clock": "top.pclk", "edge": "posedge", "sample_point": "after",
            "reset": {"signal": "top.presetn", "polarity": "active_low"},
            "paddr": "top.paddr", "psel": "top.psel",
            "penable": "top.penable", "pwrite": "top.pwrite",
            "pwdata": "top.pwdata", "prdata": "top.prdata",
            "pready": "top.pready", "pslverr": "top.pslverr",
        }})
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("value.at", args={
        "apb": "apb0", "time": "0ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["source_kind"] == "apb"
    assert rsp["summary"]["entry_count"] == 10
    assert [entry["key"] for entry in rsp["data"]["entries"]] == [
        "clock", "reset", "paddr", "psel", "penable", "pwrite", "pwdata",
        "prdata", "pready", "pslverr"]


def test_value_at_axi_source(loop_runner: StdioLoopRunner, axi_fst) -> None:
    open_session(loop_runner, axi_fst)
    rsp = loop_runner.request("axi.config.load",
                              args={"name": "axi0", "config": AXI_CONFIG})
    assert rsp.get("ok"), rsp
    rsp = loop_runner.request("value.at", args={
        "axi": "axi0", "times": ["0ps", "10ps"]})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["source_kind"] == "axi"
    assert rsp["summary"]["time_count"] == 2
    assert rsp["summary"]["entry_count"] == 31


def test_value_at_stream_source(loop_runner: StdioLoopRunner,
                                stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loaded = loop_runner.request("stream.config.load",
                                 args={"config": STREAM_CONFIG})
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("value.at", args={
        "stream": "fifo", "time": "0ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["source_kind"] == "stream"
    assert [entry["key"] for entry in rsp["data"]["entries"]] == [
        "clock", "valid", "ready", "data"]


def test_value_at_list_source(loop_runner: StdioLoopRunner,
                              counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    created = loop_runner.request("list.create", args={
        "name": "counter_context",
        "signals": ["top.clk", "top.counter_top.count"],
    })
    assert created.get("ok"), created
    rsp = loop_runner.request("value.at", args={
        "list": "counter_context", "times": ["200ps", "300ps"],
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["source_kind"] == "list"
    assert rsp["summary"]["entry_count"] == 2
    assert rsp["summary"]["time_count"] == 2
    assert [entry["path"] for entry in rsp["data"]["entries"]] == [
        "top.clk", "top.counter_top.count"]


def test_value_at_xout_renders_multiple_signals_and_times(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    created = loop_runner.request("list.create", args={
        "name": "xout_counter_context",
        "signals": ["top.clk", "top.counter_top.count"],
    })
    assert created.get("ok"), created

    xout = loop_runner.request_xout("value.at", args={
        "list": "xout_counter_context",
        "times": ["0ps", "100ps", "200ps", "300ps", "305ps"],
        "value_format": "hex", "render_time_unit": "ps",
    })
    assert xout.startswith("@xdebug.value.at.v1\nvalues:\n")
    assert "name" in xout
    for time in ("0ps", "100ps", "200ps", "300ps", "305ps"):
        assert time in xout
    assert "top.clk" in xout and "1'h1" in xout
    assert "top.counter_top.count" in xout
    for value in ("8'h0", "8'h1", "8'h6", "8'hb"):
        assert value in xout
    assert "known" not in xout and "width" not in xout


def test_value_at_preserves_x_and_decimal_fallback(
        loop_runner: StdioLoopRunner, wide_xz_fst) -> None:
    open_session(loop_runner, wide_xz_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "AXI_top_tb_from_compiled.dut.bram_r", "time": "0ps",
        "value_format": "dec",
    })
    assert rsp.get("ok"), rsp
    row = rsp["data"]["samples"][0]["values"][0]
    assert "value" in row, row
    value = row["value"]
    assert value["known"] is False
    assert value["has_x"] is True
    assert value["has_z"] is False
    assert value["requested_value_format"] == "dec"
    assert value["effective_value_format"] == "bin"
    assert value["width"] == 64
    assert "x" in value["bits"]


def test_value_at_preserves_settled_utf8_string_delta(
        loop_runner: StdioLoopRunner, string_delta_fst) -> None:
    open_session(loop_runner, string_delta_fst)
    signal = "string_test.test_string.[1:50]"
    rsp = loop_runner.request("value.at", args={
        "signal": signal, "time": "0ps", "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["value_width_complete"] is True
    row = rsp["data"]["samples"][0]["values"][0]
    assert row["status"] == "ok"
    assert row["value"]["known"] is True
    assert row["value"]["value"] == \
        "En lång röd räv" + " " * 35


def test_value_at_preserves_typed_real_value(
        loop_runner: StdioLoopRunner, real_fst) -> None:
    open_session(loop_runner, real_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "real_r", "time": "1ps", "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    row = rsp["data"]["samples"][0]["values"][0]
    assert row["status"] == "ok"
    assert row["value"]["known"] is True
    assert float(row["value"]["value"]) == pytest.approx(0.1)
    assert "width" not in row["value"]


def test_value_at_preserves_event_kind(
        loop_runner: StdioLoopRunner, event_fst) -> None:
    open_session(loop_runner, event_fst)
    rsp = loop_runner.request("value.at", args={
        "signal": "event_example.event1", "time": "0ps",
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    row = rsp["data"]["samples"][0]["values"][0]
    assert row == {"key": "event_example.event1", "status": "ok",
                   "value": {"known": True, "value": "event"}}


def test_signal_changes_preserves_same_time_string_deltas(
        loop_runner: StdioLoopRunner, string_delta_fst) -> None:
    open_session(loop_runner, string_delta_fst)
    rsp = loop_runner.request("signal.changes", args={
        "signal": "string_test.test_string.[1:50]",
        "time_range": {"begin": "0ps", "end": "max"},
        "render_time_unit": "ps", "line_limit": 10})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["actual_transition_count"] == 3
    assert rsp["summary"]["total_count"] == 4
    assert rsp["summary"]["returned_count"] == 4
    assert rsp["summary"]["scan_complete"] is True
    assert rsp["summary"]["analysis_complete"] is True
    changes = rsp["data"]["changes"]
    assert [change["time"] for change in changes] == [
        "0ps", "0ps", "10000ps", "20000ps"]
    assert changes[0]["value"] == {"known": True, "value": " " * 50}
    assert changes[1]["value"]["value"] == \
        "En lång röd räv" + " " * 35
    assert changes[2]["value"]["value"].startswith("Viel \"spaß\"")
    assert changes[3]["value"]["value"].startswith("3±0.3°C")


def test_signal_changes(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count",
        "time_range": {"begin": "0ps", "end": "490ps"},
        "render_time_unit": "ps"})
    assert rsp.get("ok")
    changes = rsp["data"]["changes"]
    assert len(changes) == 21
    assert changes[0]["time"] == "0ps"
    assert changes[0]["value"]["value"] == "8'h00"
    assert changes[1]["time"] == "100ps"
    assert changes[1]["value"]["value"] == "8'h01"
    assert rsp["summary"]["actual_transition_count"] == 20
    assert rsp["summary"]["total_count"] == 21


def test_signal_changes_window(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count",
        "time_range": {"begin": "200ps", "end": "300ps"},
        "render_time_unit": "ps"})
    assert rsp.get("ok")
    changes = rsp["data"]["changes"]
    assert changes[0]["time"] == "200ps"
    assert changes[-1]["time"] == "300ps"
    assert len(changes) == 6


def test_signal_changes_empty_summary(loop_runner: StdioLoopRunner,
                                      counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count",
        "mode": "summary",
        "time_range": {"begin": "490ps", "end": "490ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["actual_transition_count"] == 0
    assert rsp["data"]["mode"] == "summary"
    assert "changes" not in rsp["data"]


def test_signal_changes_missing_signal(loop_runner: StdioLoopRunner,
                                       counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.changes", args={"signal": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_signal_changes_summary_and_timeline_limit(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    summary = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count", "mode": "summary",
        "time_range": {"begin": "0ps", "end": "300ps"}})
    assert summary.get("ok"), summary
    assert summary["data"]["mode"] == "summary"
    assert "changes" not in summary["data"]
    assert summary["summary"]["actual_transition_count"] == 11
    limited = loop_runner.request("signal.changes", args={
        "signal": "top.counter_top.count", "mode": "timeline", "line_limit": 2,
        "time_range": {"begin": "0ps", "end": "300ps"}})
    assert limited.get("ok"), limited
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["total_count"] == 12
    assert limited["summary"]["returned_count"] == 2
    assert len(limited["data"]["changes"]) == 2


def test_scope_roots(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("scope.roots", args={"source": "auto"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["source"] == "auto"
    assert rsp["summary"]["wave_available"] is True
    assert rsp["summary"]["design_available"] is False
    assert rsp["summary"]["scan_complete"] is False
    assert rsp["summary"]["truncation_scopes"] == ["analysis_sources"]
    assert [r["path"] for r in rsp["data"]["roots"]] == ["top"]
    assert rsp["data"]["roots"][0]["status"] == "wave_only"
    assert rsp["data"]["wave_roots"][0]["queryable"] is True

    design_only = loop_runner.request("scope.roots", args={"source": "design"})
    assert design_only.get("ok"), design_only
    assert design_only["summary"]["resource_available"] is True
    assert design_only["summary"]["design_available"] is False
    assert design_only["summary"]["total_count"] == 0
    assert design_only["summary"]["returned_count"] == 0
    assert design_only["summary"]["analysis_complete"] is False
    assert design_only["summary"]["truncation_scopes"] == [
        "analysis_sources"
    ]
    assert design_only["data"]["roots"] == []


def test_scope_roots_reports_design_wave_mismatch(
        loop_runner: StdioLoopRunner, counter_fst, counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("scope.roots", args={"source": "auto"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["scan_complete"] is True
    assert rsp["summary"]["matched_count"] == 1
    assert rsp["summary"]["recommended_root"] == "top"
    assert rsp["summary"]["recommended_reason"] == "unique root"
    assert [(r["path"], r["status"]) for r in rsp["data"]["roots"]] == [
        ("top", "matched"),
    ]


def test_scope_roots_reports_multiple_direct_raw_fst_and_design_roots(
        loop_runner: StdioLoopRunner, counter_fst,
        gcd_xorigin_design_db) -> None:
    open_session(loop_runner, counter_fst, gcd_xorigin_design_db)
    rsp = loop_runner.request("scope.roots", args={"source": "auto"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 2
    assert rsp["summary"]["returned_count"] == 2
    assert [(root["path"], root["status"])
            for root in rsp["data"]["roots"]] == [
        ("GCD", "design_only"), ("top", "wave_only")]

    xout = loop_runner.request_xout("scope.roots", args={"source": "auto"})
    assert xout.startswith("@xdebug.scope.roots.v1\nsummary:\n")
    assert "roots:\n" in xout
    assert "GCD" in xout and "design_only" in xout
    assert "top" in xout and "wave_only" in xout
    assert "design_roots:" not in xout and "wave_roots:" not in xout
    assert "roots_0_" not in xout


def test_scope_list(loop_runner: StdioLoopRunner, counter_fst,
                    counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("scope.list", args={
        "path": "top", "level": 1, "kind": "all",
        "include_patterns": ["counter_top.c*", "counter_top.overflow"],
        "exclude_patterns": ["counter_top.clk"],
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["scan_complete"] is True
    assert rsp["summary"]["returned_module_count"] == 0
    assert rsp["summary"]["returned_port_count"] == 2
    assert rsp["summary"]["returned_signal_count"] == 0
    assert rsp["data"]["modules"] == []
    assert [(p["name"], p["direction"], p["width"])
            for p in rsp["data"]["ports"]] == [
                ("counter_top.count", "output", 8),
                ("counter_top.overflow", "output", 1),
            ]
    assert rsp["data"]["signals"] == []


def test_scope_list_max_rows_truncates_filtered_response(
        loop_runner: StdioLoopRunner, counter_fst, counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("scope.list", args={
        "path": "top", "level": 1, "kind": "all",
        "include_patterns": ["counter_top.c*", "counter_top.overflow"],
        "exclude_patterns": ["counter_top.clk"],
    }, limits={"max_rows": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["scanned_row_count"] == 4
    assert rsp["summary"]["total_count"] == 4
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["truncation_scopes"] == ["response_rows"]
    assert rsp["summary"]["returned_module_count"] == 0
    assert rsp["summary"]["returned_port_count"] == 1
    assert rsp["summary"]["returned_signal_count"] == 0
    assert len(rsp["data"]["ports"]) == 1


def test_waveform_not_loaded_error(cli_runner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "value.at",
                             "target": {"session_id": "missing"},
                             "args": {"signal": "top.clk", "time": "10"}})
    assert not result.ok
    assert result.response["error"]["code"] == "SESSION_NOT_FOUND"
    assert result.response["error"]["error_layer"] == "session_manager"
