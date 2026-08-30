# test_original_active_fixtures.py — locked active-driver/interface oracle
from __future__ import annotations

import hashlib
from pathlib import Path

from conftest import open_session
from runner import StdioLoopRunner


def _digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _assert_fixture_hashes(name: str) -> None:
    root = Path(__file__).resolve().parents[1] / "testdata/fixtures" / name
    recorded = {}
    for line in (root / "fixture.sha256").read_text(
            encoding="utf-8").splitlines():
        digest, path = line.split(maxsplit=1)
        recorded[path] = digest
    assert recorded
    assert all(_digest(root / path) == digest
               for path, digest in recorded.items())


def _active_lines(response: dict) -> set[int]:
    return {
        row["line"]
        for path in response["data"]["paths"]
        for row in path.get("source_context", [])
        if row.get("active")
    }


def _path_lines(response: dict) -> set[int]:
    return {
        path["line"]
        for path in response.get("data", {}).get("paths", [])
        if isinstance(path, dict) and isinstance(path.get("line"), int)
    }


def _hop_lines(response: dict) -> list[int]:
    return [
        hop["line"]
        for hop in response.get("data", {}).get("hops", [])
        if isinstance(hop, dict) and isinstance(hop.get("line"), int)
    ]


def _signal_paths(response: dict) -> list[list[str]]:
    return [
        path.get("signal_path", [])
        for path in response.get("data", {}).get("paths", [])
        if isinstance(path, dict)
    ]


def _assert_no_legacy_active_fields(response: dict) -> None:
    assert not {
        "root_driver", "driver", "trace", "controls", "events",
    } & response["data"].keys()


def test_locked_active_fixtures_are_deterministic() -> None:
    _assert_fixture_hashes("active_driver")
    _assert_fixture_hashes("active_zero_evidence")
    _assert_fixture_hashes("interface_port_root")


def test_locked_active_driver_assignment_and_force(
        loop_runner: StdioLoopRunner, active_driver_fst: Path,
        active_driver_design_db: Path) -> None:
    open_session(loop_runner, active_driver_fst, active_driver_design_db)
    q20 = loop_runner.request("trace.active_driver", args={
        "signal": "active_driver_tb.u_dut.q", "time": "20ns",
    })
    assert q20.get("ok"), q20
    assert q20["summary"]["termination"] == "assignment"
    assert "active_time" in q20["summary"]
    assert q20["summary"]["returned_count"] == len(q20["data"]["paths"])
    assert 18 in _active_lines(q20)
    _assert_no_legacy_active_fields(q20)

    forced = loop_runner.request("trace.active_driver", args={
        "signal": "active_driver_tb.u_dut.q", "time": "40ns",
    })
    assert forced.get("ok"), forced
    assert forced["summary"]["termination"] == "force"
    assert 82 in _active_lines(forced)
    _assert_no_legacy_active_fields(forced)


def test_locked_active_driver_recurses_and_preserves_limits(
        loop_runner: StdioLoopRunner, active_driver_fst: Path,
        active_driver_design_db: Path) -> None:
    open_session(loop_runner, active_driver_fst, active_driver_design_db)
    recursive = loop_runner.request("trace.active_driver", args={
        "signal": "active_driver_tb.u_dut.comb_q", "time": "16ns",
    })
    assert recursive.get("ok"), recursive
    assert 18 in _active_lines(recursive)
    assert recursive["summary"]["returned_count"] == \
        len(recursive["data"]["paths"])
    _assert_no_legacy_active_fields(recursive)

    x_branch = loop_runner.request("trace.active_driver", args={
        "signal": "active_driver_tb.u_dut.comb_q", "time": "50ns",
    })
    assert x_branch.get("ok"), x_branch
    assert "active_time" in x_branch["summary"]
    assert x_branch["summary"]["returned_count"] >= 1
    _assert_no_legacy_active_fields(x_branch)

    limited = loop_runner.request(
        "trace.active_driver",
        args={"signal": "active_driver_tb.u_dut.q", "time": "20ns"},
        limits={"max_nodes": 1},
    )
    assert limited.get("ok"), limited
    assert limited["summary"]["analysis_complete"] is False
    assert limited["summary"]["response_truncated"] is False
    assert "analysis_trace" in limited["summary"]["truncation_scopes"]


def test_locked_interface_scope_classification(
        loop_runner: StdioLoopRunner, interface_port_root_fst: Path,
        interface_port_root_design_db: Path) -> None:
    open_session(
        loop_runner, interface_port_root_fst, interface_port_root_design_db)
    signal = loop_runner.request("scope.list", args={
        "path": "if_root_tb", "level": 0, "kind": "signal",
        "include_patterns": ["link", "link.*"],
    })
    assert signal.get("ok"), signal
    assert signal["data"]["signals"] == [{"name": "link", "width": None}]
    assert signal["data"]["modules"] == []
    assert signal["data"]["ports"] == []

    port = loop_runner.request("scope.list", args={
        "path": "if_root_tb", "level": 1, "kind": "port",
        "include_patterns": ["u_src.bus", "u_src.bus.*"],
    })
    assert port.get("ok"), port
    assert port["data"]["ports"] == [{
        "name": "u_src.bus", "direction": "interface", "width": None,
    }]


def test_locked_interface_active_driver_aliases(
        loop_runner: StdioLoopRunner, interface_port_root_fst: Path,
        interface_port_root_design_db: Path) -> None:
    open_session(
        loop_runner, interface_port_root_fst, interface_port_root_design_db)
    cases = (
        ("if_root_tb.u_sink.observed_q", "30ns", 23),
        ("if_root_tb.link.data", "20ns", 21),
        ("if_root_tb.link.data", "30ns", 23),
    )
    for signal, time, line in cases:
        response = loop_runner.request("trace.active_driver", args={
            "signal": signal, "time": time,
        })
        assert response.get("ok"), response
        assert line in _active_lines(response)
        assert response["summary"]["returned_count"] == \
            len(response["data"]["paths"])
        _assert_no_legacy_active_fields(response)
        if signal.endswith("observed_q"):
            assert signal in {
                item for path in _signal_paths(response) for item in path
            }


def test_locked_active_zero_scope_roots_discovers_combined_top(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    response = loop_runner.request(
        "scope.roots", args={"source": "auto"})
    assert response.get("ok"), response
    assert response["summary"]["recommended_root"] == \
        "active_zero_evidence_tb"
    assert response["summary"]["matched_count"] == 1
    root = next(
        item for item in response["data"]["roots"]
        if item["path"] == "active_zero_evidence_tb")
    assert root["status"] == "matched"
    assert root["sources"] == ["design", "wave"]
    assert root["wave"]["queryable"] is True
    assert root["design"]["traceable"] is True
    assert root["design"]["discovery"] in {
        "npi_top", "verified_wave_root",
    }

    scope = loop_runner.request("scope.list", args={
        "path": response["summary"]["recommended_root"], "level": 0,
    })
    assert scope.get("ok"), scope
    assert scope["data"]["signals"]
    modules = {item["name"]: item for item in scope["data"]["modules"]}
    assert modules["u_primitive"]["module_name"] == "primitive_output_dut"
    assert modules["u_input_child"]["module_name"] == "input_trace_child"
    assert {
        (item["name"], item["direction"], item["width"])
        for item in scope["data"]["ports"]
    } >= {("top_input_i", "input", 1)}
    assert all(
        not item["name"].startswith("active_zero_evidence_tb.")
        for section in ("modules", "ports", "signals")
        for item in scope["data"][section]
    )

    child_ports = loop_runner.request("scope.list", args={
        "path": response["summary"]["recommended_root"],
        "level": 1,
        "kind": "port",
        "include_patterns": ["u_input_child.*"],
        "exclude_patterns": ["*data_q"],
    })
    assert child_ports.get("ok"), child_ports
    assert {
        (item["name"], item["direction"], item["width"])
        for item in child_ports["data"]["ports"]
    } == {
        ("u_input_child.clk", "input", 1),
        ("u_input_child.data_i", "input", 1),
        ("u_input_child.rst_n", "input", 1),
    }


def test_locked_active_zero_trace_driver_runs_in_combined_session(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    response = loop_runner.request("trace.driver", args={
        "signal": "active_zero_evidence_tb.u_input_child.data_q",
    })
    assert response.get("ok"), response
    assert response["data"]["paths"]
    assert response["summary"]["returned_count"] == \
        len(response["data"]["paths"])


def test_locked_active_zero_scope_roots_filters_and_xout(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    wave = loop_runner.request("scope.roots", args={"source": "wave"})
    design = loop_runner.request("scope.roots", args={"source": "design"})
    xout = loop_runner.request_xout(
        "scope.roots", args={"source": "auto"})

    assert wave.get("ok"), wave
    assert wave["summary"]["wave_count"] == 1
    assert wave["summary"]["design_count"] == 0
    assert wave["summary"]["recommended_root"] == \
        "active_zero_evidence_tb"
    assert wave["data"]["roots"][0]["status"] == "wave_only"
    assert wave["data"]["roots"][0]["design"] is None

    assert design.get("ok"), design
    assert design["summary"]["design_count"] == 1
    assert design["summary"]["wave_count"] == 0
    assert design["summary"]["recommended_root"] == \
        "active_zero_evidence_tb"
    assert design["data"]["roots"][0]["status"] == "design_only"
    assert design["data"]["roots"][0]["wave"] is None
    assert design["data"]["roots"][0]["design"]["discovery"] in {
        "npi_top", "verified_wave_root",
    }

    assert xout.startswith("@xdebug.scope.roots.v1")
    assert "pointer\tkind\tvalue" not in xout
    for evidence in (
        "summary:", "recommended: active_zero_evidence_tb",
        "source     : auto", "roots      : 1", "matched    : 1",
        "wave       : 1", "design     : 1", "roots:",
        "active_zero_evidence_tb  matched  design,wave",
    ):
        assert evidence in xout
    for generic_key in (
        "recommended_root", "recommended_reason", "matched_count",
        "response_truncated", "scan_complete", "analysis_complete",
    ):
        assert generic_key not in xout
    roots_block = xout.split("roots:\n", 1)[1]
    assert roots_block.splitlines()[0].split() == [
        "path", "status", "sources", "wave", "design",
    ]


def test_locked_active_zero_repeated_root_queries_keep_session_stable(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    for iteration in range(50):
        response = loop_runner.request(
            "scope.roots", args={"source": "auto"})
        assert response.get("ok"), (iteration, response)
        assert response["summary"]["recommended_root"] == \
            "active_zero_evidence_tb"
        assert response["summary"]["matched_count"] == 1
    doctor = loop_runner.request("session.doctor", args={})
    assert doctor.get("ok"), doctor
    assert doctor["summary"]["healthy"] is True


def test_locked_active_zero_precise_active_time(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    response = loop_runner.request("trace.active_driver", args={
        "signal": "active_zero_evidence_tb.u_reduction_10ns.sample_flag",
        "time": "15ns",
    })
    assert response.get("ok"), response
    assert response["summary"]["active_time"] == "10ns"
    assert response["summary"]["returned_count"] == \
        len(response["data"]["paths"])
    assert {50, 74, 77}.issubset(_path_lines(response))
    assert "driver" not in response["data"]
    assert "trace" not in response["data"]


def test_locked_active_zero_us_scale_active_time(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    response = loop_runner.request("trace.active_driver", args={
        "signal": "active_zero_evidence_tb.u_reduction_us_pulse.sample_flag",
        "time": "10000ns",
    })
    assert response.get("ok"), response
    assert response["summary"]["active_time"] == "9995ns"
    assert response["summary"]["returned_count"] == \
        len(response["data"]["paths"])
    assert {142, 167}.issubset(_path_lines(response))
    assert "driver" not in response["data"]
    assert "root_driver" not in response["data"]


def test_locked_active_zero_us_scale_chain(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    response = loop_runner.request("trace.active_driver_chain", args={
        "signal": "active_zero_evidence_tb.u_reduction_us_pulse.sample_flag",
        "time": "10000ns",
    })
    assert response.get("ok"), response
    assert response["summary"]["termination"] == "assignment"
    assert response["summary"]["termination_detail"] == \
        "non_direct_rhs_expression"
    assert response["summary"]["returned_count"] == \
        len(response["data"]["hops"]) == 2
    assert _hop_lines(response) == [167, 142]
    assert "chain" not in response["data"]


def test_locked_active_zero_reduction_zero_evidence(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    signal = "active_zero_evidence_tb.u_reduction.sample_flag"
    active = loop_runner.request("trace.active_driver", args={
        "signal": signal, "time": "16ns",
    })
    chain = loop_runner.request("trace.active_driver_chain", args={
        "signal": signal, "time": "16ns",
    })
    assert active.get("ok"), active
    assert active["summary"]["total_count"] == 0
    assert active["summary"]["returned_count"] == 0
    assert active["data"]["paths"] == []
    assert all(key not in active["data"] for key in (
        "driver", "root_driver", "trace",
    ))
    assert chain.get("ok"), chain
    assert chain["summary"]["termination"] == "unresolved"
    assert "chain" not in chain["data"]
    assert chain["summary"]["returned_count"] == \
        len(chain["data"]["hops"])


def test_locked_active_zero_primitive_keeps_real_evidence(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    response = loop_runner.request("trace.active_driver_chain", args={
        "signal": "active_zero_evidence_tb.u_primitive.out_bufif",
        "time": "16ns",
    })
    assert response.get("ok"), response
    hops = response["data"]["hops"]
    assert not (
        response["summary"]["termination"] == "primary_input" and
        len(hops) <= 1)
    assert response["summary"]["returned_count"] == len(hops)
    assert response["summary"]["termination"] != "unresolved"


def test_locked_active_zero_module_input_follows_parent(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    signal = "active_zero_evidence_tb.u_input_child.data_i"
    active = loop_runner.request("trace.active_driver", args={
        "signal": signal, "time": "16ns",
    })
    chain = loop_runner.request("trace.active_driver_chain", args={
        "signal": signal, "time": "16ns",
    })
    assert active.get("ok"), active
    assert active["summary"]["returned_count"] == \
        len(active["data"]["paths"])
    assert active["summary"]["returned_count"] >= 1
    assert any(
        "active_zero_evidence_tb.parent_src" in path
        for path in _signal_paths(active))
    assert "root_driver" not in active["data"]
    assert "trace" not in active["data"]

    assert chain.get("ok"), chain
    assert "text" not in chain["data"]
    assert "chain" not in chain["data"]
    hops = chain["data"]["hops"]
    assert len(hops) >= 2
    assert "active_zero_evidence_tb.parent_src" in hops[0]["signal_path"]
    assert not (
        chain["summary"]["termination"] == "primary_input" and
        len(hops) == 1)


def test_locked_active_zero_top_input_is_primary(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    signal = "active_zero_evidence_tb.top_input_i"
    active = loop_runner.request("trace.active_driver", args={
        "signal": signal, "time": "16ns",
    })
    chain = loop_runner.request("trace.active_driver_chain", args={
        "signal": signal, "time": "16ns",
    })
    assert active.get("ok"), active
    assert active["summary"]["returned_count"] == \
        len(active["data"]["paths"])
    if active["data"]["paths"]:
        assert any(
            signal in path.get("signal_path", [])
            for path in active["data"]["paths"])
    assert "root_driver" not in active["data"]
    assert "trace" not in active["data"]

    assert chain.get("ok"), chain
    assert chain["summary"]["termination"] == "primary_input"
    assert chain["summary"]["returned_count"] == \
        len(chain["data"]["hops"])
    if chain["data"]["hops"]:
        assert signal in chain["data"]["hops"][0]["signal_path"]


EXPR_ZERO_EVIDENCE_OUTPUTS = (
    "q_reduce_or", "q_reduce_and", "q_reduce_xor", "q_bit_or_ne0",
    "q_bit_and_ne0", "q_xor_reduce_mask", "q_logic_and", "q_logic_or",
    "q_reduce_and_enable", "q_ternary_reduce", "q_compare_ne0",
    "q_compare_eq_const", "q_compare_gt", "q_bit_select",
    "q_const_part_reduce", "q_indexed_part_reduce", "q_concat_reduce",
    "q_nested_mix",
)


def test_locked_active_zero_expression_outputs_have_zero_evidence(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    failures = []
    for output in EXPR_ZERO_EVIDENCE_OUTPUTS:
        signal = f"active_zero_evidence_tb.u_expr.{output}"
        response = loop_runner.request("trace.active_driver", args={
            "signal": signal, "time": "16ns",
        })
        checks = (
            response.get("ok"),
            response.get("summary", {}).get("total_count") == 0,
            response.get("summary", {}).get("returned_count") == 0,
            response.get("data", {}).get("paths") == [],
            all(key not in response.get("data", {}) for key in (
                "driver", "root_driver", "trace",
            )),
        )
        if not all(checks):
            failures.append((output, response))
    assert not failures, failures


def test_locked_active_zero_expression_chains_are_unresolved(
        loop_runner: StdioLoopRunner, active_zero_evidence_fst: Path,
        active_zero_evidence_design_db: Path) -> None:
    open_session(
        loop_runner, active_zero_evidence_fst,
        active_zero_evidence_design_db)
    failures = []
    for output in EXPR_ZERO_EVIDENCE_OUTPUTS:
        signal = f"active_zero_evidence_tb.u_expr.{output}"
        response = loop_runner.request("trace.active_driver_chain", args={
            "signal": signal, "time": "16ns",
        })
        checks = (
            response.get("ok"),
            response.get("summary", {}).get("termination") == "unresolved",
            "chain" not in response.get("data", {}),
            response.get("summary", {}).get("returned_count") ==
                len(response.get("data", {}).get("hops", [])),
        )
        if not all(checks):
            failures.append((output, response))
    assert not failures, failures
