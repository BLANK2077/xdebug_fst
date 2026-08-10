# verify.conditions / window.verify contract tests (BSD-3-Clause)
from __future__ import annotations

from conftest import open_session
from runner import StdioLoopRunner


COUNT = {"count": "top.counter_top.count"}


def test_verify_conditions_mixed_results(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "clock": "top.clk",
        "signals": COUNT,
        "conditions": [
            {"name": "equal", "expr": "count == 8'h0b"},
            {"name": "not_zero", "expr": "count != 0"},
            {"name": "wrong", "expr": "count == 8'h00"},
        ],
        "time": "300ps",
        "render_time_unit": "ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "time": "300ps", "execution_ok": True, "verdict": "fail",
        "condition_count": 3, "all_passed": False,
        "passed": 2, "failed": 1, "unknown": 0,
        "value_width_complete": True, "width_diagnostics": [],
    }
    assert [check["status"] for check in rsp["data"]["checks"]] == [
        "pass", "pass", "fail"
    ]
    context = rsp["data"]["clock_context"]
    assert context["requested_sampling"] == {
        "edge": "negedge", "sample_point": None
    }
    assert context["requested_time"] == "300ps"
    assert context["requested_any_edge_hit"] is True
    assert context["clock_edge_kind"] == "posedge"
    assert context["requested_target_edge_hit"] is False


def test_verify_conditions_unknown_is_not_execution_error(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "clock": "top.clk",
        "signals": COUNT,
        "conditions": [{"expr": "count == 8'hxx"}],
        "time": "300ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["execution_ok"] is True
    assert rsp["summary"]["verdict"] == "fail"
    assert rsp["summary"]["unknown"] == 1
    check = rsp["data"]["checks"][0]
    assert check["known"] is False
    assert check["status"] == "unknown"
    assert check["pass"] is None
    assert check["value"]["known"] is False


def test_verify_conditions_accepts_zero_time_boundary(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "clock": "top.clk",
        "signals": COUNT,
        "conditions": [{"name": "initial", "expr": "count == 0"}],
        "time": "0ps",
        "render_time_unit": "ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["time"] == "0ps"
    assert rsp["summary"]["execution_ok"] is True
    assert rsp["summary"]["condition_count"] == 1
    assert rsp["data"]["checks"][0]["name"] == "initial"


def test_verify_conditions_posedge_after_sampling(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("verify.conditions", args={
        "clock": "top.clk", "edge": "posedge", "sample_point": "after",
        "signals": COUNT, "conditions": [{"expr": "count == 8'h0b"}],
        "time": "300ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["data"]["clock_context"]["effective_sampling"] == {
        "edge": "posedge", "sample_point": "after"
    }


def test_verify_conditions_rejects_undeclared_and_unused_aliases(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    undeclared = loop_runner.request("verify.conditions", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "missing == 0"}], "time": "0ps",
    })
    assert not undeclared.get("ok")
    assert undeclared["error"]["code"] == "INVALID_FIELD"
    unused = loop_runner.request("verify.conditions", args={
        "clock": "top.clk",
        "signals": {"count": "top.counter_top.count", "clk": "top.clk"},
        "conditions": [{"expr": "count == 0"}], "time": "0ps",
    })
    assert not unused.get("ok")
    assert unused["error"]["code"] == "INVALID_FIELD"


def test_window_verify_always_passes_complete_window(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count >= 0", "mode": "always"}],
        "time_range": {"begin": "0ps", "end": "490ps"},
        "render_time_unit": "ps",
    })
    assert rsp.get("ok"), rsp
    summary = rsp["summary"]
    assert summary["verdict"] == "pass"
    assert summary["all_passed"] is True
    assert summary["sample_count"] > 0
    assert summary["scan_complete"] is True
    assert summary["analysis_complete"] is True
    assert summary["stop_reason"] == "window_end"
    assert rsp["data"]["conditions"][0]["passed"] is True


def test_window_verify_always_failure_is_decisive(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 0", "mode": "always"}],
        "time_range": {"begin": "200ps", "end": "490ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "fail"
    assert rsp["summary"]["all_passed"] is False
    assert rsp["summary"]["stop_reason"] == "decisive_result"
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["failed_samples"] == 1
    assert rsp["data"]["findings"][0]["status"] == "fail"


def test_window_verify_eventually_passes_decisively(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 8'h0b", "mode": "eventually"}],
        "time_range": {"begin": "0ps", "end": "490ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "pass"
    assert rsp["summary"]["stop_reason"] == "decisive_result"
    assert rsp["data"]["conditions"][0]["pass_samples"] == 1


def test_window_verify_never_mode(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 8'hff", "mode": "never"}],
        "time_range": {"begin": "0ps", "end": "490ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "pass"
    assert rsp["data"]["conditions"][0]["passed"] is True


def test_window_verify_max_samples_is_inconclusive(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 8'hff", "mode": "eventually"}],
        "time_range": {"begin": "0ps", "end": "490ps"},
        "max_samples": 2,
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "inconclusive"
    assert rsp["summary"]["all_passed"] is None
    assert rsp["summary"]["sample_count"] == 2
    assert rsp["summary"]["scan_complete"] is False
    assert rsp["summary"]["analysis_complete"] is False
    assert rsp["summary"]["truncation_scopes"] == ["analysis_samples"]


def test_window_verify_line_limit_only_truncates_findings(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 8'hff", "mode": "eventually"}],
        "time_range": {"begin": "0ps", "end": "100ps"},
        "line_limit": 2,
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["analysis_complete"] is True
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["total_count"] > 2
    assert rsp["summary"]["returned_count"] == 2
    assert rsp["summary"]["truncation_scopes"] == ["response_findings"]


def test_window_verify_empty_sample_window(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 0", "mode": "eventually"}],
        "time_range": {"begin": "1ps", "end": "2ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["sample_count"] == 0
    assert rsp["summary"]["verdict"] == "fail"
    assert rsp["summary"]["scanned_range"] == {"begin": None, "end": None}


def test_window_verify_unknown_is_decisive_failure(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "clock": "top.clk", "signals": COUNT,
        "conditions": [{"expr": "count == 8'hxx", "mode": "always"}],
        "time_range": {"begin": "0ps", "end": "100ps"},
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["verdict"] == "fail"
    assert rsp["summary"]["unknown_samples"] == 1
    assert rsp["data"]["findings"][0]["status"] == "unknown"


def test_window_legacy_request_fails_schema(
    loop_runner: StdioLoopRunner, counter_fst
) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("window.verify", args={
        "signal": "top.counter_top.count", "clock": "top.clk",
        "begin": "0ps", "end": "100ps",
    })
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"
    assert rsp["error"]["error_layer"] == "schema"
