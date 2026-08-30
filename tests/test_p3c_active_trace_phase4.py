from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

import pytest

from conftest import open_session
from runner import StdioLoopRunner
from tools.collect_p3c_original_oracles import inside
from tools.compare_public_action_responses import canonical_sv_literal, canonical_time


ROOT = Path(__file__).resolve().parents[1]
ACTIVE_ROOT = ROOT / "testdata/fixtures/active_trace"
ORACLE_PATH = (
    ROOT / "tests/data/rtl_wave_differential/"
    "p3c-phase4.original-oracle.json"
)
ORACLE = json.loads(ORACLE_PATH.read_text(encoding="utf-8"))
ROWS = ORACLE["rows"]
FULL_CHAIN_MAX_DEPTH = max(
    row["native_result"]["total_hops"] - 1 for row in ROWS
)
CHAIN_SCHEMA = json.loads((
    ROOT / "compat/xdebug-v1/schemas/v1/actions/"
    "trace.active_driver_chain.response.schema.json"
).read_text(encoding="utf-8"))
CHAIN_REQUEST_SCHEMA = json.loads((
    ROOT / "compat/xdebug-v1/schemas/v1/actions/"
    "trace.active_driver_chain.request.schema.json"
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
    return ACTIVE_ROOT / "phase4" / row["case"]


def ambiguity_samples(response: dict[str, Any]) -> list[dict[str, Any]]:
    evidence = response.get("data", {}).get("ambiguity_evidence", {})
    return [
        sample
        for statement in evidence.get("statements", [])
        for sample in statement.get("rhs_samples", [])
    ]


def test_p3c_phase4_oracle_is_locked_complete_and_sanitized() -> None:
    assert ORACLE["schema_version"] == \
        "xdebug.p3c-original-active-trace-oracle.v1"
    assert ORACLE["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert ORACLE["group"] == "phase4"
    assert ORACLE["catalog"] == {
        "row_count": 20,
        "schema_version": "xdebug-active-trace-cases.v1",
        "sha256": (
            "f4e61f065e1c1c4412f3c823c336d8962da559274e8c7f71387919d38e6bfb3c"
        ),
    }
    assert ORACLE["locked_runtime"] == {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": (
            "6a9d0fea1e9c68057e2ef12906dfd23f73a8a14622fc09b3be9798ee859b09ae-"
            "prepare-m3d4_3z7"
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
        f"active.phase4.{index:02d}" for index in range(1, 21)
    ]
    assert not [
        value for value in all_strings(ORACLE)
        if value.startswith("/") or "/home/" in value
    ]
    assert [
        row["native_result"]["termination"] for row in ROWS
    ] == [
        "primary_input", "primary_input", "primary_input", "primary_input",
        "primary_input", "ambiguous", "ambiguous", "ambiguous",
        "ambiguous", "ambiguous", "ambiguous", "ambiguous", "ambiguous",
        "ambiguous", "primary_input", "primary_input", "ambiguous",
        "primary_input", "primary_input", "primary_input",
    ]
    assert all(
        row["native_result"]["active_trace_calls"] ==
        row["native_result"]["total_hops"]
        and row["native_result"]["temporal_boundaries"] == 2
        and row["native_result"]["truncated"] is False
        and row["native_result"]["limitations"] == []
        for row in ROWS
    )
    assert all(
        row["catalog_expectation"] == {
            "hops": row["native_result"]["total_hops"],
            "temporal_boundaries": 2,
            "termination": row["native_result"]["termination"],
        }
        for row in ROWS
    )
    hop_schema = CHAIN_SCHEMA["$defs"]["nonSamplingTraceHop"]
    assert hop_schema["properties"]["file"]["minLength"] == 1
    assert hop_schema["properties"]["line"]["minimum"] == 1
    assert CHAIN_REQUEST_SCHEMA["properties"]["limits"]["properties"][
        "max_depth"
    ]["default"] == 8
    assert FULL_CHAIN_MAX_DEPTH == 16
    assert sum(
        hop["file"] == "" and hop["line"] == 0
        for row in ROWS for hop in row["native_result"]["chain"]
    ) == 5
    assert inside(ORACLE_PATH, ROOT, "oracle") == ORACLE_PATH
    with pytest.raises(RuntimeError, match="必须位于当前仓库"):
        inside(ROOT.parent / "phase4-oracle.json", ROOT, "oracle")


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_phase4_exact_rtl_and_generated_fixture_hashes(
        row: dict[str, Any]) -> None:
    mirrors = row["rtl_mirrors"]
    assert len(mirrors) == 3
    for mirror in mirrors:
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
    assert len(recorded) == 7
    assert all(digest(ROOT / path) == checksum
               for path, checksum in recorded.items())
    assert set(recorded) == {
        *(mirror["current_path"] for mirror in mirrors),
        "testdata/fixtures/active_trace/dump_probe.sv",
        f"testdata/fixtures/active_trace/phase4/{row['case']}/waves.fst",
        (
            "testdata/fixtures/active_trace/phase4/"
            f"{row['case']}/design_db/Vactive_trace__DesignDb.xddb"
        ),
        (
            "testdata/fixtures/active_trace/phase4/"
            f"{row['case']}/design_db/xdebug-design-db.json"
        ),
    }
    design = fixture / "design_db"
    manifest = json.loads(
        (design / "xdebug-design-db.json").read_text(encoding="utf-8")
    )
    assert manifest == {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": "Vactive_trace__DesignDb.xddb",
    }
    assert {path.name for path in design.iterdir()} == {
        "Vactive_trace__DesignDb.xddb", "xdebug-design-db.json",
    }


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_phase4_matches_locked_native_chain_semantics(
        loop_runner: StdioLoopRunner, row: dict[str, Any]) -> None:
    fixture = fixture_dir(row)
    opened = open_session(
        loop_runner, fixture / "waves.fst", fixture / "design_db"
    )
    assert opened["session"]["mode"] == "combined"
    request = row["request"]
    assert request["stop_on_temporal"] is False
    # The native chain oracle disables its temporal stop and reaches 17 hops.
    # Keep the frozen public default (8) intact and request the locked full
    # comparison depth explicitly.
    response = loop_runner.request("trace.active_driver_chain", args={
        "signal": request["signal"], "time": request["time"],
    }, limits={"max_depth": FULL_CHAIN_MAX_DEPTH})
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
    assert [hop["line"] for hop in hops] == [
        1 if hop["file"] == "" and hop["line"] == 0 else hop["line"]
        for hop in native_hops
    ]
    assert [time_fs(hop["active_time"]) for hop in hops] == [
        time_fs(hop["active_time"]) for hop in native_hops
    ]
    assert [logic_bits(hop["value"]) for hop in hops] == [
        logic_bits(hop["value"]) for hop in native_hops
    ]
    assert all(hop["value_known"] is True for hop in native_hops)
    assert [
        hop["relation"] == "root" or
        time_fs(hop["time"]) != time_fs(hop["active_time"])
        for hop in hops
    ] == [
        hop["hop_type"] == "temporal_boundary" for hop in native_hops
    ]
    for current_hop, native_hop in zip(hops, native_hops):
        if native_hop["file"] == "" and native_hop["line"] == 0:
            assert current_hop["file"] == "<unknown>"
            assert current_hop["line"] == 1
            assert current_hop["source_context"] == []
        else:
            assert any(
                context["active"] and context["line"] == current_hop["line"]
                for context in current_hop["source_context"]
            )

    native_branches = native["branch_evidence"]
    if native_branches:
        assert native["termination"] == "ambiguous"
        samples = ambiguity_samples(response)
        assert response["data"]["ambiguity_evidence"][
            "analysis_complete"] is True
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
    else:
        assert "ambiguity_evidence" not in response["data"]


def test_p3c_phase4_limits_are_explicit_analysis_boundaries(
        loop_runner: StdioLoopRunner) -> None:
    row = ROWS[0]
    fixture = fixture_dir(row)
    open_session(loop_runner, fixture / "waves.fst", fixture / "design_db")
    for limits, detail in (({"max_depth": 1}, "max_depth"),
                           ({"max_nodes": 1}, "max_nodes")):
        response = loop_runner.request(
            "trace.active_driver_chain",
            args={"signal": row["request"]["signal"],
                  "time": row["request"]["time"]},
            limits=limits,
        )
        assert response.get("ok"), response
        assert response["summary"]["termination"] == "limit"
        assert response["summary"]["termination_detail"] == detail
        assert response["summary"]["scan_complete"] is False
        assert response["summary"]["analysis_complete"] is False
        assert response["summary"]["response_truncated"] is False
        assert response["summary"]["truncation_scopes"] == ["analysis_trace"]
