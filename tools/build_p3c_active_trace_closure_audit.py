#!/usr/bin/env python3
"""Build the bounded P3-C runner/orphan unobservability audit.

The original checkout and every sibling dependency are immutable inputs.  The
only supported output is a deterministic JSON file below this repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.build_rtl_wave_semantic_matrix import (
    GOAL_ID,
    MatrixError,
    asset_lookup,
    canonical_json,
    ensure_within_repo,
    parse_active_catalog,
    validate_frozen_file,
)


SCHEMA_VERSION = "xdebug.p3c-active-trace-closure-audit.v1"
MANIFEST_PATH = Path("compat/xdebug-v1/rtl-wave-assets.manifest.json")
OUTPUT_PATH = Path(
    "tests/data/rtl_wave_differential/"
    "p3c-active-trace-closure.audit.json"
)
ACTIVE_CATALOG = "xdebug/tests/active_trace_chain/cases.v1.yaml"
MIRRORED_CATALOG = "testdata/fixtures/active_trace/original-cases.v1.yaml"
REQUEST_SCHEMA = (
    "compat/xdebug-v1/schemas/v1/actions/"
    "trace.active_driver_chain.request.schema.json"
)
RUNNER_FIXTURE_ID = "xdebug.active_trace_runner"
RUNNER_SHA256 = (
    "f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237"
)
RUNNER_INPUTS = [
    "xdebug/tests/active_trace_chain/Makefile",
    "xdebug/tests/active_trace_chain/chain_test.cpp",
    "xdebug/tests/active_trace_chain/chain_test.h",
]
NATIVE_ORACLES = [
    (
        "p0",
        "tests/data/rtl_wave_differential/p3c-p0.original-oracle.json",
        6,
    ),
    (
        "composite",
        "tests/data/rtl_wave_differential/"
        "p3c-composite.original-oracle.json",
        20,
    ),
    (
        "timing",
        "tests/data/rtl_wave_differential/"
        "p3c-timing.original-oracle.json",
        12,
    ),
    (
        "phase4",
        "tests/data/rtl_wave_differential/"
        "p3c-phase4.original-oracle.json",
        20,
    ),
]
PHASE5_ORACLE = (
    "tests/data/rtl_wave_differential/p3c-phase5.public-oracle.json"
)
ORPHAN_CASE = "p0_4_interface_modport"
ORPHAN_DIRECTORY = (
    "xdebug/tests/active_trace_chain/p0_composability/"
    "p0_4_interface_modport"
)
ORPHAN_ENTRY = f"{ORPHAN_DIRECTORY}/.gitignore"
ACTIVE_README = "xdebug/tests/active_trace_chain/README.md"


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def frozen_json(repo_root: Path, asset: dict) -> dict:
    return json.loads(validate_frozen_file(repo_root, asset).decode("utf-8"))


def asset_record(root: Path, asset: dict) -> dict:
    validate_frozen_file(root, asset)
    return {
        "path": asset["path"],
        "sha256": asset["sha256"],
        "size_bytes": asset["size_bytes"],
    }


def require_asset(assets: dict[str, dict], path: str, label: str) -> dict:
    asset = assets.get(path)
    if asset is None:
        raise MatrixError(f"{label} is not frozen in the asset manifest: {path}")
    return asset


def build_audit(
    repo_root: Path,
    original_root: Path,
    manifest_path: Path,
) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("goal_id") != GOAL_ID:
        raise MatrixError("P3-C closure audit manifest belongs to another Goal")
    original_assets = asset_lookup(manifest, "original")
    current_assets = asset_lookup(manifest, "current")

    catalog_asset = require_asset(
        original_assets, ACTIVE_CATALOG, "active-trace catalog"
    )
    catalog_payload = validate_frozen_file(original_root, catalog_asset)
    catalog_rows = parse_active_catalog(catalog_payload.decode("utf-8"))
    group_counts: dict[str, int] = {}
    for row in catalog_rows:
        group = row["group"]
        group_counts[group] = group_counts.get(group, 0) + 1
    if group_counts != {
        "composite": 20,
        "p0": 6,
        "phase4": 20,
        "phase5": 10,
        "timing": 12,
    }:
        raise MatrixError(f"active-trace catalog group counts drifted: {group_counts}")
    mirrored_catalog_asset = require_asset(
        current_assets, MIRRORED_CATALOG, "mirrored active-trace catalog"
    )
    validate_frozen_file(repo_root, mirrored_catalog_asset)
    if mirrored_catalog_asset["sha256"] != catalog_asset["sha256"]:
        raise MatrixError("mirrored active-trace catalog is not byte-identical")

    fixtures = {
        item["id"]: item for item in manifest.get("original_fixtures", [])
    }
    runner_fixture = fixtures.get(RUNNER_FIXTURE_ID)
    expected_fixture = {
        "build_capabilities": ["make", "gxx", "npi"],
        "builder_argv": ["make", "chain_test", "BUILD={resources}/build"],
        "catalog_reference_lines": [693, 706, 719, 732, 745],
        "id": RUNNER_FIXTURE_ID,
        "inputs": ["Makefile", "chain_test.cpp", "chain_test.h"],
        "outputs": [{
            "kind": "file",
            "min_bytes": 1024,
            "name": "runner",
            "path": "build/chain_test",
        }],
        "probe_argv": [[
            "python3",
            "{repo}/testinfra/leaf/probe_fixture.py",
            "--resources",
            "{resources}",
            "--executable-glob",
            "build/chain_test",
        ]],
        "source_dir": "xdebug/tests/active_trace_chain",
    }
    if runner_fixture != expected_fixture:
        raise MatrixError("active-trace runner fixture contract drifted")
    runner_inputs = [
        asset_record(
            original_root,
            require_asset(original_assets, path, "runner source"),
        )
        for path in RUNNER_INPUTS
    ]
    runner_cpp = validate_frozen_file(
        original_root,
        require_asset(
            original_assets,
            "xdebug/tests/active_trace_chain/chain_test.cpp",
            "runner implementation",
        ),
    ).decode("utf-8")
    if "npi_active_trace_driver_by_hdl" not in runner_cpp:
        raise MatrixError("runner no longer calls the locked private NPI entry point")
    if any(
        token in runner_cpp
        for token in (
            '"api_version"', '"action"', '"session.open"',
            '"trace.active_driver_chain"',
        )
    ):
        raise MatrixError("private runner unexpectedly exposes a public Action request")

    request_schema_path = repo_root / REQUEST_SCHEMA
    if not request_schema_path.is_file():
        raise MatrixError("trace.active_driver_chain request schema is missing")
    request_schema_payload = request_schema_path.read_bytes()
    request_schema = json.loads(request_schema_payload.decode("utf-8"))
    request_schema_sha256 = sha256_bytes(request_schema_payload)
    args_schema = request_schema.get("properties", {}).get("args", {})
    if (
        request_schema.get("additionalProperties") is not False
        or request_schema.get("required") != [
            "api_version", "action", "args", "target"
        ]
        or args_schema.get("required") != ["signal", "time"]
        or args_schema.get("additionalProperties") is not False
        or set(args_schema.get("properties", {})) != {
            "render_time_unit", "signal", "time", "value_format"
        }
    ):
        raise MatrixError("trace.active_driver_chain request boundary drifted")
    serialized_schema = json.dumps(request_schema, sort_keys=True)
    private_fields = [
        "active_trace_calls",
        "edgecheck_direct_count",
        "fallback_0_5ns_count",
        "stop_on_temporal",
        "temporal_boundary_stops",
    ]
    if any(field in serialized_schema for field in private_fields):
        raise MatrixError("private native runner field leaked into the public request")

    covered_scenarios: list[str] = []
    native_evidence = []
    for group, path, row_count in NATIVE_ORACLES:
        asset = require_asset(current_assets, path, f"{group} native oracle")
        document = frozen_json(repo_root, asset)
        rows = document.get("rows", [])
        expected_ids = [
            f"active.{group}.{index:02d}"
            for index in range(1, row_count + 1)
        ]
        if (
            document.get("schema_version") !=
                "xdebug.p3c-original-active-trace-oracle.v1"
            or document.get("goal_id") != GOAL_ID
            or document.get("group") != group
            or document.get("locked_runtime", {}).get("runner_sha256") !=
                RUNNER_SHA256
            or document.get("locked_runtime", {}).get("cache_reused") is not True
            or document.get("locked_runtime", {}).get("fixture_rebuilt") is not False
            or document.get("session", {}).get("fallback_used") is not False
            or [row.get("scenario_id") for row in rows] != expected_ids
        ):
            raise MatrixError(f"{group} native runner coverage drifted")
        native_evidence.append({
            "group": group,
            "path": path,
            "row_count": row_count,
            "runner_sha256": RUNNER_SHA256,
            "sha256": asset["sha256"],
        })
        covered_scenarios.extend(expected_ids)

    phase5_asset = require_asset(
        current_assets, PHASE5_ORACLE, "Phase5 public runtime oracle"
    )
    phase5 = frozen_json(repo_root, phase5_asset)
    phase5_ids = [f"active.phase5.{index:02d}" for index in range(1, 11)]
    if (
        phase5.get("schema_version") != "xdebug.p3c-phase5-public-oracle.v1"
        or phase5.get("goal_id") != GOAL_ID
        or phase5.get("group") != "phase5"
        or phase5.get("session", {}).get("fallback_used") is not False
        or phase5.get("locked_runtime", {}).get("cache_reused") is not True
        or phase5.get("locked_runtime", {}).get("fixture_rebuilt") is not False
        or [row.get("scenario_id") for row in phase5.get("rows", [])] !=
            phase5_ids
        or any(
            row.get("response", {}).get("summary", {}).get(
                "analysis_complete"
            ) is not True
            for row in phase5.get("rows", [])
        )
    ):
        raise MatrixError("Phase5 public runtime coverage drifted")
    covered_scenarios.extend(phase5_ids)
    expected_scenarios = [
        f"active.{row['group']}.{index:02d}"
        for group in ("p0", "composite", "timing", "phase4", "phase5")
        for index, row in enumerate(
            [item for item in catalog_rows if item["group"] == group], 1
        )
    ]
    if covered_scenarios != expected_scenarios or len(set(covered_scenarios)) != 68:
        raise MatrixError("runner catalog coverage is not exhaustive and ordered")

    orphan_root = original_root / ORPHAN_DIRECTORY
    if not orphan_root.is_dir():
        raise MatrixError("declared-only p0_4 directory is missing")
    live_entries = sorted(
        path.relative_to(orphan_root).as_posix()
        for path in orphan_root.rglob("*")
    )
    if live_entries != [".gitignore"]:
        raise MatrixError(
            f"declared-only p0_4 live directory drifted: {live_entries}"
        )
    frozen_orphan_assets = sorted(
        path for path in original_assets
        if path == ORPHAN_DIRECTORY or path.startswith(ORPHAN_DIRECTORY + "/")
    )
    if frozen_orphan_assets != [ORPHAN_ENTRY]:
        raise MatrixError(
            f"declared-only p0_4 frozen inventory drifted: {frozen_orphan_assets}"
        )
    orphan_asset = require_asset(
        original_assets, ORPHAN_ENTRY, "declared-only p0_4 .gitignore"
    )
    orphan_payload = validate_frozen_file(original_root, orphan_asset)
    if orphan_payload != b"out/\ncsrc/\nsimv.daidir/\nucli.key\n":
        raise MatrixError("declared-only p0_4 .gitignore content drifted")
    if any(row.get("case") == ORPHAN_CASE for row in catalog_rows):
        raise MatrixError("declared-only p0_4 unexpectedly gained a catalog row")
    readme_asset = require_asset(
        original_assets, ACTIVE_README, "active-trace README"
    )
    readme = validate_frozen_file(original_root, readme_asset).decode("utf-8")
    readme_lines = readme.splitlines()
    if len(readme_lines) < 22 or ORPHAN_CASE not in readme_lines[21]:
        raise MatrixError("declared-only p0_4 README declaration drifted")
    documentation_lines = [9, 10, 27]
    if any(
        "trace.active_driver" not in readme_lines[line - 1]
        for line in documentation_lines
    ):
        raise MatrixError("runner trace.active_driver documentation drifted")
    phase5_test_path = "xdebug/tests/active_trace_chain/test_phase5.py"
    phase5_test_asset = require_asset(
        original_assets, phase5_test_path, "Phase5 public consumer"
    )
    phase5_test = validate_frozen_file(
        original_root, phase5_test_asset
    ).decode("utf-8")
    executable_actions = {
        action: phase5_test.count(f'"action": "{action}"')
        for action in (
            "session.open", "trace.active_driver_chain", "session.close"
        )
    }
    if executable_actions != {
        "session.open": 1,
        "trace.active_driver_chain": 1,
        "session.close": 1,
    }:
        raise MatrixError("Phase5 executable public Action inventory drifted")
    consumer_public_action_disposition = {
        "session.close": {
            "covered_by": PHASE5_ORACLE,
            "executable_request_count": 1,
            "source": phase5_test_path,
            "source_sha256": phase5_test_asset["sha256"],
        },
        "session.open": {
            "covered_by": PHASE5_ORACLE,
            "executable_request_count": 1,
            "source": phase5_test_path,
            "source_sha256": phase5_test_asset["sha256"],
        },
        "trace.active_driver": {
            "documentation_only_lines": documentation_lines,
            "executable_request_count": 0,
            "source": ACTIVE_README,
            "source_sha256": readme_asset["sha256"],
        },
        "trace.active_driver_chain": {
            "catalog_observation_count": 68,
            "covered_by": [
                *(path for _, path, _ in NATIVE_ORACLES),
                PHASE5_ORACLE,
            ],
            "executable_request_count": 1,
            "source": phase5_test_path,
            "source_sha256": phase5_test_asset["sha256"],
        },
    }

    return {
        "schema_version": SCHEMA_VERSION,
        "goal_id": GOAL_ID,
        "session": {
            "all_writes_repository_local": True,
            "fallback_used": False,
            "fixture_rebuilt": False,
            "source_access": "read_only",
        },
        "runner": {
            "fixture_id": RUNNER_FIXTURE_ID,
            "classification": "proven-unobservable",
            "fixture_contract": expected_fixture,
            "source_assets": runner_inputs,
            "private_helper": {
                "output_kind": "native_test_executable",
                "output_path": "build/chain_test",
                "public_action": False,
                "rtl_input_count": 0,
                "waveform_output_count": 0,
            },
            "public_contract": {
                "request_schema": REQUEST_SCHEMA,
                "request_schema_sha256": request_schema_sha256,
                "required_args": ["signal", "time"],
                "private_fields_absent": private_fields,
            },
            "coverage": {
                "catalog": {
                    "path": ACTIVE_CATALOG,
                    "sha256": catalog_asset["sha256"],
                    "group_counts": group_counts,
                },
                "catalog_case_count": 68,
                "native_runner_case_count": 58,
                "native_oracles": native_evidence,
                "public_runtime_case_count": 10,
                "public_runtime_oracle": {
                    "group": "phase5",
                    "path": PHASE5_ORACLE,
                    "row_count": 10,
                    "sha256": phase5_asset["sha256"],
                },
                "consumer_public_action_count": 4,
                "consumer_public_action_disposition":
                    consumer_public_action_disposition,
                "covered_scenario_ids": covered_scenarios,
                "remaining_unmapped_consumer_action_count": 0,
                "remaining_distinct_public_observation_count": 0,
            },
            "proof_scope": (
                "The native executable remains a private oracle helper; its "
                "catalog-derived public observations are closed elsewhere."
            ),
        },
        "declared_only_orphan": {
            "scenario_id": "active.p0.declared_only_p0_4",
            "case": ORPHAN_CASE,
            "classification": "proven-unobservable",
            "declaration": {
                "path": ACTIVE_README,
                "line": 22,
                "sha256": readme_asset["sha256"],
            },
            "frozen_directory": {
                "path": ORPHAN_DIRECTORY,
                "entries": [".gitignore"],
                "entry_sha256": orphan_asset["sha256"],
                "rtl_count": 0,
                "stimulus_count": 0,
                "concrete_waveform_count": 0,
            },
            "catalog": {
                "path": ACTIVE_CATALOG,
                "sha256": catalog_asset["sha256"],
            },
            "catalog_row_count": 0,
            "authoritative_public_request_count": 0,
            "remaining_distinct_public_observation_count": 0,
            "current_capability_is_not_equivalence": True,
            "proof_scope": (
                "Only the frozen declaration and empty case skeleton are "
                "classified; interface/modport behavior remains testable when "
                "an authoritative RTL and signal/time request exist."
            ),
        },
    }


def write_atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            stream.write(content)
            temporary = Path(stream.name)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path)
    parser.add_argument("--original-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=MANIFEST_PATH)
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    actual_root = Path(__file__).resolve().parents[1]
    repo_root = (args.repo_root or actual_root).resolve()
    if repo_root != actual_root.resolve():
        raise MatrixError("repo-root must be this repository; no fallback is allowed")
    original_root = args.original_root.resolve()
    manifest_path = (
        args.manifest if args.manifest.is_absolute()
        else repo_root / args.manifest
    )
    output = args.output if args.output.is_absolute() else repo_root / args.output
    output = ensure_within_repo(repo_root, output)
    audit = build_audit(repo_root, original_root, manifest_path)
    content = canonical_json(audit)
    if args.check:
        if not output.is_file() or output.read_text(encoding="utf-8") != content:
            raise MatrixError(
                f"P3-C closure audit is stale: {output.relative_to(repo_root)}"
            )
        print(f"OK: {output.relative_to(repo_root)} is reproducible")
        return 0
    write_atomic(output, content)
    print(f"wrote {output.relative_to(repo_root)}")
    print(f"sha256={sha256_bytes(content.encode('utf-8'))}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except MatrixError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
