# test_design.py — discovery and static-design action contracts (BSD-3-Clause)
from __future__ import annotations

from conftest import open_session
from runner import StdioLoopRunner


def test_signal_resolve_contract(loop_runner: StdioLoopRunner, counter_fst,
                                 counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("signal.resolve", args={"signal": "top.count"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "status": "found", "query": "top.count", "scan_complete": True,
        "analysis_complete": True, "response_truncated": False,
        "total_count": 1, "returned_count": 1, "truncation_scopes": [],
    }
    assert rsp["data"]["matches"] == [{
        "signal": "top.count", "type": "port", "file": "counter_top.sv",
        "line": 4,
    }]


def test_signal_resolve_not_found(loop_runner: StdioLoopRunner, counter_fst,
                                  counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("signal.resolve", args={"signal": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_signal_resolve_requires_design(cli_runner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1", "action": "signal.resolve",
        "target": {"daidir": "missing-design-bundle"},
        "args": {"signal": "top.clk"},
    })
    assert not result.ok
    assert result.response["error"]["code"] == "DESIGN_NOT_LOADED"


def test_signal_canonicalize_port_connection(loop_runner: StdioLoopRunner,
                                             counter_fst,
                                             counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("signal.canonicalize",
                              args={"signal": "top.counter_top.clk"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["canonicalization_scope"] == \
        "static_design_connectivity"
    assert rsp["data"]["resolved_path"] == "top.counter_top.clk"
    assert rsp["data"]["connected_path"] == "top.clk"
    assert rsp["data"]["canonical_path"] == "top.clk"
    assert rsp["data"]["mapping_kind"] == "static_port_connection"
    assert rsp["data"]["connection"] == {
        "instance": "top.counter_top", "port": "clk",
        "direction": "input_or_inout",
        "evidence": "npi_static_port_connection",
    }


def test_trace_driver_contract_and_role_filter(loop_runner: StdioLoopRunner,
                                               counter_fst,
                                               counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.driver", args={
        "signal": "top.overflow", "role": "control",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["mode"] == "driver"
    assert rsp["summary"]["scan_complete"] is True
    assert rsp["summary"]["returned_count"] == len(rsp["data"]["paths"])
    assert rsp["data"]["paths"]
    for path in rsp["data"]["paths"]:
        assert path["file"] == "counter_top.sv"
        assert path["line"] > 0
        assert path["source_context"] == []
        assert path["signal_path"][-1] == "top.overflow"
        assert path["signal_path"][0] == "top.reset"

    limited = loop_runner.request("trace.driver", args={
        "signal": "top.overflow", "role": "control",
    }, limits={"max_results": 1})
    assert limited.get("ok"), limited
    assert limited["summary"]["total_count"] == 2
    assert limited["summary"]["returned_count"] == 1
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["truncation_scopes"] == ["response_paths"]
    assert len(limited["data"]["paths"]) == 1


def test_trace_driver_not_found(loop_runner: StdioLoopRunner, counter_fst,
                                counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.driver", args={"signal": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_trace_load_contract(loop_runner: StdioLoopRunner, counter_fst,
                             counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("trace.load", args={"signal": "top.reset"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["mode"] == "load"
    assert rsp["summary"]["returned_count"] == len(rsp["data"]["paths"])
    assert rsp["data"]["paths"]
    for path in rsp["data"]["paths"]:
        assert path["signal_path"][0] == "top.reset"
        assert len(path["signal_path"]) == 2

    limited = loop_runner.request(
        "trace.load", args={"signal": "top.reset"},
        limits={"max_results": 1},
    )
    assert limited.get("ok"), limited
    assert limited["summary"]["total_count"] == 5
    assert limited["summary"]["returned_count"] == 1
    assert limited["summary"]["response_truncated"] is True
    assert limited["summary"]["truncation_scopes"] == ["response_paths"]
    assert len(limited["data"]["paths"]) == 1


def test_trace_driver_and_load_empty_at_static_boundaries(
        loop_runner: StdioLoopRunner, counter_fst, counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    drivers = loop_runner.request("trace.driver", args={"signal": "top.reset"})
    assert drivers.get("ok"), drivers
    assert drivers["summary"]["total_count"] == 0
    assert drivers["summary"]["returned_count"] == 0
    assert drivers["data"]["paths"] == []

    loads = loop_runner.request(
        "trace.load", args={"signal": "top.counter_top.overflow"}
    )
    assert loads.get("ok"), loads
    assert loads["summary"]["total_count"] == 0
    assert loads["summary"]["returned_count"] == 0
    assert loads["data"]["paths"] == []


def test_expr_normalize_contract(loop_runner: StdioLoopRunner) -> None:
    rsp = loop_runner.request("expr.normalize",
                              args={"expr": "valid && !ready"}, target={})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "status": "parsed", "source": "deterministic_syntax_parser",
        "confidence": "syntax_validated",
    }
    assert rsp["data"]["expr"] == {
        "op": "and", "args": [
            {"type": "signal", "name": "valid"},
            {"op": "not", "args": [{"type": "signal", "name": "ready"}]},
        ],
    }
    assert rsp["data"]["parsed"] is True


def test_expr_normalize_signal_without_structured_assignment(
        loop_runner: StdioLoopRunner, counter_fst, counter_design_db) -> None:
    open_session(loop_runner, counter_fst, counter_design_db)
    rsp = loop_runner.request("expr.normalize", args={
        "signal": "top.count", "line_limit": 1,
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["source"] == "npi_trace_assignment"
    assert rsp["summary"]["confidence"] == "unknown"
    assert rsp["data"] == {"expr": {}, "assignment": {}, "rhs_signals": []}


def test_expr_normalize_parse_error(loop_runner: StdioLoopRunner) -> None:
    rsp = loop_runner.request("expr.normalize", args={"expr": "a +* b"},
                              target={})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "PARSE_ERROR"
