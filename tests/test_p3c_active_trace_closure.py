from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest

from tools.build_rtl_wave_semantic_matrix import (
    MatrixError,
    asset_lookup,
    validate_p3c_active_trace_closure_audit,
)


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = (
    ROOT / "tests/data/rtl_wave_differential/"
    "p3c-active-trace-closure.audit.json"
)
MATRIX_PATH = ROOT / "tests/coverage/rtl_wave_semantic_matrix.json"
MANIFEST_PATH = ROOT / "compat/xdebug-v1/rtl-wave-assets.manifest.json"
AUDIT_SHA256 = (
    "c9d29f6e93c598501013c011243b62aa81574f104ffdd8d53f5474a8ac51c7fc"
)


def load_audit() -> dict:
    assert AUDIT_PATH.is_file(), "缺少 P3-C runner/orphan 冻结裁决证据"
    assert hashlib.sha256(AUDIT_PATH.read_bytes()).hexdigest() == AUDIT_SHA256
    return json.loads(AUDIT_PATH.read_text(encoding="utf-8"))


def load_matrix() -> dict:
    return json.loads(MATRIX_PATH.read_text(encoding="utf-8"))


def test_runner_private_helper_has_no_uncovered_public_observation() -> None:
    audit = load_audit()
    runner = audit["runner"]
    assert audit["schema_version"] == "xdebug.p3c-active-trace-closure-audit.v1"
    assert audit["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert audit["session"] == {
        "all_writes_repository_local": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
        "source_access": "read_only",
    }
    assert runner["fixture_id"] == "xdebug.active_trace_runner"
    assert runner["classification"] == "proven-unobservable"
    assert runner["private_helper"] == {
        "output_kind": "native_test_executable",
        "output_path": "build/chain_test",
        "public_action": False,
        "rtl_input_count": 0,
        "waveform_output_count": 0,
    }
    assert runner["coverage"]["catalog_case_count"] == 68
    assert runner["coverage"]["native_runner_case_count"] == 58
    assert runner["coverage"]["public_runtime_case_count"] == 10
    assert set(runner["coverage"]["consumer_public_action_disposition"]) == {
        "session.close",
        "session.open",
        "trace.active_driver",
        "trace.active_driver_chain",
    }
    assert runner["coverage"]["consumer_public_action_disposition"][
        "trace.active_driver"
    ]["executable_request_count"] == 0
    assert runner["coverage"]["remaining_unmapped_consumer_action_count"] == 0
    assert runner["coverage"]["remaining_distinct_public_observation_count"] == 0

    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    scenario = scenarios["fixture.active_trace_runner"]
    assert scenario["status"] == "proven-unobservable"
    assert scenario["unobservable_proof"]["path"] == AUDIT_PATH.relative_to(
        ROOT
    ).as_posix()
    assert scenario["unobservable_proof"][
        "remaining_distinct_public_observation_count"
    ] == 0
    assert all(
        scenarios[scenario_id]["status"] == "semantic-equivalent"
        for scenario_id in runner["coverage"]["covered_scenario_ids"]
    )


def test_declared_only_p0_4_has_a_bounded_frozen_absence_proof() -> None:
    audit = load_audit()
    orphan = audit["declared_only_orphan"]
    assert orphan["scenario_id"] == "active.p0.declared_only_p0_4"
    assert orphan["classification"] == "proven-unobservable"
    assert orphan["frozen_directory"]["entries"] == [".gitignore"]
    assert orphan["frozen_directory"]["rtl_count"] == 0
    assert orphan["frozen_directory"]["stimulus_count"] == 0
    assert orphan["frozen_directory"]["concrete_waveform_count"] == 0
    assert orphan["catalog_row_count"] == 0
    assert orphan["authoritative_public_request_count"] == 0
    assert orphan["remaining_distinct_public_observation_count"] == 0

    matrix = load_matrix()
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    scenario = scenarios[orphan["scenario_id"]]
    assert scenario["status"] == "proven-unobservable"
    assert scenario["original"]["catalog_presence"] is False
    assert scenario["original"]["sources"] == []
    assert scenario["current"]["candidate_fixture_ids"] == []
    assert scenario["unobservable_proof"]["path"] == AUDIT_PATH.relative_to(
        ROOT
    ).as_posix()


def test_p3c_queue_is_empty_only_after_both_bounded_proofs() -> None:
    matrix = load_matrix()
    assert matrix["p3_queue"].get("P3-C", []) == []
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    assert scenarios["fixture.active_trace_runner"]["status"] == \
        "proven-unobservable"
    assert scenarios["active.p0.declared_only_p0_4"]["status"] == \
        "proven-unobservable"


def test_closure_validator_rejects_broadened_or_mutated_proofs() -> None:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    original_assets = asset_lookup(manifest, "original")
    current_assets = asset_lookup(manifest, "current")

    def validate(document: dict) -> dict:
        return validate_p3c_active_trace_closure_audit(
            document,
            ROOT,
            manifest,
            original_assets,
            current_assets,
        )

    audit = load_audit()
    assert validate(audit) == audit

    fallback = deepcopy(audit)
    fallback["session"]["fallback_used"] = True
    with pytest.raises(MatrixError, match="write/cache/fallback boundary"):
        validate(fallback)

    uncovered = deepcopy(audit)
    uncovered["runner"]["coverage"][
        "remaining_distinct_public_observation_count"
    ] = 1
    with pytest.raises(MatrixError, match="catalog coverage drifted"):
        validate(uncovered)

    public_helper = deepcopy(audit)
    public_helper["runner"]["private_helper"]["public_action"] = True
    with pytest.raises(MatrixError, match="private runner boundary drifted"):
        validate(public_helper)

    invented_rtl = deepcopy(audit)
    invented_rtl["declared_only_orphan"]["frozen_directory"]["rtl_count"] = 1
    with pytest.raises(MatrixError, match="declared-only orphan proof drifted"):
        validate(invented_rtl)
