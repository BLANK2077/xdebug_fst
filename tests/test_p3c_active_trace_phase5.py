from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any, Iterable

import pytest

from conftest import open_session
from runner import StdioLoopRunner
from tools.compare_public_action_responses import (
    canonical_sv_literal,
    canonical_time,
)


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "testdata/fixtures/active_trace/phase5/phase5"
ORACLE_PATH = (
    ROOT / "tests/data/rtl_wave_differential/"
    "p3c-phase5.public-oracle.json"
)
ORACLE = json.loads(ORACLE_PATH.read_text(encoding="utf-8"))
ROWS = ORACLE["rows"]
ORACLE_SHA256 = (
    "2da78f7e629356e4fe55bad144a9e42a1b799073999fa13a67aae1d7c2108cca"
)
CURRENT_DUT = "testdata/fixtures/active_trace/rtl/phase5/dut.sv"
ORIGINAL_DUT = "xdebug/tests/active_trace_chain/phase5/dut.sv"


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
    canonical = canonical_time(value)
    assert canonical is not None, value
    return canonical["$xdebug_time_fs"]


def sized_bits(value: str) -> tuple[int, str]:
    canonical = canonical_sv_literal(value)
    assert canonical is not None, value
    logic = canonical["$xdebug_logic"]
    return logic["width"], logic["bits"]


def unsized_hex_bits(value: str, width: int) -> str:
    match = re.fullmatch(r"'h([0-9a-fA-F_xXzZ?]+)", value)
    assert match is not None, value
    digits = match.group(1).replace("_", "")
    assert set(digits.lower()) <= set("0123456789abcdef"), value
    return f"{int(digits, 16):0{width}b}"[-width:]


def assert_sample_equivalent(original: dict[str, Any],
                             current: dict[str, Any]) -> None:
    assert current["status"] == original["status"]
    assert current["known"] == original["known"]
    if original["status"] != "ok":
        assert current == original
        return
    assert sized_bits(current["value"]) == sized_bits(original["value"])
    assert time_fs(current["value_time"]) == time_fs(original["value_time"])


def assert_statement_equivalent(original: dict[str, Any],
                                current: dict[str, Any]) -> None:
    assert original["kind"] == "assignment"
    assert current["kind"] == "proc_assign"
    assert current["driver"] == "proc_assign"
    assert original["file"] == ORIGINAL_DUT
    assert current["file"] == CURRENT_DUT
    for field in (
        "line", "rhs_signal_count", "returned_rhs_signal_count", "complete"
    ):
        assert current[field] == original[field]
    original_samples = original["rhs_samples"]
    current_samples = current["rhs_samples"]
    assert [sample["signal"] for sample in current_samples] == [
        sample["signal"] for sample in original_samples
    ]
    for expected, actual in zip(original_samples, current_samples):
        assert actual["signal"] == expected["signal"]
        assert actual["changed"] == expected["changed"]
        assert_sample_equivalent(expected["before"], actual["before"])
        assert_sample_equivalent(expected["after"], actual["after"])


def assert_phase5_response_equivalent(row: dict[str, Any],
                                      current: dict[str, Any]) -> None:
    original = row["response"]
    expected_summary = original["summary"]
    summary = current["summary"]
    for field in (
        "signal", "termination", "termination_detail", "scan_complete",
        "analysis_complete", "response_truncated", "total_count",
        "returned_count", "truncation_scopes",
    ):
        assert summary[field] == expected_summary[field], field
    assert time_fs(summary["time"]) == time_fs(expected_summary["time"])

    # NPI cannot recover the width of unpacked dout elements and reports an
    # unsized hop plus an explicit diagnostic.  FST/DesignDB knows the exact
    # width; retaining that stronger fact is the only allowed width projection.
    assert summary["value_width_complete"] is True
    assert summary["width_diagnostics"] == []
    assert expected_summary["value_width_complete"] is (
        "flag" in expected_summary["signal"]
    )

    expected_data = original["data"]
    data = current["data"]
    expected_evidence = expected_data["ambiguity_evidence"]
    evidence = data["ambiguity_evidence"]
    for field in (
        "kind", "signal", "hop_index", "statement_count",
        "rhs_signal_count", "returned_rhs_signal_count",
        "omitted_rhs_signal_count", "analysis_complete", "truncation_scopes",
    ):
        assert evidence[field] == expected_evidence[field], field
    assert time_fs(evidence["active_time"]) == time_fs(
        expected_evidence["active_time"]
    )
    assert len(evidence["statements"]) == len(expected_evidence["statements"])
    for expected, actual in zip(
            expected_evidence["statements"], evidence["statements"]):
        assert_statement_equivalent(expected, actual)

    expected_hops = expected_data["hops"]
    hops = data["hops"]
    assert len(hops) == len(expected_hops)
    for expected, actual in zip(expected_hops, hops):
        for field in (
            "index", "chain_id", "signal", "relation", "line", "signal_path",
            "source_context",
        ):
            assert actual[field] == expected[field], field
        assert expected["file"] == ORIGINAL_DUT
        assert actual["file"] == CURRENT_DUT
        assert time_fs(actual["time"]) == time_fs(expected["time"])
        assert time_fs(actual["active_time"]) == time_fs(
            expected["active_time"]
        )
        width, bits = sized_bits(actual["value"])
        assert width == 8
        assert bits == unsized_hex_bits(expected["value"], width)


def test_p3c_phase5_public_oracle_is_locked_complete_and_sanitized() -> None:
    assert digest(ORACLE_PATH) == ORACLE_SHA256
    assert ORACLE["schema_version"] == "xdebug.p3c-phase5-public-oracle.v1"
    assert ORACLE["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert ORACLE["group"] == "phase5"
    assert ORACLE["catalog"] == {
        "row_count": 10,
        "schema_version": "xdebug-active-trace-cases.v1",
        "sha256": (
            "f4e61f065e1c1c4412f3c823c336d8962da559274e8c7f71387919d38e6bfb3c"
        ),
    }
    assert ORACLE["locked_runtime"] == {
        "action_count": 73,
        "binary_sha256": (
            "0f54515fa1c7e80634cdba1e9455ed7cdc1acae26144c7c5a39200853becf46d"
        ),
        "build_id": (
            "8eecf71271cc-"
            "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
        ),
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": (
            "2ee4a76b564e9c59197f85abde73928afc903d82fcf77224ccf1b99b3d160442-"
            "prepare-tydl8ogq"
        ),
        "npi_version": "X-2025.06-SP1",
        "runtime_revision": (
            "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
        ),
        "schema_revision": (
            "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
        ),
        "source_access": "read_only",
        "wrapper_sha256": (
            "c9569332281ccad39099e06d547075d33b35f9b50699e4d148203ad5645978e7"
        ),
    }
    assert ORACLE["session"] == {
        "all_runtime_writes_repository_local": True,
        "closed_gracefully": True,
        "fallback_used": False,
        "mode": "combined",
        "opened": True,
        "transport": "uds",
    }
    assert [row["scenario_id"] for row in ROWS] == [
        f"active.phase5.{index:02d}" for index in range(1, 11)
    ]
    assert not [
        value for value in all_strings(ORACLE)
        if value.startswith("/") or "/home/" in value
    ]
    assert all(
        row["response"]["summary"]["termination"] == "ambiguous"
        and row["response"]["summary"]["scan_complete"] is True
        and row["response"]["summary"]["analysis_complete"] is True
        and row["response"]["summary"]["response_truncated"] is False
        and row["response"]["summary"]["truncation_scopes"] == []
        for row in ROWS
    )
    assert [row["catalog_expectation"]["termination"] for row in ROWS] == [
        "ambiguous", "primary_input", "primary_input", "primary_input",
        "control_only", "ambiguous", "control_only", "primary_input",
        "primary_input", "control_only",
    ]


def test_p3c_phase5_exact_rtl_and_generated_fixture_hashes() -> None:
    mirrors = ROWS[0]["rtl_mirrors"]
    assert all(row["rtl_mirrors"] == mirrors for row in ROWS)
    assert len(mirrors) == 2
    for mirror in mirrors:
        current = ROOT / mirror["current_path"]
        assert mirror["byte_identical"] is True
        assert current.stat().st_size == mirror["size"]
        assert digest(current) == mirror["sha256"]

    recorded = {}
    for line in (FIXTURE / "fixture.sha256").read_text(
            encoding="utf-8").splitlines():
        checksum, path = line.split(maxsplit=1)
        recorded[path] = checksum
    assert len(recorded) == 6
    assert all(digest(ROOT / path) == checksum
               for path, checksum in recorded.items())
    assert set(recorded) == {
        *(mirror["current_path"] for mirror in mirrors),
        "testdata/fixtures/active_trace/dump_probe.sv",
        "testdata/fixtures/active_trace/phase5/phase5/waves.fst",
        (
            "testdata/fixtures/active_trace/phase5/phase5/"
            "design_db/Vactive_trace__DesignDb.xddb"
        ),
        (
            "testdata/fixtures/active_trace/phase5/phase5/"
            "design_db/xdebug-design-db.json"
        ),
    }
    assert digest(FIXTURE / "waves.fst") == \
        "f242c3fe96e7a604170b858eb17a84cd25288ec4e2458a6c5e2ffb4be04b597b"
    assert digest(FIXTURE / "design_db/Vactive_trace__DesignDb.xddb") == \
        "065f226b7bf39aa63dc492572a600ccf8018504d0b57f579ed8debaa18f84edd"
    assert json.loads((
        FIXTURE / "design_db/xdebug-design-db.json"
    ).read_text(encoding="utf-8")) == {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": "Vactive_trace__DesignDb.xddb",
    }


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_phase5_matches_locked_full_public_response(
        loop_runner: StdioLoopRunner, row: dict[str, Any]) -> None:
    opened = open_session(
        loop_runner, FIXTURE / "waves.fst", FIXTURE / "design_db"
    )
    assert opened["session"]["mode"] == "combined"
    response = loop_runner.request(
        "trace.active_driver_chain",
        args=row["request"],
        limits=row["limits"],
    )
    assert response.get("ok"), response
    assert_phase5_response_equivalent(row, response)


def test_p3c_phase5_limits_are_explicit_analysis_boundaries(
        loop_runner: StdioLoopRunner) -> None:
    row = ROWS[0]
    open_session(loop_runner, FIXTURE / "waves.fst", FIXTURE / "design_db")
    response = loop_runner.request(
        "trace.active_driver_chain",
        args=row["request"],
        limits={"max_trace_signals": 1},
    )
    assert response.get("ok"), response
    assert response["summary"]["termination"] == "ambiguous"
    assert response["summary"]["scan_complete"] is False
    assert response["summary"]["analysis_complete"] is False
    assert response["summary"]["response_truncated"] is False
    assert response["summary"]["truncation_scopes"] == [
        "ambiguity_rhs_samples"
    ]
    evidence = response["data"]["ambiguity_evidence"]
    assert evidence["analysis_complete"] is False
    assert evidence["returned_rhs_signal_count"] == 1
    assert evidence["omitted_rhs_signal_count"] == 5
    assert evidence["truncation_scopes"] == ["ambiguity_rhs_samples"]
