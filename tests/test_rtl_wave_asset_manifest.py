import json
import re
from pathlib import Path

import pytest

from tools import freeze_rtl_wave_assets as freezer
from tools.freeze_rtl_wave_assets import (
    CURRENT_BASELINE_HEAD,
    GOAL_ID,
    InventoryError,
    ORIGINAL_BASELINE_HEAD,
    ORIGINAL_TRANSITIVE_CONSUMERS,
    SCHEMA_VERSION,
    canonical_json,
    ensure_output_within_repo,
    fixture_ids_for_path,
    fixture_ids_referenced_by_consumer,
    parse_fixture_registry,
    parse_porcelain_z,
    relative_output_records,
    verify_frozen_original_content,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = REPO_ROOT / "compat/xdebug-v1/rtl-wave-assets.manifest.json"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def load_manifest() -> dict:
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def all_strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from all_strings(key)
            yield from all_strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from all_strings(item)


def test_fixture_registry_parser_preserves_build_and_wave_probe_contracts() -> None:
    registry = """\
version: test.v1
fixtures:
  - id: xdebug.root
    build_capabilities: [make, vcs, npi]
    source_dir: xdebug/testdata/root
    inputs: [Makefile, "*.sv"]
    builder:
      argv: [make, run, "OUT={resources}"]
    outputs:
      - {name: cases, path: cases, kind: dir}
    probes:
      - argv: [python3, probe.py, --fsdb-glob, "cases/*/waves.fsdb"]

  - id: xdebug.root.child
    build_capabilities: [vcs]
    source_dir: xdebug/testdata/root/child
    inputs: [child.sv]
    builder:
      argv: [vcs, child.sv]
    outputs:
      - {name: fsdb, path: out/waves.fsdb, kind: file, min_bytes: 1024}
"""
    fixtures = parse_fixture_registry(registry)
    assert [fixture["id"] for fixture in fixtures] == [
        "xdebug.root", "xdebug.root.child",
    ]
    assert fixtures[0]["build_capabilities"] == ["make", "vcs", "npi"]
    assert fixtures[0]["builder_argv"] == ["make", "run", "OUT={resources}"]
    assert fixtures[0]["probe_argv"] == [
        ["python3", "probe.py", "--fsdb-glob", "cases/*/waves.fsdb"]
    ]
    assert fixture_ids_for_path(
        "xdebug/testdata/root/child/child.sv", fixtures
    ) == ["xdebug.root.child"]
    assert relative_output_records(fixtures) == [
        {
            "declarations": ["probe"],
            "fixture_id": "xdebug.root",
            "kind": "probe_glob",
            "name": "fsdb",
            "path": "cases/*/waves.fsdb",
        },
        {
            "declarations": ["output"],
            "fixture_id": "xdebug.root.child",
            "kind": "file",
            "min_bytes": 1024,
            "name": "fsdb",
            "path": "out/waves.fsdb",
        },
    ]


def test_consumer_fixture_reference_recognizes_constructed_source_path() -> None:
    fixtures = [
        {
            "id": "xdebug.ai_complex_wave",
            "source_dir": "xdebug/testdata/waveform/ai_complex_wave",
        },
        {
            "id": "xdebug.counter_stats",
            "source_dir": "xdebug/testdata/waveform/counter_stats",
        },
    ]
    text = '''
NONAXI_DIR = os.path.join(
    ROOT, "testdata", "waveform", "ai_complex_wave")
'''
    assert fixture_ids_referenced_by_consumer(text, fixtures) == [
        "xdebug.ai_complex_wave"
    ]
    assert fixture_ids_referenced_by_consumer(
        "fixture = 'xdebug.counter_stats'", fixtures
    ) == ["xdebug.counter_stats"]
    assert fixture_ids_referenced_by_consumer(
        "from run_complex_wave import NONAXI_FSDB", fixtures
    ) == ["xdebug.ai_complex_wave"]
    assert fixture_ids_referenced_by_consumer(
        "output = 'out/waves.fsdb'", fixtures
    ) == []


def test_porcelain_parser_preserves_dirty_and_untracked_states() -> None:
    entries = parse_porcelain_z(
        b" M tracked.sv\0?? new.sv\0R  renamed.sv\0old.sv\0"
    )
    assert entries == [
        {
            "index_status": " ",
            "worktree_status": "M",
            "path": "tracked.sv",
        },
        {
            "index_status": "?",
            "worktree_status": "?",
            "path": "new.sv",
        },
        {
            "index_status": "R",
            "worktree_status": " ",
            "path": "renamed.sv",
            "source_path": "old.sv",
        },
    ]


def test_output_boundary_rejects_paths_outside_current_repository() -> None:
    root = Path("/workspace/xdebug_fst")
    assert ensure_output_within_repo(root, root / "compat/manifest.json") == (
        root / "compat/manifest.json"
    )
    with pytest.raises(InventoryError, match="escapes the only writable repository"):
        ensure_output_within_repo(root, Path("/workspace/external/manifest.json"))


def test_output_boundary_rejects_symlink_escape(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    outside = tmp_path / "outside"
    root.mkdir()
    outside.mkdir()
    (root / "escape").symlink_to(outside, target_is_directory=True)
    with pytest.raises(InventoryError, match="escapes the only writable repository"):
        ensure_output_within_repo(root, root / "escape/manifest.json")


def test_checked_in_manifest_freezes_complete_p0_inventory() -> None:
    manifest = load_manifest()
    assert manifest["schema_version"] == SCHEMA_VERSION
    assert manifest["goal_id"] == GOAL_ID
    assert manifest["baselines"]["current"]["head"] == CURRENT_BASELINE_HEAD
    assert manifest["baselines"]["original_assets"]["head"] == ORIGINAL_BASELINE_HEAD
    assert manifest["baselines"]["initial_counts"] == {
        "current_fst": 12,
        "current_rtl": 11,
        "current_vcd": 1,
        "original_hdl": 103,
    }
    summary = manifest["summary"]
    assert summary["original"]["rtl_count"] == 103
    assert summary["original"]["fixture_count"] == 23
    assert summary["current"]["rtl_count"] >= 11
    assert summary["current"]["fst_count"] >= 12
    assert summary["current"]["vcd_count"] >= 1
    assert summary["unassigned_original_hdl"] == []
    assert summary["missing_assets"] == []

    goal_start = manifest["external_read_only_goal_start_snapshot"]
    audit = manifest["external_read_only_audit_snapshot"]
    assert [item["repository"] for item in goal_start] == [
        "original_xverif", "wellen", "verilator",
    ]
    assert goal_start[0]["head"] == ORIGINAL_BASELINE_HEAD
    assert [item["repository"] for item in audit] == [
        "original_xverif", "wellen", "verilator",
    ]
    assert manifest["write_boundary"] == {
        "external_repositories_read_only": [
            "original_xverif", "wellen", "verilator",
        ],
        "only_writable_repository": "xdebug_fst",
    }


def test_checked_in_manifest_has_unique_hashed_relative_assets() -> None:
    manifest = load_manifest()
    assets = manifest["assets"]
    identities = [(asset["side"], asset["path"]) for asset in assets]
    assert len(identities) == len(set(identities))
    assert all(not Path(asset["path"]).is_absolute() for asset in assets)
    assert all(asset["exists"] for asset in assets)
    assert all(asset["file_type"] in {"file", "symlink"} for asset in assets)
    assert all(SHA256_PATTERN.fullmatch(asset["sha256"]) for asset in assets)
    assert all(asset["fixture_ids"] for asset in assets)
    assert not any("original.unassigned" in asset["fixture_ids"] for asset in assets)
    local_home_prefix = "/" + "home/"
    assert not any(text.startswith(local_home_prefix) for text in all_strings(manifest))
    assert canonical_json(manifest) == MANIFEST_PATH.read_text(encoding="utf-8")


def test_ai_complex_runners_are_assigned_to_their_reused_fixture() -> None:
    manifest = load_manifest()
    assets = {
        item["path"]: item
        for item in manifest["assets"]
        if item["side"] == "original"
    }
    complex_runner = assets["xdebug/tests/waveform/run_complex_wave.py"]
    counter_runner = assets["xdebug/tests/waveform/run_counter_statistics.py"]
    assert "xdebug.ai_complex_wave" in complex_runner["fixture_ids"]
    assert counter_runner["fixture_ids"] == ["xdebug.ai_complex_wave"]


def test_transitive_oracle_runners_are_frozen_with_their_fixtures() -> None:
    manifest = load_manifest()
    assets = {
        item["path"]: item
        for item in manifest["assets"]
        if item["side"] == "original"
    }
    assert manifest["discovery_contract"][
        "original_transitive_consumers"
    ] == ORIGINAL_TRANSITIVE_CONSUMERS
    active_runner = assets[
        "xdebug/tests/combined/run_active_driver_fixture.py"
    ]
    assert active_runner["fixture_ids"] == [
        "xdebug.active_driver", "xdebug.interface_port_root",
    ]
    design_runner = assets["xdebug/tests/design/run_semantics.sh"]
    assert design_runner["fixture_ids"] == [
        "xdebug.design_p3", "xdebug.design_uart",
    ]
    assert design_runner["sha256"] == \
        "74f0cc841505d5a2290102f57b0ee60ffc270ba4c94b8e66d546adc8f51beee1"


def test_frozen_original_allows_only_goal_start_transitive_additions(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    existing = b"existing\n"
    transitive = b"goal-start runner\n"

    def asset(path: str, content: bytes) -> dict:
        return {
            "side": "original",
            "path": path,
            "exists": True,
            "file_type": "file",
            "sha256": freezer.sha256_bytes(content),
            "size_bytes": len(content),
        }

    frozen = {"assets": [asset("xdebug/tests/existing.py", existing)]}
    unchanged = asset("xdebug/tests/existing.py", existing)
    approved = asset("xdebug/tests/design/run_semantics.sh", transitive)
    monkeypatch.setattr(freezer, "git_bytes", lambda *_args: transitive)

    verify_frozen_original_content(tmp_path, [unchanged, approved], frozen)
    with pytest.raises(InventoryError, match="not an explicitly reviewed"):
        verify_frozen_original_content(
            tmp_path,
            [unchanged, asset("xdebug/tests/new_unreviewed.py", b"new\n")],
            frozen,
        )
    with pytest.raises(InventoryError, match="does not match the Goal-start"):
        verify_frozen_original_content(
            tmp_path,
            [unchanged, asset("xdebug/tests/design/run_semantics.sh", b"drift\n")],
            frozen,
        )


def test_active_trace_declares_dynamic_fsdb_outputs_from_probe_contracts() -> None:
    manifest = load_manifest()
    outputs = {
        (output["fixture_id"], output["path"])
        for output in manifest["original_declared_waveform_outputs"]
    }
    for fixture_id in (
        "xdebug.active_trace_p0",
        "xdebug.active_trace_composite",
        "xdebug.active_trace_timing",
        "xdebug.active_trace_phase4",
        "xdebug.active_trace_phase5",
    ):
        assert (fixture_id, "cases/*/out/waves.fsdb") in outputs
