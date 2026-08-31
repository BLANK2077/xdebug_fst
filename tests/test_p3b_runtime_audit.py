from __future__ import annotations

import hashlib
import json
import re
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = (
    REPO_ROOT / "tests/data/rtl_wave_differential/p3b.runtime-audit.json"
)
MANIFEST_PATH = REPO_ROOT / "compat/xdebug-v1/rtl-wave-assets.manifest.json"
MATRIX_PATH = REPO_ROOT / "tests/coverage/rtl_wave_semantic_matrix.json"
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
RUNTIME_REVISION = "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
SCHEMA_REVISION = (
    "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _all_strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from _all_strings(key)
            yield from _all_strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from _all_strings(item)


def _assets(manifest: dict, side: str) -> dict[str, dict]:
    return {
        item["path"]: item
        for item in manifest["assets"]
        if item["side"] == side
    }


def _comparisons(audit: dict) -> dict[str, dict]:
    return {item["scenario_id"]: item for item in audit["comparisons"]}


def test_p3b_audit_locks_both_original_authorities() -> None:
    audit = _load(AUDIT_PATH)
    locked = audit["locked_original_runtime"]
    policy = audit["authority_policy"]

    assert audit["schema_version"] == "xdebug.p3b-runtime-audit.v1"
    assert audit["goal_id"] == GOAL_ID
    assert locked["git_revision"] == RUNTIME_REVISION
    assert locked["schema_revision"] == SCHEMA_REVISION
    assert locked["action_count"] == 73
    assert locked["source_access"] == "read_only"
    assert locked["fixture_cache_reused"] is True
    assert locked["fixture_rebuilt"] is False
    assert policy == {
        "behavior_authority": "locked_original_runtime",
        "asset_authority": "goal_start_original_assets",
        "authority_conflicts_preserved": True,
        "binary_waveform_comparison_used": False,
        "comparison_unit": (
            "RTL/刺激/时间/公开 Action/完整响应或冻结 schema 不可观察性证明"
        ),
    }


def test_p3b_locked_original_gates_and_caches_are_complete() -> None:
    audit = _load(AUDIT_PATH)
    locked = audit["locked_original_runtime"]
    gates = {item["gate_id"]: item for item in locked["runner_gates"]}
    assert set(gates) == {
        "active_driver_and_interface",
        "active_semantics",
        "active_zero_evidence",
        "trace_x_xprop",
        "design_semantics",
    }
    assert all(
        item["result"] == "passed"
        and item["passed"] > 0
        and item["failed"] == 0
        and item["skipped"] == 0
        and item["session_closed_gracefully"] is True
        for item in gates.values()
    )
    assert gates["active_driver_and_interface"]["passed"] == 10
    assert gates["active_zero_evidence"]["passed"] == 16
    assert gates["active_driver_and_interface"][
        "locked_revision_sha256"
    ] == "d7c31e53aebb84b019917241d58b7c4a29f3a431a45bfe58fe7f59a86c7c96f7"
    assert gates["design_semantics"][
        "locked_transitive_runner_sha256"
    ] == "0e16294cf8a6fdcec74f12f376b6a8480716375c4a94a05cefbc42133dd3b323"

    caches = {item["fixture_id"]: item for item in locked["fixture_caches"]}
    assert set(caches) == {
        "xdebug.active_driver",
        "xdebug.interface_port_root",
        "xdebug.active_semantics",
        "xdebug.active_zero_evidence",
        "xdebug.trace_x_xprop",
        "xdebug.design_uart",
        "xdebug.design_p3",
    }
    for fixture in caches.values():
        assert SHA256_PATTERN.fullmatch(fixture["cache_fingerprint"])
        assert fixture["cache_version"].startswith(
            fixture["cache_fingerprint"] + "-prepare-"
        )
        assert SHA256_PATTERN.fullmatch(fixture["manifest_sha256"])
        assert fixture["tool_identity"] == "X-2025.06"
        if fixture["format"] == "fsdb":
            assert SHA256_PATTERN.fullmatch(fixture["waveform_sha256"])
            assert fixture["waveform_size"] > 1024


def test_p3b_goal_start_oracles_are_frozen_in_the_p0_manifest() -> None:
    audit = _load(AUDIT_PATH)
    assets = _assets(_load(MANIFEST_PATH), "original")
    for gate in audit["locked_original_runtime"]["runner_gates"]:
        path = gate["relative_path"]
        assert assets[path]["sha256"] == gate["goal_start_asset_sha256"]
        if "transitive_runner_relative_path" in gate:
            transitive = gate["transitive_runner_relative_path"]
            assert assets[transitive]["sha256"] == gate[
                "goal_start_transitive_runner_sha256"
            ]


def test_p3b_current_fixture_hashes_are_live_and_manifest_frozen() -> None:
    audit = _load(AUDIT_PATH)
    current_assets = _assets(_load(MANIFEST_PATH), "current")
    for fixture in audit["current_runtime"]["fixtures"]:
        for path_key, hash_key in (
            ("rtl_path", "rtl_sha256"),
            ("harness_path", "harness_sha256"),
            ("design_db_path", "design_db_sha256"),
            ("fst_path", "fst_sha256"),
            ("hash_record_path", "hash_record_sha256"),
        ):
            if path_key not in fixture:
                continue
            relative = fixture[path_key]
            assert _sha256(REPO_ROOT / relative) == fixture[hash_key]
            assert current_assets[relative]["sha256"] == fixture[hash_key]

    generation = audit["current_runtime"]["fixture_generation"]
    assert _sha256(REPO_ROOT / generation["script_path"]) == generation[
        "script_sha256"
    ]
    assert _sha256(REPO_ROOT / generation["verilator_patch_path"]) == generation[
        "verilator_patch_sha256"
    ]
    assert generation["deterministic_second_build"] is True
    assert generation["vcd_or_json_conversion_used"] is False
    assert generation["fallback_used"] is False


def test_p3b_exact_mirrors_match_frozen_original_rtl() -> None:
    audit = _load(AUDIT_PATH)
    original_assets = _assets(_load(MANIFEST_PATH), "original")
    comparisons = _comparisons(audit)
    exact = {
        "fixture.active_driver",
        "fixture.interface_port_root",
        "fixture.active_zero_evidence",
    }
    for scenario_id in exact:
        item = comparisons[scenario_id]
        assert item["comparison_method"] == "exact_rtl_and_ported_public_oracle"
        assert item["exact_rtl_match"] is True
        assert item["ported_public_oracle_complete"] is True
        assert item["same_locked_oracle_executable_used_on_current"] is False
        assert item["original_rtl_sha256"] == item["current_rtl_sha256"]
        assert original_assets[item["original_rtl_path"]]["sha256"] == item[
            "original_rtl_sha256"
        ]
        assert _sha256(REPO_ROOT / item["current_rtl_path"]) == item[
            "current_rtl_sha256"
        ]


def test_p3b_all_eight_scenarios_are_closed_without_overclaiming_runner_reuse() -> None:
    audit = _load(AUDIT_PATH)
    comparisons = audit["comparisons"]
    current_gates = {
        item["gate_id"]: item
        for item in audit["current_runtime"]["repository_gates"]
    }
    statuses = Counter(item["status"] for item in comparisons)
    assert len(comparisons) == 8
    assert statuses == {"semantic-equivalent": 6, "proven-unobservable": 2}
    assert all(item["p3_batch"] == "P3-B" for item in comparisons)
    assert all(item["remaining_observable_gap_count"] == 0 for item in comparisons)
    for item in comparisons:
        if item["status"] == "semantic-equivalent":
            assert item["same_locked_oracle_executable_used_on_current"] is False
            assert item.get("ported_public_oracle_complete") is True or item.get(
                "normalized_contract_mapping_complete"
            ) is True
    assert {
        gate_id: (item["passed"], item["failed"])
        for gate_id, item in current_gates.items()
    } == {
        "ported_original_active_oracles": (18, 0),
        "design_contracts": (13, 0),
        "adjacent_regression": (146, 0),
        "static_differential_contracts": (27, 0),
        "ctest": (7, 0),
    }
    assert audit["verdict"] == {
        "p3_batch": "P3-B",
        "scenario_count": 8,
        "semantic_equivalent_count": 6,
        "proven_unobservable_count": 2,
        "partial_count": 0,
        "missing_count": 0,
        "remaining_observable_gap_count": 0,
    }


def test_design_hierarchy_unobservable_proof_matches_locked_schema() -> None:
    audit = _load(AUDIT_PATH)
    item = _comparisons(audit)["fixture.design_hierarchy"]
    request_schema = _load(
        REPO_ROOT
        / "compat/xdebug-v1/schemas/v1/actions/scope.list.request.schema.json"
    )
    response_schema = _load(
        REPO_ROOT
        / "compat/xdebug-v1/schemas/v1/actions/scope.list.response.schema.json"
    )
    catalog = _load(REPO_ROOT / "compat/xdebug-v1/catalog.response.json")

    locked_kinds = request_schema["properties"]["args"]["properties"]["kind"][
        "enum"
    ]
    locked_groups = sorted(
        response_schema["$defs"]["successData"]["properties"]
    )
    assert item["status"] == "proven-unobservable"
    assert item["test_present_at_locked_runtime"] is False
    assert item["locked_scope_list_kind_enum"] == locked_kinds
    assert item["locked_scope_list_data_groups"] == locked_groups
    assert set(item["goal_start_requested_unsupported_kinds"]).isdisjoint(
        locked_kinds
    )
    assert set(item["goal_start_requested_unsupported_groups"]).isdisjoint(
        locked_groups
    )
    assert "scope.list" not in catalog["data"]["modes"]["design"]


def test_design_p3_unobservable_proof_is_hash_anchored() -> None:
    audit = _load(AUDIT_PATH)
    item = _comparisons(audit)["fixture.design_p3"]
    assets = _assets(_load(MANIFEST_PATH), "original")
    assert item["status"] == "proven-unobservable"
    assert item["public_actions"] == ["session.open"]
    assert item["p3_session_opened"] is True
    assert item["p3_semantic_query_count"] == 0
    assert assets[item["goal_start_runner_path"]]["sha256"] == item[
        "goal_start_runner_sha256"
    ]
    assert item["locked_runner_sha256"] == (
        "0e16294cf8a6fdcec74f12f376b6a8480716375c4a94a05cefbc42133dd3b323"
    )


def test_p3b_audit_enforces_write_boundary_and_contains_no_absolute_paths() -> None:
    audit = _load(AUDIT_PATH)
    boundary = audit["write_boundary_audit"]
    assert boundary["only_writable_repository"] == "xdebug_fst"
    assert boundary["external_inputs_read_only"] is True
    assert boundary["fallback_used"] is False
    assert boundary["all_runtime_home_tmp_socket_and_artifacts_repository_local"] \
        is True
    for repository in ("original_xverif", "wellen", "verilator"):
        assert boundary[f"{repository}_snapshot_sha256_before"] == boundary[
            f"{repository}_snapshot_sha256_after"
        ]
    assert not [text for text in _all_strings(audit) if text.startswith("/")]


def test_p3b_audit_is_integrated_into_the_semantic_matrix() -> None:
    audit = _load(AUDIT_PATH)
    matrix = _load(MATRIX_PATH)
    scenarios = {item["scenario_id"]: item for item in matrix["scenarios"]}
    audit_sha256 = _sha256(AUDIT_PATH)

    for comparison in audit["comparisons"]:
        matrix_row = scenarios[comparison["scenario_id"]]
        assert matrix_row["status"] == comparison["status"]
        assert matrix_row["p3_batch"] == "P3-B"
        assert matrix_row["runtime_audit"] == {
            "path": "tests/data/rtl_wave_differential/p3b.runtime-audit.json",
            "sha256": audit_sha256,
            "status": comparison["status"],
            "p3_batch": "P3-B",
            "comparison_method": comparison["comparison_method"],
            "remaining_observable_gap_count": 0,
        }

    # P4 final gate: every observable gap must have been closed, while the
    # bounded private-NPI cases remain explicitly proven unobservable.
    status_counts = matrix["summary"]["status_counts"]
    assert status_counts == {
        "proven-unobservable": 7,
        "semantic-equivalent": 81,
    }
    assert sum(status_counts.values()) == matrix["summary"]["scenario_count"]
    assert matrix["p3_queue"] == {}
    assert "P3-B" not in matrix["p3_queue"]
