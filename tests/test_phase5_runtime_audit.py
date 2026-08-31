from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = (
    ROOT / "tests/data/rtl_wave_differential/phase5.runtime-audit.json"
)
MATRIX_PATH = ROOT / "tests/coverage/rtl_wave_semantic_matrix.json"


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def all_strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from all_strings(key)
            yield from all_strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from all_strings(item)


def test_phase5_runtime_audit_locks_identity_cache_and_zero_external_write() -> None:
    audit = load(AUDIT_PATH)
    assert audit["schema_version"] == "xdebug.phase5-runtime-audit.v1"
    assert audit["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"

    locked = audit["locked_original_runtime"]
    assert locked["git_revision"] == \
        "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
    assert locked["self_reported_git_revision"] == "8eecf71271cc"
    assert locked["schema_revision"] == \
        "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
    assert locked["action_count"] == 73
    assert locked["session"] == {
        "mode": "combined",
        "transport": "uds",
        "opened": True,
        "closed_gracefully": True,
        "all_runtime_writes_repository_local": True,
    }
    fixture = locked["fixture"]
    assert fixture["fsdb_sha256"] == \
        "9fdc31f039e65a24a25cb2808f0d62c232e3c4d91ea90e0cf6252c9e1d2df7ba"
    assert fixture["fsdb_size"] == 10799
    assert fixture["cache_reused"] is True
    assert fixture["fixture_rebuilt"] is False
    assert fixture["source_access"] == "read_only"
    assert fixture["proprietary_artifacts_committed"] is False
    write_audit = locked["external_write_audit"]
    assert write_audit["unchanged"] is True
    assert write_audit["xverif_status_porcelain_sha256_before"] == \
        write_audit["xverif_status_porcelain_sha256_after"]

    # Committed evidence must be sanitized: no host-absolute fixture, source,
    # socket, cache, HOME or temporary path may leak into the repository.
    strings = list(all_strings(audit))
    assert not [text for text in strings if text.startswith("/")]
    assert not [text for text in strings if "/home/" in text]


def test_phase5_runtime_audit_covers_all_ten_scenes_without_hiding_completeness() -> None:
    audit = load(AUDIT_PATH)
    rows = audit["scene_results"]
    assert [row["scene"] for row in rows] == [f"S{index}" for index in range(1, 11)]
    assert [row["scenario_id"] for row in rows] == [
        f"active.phase5.{index:02d}" for index in range(1, 11)
    ]

    for row in rows:
        locked = row["locked_runtime"]
        current = row["current_gate"]
        assert locked["scan_complete"] is True
        assert locked["analysis_complete"] is True
        assert locked["response_truncated"] is False
        for field in (
            "termination",
            "termination_detail",
            "active_time",
            "total_count",
            "returned_count",
            "evidence_kind",
            "statement_count",
            "rhs_signal_count",
        ):
            assert current[field] == locked[field], (row["scene"], field)
        assert row["locked_request"]["time"] == row["current_request"]["time"]
        assert row["current_request"]["signal"] == \
            row["locked_request"]["signal"].replace(
                "top.u_dut.", "top.phase5_dut."
            )
        assert row["status"] == "partial"
        assert row["p3_batch"] == "P3-C"

    dout = [row for row in rows if "dout" in row["locked_request"]["signal"]]
    flag = [row for row in rows if "flag" in row["locked_request"]["signal"]]
    assert len(dout) == 7
    assert len(flag) == 3
    assert all(row["locked_runtime"]["value_width_complete"] is False for row in dout)
    assert all(row["locked_runtime"]["width_diagnostic_reasons"] == [
        "npi_range_size_unavailable"
    ] for row in dout)
    assert all(row["locked_runtime"]["value_width_complete"] is True for row in flag)
    assert all(row["locked_runtime"]["width_diagnostic_reasons"] == [] for row in flag)


def test_phase5_asset_oracle_drift_is_measured_against_locked_runtime() -> None:
    audit = load(AUDIT_PATH)
    rows = audit["scene_results"]
    catalog_mismatches = [
        row["scene"] for row in rows
        if row["asset_oracles"]["catalog_termination"]
        != row["locked_runtime"]["termination"]
    ]
    report_mismatches = [
        row["scene"] for row in rows
        if row["asset_oracles"]["report_termination"]
        != row["locked_runtime"]["termination"]
    ]
    internal_conflicts = [
        row["scene"] for row in rows
        if row["asset_oracles"]["catalog_termination"]
        != row["asset_oracles"]["report_termination"]
    ]
    summary = audit["oracle_drift_summary"]
    assert len(catalog_mismatches) == summary["catalog_mismatch_scene_count"] == 8
    assert len(report_mismatches) == summary["report_mismatch_scene_count"] == 9
    assert internal_conflicts == summary["catalog_report_conflict_scenes"] == ["S6", "S8"]


def test_phase5_subset_match_cannot_be_promoted_to_full_equivalence() -> None:
    audit = load(AUDIT_PATH)
    spot = audit["full_response_spot_check"]
    differences = spot["remaining_observable_differences"]
    assert spot["scenario_id"] == "active.phase5.01"
    assert len(differences) == 5
    joined = "\n".join(differences)
    for required in (
        "value_width_complete",
        "hop value",
        "RHS sample 顺序",
        "statement kind/driver",
        "source_context",
    ):
        assert required in joined

    verdict = audit["verdict"]
    assert verdict["status"] == "partial"
    assert verdict["p3_batch"] == "P3-C"
    assert verdict["termination_and_ambiguity_subset_equivalent_scene_count"] == 10
    assert verdict["full_response_equivalent_scene_count"] == 0


def test_phase5_runtime_audit_is_linked_from_every_matrix_scene() -> None:
    matrix = load(MATRIX_PATH)
    phase5 = [
        row for row in matrix["scenarios"]
        if row["scenario_id"].startswith("active.phase5.")
    ]
    assert len(phase5) == 10
    for row in phase5:
        evidence = row["runtime_audit"]
        assert evidence["path"] == \
            "tests/data/rtl_wave_differential/p3c-phase5.public-oracle.json"
        assert evidence["scenario_id"] == row["scenario_id"]
        assert evidence["status"] == row["status"] == "semantic-equivalent"
        assert evidence["p3_batch"] == row["p3_batch"] == "P3-C"
        assert evidence["full_response_equivalent"] is True
        assert evidence["remaining_observable_gap_count"] == 0
        assert evidence["historical_subset_audit"] == {
            "path": "tests/data/rtl_wave_differential/phase5.runtime-audit.json",
            "sha256": (
                "be45c6e2b664b8c33b8f338c207dda3d1d0b6c212d65b2a6ef6d2a18e20a62f8"
            ),
            "status": "partial",
        }
