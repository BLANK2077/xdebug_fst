from __future__ import annotations

from copy import deepcopy
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
    validate_p3c_composite_oracle,
    validate_p3c_p0_oracle,
    validate_p3c_phase4_oracle,
    validate_p3c_phase5_public_oracle,
    validate_p3c_timing_oracle,
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
        "original_consumer_count": 260,
        "original_fixture_count": 23,
        "original_hdl_count": 103,
        "scenario_count": 88,
        "status_counts": {
            "missing": 1,
            "partial": 8,
            "proven-unobservable": 4,
            "semantic-equivalent": 75,
        },
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
    assert queued == gaps
    assert ids - gaps == {
        "fixture.ai_complex_wave",
        "fixture.active_driver",
        "fixture.active_semantics",
        "fixture.active_zero_evidence",
        "fixture.interface_port_root",
        "fixture.trace_x_xprop",
        "fixture.design_uart",
        "fixture.design_p3",
        "fixture.design_hierarchy",
        "fixture.active_trace_runner",
        "active.p0.declared_only_p0_4",
        *{f"active.p0.{index:02d}" for index in range(1, 7)},
        *{f"active.composite.{index:02d}" for index in range(1, 21)},
        *{f"active.timing.{index:02d}" for index in range(1, 13)},
        *{f"active.phase4.{index:02d}" for index in range(1, 21)},
        *{f"active.phase5.{index:02d}" for index in range(1, 11)},
    }


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


def test_phase5_full_responses_are_closed_without_hiding_historical_drift() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    phase5 = [scenarios[f"active.phase5.{index:02d}"] for index in range(1, 11)]
    assert [item["status"] for item in phase5] == ["semantic-equivalent"] * 10
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
    oracle_path = (
        "tests/data/rtl_wave_differential/p3c-phase5.public-oracle.json"
    )
    historical_path = (
        "tests/data/rtl_wave_differential/phase5.runtime-audit.json"
    )
    assert all(
        item["original"]["locked_runtime_oracle"]["path"] == oracle_path
        and item["original"]["locked_runtime_oracle"]["termination"] ==
        "ambiguous"
        and item["original"]["locked_runtime_oracle"]["scan_complete"] is True
        and item["original"]["locked_runtime_oracle"]["analysis_complete"] is True
        and item["original"]["locked_runtime_oracle"][
            "response_truncated"
        ] is False
        and item["original"]["historical_subset_audit"]["path"] ==
        historical_path
        and item["original"]["historical_subset_audit"]["status"] == "partial"
        and item["runtime_audit"]["path"] == oracle_path
        and SHA256.fullmatch(item["runtime_audit"]["sha256"])
        and item["runtime_audit"]["status"] == "semantic-equivalent"
        and item["runtime_audit"]["remaining_observable_gap_count"] == 0
        and item["runtime_audit"]["full_response_equivalent"] is True
        and item["runtime_audit"]["historical_subset_audit"]["path"] ==
        historical_path
        and item["runtime_audit"]["historical_subset_audit"]["status"] ==
        "partial"
        and item["current"]["candidate_fixture_ids"] ==
        ["current.active_trace"]
        and item["public_request"]["limits"] == {
            "max_depth": 64, "max_nodes": 64,
        }
        and set(item["public_request"]["args"]) == {
            "render_time_unit", "signal", "time",
        }
        for item in phase5
    )
    required_tests = {
        "test_p3c_phase5_public_oracle_is_locked_complete_and_sanitized",
        "test_p3c_phase5_exact_rtl_and_generated_fixture_hashes",
        "test_p3c_phase5_matches_locked_full_public_response",
        "test_p3c_phase5_limits_are_explicit_analysis_boundaries",
    }
    for item in phase5:
        assert {
            source["path"] for source in item["current"]["candidate_sources"]
        } == {
            "testdata/fixtures/active_trace/rtl/phase5/dut.sv",
            "testdata/fixtures/active_trace/rtl/phase5/tb.sv",
            "testdata/fixtures/active_trace/phase5/phase5/waves.fst",
        }
        assert {
            evidence["test"] for evidence in item["current"]["test_evidence"]
        } == required_tests
        projection_kinds = [
            projection["kind"]
            for projection in item["runtime_audit"]["schema_projections"]
        ]
        assert projection_kinds == (
            ["statement_kind_projection", "exact_width_strengthening"]
            if "dout" in item["public_request"]["args"]["signal"]
            else ["statement_kind_projection"]
        )
        assert item["scenario_id"] not in matrix["p3_queue"].get("P3-C", [])


def test_p3c_phase5_matrix_gate_rejects_full_response_evidence_drift() -> None:
    manifest = load_assets()
    original_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "original"
    }
    current_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "current"
    }
    baseline = manifest["baselines"]["original_runtime"]
    oracle = json.loads((
        ROOT / "tests/data/rtl_wave_differential/"
        "p3c-phase5.public-oracle.json"
    ).read_text(encoding="utf-8"))

    def validate(document: dict) -> dict[str, dict]:
        return validate_p3c_phase5_public_oracle(
            document,
            ROOT,
            original_assets,
            current_assets,
            baseline["runtime_revision"],
            baseline["schema_revision"],
        )

    assert set(validate(oracle)) == {
        f"active.phase5.{index:02d}" for index in range(1, 11)
    }

    wrong_goal = deepcopy(oracle)
    wrong_goal["goal_id"] = "wrong-goal"
    with pytest.raises(MatrixError, match="different Goal/group"):
        validate(wrong_goal)

    incomplete = deepcopy(oracle)
    incomplete["rows"][0]["response"]["summary"]["analysis_complete"] = False
    with pytest.raises(MatrixError, match="locked full response is incomplete"):
        validate(incomplete)

    wrong_rhs_order = deepcopy(oracle)
    samples = wrong_rhs_order["rows"][0]["response"]["data"][
        "ambiguity_evidence"
    ]["statements"][0]["rhs_samples"]
    samples[0], samples[1] = samples[1], samples[0]
    with pytest.raises(MatrixError, match="statement/RHS evidence drifted"):
        validate(wrong_rhs_order)

    wrong_value_time = deepcopy(oracle)
    wrong_value_time["rows"][0]["response"]["data"][
        "ambiguity_evidence"
    ]["statements"][0]["rhs_samples"][0]["after"]["value_time"] = "11ns"
    with pytest.raises(MatrixError, match="sampled value evidence drifted"):
        validate(wrong_value_time)

    fallback = deepcopy(oracle)
    fallback["session"]["fallback_used"] = True
    with pytest.raises(MatrixError, match="write/session/fallback boundary"):
        validate(fallback)


def test_declared_only_p0_case_is_closed_only_by_the_bounded_absence_proof() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}

    orphan = scenarios["active.p0.declared_only_p0_4"]
    assert orphan["status"] == "proven-unobservable"
    assert orphan["original"]["catalog_presence"] is False
    assert orphan["original"]["sources"] == []
    assert orphan["current"]["candidate_fixture_ids"] == []
    assert [
        evidence["test"] for evidence in orphan["current"]["test_evidence"]
    ] == ["test_declared_only_p0_4_has_a_bounded_frozen_absence_proof"]
    assert orphan["unobservable_proof"]["catalog_row_count"] == 0
    assert orphan["unobservable_proof"]["authoritative_public_request_count"] == 0
    assert orphan["scenario_id"] not in matrix["p3_queue"].get("P3-C", [])


def test_p3c_p0_cases_are_closed_individually_with_locked_evidence() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    p0 = [scenarios[f"active.p0.{index:02d}"] for index in range(1, 7)]
    assert [item["status"] for item in p0] == ["semantic-equivalent"] * 6
    assert [
        item["original"]["locked_native_oracle"]["termination"]
        for item in p0
    ] == [
        "ambiguous", "primary_input", "control_only", "control_only",
        "ambiguous", "primary_input",
    ]
    assert [
        item["original"]["locked_native_oracle"]["total_hops"]
        for item in p0
    ] == [3, 4, 1, 1, 1, 1]
    assert all(
        item["runtime_audit"]["path"] ==
        "tests/data/rtl_wave_differential/p3c-p0.original-oracle.json"
        and SHA256.fullmatch(item["runtime_audit"]["sha256"])
        and item["runtime_audit"]["remaining_observable_gap_count"] == 0
        and item["current"]["candidate_fixture_ids"] ==
        ["current.active_trace"]
        for item in p0
    )
    assert [
        [projection["kind"] for projection in
         item["runtime_audit"]["schema_projections"]]
        for item in p0
    ] == [
        [], [], ["control_only_candidate_sampling"],
        ["control_only_candidate_sampling"], [],
        ["source_location_sentinel"],
    ]
    for item in p0:
        case = item["original"]["case"]
        assert {source["path"] for source in item["current"]["candidate_sources"]} == {
            f"testdata/fixtures/active_trace/rtl/p0/{case}/tb.sv",
            f"testdata/fixtures/active_trace/p0/{case}/waves.fst",
        }
    required_tests = {
        "test_p3c_p0_oracle_is_locked_complete_and_sanitized",
        "test_p3c_p0_exact_rtl_and_generated_fixture_hashes",
        "test_p3c_p0_matches_locked_native_chain_semantics",
        "test_p3c_p0_limits_are_explicit_analysis_boundaries",
    }
    assert all(
        {evidence["test"] for evidence in item["current"]["test_evidence"]}
        == required_tests
        for item in p0
    )
    assert not any(
        item["scenario_id"] in matrix["p3_queue"].get("P3-C", [])
        for item in p0
    )


def test_p3c_p0_matrix_gate_rejects_oracle_and_schema_boundary_drift() -> None:
    manifest = load_assets()
    original_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "original"
    }
    current_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "current"
    }
    oracle = json.loads((
        ROOT / "tests/data/rtl_wave_differential/"
        "p3c-p0.original-oracle.json"
    ).read_text(encoding="utf-8"))
    assert set(validate_p3c_p0_oracle(
        oracle, ROOT, original_assets, current_assets
    )) == {f"active.p0.{index:02d}" for index in range(1, 7)}

    wrong_goal = deepcopy(oracle)
    wrong_goal["goal_id"] = "wrong-goal"
    with pytest.raises(MatrixError, match="different Goal/group"):
        validate_p3c_p0_oracle(
            wrong_goal, ROOT, original_assets, current_assets
        )

    wrong_result = deepcopy(oracle)
    wrong_result["rows"][0]["native_result"]["termination"] = "primary_input"
    with pytest.raises(MatrixError, match="native result is incomplete"):
        validate_p3c_p0_oracle(
            wrong_result, ROOT, original_assets, current_assets
        )

    missing_boundaries = deepcopy(oracle)
    for row in missing_boundaries["rows"]:
        native = row["native_result"]
        if native["termination"] == "control_only":
            native["branch_evidence"] = []
        for hop in native["chain"]:
            if hop["file"] == "" and hop["line"] == 0:
                hop["file"] = "<unknown>"
                hop["line"] = 1
    with pytest.raises(MatrixError, match="representability boundaries disappeared"):
        validate_p3c_p0_oracle(
            missing_boundaries, ROOT, original_assets, current_assets
        )


def test_p3c_composite_cases_are_closed_individually_with_locked_evidence() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    composite = [
        scenarios[f"active.composite.{index:02d}"]
        for index in range(1, 21)
    ]
    assert [item["status"] for item in composite] == \
        ["semantic-equivalent"] * 20
    assert [
        item["original"]["locked_native_oracle"]["termination"]
        for item in composite
    ] == [
        "primary_input", "primary_input", "primary_input", "primary_input",
        "primary_input", "ambiguous", "ambiguous", "ambiguous",
        "ambiguous", "ambiguous", "ambiguous", "ambiguous", "ambiguous",
        "ambiguous", "primary_input", "primary_input", "ambiguous",
        "primary_input", "primary_input", "primary_input",
    ]
    assert [
        item["original"]["locked_native_oracle"]["total_hops"]
        for item in composite
    ] == [11, 11, 11, 12, 12, 6, 6, 6, 6, 6,
          6, 6, 6, 7, 6, 6, 7, 7, 7, 6]
    assert all(
        item["runtime_audit"]["path"] ==
        "tests/data/rtl_wave_differential/p3c-composite.original-oracle.json"
        and SHA256.fullmatch(item["runtime_audit"]["sha256"])
        and item["runtime_audit"]["locked_temporal_boundaries"] == 1
        and item["runtime_audit"]["remaining_observable_gap_count"] == 0
        and item["current"]["candidate_fixture_ids"] ==
        ["current.active_trace"]
        and item["public_request"]["limits"] == {"max_depth": 11}
        and set(item["public_request"]["args"]) == {"signal", "time"}
        for item in composite
    )
    assert [
        [projection["kind"] for projection in
         item["runtime_audit"]["schema_projections"]]
        for item in composite
    ] == [
        *([[]] * 14), ["source_location_sentinel"],
        ["source_location_sentinel"], [],
        ["source_location_sentinel"], ["source_location_sentinel"],
        ["source_location_sentinel"],
    ]
    required_tests = {
        "test_p3c_composite_oracle_is_locked_complete_and_sanitized",
        "test_p3c_composite_exact_rtl_and_generated_fixture_hashes",
        "test_p3c_composite_matches_locked_native_chain_semantics",
        "test_p3c_composite_limits_are_explicit_analysis_boundaries",
    }
    for index, item in enumerate(composite, 1):
        case = f"case_{index:02d}"
        assert {
            source["path"] for source in item["current"]["candidate_sources"]
        } == {
            f"testdata/fixtures/active_trace/rtl/composite/{case}/tb.sv",
            "testdata/fixtures/active_trace/rtl/composite/chain_dut.sv",
            f"testdata/fixtures/active_trace/composite/{case}/waves.fst",
        }
        assert {
            evidence["test"] for evidence in
            item["current"]["test_evidence"]
        } == required_tests
        assert item["scenario_id"] not in matrix["p3_queue"].get("P3-C", [])


def test_p3c_composite_matrix_gate_rejects_oracle_boundary_drift() -> None:
    manifest = load_assets()
    original_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "original"
    }
    current_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "current"
    }
    oracle = json.loads((
        ROOT / "tests/data/rtl_wave_differential/"
        "p3c-composite.original-oracle.json"
    ).read_text(encoding="utf-8"))
    assert set(validate_p3c_composite_oracle(
        oracle, ROOT, original_assets, current_assets
    )) == {f"active.composite.{index:02d}" for index in range(1, 21)}

    wrong_goal = deepcopy(oracle)
    wrong_goal["goal_id"] = "wrong-goal"
    with pytest.raises(MatrixError, match="different Goal/group"):
        validate_p3c_composite_oracle(
            wrong_goal, ROOT, original_assets, current_assets
        )

    wrong_result = deepcopy(oracle)
    wrong_result["rows"][0]["native_result"]["temporal_boundaries"] = 2
    with pytest.raises(MatrixError, match="native result is incomplete"):
        validate_p3c_composite_oracle(
            wrong_result, ROOT, original_assets, current_assets
        )

    wrong_branch = deepcopy(oracle)
    del wrong_branch["rows"][5]["native_result"][
        "branch_evidence"
    ][0]["candidates"][-1]
    with pytest.raises(MatrixError, match="branch evidence drifted"):
        validate_p3c_composite_oracle(
            wrong_branch, ROOT, original_assets, current_assets
        )

    missing_endpoints = deepcopy(oracle)
    for row in missing_endpoints["rows"]:
        for hop in row["native_result"]["chain"]:
            if hop["file"] == "" and hop["line"] == 0:
                hop["file"] = "source.sv"
                hop["line"] = 1
    with pytest.raises(MatrixError, match="source-less endpoint count drifted"):
        validate_p3c_composite_oracle(
            missing_endpoints, ROOT, original_assets, current_assets
        )


def test_p3c_timing_cases_are_closed_individually_with_locked_evidence() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    timing = [
        scenarios[f"active.timing.{index:02d}"] for index in range(1, 13)
    ]
    assert [item["status"] for item in timing] == \
        ["semantic-equivalent"] * 12
    assert all(
        item["original"]["locked_native_oracle"]["termination"] ==
        "temporal_boundary"
        and item["original"]["locked_native_oracle"]["total_hops"] == 1
        and item["runtime_audit"]["path"] ==
        "tests/data/rtl_wave_differential/p3c-timing.original-oracle.json"
        and SHA256.fullmatch(item["runtime_audit"]["sha256"])
        and item["runtime_audit"]["locked_temporal_boundaries"] == 1
        and item["runtime_audit"]["remaining_observable_gap_count"] == 0
        and item["current"]["candidate_fixture_ids"] ==
        ["current.active_trace"]
        and item["public_request"]["limits"] == {
            "max_depth": 64, "max_nodes": 64,
        }
        and set(item["public_request"]["args"]) == {"signal", "time"}
        for item in timing
    )
    assert all(
        [projection["kind"] for projection in
         item["runtime_audit"]["schema_projections"]] ==
        ["native_stop_on_temporal_prefix"]
        and item["runtime_audit"]["scheduler_projection"]["kind"] ==
        "same_slot_nba_active_time_projection"
        and item["runtime_audit"]["scheduler_projection"]
        ["raw_waveforms_declared_exact"] is False
        for item in timing
    )
    required_tests = {
        "test_p3c_timing_oracle_is_locked_complete_and_sanitized",
        "test_p3c_timing_exact_rtl_and_generated_fixture_hashes",
        "test_p3c_timing_matches_locked_native_temporal_prefix_semantics",
        "test_p3c_timing_limits_are_explicit_analysis_boundaries",
    }
    for index, item in enumerate(timing, 1):
        case = f"case_{index:02d}"
        assert {
            source["path"] for source in item["current"]["candidate_sources"]
        } == {
            f"testdata/fixtures/active_trace/rtl/timing/{case}/tb.sv",
            "testdata/fixtures/active_trace/rtl/timing/timing_boundary_dut.sv",
            f"testdata/fixtures/active_trace/timing/{case}/waves.fst",
        }
        assert {
            evidence["test"] for evidence in
            item["current"]["test_evidence"]
        } == required_tests
        assert item["scenario_id"] not in matrix["p3_queue"].get("P3-C", [])


def test_p3c_timing_matrix_gate_rejects_oracle_boundary_drift() -> None:
    manifest = load_assets()
    original_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "original"
    }
    current_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "current"
    }
    oracle = json.loads((
        ROOT / "tests/data/rtl_wave_differential/"
        "p3c-timing.original-oracle.json"
    ).read_text(encoding="utf-8"))
    assert set(validate_p3c_timing_oracle(
        oracle, ROOT, original_assets, current_assets
    )) == {f"active.timing.{index:02d}" for index in range(1, 13)}

    wrong_goal = deepcopy(oracle)
    wrong_goal["goal_id"] = "wrong-goal"
    with pytest.raises(MatrixError, match="different Goal/group"):
        validate_p3c_timing_oracle(
            wrong_goal, ROOT, original_assets, current_assets
        )

    wrong_result = deepcopy(oracle)
    wrong_result["rows"][0]["native_result"]["temporal_boundary_stops"] = 0
    with pytest.raises(MatrixError, match="native result is incomplete"):
        validate_p3c_timing_oracle(
            wrong_result, ROOT, original_assets, current_assets
        )

    wrong_request = deepcopy(oracle)
    wrong_request["rows"][0]["request"]["stop_on_temporal"] = False
    with pytest.raises(MatrixError, match="request drifted"):
        validate_p3c_timing_oracle(
            wrong_request, ROOT, original_assets, current_assets
        )

    wrong_boundary = deepcopy(oracle)
    wrong_boundary["rows"][0]["native_result"]["chain"][0][
        "active_time"
    ] = "45.0n"
    with pytest.raises(MatrixError, match="temporal boundary drifted"):
        validate_p3c_timing_oracle(
            wrong_boundary, ROOT, original_assets, current_assets
        )


def test_p3c_phase4_cases_are_closed_individually_with_locked_evidence() -> None:
    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    phase4 = [
        scenarios[f"active.phase4.{index:02d}"] for index in range(1, 21)
    ]
    assert [item["status"] for item in phase4] == \
        ["semantic-equivalent"] * 20
    assert [
        item["original"]["locked_native_oracle"]["termination"]
        for item in phase4
    ] == [
        "primary_input", "primary_input", "primary_input", "primary_input",
        "primary_input", "ambiguous", "ambiguous", "ambiguous",
        "ambiguous", "ambiguous", "ambiguous", "ambiguous", "ambiguous",
        "ambiguous", "primary_input", "primary_input", "ambiguous",
        "primary_input", "primary_input", "primary_input",
    ]
    assert [
        item["original"]["locked_native_oracle"]["total_hops"]
        for item in phase4
    ] == [16, 16, 16, 17, 16, 11, 11, 11, 10, 11,
          11, 11, 11, 12, 11, 11, 11, 12, 12, 11]
    assert all(
        item["runtime_audit"]["path"] ==
        "tests/data/rtl_wave_differential/p3c-phase4.original-oracle.json"
        and SHA256.fullmatch(item["runtime_audit"]["sha256"])
        and item["runtime_audit"]["locked_temporal_boundaries"] == 2
        and item["runtime_audit"]["remaining_observable_gap_count"] == 0
        and item["current"]["candidate_fixture_ids"] ==
        ["current.active_trace"]
        and item["public_request"]["limits"] == {"max_depth": 16}
        and set(item["public_request"]["args"]) == {"signal", "time"}
        for item in phase4
    )
    assert [
        [projection["kind"] for projection in
         item["runtime_audit"]["schema_projections"]]
        for item in phase4
    ] == [
        *([[]] * 14), ["source_location_sentinel"],
        ["source_location_sentinel"], [],
        ["source_location_sentinel"], ["source_location_sentinel"],
        ["source_location_sentinel"],
    ]
    required_tests = {
        "test_p3c_phase4_oracle_is_locked_complete_and_sanitized",
        "test_p3c_phase4_exact_rtl_and_generated_fixture_hashes",
        "test_p3c_phase4_matches_locked_native_chain_semantics",
        "test_p3c_phase4_limits_are_explicit_analysis_boundaries",
    }
    for index, item in enumerate(phase4, 1):
        case = f"case_{index:02d}"
        assert {
            source["path"] for source in item["current"]["candidate_sources"]
        } == {
            f"testdata/fixtures/active_trace/rtl/phase4/{case}/tb.sv",
            "testdata/fixtures/active_trace/rtl/phase4/phase4_dut.sv",
            "testdata/fixtures/active_trace/rtl/composite/chain_dut.sv",
            f"testdata/fixtures/active_trace/phase4/{case}/waves.fst",
        }
        assert {
            evidence["test"] for evidence in
            item["current"]["test_evidence"]
        } == required_tests
        assert item["scenario_id"] not in matrix["p3_queue"].get("P3-C", [])


def test_p3c_phase4_matrix_gate_rejects_oracle_boundary_drift() -> None:
    manifest = load_assets()
    original_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "original"
    }
    current_assets = {
        asset["path"]: asset for asset in manifest["assets"]
        if asset["side"] == "current"
    }
    oracle = json.loads((
        ROOT / "tests/data/rtl_wave_differential/"
        "p3c-phase4.original-oracle.json"
    ).read_text(encoding="utf-8"))
    assert set(validate_p3c_phase4_oracle(
        oracle, ROOT, original_assets, current_assets
    )) == {f"active.phase4.{index:02d}" for index in range(1, 21)}

    wrong_goal = deepcopy(oracle)
    wrong_goal["goal_id"] = "wrong-goal"
    with pytest.raises(MatrixError, match="different Goal/group"):
        validate_p3c_phase4_oracle(
            wrong_goal, ROOT, original_assets, current_assets
        )

    wrong_result = deepcopy(oracle)
    wrong_result["rows"][0]["native_result"]["temporal_boundaries"] = 1
    with pytest.raises(MatrixError, match="native result is incomplete"):
        validate_p3c_phase4_oracle(
            wrong_result, ROOT, original_assets, current_assets
        )

    wrong_branch = deepcopy(oracle)
    del wrong_branch["rows"][5]["native_result"][
        "branch_evidence"
    ][0]["candidates"][-1]
    with pytest.raises(MatrixError, match="branch evidence drifted"):
        validate_p3c_phase4_oracle(
            wrong_branch, ROOT, original_assets, current_assets
        )

    missing_endpoints = deepcopy(oracle)
    for row in missing_endpoints["rows"]:
        for hop in row["native_result"]["chain"]:
            if hop["file"] == "" and hop["line"] == 0:
                hop["file"] = "source.sv"
                hop["line"] = 1
    with pytest.raises(MatrixError, match="source-less endpoint count drifted"):
        validate_p3c_phase4_oracle(
            missing_endpoints, ROOT, original_assets, current_assets
        )
