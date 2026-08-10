# signal analysis contract tests (BSD-3-Clause)
from __future__ import annotations

from conftest import open_session
from runner import StdioLoopRunner


def time_range(begin: str, end: str) -> dict[str, str]:
    return {"begin": begin, "end": end}


def test_signal_statistics_raw_contract(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.counter_top.count",
        "time_range": time_range("0ps", "490ps"),
        "value_format": "hex",
        "render_time_unit": "ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "signal": "top.counter_top.count",
        "sampling_mode": "raw_value_changes",
        "begin": "0ps",
        "end": "490ps",
        "actual_transition_count": 20,
        "scan_complete": True,
        "analysis_complete": True,
        "response_truncated": False,
        "total_count": 21,
        "returned_count": 21,
        "truncation_scopes": [],
    }
    data = rsp["data"]
    assert data["includes_initial_value"] is True
    assert data["initial_value"]["bits"] == "00000000"
    assert data["final_value"]["bits"] == "00010100"
    assert len(data["evidence"]) == 21
    assert data["evidence"][0]["kind"] == "initial"


def test_signal_statistics_raw_line_limit_is_projection_only(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.counter_top.count",
        "time_range": time_range("0ps", "490ps"),
        "line_limit": 1,
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["actual_transition_count"] == 20
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["total_count"] == 21
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["summary"]["truncation_scopes"] == ["response_evidence"]
    assert len(rsp["data"]["evidence"]) == 1


def test_signal_statistics_clock_sampling_and_contract(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.counter_top.count",
        "clock": "top.clk",
        "edge": "posedge",
        "sample_point": "before",
        "time_range": time_range("0ps", "490ps"),
    })
    assert rsp.get("ok"), rsp
    summary = rsp["summary"]
    assert summary["sampling_mode"] == "clock_edge"
    assert summary["clock"] == "top.clk"
    assert summary["sample_count"] > 0
    assert summary["known_count"] == summary["sample_count"]
    assert summary["unknown_count"] == 0
    assert summary["analysis_complete"] is True
    assert rsp["data"]["transition_count"] > 0
    assert rsp["data"]["sampling"] == {
        "requested": {"edge": "posedge", "sample_point": "before"},
        "effective": {"edge": "posedge", "sample_point": "before"},
        "sample_point_applied": True,
        "sample_point_ignored_for_negedge": False,
    }


def test_signal_statistics_max_samples_marks_analysis_incomplete(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.counter_top.count",
        "clock": "top.clk",
        "time_range": time_range("0ps", "490ps"),
        "max_samples": 2,
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["sample_count"] == 2
    assert rsp["summary"]["scan_complete"] is False
    assert rsp["summary"]["analysis_complete"] is False
    assert "analysis_samples" in rsp["summary"]["truncation_scopes"]


def test_signal_statistics_rejects_sampling_without_clock(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.clk", "edge": "posedge"
    })
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_FIELD"


def test_signal_stability_stops_on_first_transition(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.stability", args={
        "signal": "top.counter_top.count",
        "time_range": time_range("200ps", "300ps"),
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["stable"] is False
    assert rsp["summary"]["actual_transition_count"] == 1
    assert rsp["summary"]["scan_stopped_on_first_transition"] is True
    assert rsp["summary"]["scan_complete"] is False
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["truncation_scopes"] == [
        "scan_after_first_transition"
    ]
    assert len(rsp["data"]["changes"]) == 2


def test_signal_stability_single_point_is_stable(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.stability", args={
        "signal": "top.counter_top.count",
        "time_range": time_range("0ps", "0ps"),
        "render_time_unit": "ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["stable"] is True
    assert rsp["summary"]["change_row_count"] == 1
    assert rsp["data"]["begin"] == "0ps"
    assert rsp["data"]["end"] == "0ps"


def test_changes_stability_and_statistics_preserve_x_from_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    signal = "top.masslav_if.Pslave_err"
    clock = "top.masslav_if.clk"
    window = time_range("0ps", "20ns")

    changes = loop_runner.request("signal.changes", args={
        "signal": signal, "mode": "timeline", "time_range": window,
    })
    assert changes.get("ok"), changes
    assert changes["data"]["initial_value"]["has_x"] is True
    assert changes["data"]["changes"][0]["value"]["bits"] == "x"

    stability = loop_runner.request("signal.stability", args={
        "signal": signal, "time_range": window,
    })
    assert stability.get("ok"), stability
    assert stability["summary"]["stable"] is False
    assert stability["data"]["changes"][0]["value"]["has_x"] is True

    statistics = loop_runner.request("signal.statistics", args={
        "signal": signal, "clock": clock,
        "edge": "posedge", "sample_point": "after",
        "time_range": window,
    })
    assert statistics.get("ok"), statistics
    assert statistics["summary"]["unknown_count"] == 2
    assert all(
        evidence["value"]["has_x"] is True
        for evidence in statistics["data"]["evidence"]
    )


def test_signal_xz_verify_fail_evidence(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "top.counter_top.count",
        "expected_state": "x",
        "match_mode": "contains",
        "time_range": time_range("0ps", "490ps"),
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "fail"
    assert rsp["summary"]["always_matched"] is False
    assert rsp["summary"]["checked_value_count"] == 1
    assert rsp["summary"]["scan_complete"] is False
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["data"]["first_mismatch"]["sample_time"] == "0ns"


def test_signal_xz_verify_x_present(
    loop_runner: StdioLoopRunner, wide_xz_fst
) -> None:
    open_session(loop_runner, wide_xz_fst)
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "AXI_top_tb_from_compiled.dut.bram_r",
        "expected_state": "x",
        "match_mode": "contains",
        "time_range": time_range("0ps", "0ps"),
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "pass"
    assert rsp["summary"]["always_matched"] is True
    assert rsp["data"]["initial_value"]["has_x"] is True
    assert rsp["data"]["first_mismatch"] is None


def test_signal_xz_verify_checks_multiple_direct_raw_fst_values(
    loop_runner: StdioLoopRunner, wellen_apb_fst
) -> None:
    open_session(loop_runner, wellen_apb_fst)
    rsp = loop_runner.request("signal.xz_verify", args={
        "signal": "top.masslav_if.Pslave_err",
        "expected_state": "x",
        "match_mode": "contains",
        "time_range": time_range("0ps", "20ns"),
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["checked_value_count"] == 2
    assert rsp["summary"]["total_count"] == 2
    assert rsp["data"]["initial_value"]["has_x"] is True
    assert rsp["data"]["first_mismatch"]["sample_time"] == "16ns"


def test_signal_anomaly_unknown_and_scan_status(
    loop_runner: StdioLoopRunner, wide_xz_fst
) -> None:
    open_session(loop_runner, wide_xz_fst)
    rsp = loop_runner.request("signal.anomaly.inspect", args={
        "signals": ["AXI_top_tb_from_compiled.dut.bram_r"],
        "checks": [{"type": "unknown_xz"}],
        "time_range": time_range("0ps", "0ps"),
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["signal_count"] == 1
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["total_count"] >= 1
    finding = rsp["data"]["findings"][0]
    assert finding["type"] == "unknown_xz"
    assert finding["value"]["has_x"] is True
    assert rsp["data"]["scan_status"][0]["status"] == "ok"


def test_signal_anomaly_glitch_and_stuck(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.anomaly.inspect", args={
        "signals": ["top.clk", "top.counter_top.count"],
        "checks": [
            {"type": "glitch", "min_pulse_width": "11ps"},
            {"type": "stuck", "min_duration": "30ps"},
        ],
        "time_range": time_range("0ps", "120ps"),
        "line_limit": 100,
    })
    assert rsp.get("ok"), rsp
    types = {finding["type"] for finding in rsp["data"]["findings"]}
    assert "glitch" in types
    assert "stuck" in types
    assert rsp["summary"]["glitch_threshold"] == "0.011ns"
    assert rsp["summary"]["stuck_threshold"] == "0.03ns"


def test_signal_anomaly_missing_signal_is_partial_analysis(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.anomaly.inspect", args={
        "signals": ["nope", "top.clk"],
        "checks": [{"type": "unknown_xz"}],
        "time_range": time_range("0ps", "100ps"),
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["scan_complete"] is False
    assert rsp["summary"]["analysis_complete"] is False
    assert rsp["summary"]["truncation_scopes"] == ["analysis_signals"]
    assert rsp["data"]["scan_status"][0]["status"] == "error"
    assert rsp["data"]["scan_status"][1]["status"] == "ok"


def test_signal_analysis_missing_signal_errors(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    for action, args in (
        ("signal.statistics", {"signal": "nope"}),
        ("signal.stability", {"signal": "nope"}),
        ("signal.xz_verify", {
            "signal": "nope", "expected_state": "x",
            "time_range": time_range("0ps", "100ps"),
        }),
    ):
        rsp = loop_runner.request(action, args=args)
        assert not rsp.get("ok"), (action, rsp)
        assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND", (action, rsp)


def test_signal_analysis_legacy_flat_range_fails_schema(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("signal.statistics", args={
        "signal": "top.clk", "begin": "0ns", "end": "100ns"
    })
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"
    assert rsp["error"]["error_layer"] == "schema"
