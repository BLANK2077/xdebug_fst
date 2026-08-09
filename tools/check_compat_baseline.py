#!/usr/bin/env python3
"""Verify the frozen xdebug v1 baseline and locked local dependencies."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def git_revision(repository: Path) -> str:
    return subprocess.run(
        ["git", "-C", str(repository), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def verify_frozen_files(repo_root: Path) -> list[str]:
    errors: list[str] = []
    baseline_dir = repo_root / "compat/xdebug-v1"
    manifest = baseline_dir / "SHA256SUMS"
    expected_paths: set[Path] = set()

    for line_number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            errors.append(f"SHA256SUMS:{line_number}: malformed entry")
            continue
        expected_hash, relative_text = match.groups()
        relative = Path(relative_text)
        expected_paths.add(relative)
        path = baseline_dir / relative
        if not path.is_file():
            errors.append(f"missing frozen file: {relative}")
        elif sha256(path) != expected_hash:
            errors.append(f"hash mismatch: {relative}")

    actual_schemas = {
        path.relative_to(baseline_dir)
        for path in (baseline_dir / "schemas/v1").rglob("*.json")
    }
    expected_schemas = {path for path in expected_paths if path.parts[:2] == ("schemas", "v1")}
    for path in sorted(actual_schemas - expected_schemas):
        errors.append(f"untracked frozen schema: {path}")
    for path in sorted(expected_schemas - actual_schemas):
        errors.append(f"manifest schema missing from tree: {path}")

    metadata = load_json(baseline_dir / "BASELINE.json")
    catalog = load_json(baseline_dir / "catalog.response.json")
    actions = catalog.get("data", {}).get("actions", [])
    if catalog.get("ok") is not True or catalog.get("action") != "actions":
        errors.append("frozen catalog is not a successful actions response")
    if len(actions) != 73 or len(set(actions)) != 73:
        errors.append(f"public action catalog must contain 73 unique entries, found {len(actions)}")
    if "clock_point_query" in actions:
        errors.append("non-canonical clock_point_query is present in public catalog")

    public_schema_dir = baseline_dir / "schemas/v1/actions"
    public_schemas = list(public_schema_dir.glob("*.json"))
    if len(public_schemas) != 146:
        errors.append(f"expected 146 public action schemas, found {len(public_schemas)}")
    for action in actions:
        for kind in ("request", "response"):
            if not (public_schema_dir / f"{action}.{kind}.schema.json").is_file():
                errors.append(f"missing public {kind} schema for {action}")

    all_schemas = list((baseline_dir / "schemas/v1").rglob("*.json"))
    if len(all_schemas) != 282:
        errors.append(f"expected 282 total v1 schemas, found {len(all_schemas)}")
    for schema in all_schemas:
        try:
            load_json(schema)
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"invalid JSON schema {schema.relative_to(baseline_dir)}: {exc}")

    expected_manifest_hash = metadata["schemas"]["sha256_manifest_sha256"]
    if sha256(manifest) != expected_manifest_hash:
        errors.append("BASELINE.json manifest hash does not match SHA256SUMS")
    if sha256(baseline_dir / "catalog.response.json") != metadata["catalog"]["response_sha256"]:
        errors.append("BASELINE.json catalog hash does not match frozen response")
    return errors


def parse_cmake_lock(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    contents = path.read_text(encoding="utf-8")
    for name, value in re.findall(r'set\((XDEBUG_[A-Z0-9_]+)\s+"([0-9a-f]+)"\)', contents):
        values[name] = value
    return values


def verify_dependencies(repo_root: Path) -> list[str]:
    errors: list[str] = []
    lock = load_json(repo_root / "dependencies.lock.json")
    cmake_lock = parse_cmake_lock(repo_root / "cmake/DependenciesLock.cmake")
    expected_cmake = {
        "XDEBUG_WELLEN_REVISION": lock["wellen"]["revision"],
        "XDEBUG_VERILATOR_REVISION": lock["verilator"]["revision"],
        "XDEBUG_WELLEN_CAPI_HEADER_SHA256": lock["wellen"]["capi_header_sha256"],
        "XDEBUG_WELLENX_CAPI_HEADER_SHA256": lock["wellenx_capi"]["header_sha256"],
        "XDEBUG_XDD_HEADER_SHA256": lock["verilator"]["xdd_header_sha256"],
    }
    if cmake_lock != expected_cmake:
        errors.append("cmake/DependenciesLock.cmake differs from dependencies.lock.json")

    for name in ("wellen", "verilator"):
        repository = (repo_root / lock[name]["repository"]).resolve()
        try:
            actual_revision = git_revision(repository)
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append(f"cannot read {name} revision: {exc}")
            continue
        if actual_revision != lock[name]["revision"]:
            errors.append(
                f"{name} revision drift: expected {lock[name]['revision']}, found {actual_revision}"
            )

    hash_checks = (
        (repo_root / lock["wellen"]["repository"] / "wellen_capi/include/wellen_capi.h",
         lock["wellen"]["capi_header_sha256"], "Wellen C API header"),
        (repo_root / lock["verilator"]["repository"] / "include/xdd_api.h",
         lock["verilator"]["xdd_header_sha256"], "Verilator XDD header"),
        (repo_root / "wellenx_capi/include/wellenx_capi.h",
         lock["wellenx_capi"]["header_sha256"], "wellenx C API header"),
        (repo_root / "wellenx_capi/src/lib.rs",
         lock["wellenx_capi"]["source_sha256"], "wellenx source"),
        (repo_root / "wellenx_capi/Cargo.lock",
         lock["wellenx_capi"]["cargo_lock_sha256"], "wellenx Cargo lock"),
    )
    for path, expected, label in hash_checks:
        if not path.is_file():
            errors.append(f"{label} is missing: {path}")
        elif sha256(path) != expected:
            errors.append(f"{label} hash drift: {path}")
    return errors


def verify_original(repo_root: Path, original_root: Path) -> list[str]:
    errors: list[str] = []
    baseline_dir = repo_root / "compat/xdebug-v1"
    metadata = load_json(baseline_dir / "BASELINE.json")
    source = metadata["source"]
    executables = (
        (original_root / "xdebug/xdebug", source["runtime_executable_sha256"], "runtime"),
        (original_root / "xdebug/libexec/xdebug-engine", source["engine_executable_sha256"], "engine"),
    )
    for path, expected, label in executables:
        if not path.is_file() or sha256(path) != expected:
            errors.append(f"original {label} binary drift: {path}")

    original_schemas = original_root / "xdebug/schemas/v1"
    frozen_schemas = baseline_dir / "schemas/v1"
    for frozen in frozen_schemas.rglob("*.json"):
        relative = frozen.relative_to(frozen_schemas)
        original = original_schemas / relative
        if not original.is_file() or sha256(original) != sha256(frozen):
            errors.append(f"original schema drift: {relative}")

    request = json.dumps({"api_version": "xdebug.v1", "action": "actions", "args": {}}) + "\n"
    try:
        result = subprocess.run(
            [str(original_root / "xdebug/xdebug"), "--json", "-"],
            input=request,
            text=True,
            capture_output=True,
            cwd=original_root,
            timeout=30,
            check=False,
        )
        live_catalog = json.loads(result.stdout)
        if result.returncode != 0 or live_catalog != load_json(baseline_dir / "catalog.response.json"):
            errors.append("original runtime actions response drift")
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
        errors.append(f"cannot query original runtime catalog: {exc}")
    return errors


def verify(repo_root: Path, original_root: Path | None = None) -> list[str]:
    errors = verify_frozen_files(repo_root)
    errors.extend(verify_dependencies(repo_root))
    if original_root is not None:
        errors.extend(verify_original(repo_root, original_root))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument(
        "--original-root",
        type=Path,
        help="also compare against a read-only original xverif checkout",
    )
    args = parser.parse_args()
    errors = verify(args.repo_root.resolve(), args.original_root.resolve() if args.original_root else None)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("compat baseline: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
