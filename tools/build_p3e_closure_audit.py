#!/usr/bin/env python3
"""Build the bounded P3-E XIF/SVA/cross-fixture closure audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
SCHEMA_VERSION = "xdebug.p3e-closure-audit.v1"
DEFAULT_MANIFEST = Path("compat/xdebug-v1/rtl-wave-assets.manifest.json")
DEFAULT_BOUNDARY = Path("tests/data/rtl_wave_differential/p3e-boundary.audit.json")
DEFAULT_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3e-xif-event.public-oracle.json"
)
DEFAULT_OUTPUT = Path("tests/data/rtl_wave_differential/p3e-closure.audit.json")
XIF_FIXTURE_ROOT = Path("testdata/fixtures/xif_event")
EXPECTED_BOUNDARY_SHA256 = (
    "242750e22f7cb2fe76b661d4040e10db771fb2f4388f25d1ddac9174c5d1a1c0"
)
EXPECTED_ORACLE_SHA256 = (
    "251255661924fe455eccd79ecc863f62304d0052b9bd242dca474d65fd52f35f"
)
REQUIRED_XIF_OBSERVATIONS = {
    "direct_packed_struct_fields": [f"direct.{index:02d}" for index in range(1, 11)],
    "rdy_flow_control": ["full.rdy"],
    "bp_flow_control": ["full.bp"],
    "none_flow_control": ["full.none"],
    "paired_master_slave": ["full.pair_master", "full.pair_slave"],
    "xz_unknown_expression": ["full.xz", "xz.unknown"],
    "relational_expression": ["relational.rdy"],
    "event_find_line_limit": ["find.rdy.limit2"],
    "event_find_flat_xout": ["find.rdy.limit2"],
    "event_export_full_set": [
        "full.rdy", "full.bp", "full.none", "full.pair_master",
        "full.pair_slave", "full.xz",
    ],
    "unknown_alias_error": ["find.unknown_alias"],
    "value_at_struct_member": [
        "value.01", "value.02", "value.03", "value.04", "value.05",
    ],
}


class ClosureError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inside(repo_root: Path, path: Path, label: str) -> Path:
    root = repo_root.resolve()
    resolved = path.resolve(strict=False)
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ClosureError(f"{label} escapes repository: {resolved}") from error
    return resolved


def current_assets(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        row["path"]: row
        for row in manifest["assets"]
        if row["side"] == "current"
    }


def require_current_asset(
    repo_root: Path,
    assets: dict[str, dict[str, Any]],
    relative: str,
) -> dict[str, Any]:
    row = assets.get(relative)
    path = repo_root / relative
    if row is None or not path.is_file() or row["sha256"] != sha256(path):
        raise ClosureError(f"P3-E current asset is not frozen: {relative}")
    return row


def strings(value: Any):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from strings(key)
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


def build_audit(repo_root: Path, manifest_path: Path) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("goal_id") != GOAL_ID:
        raise ClosureError("P3-E manifest belongs to a different Goal")
    assets = current_assets(manifest)

    boundary_path = repo_root / DEFAULT_BOUNDARY
    oracle_path = repo_root / DEFAULT_ORACLE
    boundary_asset = require_current_asset(repo_root, assets, DEFAULT_BOUNDARY.as_posix())
    oracle_asset = require_current_asset(repo_root, assets, DEFAULT_ORACLE.as_posix())
    if boundary_asset["sha256"] != EXPECTED_BOUNDARY_SHA256:
        raise ClosureError("P3-E boundary audit identity drifted")
    if oracle_asset["sha256"] != EXPECTED_ORACLE_SHA256:
        raise ClosureError("P3-E XIF oracle identity drifted")
    boundary = json.loads(boundary_path.read_text(encoding="utf-8"))
    oracle = json.loads(oracle_path.read_text(encoding="utf-8"))

    if boundary.get("schema_version") != "xdebug.p3e-boundary-audit.v1":
        raise ClosureError("P3-E boundary audit schema drifted")
    if oracle.get("schema_version") != "xdebug.p3e-xif-event-public-oracle.v1":
        raise ClosureError("P3-E XIF oracle schema drifted")
    observations = {row["observation_id"]: row for row in oracle["observations"]}
    if len(observations) != 32:
        raise ClosureError("P3-E XIF oracle must contain 32 unique observations")
    required = boundary["xif_event"]["required_observations"]
    if set(required) != set(REQUIRED_XIF_OBSERVATIONS):
        raise ClosureError("P3-E XIF required observation surface drifted")
    for requirement, observation_ids in REQUIRED_XIF_OBSERVATIONS.items():
        missing = [name for name in observation_ids if name not in observations]
        if missing:
            raise ClosureError(f"P3-E XIF requirement is unsatisfied: {requirement}: {missing}")
    if "xout" not in observations["find.rdy.limit2"]:
        raise ClosureError("P3-E XIF flat XOUT evidence is missing")
    if "artifact" not in observations["artifact.rdy.full"]:
        raise ClosureError("P3-E XIF artifact evidence is missing")

    fixture_manifest_path = XIF_FIXTURE_ROOT / "fixture.manifest.json"
    fixture_manifest_asset = require_current_asset(
        repo_root, assets, fixture_manifest_path.as_posix()
    )
    fixture_manifest = json.loads(
        (repo_root / fixture_manifest_path).read_text(encoding="utf-8")
    )
    fixture_lock_asset = require_current_asset(
        repo_root, assets, (XIF_FIXTURE_ROOT / "fixture.sha256").as_posix()
    )
    fst_asset = require_current_asset(
        repo_root, assets, (XIF_FIXTURE_ROOT / "waves.fst").as_posix()
    )
    if (
        fixture_manifest.get("fixture_id") != "current.xif_event"
        or fixture_manifest["output_contract"]["sha256"] != fst_asset["sha256"]
        or fixture_manifest["output_contract"]["size"] != fst_asset["size_bytes"]
    ):
        raise ClosureError("P3-E XIF fixture output contract drifted")

    original_sources = {
        row["path"]: row["sha256"] for row in oracle["original_fixture"]["sources"]
    }
    direct_configs: list[dict[str, Any]] = []
    for name in ("bp", "none", "pair_master", "pair_slave", "rdy", "xz"):
        relative = (XIF_FIXTURE_ROOT / f"event_{name}.json").as_posix()
        current = require_current_asset(repo_root, assets, relative)
        original_hash = original_sources[f"event_{name}.json"]
        if current["sha256"] != original_hash:
            raise ClosureError(f"P3-E directly reused config drifted: {name}")
        direct_configs.append({
            "name": f"event_{name}.json",
            "sha256": current["sha256"],
            "reuse": "byte-identical-copy",
        })

    test_path = "tests/test_p3e_xif_event_differential.py"
    test_asset = require_current_asset(repo_root, assets, test_path)
    test_text = (repo_root / test_path).read_text(encoding="utf-8")
    test_names = [
        "test_xif_fixture_exists_and_is_locked",
        "test_xif_original_oracle_locks_all_e2_observations",
        "test_xif_all_32_original_observations_replay_on_raw_fst",
        "test_xif_event_find_xout_preserves_complete_public_evidence",
        "test_xif_field_shorthand_rejects_partial_integer_bounds",
    ]
    if any(f"def {name}(" not in test_text for name in test_names):
        raise ClosureError("P3-E XIF executable gate inventory drifted")

    sva = boundary["sva_npi"]
    if (
        sva["observed_public_actions"] != []
        or sva["public_exposure_count"] != 0
        or sva["remaining_distinct_public_observation_count"] != 0
        or any(sva["public_schema_token_hits"].values())
    ):
        raise ClosureError("P3-E SVA bounded private/public proof drifted")
    cross = boundary["cross_fixture"]
    if (
        cross["consumer_count"] != 37
        or cross["observed_public_action_count"] != 72
        or len(cross["contracts"]) != 72
        or cross["remaining_unmapped_consumer_action_count"] != 0
        or cross["catalog_actions_not_observed"] != ["session.kill"]
    ):
        raise ClosureError("P3-E cross-fixture contract index drifted")

    document = {
        "schema_version": SCHEMA_VERSION,
        "goal_id": GOAL_ID,
        "boundary": {
            "path": DEFAULT_BOUNDARY.as_posix(),
            "sha256": boundary_asset["sha256"],
        },
        "xif_event": {
            "classification": "semantic-equivalent",
            "original_fixture_id": "xdebug.xif_event",
            "current_fixture_id": "current.xif_event",
            "oracle": {
                "path": DEFAULT_ORACLE.as_posix(),
                "sha256": oracle_asset["sha256"],
                "observation_count": len(observations),
                "cache_reused": oracle["original_fixture"]["cache_reused"],
                "fixture_rebuilt": oracle["original_fixture"]["fixture_rebuilt"],
            },
            "asset_reuse_policy": {
                "direct_copy_preferred": True,
                "directly_reused_configs": direct_configs,
                "original_rtl_directly_executable_open_source": False,
                "rtl_replacement_scope": "minimal-pin-level-semantic-mirror",
                "rtl_replacement_reason": "original-requires-uvm-xif-agent-vcs-fsdb",
                "cross_side_hash_equality_required": False,
                "public_content_equivalence_required": True,
            },
            "fixture": {
                "manifest_path": fixture_manifest_path.as_posix(),
                "manifest_sha256": fixture_manifest_asset["sha256"],
                "lock_sha256": fixture_lock_asset["sha256"],
                "fst_sha256": fst_asset["sha256"],
                "fst_size": fst_asset["size_bytes"],
                "deterministic_build_directories": fixture_manifest[
                    "output_contract"
                ]["deterministic_build_directories"],
                "proprietary_vip_used": False,
                "fsdb_conversion_used": False,
                "action_export_feedback_used": False,
            },
            "requirements": REQUIRED_XIF_OBSERVATIONS,
            "test_gate": {"path": test_path, "sha256": test_asset["sha256"],
                          "tests": test_names},
            "remaining_observable_gap_count": 0,
        },
        "sva_npi": {
            "classification": "proven-unobservable",
            "fixture_id": "xdebug.npi_fsdb_sva",
            "consumer_count": len(sva["consumer_paths"]),
            "observed_public_action_count": 0,
            "public_exposure_count": 0,
            "private_probe_schema_versions": sva["private_probe_schema_versions"],
            "candidate_action_disposition": sva["candidate_action_disposition"],
            "remaining_distinct_public_observation_count": 0,
            "proof_scope": (
                "only the frozen SVA/NPI private probe fields and the frozen "
                "73-action public schemas; future schema exposure fails closed"
            ),
        },
        "cross_fixture": {
            "classification": "proven-unobservable",
            "fixture_id": "original.cross_fixture",
            "consumer_count": cross["consumer_count"],
            "observed_public_actions": cross["observed_public_actions"],
            "observed_public_action_count": cross["observed_public_action_count"],
            "contract_count": len(cross["contracts"]),
            "catalog_actions_not_observed": cross["catalog_actions_not_observed"],
            "remaining_unmapped_consumer_action_count": 0,
            "remaining_distinct_public_observation_count": 0,
            "proof_scope": (
                "closes only the synthetic cross-fixture consumer index; each "
                "waveform fixture remains governed by its own runtime differential"
            ),
        },
        "session": {
            "all_writes_repository_local": True,
            "external_sources_read_only": True,
            "fallback_used": False,
            "fixture_rebuilt": False,
        },
        "verdict": {
            "p3_batch": "P3-E",
            "scenario_count": 3,
            "semantic_equivalent_count": 1,
            "proven_unobservable_count": 2,
            "partial_count": 0,
            "missing_count": 0,
            "remaining_observable_gap_count": 0,
        },
    }
    if any(value.startswith("/") or "/home/" in value for value in strings(document)):
        raise ClosureError("P3-E closure audit leaks an absolute path")
    return document


def canonical_json(document: dict[str, Any]) -> str:
    return json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def validate_audit(
    document: dict[str, Any], repo_root: Path, manifest_path: Path
) -> None:
    if document != build_audit(repo_root, manifest_path):
        raise ClosureError("P3-E closure audit content drifted")


def write_document(repo_root: Path, output: Path, document: dict[str, Any]) -> None:
    destination = inside(repo_root, output, "P3-E closure output")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=destination.parent,
        prefix=f".{destination.name}.", suffix=".tmp", delete=False,
    ) as stream:
        stream.write(canonical_json(document))
        temporary = Path(stream.name)
    temporary.replace(destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()
    manifest = inside(ROOT, ROOT / args.manifest, "P3-E manifest")
    output = inside(ROOT, ROOT / args.output, "P3-E closure output")
    document = build_audit(ROOT, manifest)
    rendered = canonical_json(document)
    if args.check:
        if not output.is_file() or output.read_text(encoding="utf-8") != rendered:
            raise ClosureError("P3-E closure audit is stale")
        print(f"OK: {output.relative_to(ROOT)} is reproducible")
    else:
        write_document(ROOT, output, document)
        print(f"wrote {output.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ClosureError, KeyError, json.JSONDecodeError, OSError) as error:
        print(f"error: {error}", file=os.sys.stderr)
        raise SystemExit(2)
