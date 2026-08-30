from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

import pytest

from conftest import open_session
from runner import StdioLoopRunner
from tools.compare_public_action_responses import canonical_sv_literal, canonical_time


ROOT = Path(__file__).resolve().parents[1]
ACTIVE_ROOT = ROOT / "testdata/fixtures/active_trace"
ORACLE_PATH = (
    ROOT / "tests/data/rtl_wave_differential/p3c-p0.original-oracle.json"
)
ORACLE = json.loads(ORACLE_PATH.read_text(encoding="utf-8"))
ROWS = ORACLE["rows"]
CHAIN_SCHEMA = json.loads((
    ROOT / "compat/xdebug-v1/schemas/v1/actions/"
    "trace.active_driver_chain.response.schema.json"
).read_text(encoding="utf-8"))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def all_strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from all_strings(item)
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from all_strings(key)
            yield from all_strings(item)


def time_fs(value: str) -> str:
    if value.strip("0.") == "":
        return "0"
    canonical = canonical_time(value)
    assert canonical is not None, value
    return canonical["$xdebug_time_fs"]


def logic_bits(value: str) -> str:
    canonical = canonical_sv_literal(value)
    if canonical is not None:
        return canonical["$xdebug_logic"]["bits"]
    assert value and set(value.lower()) <= set("01xz?"), value
    return value.upper().replace("?", "Z")


def fixture_dir(row: dict[str, Any]) -> Path:
    return ACTIVE_ROOT / "p0" / row["case"]


def ambiguity_samples(response: dict[str, Any]) -> list[dict[str, Any]]:
    evidence = response.get("data", {}).get("ambiguity_evidence", {})
    return [
        sample
        for statement in evidence.get("statements", [])
        for sample in statement.get("rhs_samples", [])
    ]


def value_at_bits(loop_runner: StdioLoopRunner, signal: str,
                  time: str) -> str:
    response = loop_runner.request(
        "value.at", args={"signal": signal, "time": time}
    )
    assert response.get("ok"), response
    values = response["data"]["samples"][0]["values"]
    assert len(values) == 1 and values[0]["status"] == "ok", values
    return values[0]["value"]["bits"]


def test_p3c_p0_oracle_is_locked_complete_and_sanitized() -> None:
    assert ORACLE["schema_version"] == \
        "xdebug.p3c-original-active-trace-oracle.v1"
    assert ORACLE["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert ORACLE["group"] == "p0"
    assert ORACLE["catalog"] == {
        "row_count": 6,
        "schema_version": "xdebug-active-trace-cases.v1",
        "sha256": "f4e61f065e1c1c4412f3c823c336d8962da559274e8c7f71387919d38e6bfb3c",
    }
    assert ORACLE["locked_runtime"] == {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": (
            "c2bff810935847ad3debd15d1facb671d4943bc209ba25f38cb4b92d3d1ca06c-"
            "prepare-98pbtd3b"
        ),
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": (
            "f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237"
        ),
        "source_access": "read_only",
    }
    assert ORACLE["session"] == {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }
    assert [row["scenario_id"] for row in ROWS] == [
        f"active.p0.{index:02d}" for index in range(1, 7)
    ]
    assert not [
        value for value in all_strings(ORACLE)
        if value.startswith("/") or "/home/" in value
    ]

    # The locked native NPI collector can expose a source-less control handle
    # and branch samples on control_only.  The independently frozen public v1
    # schema cannot encode either shape: trace hops require file/line sentinels,
    # and ambiguity_evidence is coupled to termination=ambiguous.  Keep the raw
    # oracle intact and freeze the representational boundary explicitly.
    hop = CHAIN_SCHEMA["$defs"]["nonSamplingTraceHop"]
    assert hop["properties"]["file"]["minLength"] == 1
    assert hop["properties"]["line"]["minimum"] == 1
    assert any(
        row["native_result"]["termination"] == "control_only" and
        row["native_result"]["branch_evidence"]
        for row in ROWS
    )
    assert any(
        native_hop["file"] == "" and native_hop["line"] == 0
        for row in ROWS
        for native_hop in row["native_result"]["chain"]
    )


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_p0_exact_rtl_and_generated_fixture_hashes(
        row: dict[str, Any]) -> None:
    mirrors = row["rtl_mirrors"]
    assert len(mirrors) == 1
    mirror = mirrors[0]
    source = ROOT / mirror["current_path"]
    assert mirror["byte_identical"] is True
    assert source.stat().st_size == mirror["size"]
    assert digest(source) == mirror["sha256"]

    fixture = fixture_dir(row)
    recorded = {}
    for line in (fixture / "fixture.sha256").read_text(
            encoding="utf-8").splitlines():
        checksum, path = line.split(maxsplit=1)
        recorded[path] = checksum
    assert len(recorded) == 5
    assert all(digest(ROOT / path) == checksum
               for path, checksum in recorded.items())
    design = fixture / "design_db"
    manifest = json.loads(
        (design / "xdebug-design-db.json").read_text(encoding="utf-8")
    )
    assert manifest == {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": "Vactive_trace__DesignDb.xddb",
    }
    assert set(path.name for path in design.iterdir()) == {
        "Vactive_trace__DesignDb.xddb", "xdebug-design-db.json",
    }


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_p0_matches_locked_native_chain_semantics(
        loop_runner: StdioLoopRunner, row: dict[str, Any]) -> None:
    fixture = fixture_dir(row)
    opened = open_session(
        loop_runner, fixture / "waves.fst", fixture / "design_db"
    )
    assert opened["session"]["mode"] == "combined"
    request = row["request"]
    response = loop_runner.request("trace.active_driver_chain", args={
        "signal": request["signal"], "time": request["time"],
    })
    assert response.get("ok"), response
    summary = response["summary"]
    native = row["native_result"]
    hops = response["data"]["hops"]
    native_hops = native["chain"]

    assert summary["scan_complete"] is True
    assert summary["analysis_complete"] is True
    assert summary["response_truncated"] is False
    assert summary["truncation_scopes"] == []
    assert summary["termination"] == native["termination"]
    assert summary["total_count"] == summary["returned_count"] == \
        len(hops) == native["total_hops"]
    assert [hop["signal"] for hop in hops] == [
        hop["signal"] for hop in native_hops
    ]
    expected_lines = [
        1 if hop["file"] == "" and hop["line"] == 0 else hop["line"]
        for hop in native_hops
    ]
    assert [hop["line"] for hop in hops] == expected_lines
    for current_hop, native_hop in zip(hops, native_hops):
        if native_hop["file"] == "" and native_hop["line"] == 0:
            assert current_hop["file"] == "<unknown>"
            assert current_hop["line"] == 1
            assert current_hop["source_context"] == []
    assert [time_fs(hop["active_time"]) for hop in hops] == [
        time_fs(hop["active_time"]) for hop in native_hops
    ]
    for current_hop, native_hop in zip(hops, native_hops):
        current_bits = logic_bits(current_hop["value"])
        if native_hop["value"]:
            assert current_bits == logic_bits(native_hop["value"])
        else:
            # Native NPI reports value_known=true but an empty value for the
            # generated packed-bit control handle.  Only knownness is
            # observable there; current FST retains the actual known bit.
            assert native_hop["value_known"] is True
            assert current_bits and set(current_bits) <= set("01")
    assert all(hop["value_known"] is True for hop in native_hops)
    assert [
        hop["relation"] == "root" or
        time_fs(hop["time"]) != time_fs(hop["active_time"])
        for hop in hops
    ] == [
        hop["hop_type"] == "temporal_boundary" for hop in native_hops
    ]
    assert native["temporal_boundaries"] == sum(
        hop["hop_type"] == "temporal_boundary" for hop in native_hops
    )
    assert all(
        any(context["active"] and context["line"] == hop["line"]
            for context in hop["source_context"])
        for hop in hops
        if hop["line"] > 0 and hop["file"] != "<unknown>"
    )
    assert all(
        hop["line"] == 1 and hop["source_context"] == []
        for hop in hops if hop["file"] == "<unknown>"
    )

    native_branches = native["branch_evidence"]
    if native_branches and native["termination"] == "ambiguous":
        samples = ambiguity_samples(response)
        assert response["data"]["ambiguity_evidence"][
            "analysis_complete"] is True
        assert {sample["signal"] for sample in samples} == {
            candidate["name"]
            for branch in native_branches
            for candidate in branch["candidates"]
        }
        assert {
            (sample["signal"], logic_bits(sample["before"]["value"]),
             logic_bits(sample["after"]["value"]), sample["changed"])
            for sample in samples
        } == {
            (candidate["name"], logic_bits(candidate["before"]),
             logic_bits(candidate["after"]), candidate["toggled"])
            for branch in native_branches
            for candidate in branch["candidates"]
        }
    elif native_branches:
        assert native["termination"] == "control_only"
        assert "ambiguity_evidence" not in response["data"]
        query_fs = int(time_fs(request["time"]))
        assert query_fs > 1_000
        before_time = f"{query_fs - 1_000}fs"
        for branch in native_branches:
            for candidate in branch["candidates"]:
                assert candidate["toggled"] is False
                assert logic_bits(candidate["before"]) == \
                    logic_bits(candidate["after"])
                assert logic_bits(value_at_bits(
                    loop_runner, candidate["name"], before_time
                )) == logic_bits(candidate["before"])
                assert logic_bits(value_at_bits(
                    loop_runner, candidate["name"], request["time"]
                )) == logic_bits(candidate["after"])
    else:
        assert "ambiguity_evidence" not in response["data"]


def test_p3c_p0_limits_are_explicit_analysis_boundaries(
        loop_runner: StdioLoopRunner) -> None:
    row = ROWS[0]
    fixture = fixture_dir(row)
    open_session(loop_runner, fixture / "waves.fst", fixture / "design_db")
    for limits, detail in (({"max_depth": 1}, "max_depth"),
                           ({"max_nodes": 1}, "max_nodes")):
        response = loop_runner.request(
            "trace.active_driver_chain",
            args={"signal": "top.out", "time": "10ns"},
            limits=limits,
        )
        assert response.get("ok"), response
        assert response["summary"]["termination"] == "limit"
        assert response["summary"]["termination_detail"] == detail
        assert response["summary"]["scan_complete"] is False
        assert response["summary"]["analysis_complete"] is False
        assert response["summary"]["response_truncated"] is False
        assert response["summary"]["truncation_scopes"] == ["analysis_trace"]
