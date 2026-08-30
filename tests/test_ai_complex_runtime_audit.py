from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = (
    ROOT / "tests/data/rtl_wave_differential/ai_complex.runtime-audit.json"
)
MATRIX_PATH = ROOT / "tests/coverage/rtl_wave_semantic_matrix.json"


def _load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _all_strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from _all_strings(key)
            yield from _all_strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from _all_strings(item)


def test_ai_complex_runtime_audit_locks_two_sided_oracle_and_boundaries() -> None:
    audit = _load(AUDIT_PATH)
    assert audit["schema_version"] == "xdebug.ai-complex-runtime-audit.v1"
    assert audit["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    locked = audit["locked_original_runtime"]
    assert locked["git_revision"] == \
        "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
    assert locked["schema_revision"] == \
        "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
    assert locked["action_count"] == 73
    assert locked["fixture"]["cache_reused"] is True
    assert locked["fixture"]["fixture_rebuilt"] is False
    assert locked["suite_gate"]["result"] == "passed"
    assert locked["suite_gate"]["session_closed_gracefully"] is True
    assert locked["counter_runner"]["sha256"] == \
        "fe0bafa4ff50d36d1fc283da07f915981ce613aae618341324edfb683c29ead5"
    assert locked["counter_suite_gate"]["result"] == "passed"
    assert locked["counter_suite_gate"]["session_closed_gracefully"] is True

    current = audit["current_runtime"]
    assert current["locked_suite_gate"]["result"] == "passed"
    assert current["locked_suite_gate"]["same_runner_sha256"] == \
        locked["runner"]["sha256"]
    assert current["locked_suite_gate"]["same_mode"] == \
        locked["runner"]["mode"] == "nonaxi"
    assert current["locked_counter_suite_gate"]["result"] == "passed"
    assert current["locked_counter_suite_gate"]["same_runner_sha256"] == \
        locked["counter_runner"]["sha256"]
    assert current["repository_gate"] == {
        "path": "tests/test_ai_complex_wave.py",
        "sha256": "97e070e2360b4da2d1ed3de7a60a2de24f694476074be3cb3066d9f23c67407d",
        "passed": 7,
        "failed": 0,
    }
    assert current["focused_regression"]["test"] == \
        "test_counter_statistics_resolves_named_cursor_time_range"
    assert current["focused_regression"]["passed"] == 1
    assert current["focused_regression"]["failed"] == 0

    boundary = audit["write_boundary_audit"]
    assert boundary["only_writable_repository"] == "xdebug_fst"
    assert boundary["external_inputs_read_only"] is True
    assert boundary["fallback_used"] is False
    for repository in ("original_xverif", "wellen", "verilator"):
        assert boundary[f"{repository}_snapshot_sha256_before"] == \
            boundary[f"{repository}_snapshot_sha256_after"]

    strings = list(_all_strings(audit))
    assert not [value for value in strings if value.startswith("/")]
    assert not [value for value in strings if "/home/" in value]


def test_ai_complex_runtime_audit_hashes_all_committed_current_evidence() -> None:
    audit = _load(AUDIT_PATH)
    current = audit["current_runtime"]
    fixture = current["fixture"]
    expected = {
        "testdata/fixtures/ai_complex/ai_complex_top.sv":
            fixture["rtl_sha256"],
        "testdata/fixtures/ai_complex/generate_ai_complex_fst.cpp":
            fixture["generator_sha256"],
        "testdata/fixtures/ai_complex/fstcpp-four-state-vector.patch":
            fixture["writer_patch_sha256"],
        "testdata/fixtures/ai_complex/waves.fst": fixture["fst_sha256"],
        current["repository_gate"]["path"]:
            current["repository_gate"]["sha256"],
        current["focused_regression"]["path"]:
            current["focused_regression"]["sha256"],
    }
    for relative, expected_hash in expected.items():
        path = ROOT / relative
        assert path.is_file(), relative
        assert _sha256(path) == expected_hash, relative
    assert fixture["deterministic_second_build"] is True
    assert fixture["vcd_or_json_conversion_used"] is False


def test_ai_complex_runtime_audit_closes_only_its_matrix_scenario() -> None:
    audit = _load(AUDIT_PATH)
    matrix = _load(MATRIX_PATH)
    scenario = next(
        item for item in matrix["scenarios"]
        if item["scenario_id"] == "fixture.ai_complex_wave"
    )
    assert audit["verdict"]["remaining_observable_gap_count"] == 0
    assert scenario["status"] == audit["verdict"]["status"] == \
        "semantic-equivalent"
    assert scenario["p3_batch"] == audit["verdict"]["p3_batch"] == "P3-A"
    assert scenario["scenario_id"] not in matrix["p3_queue"].get("P3-A", [])
    evidence = scenario["runtime_audit"]
    assert evidence["path"] == \
        "tests/data/rtl_wave_differential/ai_complex.runtime-audit.json"
    assert evidence["sha256"] == _sha256(AUDIT_PATH)
    assert evidence["same_locked_oracle_passed_both_sides"] is True
    assert evidence["remaining_observable_gap_count"] == 0
