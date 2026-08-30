from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

import pytest

from tools.build_rtl_wave_semantic_matrix import (
    ALLOWED_STATUSES,
    GOAL_ID,
    MatrixError,
    SCHEMA_VERSION,
    ensure_within_repo,
    parse_inline_mapping,
    scan_constructs,
)


ROOT = Path(__file__).resolve().parents[1]
MATRIX_PATH = ROOT / "tests/coverage/rtl_wave_semantic_matrix.json"
ASSET_PATH = ROOT / "compat/xdebug-v1/rtl-wave-assets.manifest.json"
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def load_matrix() -> dict:
    return json.loads(MATRIX_PATH.read_text(encoding="utf-8"))


def load_assets() -> dict:
    return json.loads(ASSET_PATH.read_text(encoding="utf-8"))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_inline_catalog_and_construct_scanners_preserve_semantic_boundaries() -> None:
    row = parse_inline_mapping(
        'case: case_01, signal: "top.data[2]", time: 30ns, '
        'hops: 7, stop_on_temporal: true'
    )
    assert row == {
        "case": "case_01",
        "signal": "top.data[2]",
        "time": "30ns",
        "hops": 7,
        "stop_on_temporal": True,
    }
    constructs = scan_constructs(
        """\
module m(input logic clk, reset, input logic [3:0] a, output logic y);
  always_ff @(posedge clk) if (reset) y <= 1'bx;
  else for (int i = 0; i < 4; i++) y <= a[i];
endmodule
"""
    )
    assert constructs["module"] == [1]
    assert constructs["always_ff"] == [2]
    assert constructs["clock"] == [1, 2]
    assert constructs["reset"] == [1, 2]
    assert constructs["procedural_for"] == [3]
    assert constructs["nonblocking_assignment"] == [2, 3]
    assert constructs["four_state_literal"] == [2]


def test_matrix_output_boundary_rejects_parent_and_symlink_escape(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    outside = tmp_path / "outside"
    repo.mkdir()
    outside.mkdir()
    assert ensure_within_repo(repo, repo / "matrix.json") == repo / "matrix.json"
    with pytest.raises(MatrixError, match="escapes the only writable repository"):
        ensure_within_repo(repo, outside / "matrix.json")
    (repo / "escape").symlink_to(outside, target_is_directory=True)
    with pytest.raises(MatrixError, match="escapes the only writable repository"):
        ensure_within_repo(repo, repo / "escape/matrix.json")


def test_checked_matrix_has_exhaustive_reverse_indexes_and_gap_queue() -> None:
    matrix = load_matrix()
    assets = load_assets()
    assert matrix["schema_version"] == SCHEMA_VERSION
    assert matrix["goal_id"] == GOAL_ID
    assert matrix["policy"]["only_writable_repository"] == "xdebug_fst"
    assert matrix["policy"]["external_inputs_read_only"] is True
    assert matrix["policy"]["candidate_evidence_is_not_equivalence"] is True

    summary = matrix["summary"]
    assert summary == {
        "active_catalog_case_count": 68,
        "declared_only_orphan_count": 1,
        "declared_waveform_output_count": 25,
        "original_consumer_count": 259,
        "original_fixture_count": 23,
        "original_hdl_count": 103,
        "scenario_count": 88,
        "status_counts": {"missing": 2, "partial": 86},
        "unclassified_count": 0,
        "unqueued_gap_count": 0,
    }

    fixture_ids = {item["id"] for item in assets["original_fixtures"]}
    original_hdl = {
        item["path"] for item in assets["assets"]
        if item["side"] == "original" and item["kind"] == "rtl"
    }
    consumers = {
        item["path"] for item in assets["assets"]
        if item["side"] == "original" and "test_consumer" in item["roles"]
    }
    outputs = {
        f"{item['fixture_id']}::{item['path']}"
        for item in assets["original_declared_waveform_outputs"]
    }
    coverage = matrix["coverage"]
    assert set(coverage["original_fixtures"]) == fixture_ids
    assert set(coverage["original_hdl"]) == original_hdl
    assert set(coverage["original_consumers"]) == consumers
    assert set(coverage["declared_waveform_outputs"]) == outputs
    assert all(
        scenario_ids
        for coverage_block in coverage.values()
        for scenario_ids in coverage_block.values()
    )

    scenarios = matrix["scenarios"]
    ids = {item["scenario_id"] for item in scenarios}
    assert len(ids) == len(scenarios)
    assert {item["status"] for item in scenarios} <= ALLOWED_STATUSES
    assert "unclassified" not in {item["status"] for item in scenarios}
    queued = {
        scenario_id
        for batch, scenario_ids in matrix["p3_queue"].items()
        for scenario_id in scenario_ids
        if batch in {"P3-A", "P3-B", "P3-C", "P3-D", "P3-E"}
    }
    gaps = {
        item["scenario_id"] for item in scenarios
        if item["status"] in {"partial", "missing"}
    }
    assert queued == gaps == ids


def test_matrix_evidence_is_relative_hashed_and_line_addressable() -> None:
    matrix = load_matrix()
    assets = load_assets()
    original_hashes = {
        item["path"]: item["sha256"]
        for item in assets["assets"] if item["side"] == "original"
    }
    seen_original_sources = set()
    for scenario in matrix["scenarios"]:
        assert scenario["p3_batch"] in {"P3-A", "P3-B", "P3-C", "P3-D", "P3-E"}
        assert scenario["rationale"]
        for source in scenario["original"]["sources"]:
            seen_original_sources.add(source["path"])
            assert not Path(source["path"]).is_absolute()
            assert source["sha256"] == original_hashes[source["path"]]
            assert 1 <= source["line_anchor"] <= max(1, source["line_count"])
            for lines in source["construct_lines"].values():
                assert lines == sorted(set(lines))
                assert all(1 <= line <= source["line_count"] for line in lines)
        for source in scenario["current"]["candidate_sources"]:
            path = ROOT / source["path"]
            assert path.is_file()
            assert SHA256.fullmatch(source["sha256"])
            assert digest(path) == source["sha256"]
            assert 1 <= source["line_anchor"] <= max(1, source["line_count"])
        for evidence in scenario["current"]["test_evidence"]:
            path = ROOT / evidence["path"]
            assert digest(path) == evidence["sha256"]
            line = path.read_text(encoding="utf-8").splitlines()[evidence["line"] - 1]
            assert line.startswith(f"def {evidence['test']}(")
    assert seen_original_sources == set(matrix["coverage"]["original_hdl"])


def test_all_73_action_contracts_link_frozen_examples_and_schemas() -> None:
    matrix = load_matrix()
    contracts = matrix["action_contracts"]
    assert len(contracts) == 73
    assert "trace.active_driver_chain" in contracts
    for action, contract in contracts.items():
        request = ROOT / contract["request_example"]
        response = ROOT / contract["response_schema"]
        assert request.is_file(), action
        assert response.is_file(), action
        request_document = json.loads(request.read_text(encoding="utf-8"))
        assert request_document["action"] == action
    active_fields = set(contracts["trace.active_driver_chain"]["completeness_fields"])
    assert {
        "summary.scan_complete",
        "summary.analysis_complete",
        "summary.response_truncated",
        "summary.total_count",
        "summary.returned_count",
        "summary.termination",
        "summary.termination_detail",
    } <= active_fields


def test_phase5_differences_and_declared_only_p0_case_are_not_hidden() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    phase5 = [scenarios[f"active.phase5.{index:02d}"] for index in range(1, 11)]
    assert all(item["status"] == "partial" for item in phase5)
    assert [item["original"]["oracle"]["termination"] for item in phase5] == [
        "ambiguous", "primary_input", "primary_input", "primary_input",
        "control_only", "ambiguous", "control_only", "primary_input",
        "primary_input", "control_only",
    ]
    conflicts = {
        item["scenario_id"]: item["original"]["authority_conflict"]
        for item in phase5 if "authority_conflict" in item["original"]
    }
    assert set(conflicts) == {"active.phase5.06", "active.phase5.08"}
    assert conflicts["active.phase5.06"]["report_value"] == "control_only"
    assert conflicts["active.phase5.08"]["report_value"] == "control_only"
    assert all(
        any(
            evidence["test"]
            == "test_trace_active_driver_chain_matches_original_phase5_public_semantics"
            for evidence in item["current"]["test_evidence"]
        )
        for item in phase5
    )

    orphan = scenarios["active.p0.declared_only_p0_4"]
    assert orphan["status"] == "missing"
    assert orphan["original"]["catalog_presence"] is False
    assert orphan["original"]["sources"] == []
    assert orphan["scenario_id"] in matrix["p3_queue"]["P3-C"]
