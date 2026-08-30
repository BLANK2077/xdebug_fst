#!/usr/bin/env python3
"""Freeze the original/current xdebug RTL and waveform test asset inventory.

This tool is deliberately read-only for the original xverif, Wellen, and
Verilator repositories.  The only supported output is a path whose resolved
location remains below the current xdebug-fst repository root.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = "xdebug-rtl-wave-assets.v1"
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
CURRENT_BASELINE_HEAD = "302d0c2b40eff1547d7479c6838b6866c8ab7c7f"
ORIGINAL_BASELINE_HEAD = "478a944d2b1efefff55afb6f4130944ed19fee1b"

HDL_SUFFIXES = {".sv", ".svh", ".v", ".vh", ".vhd", ".vhdl"}
WAVE_SUFFIXES = {".fst", ".fsdb", ".vcd", ".vpd"}
TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".f", ".h", ".hpp", ".json", ".jsonl",
    ".md", ".py", ".svi", ".sv", ".svh", ".tcl", ".txt", ".v",
    ".vh", ".vhd", ".vhdl", ".yaml", ".yml",
}
CURRENT_ASSET_PREFIX = "testdata/fixtures/"
ORIGINAL_TEST_PREFIXES = (
    "xdebug/testdata/",
    "xdebug/tests/active_trace_chain/",
)
ORIGINAL_REGISTRY_PATHS = {
    "testinfra/catalog.v1.yaml",
    "testinfra/fixtures.v1.yaml",
    "testinfra/leaf/prepare_active_trace.py",
}
ORIGINAL_AUDIT_SOURCE_PATHS = {
    "xdebug/Makefile",
    "xdebug/src/engine/service/actions/stream/stream_export.cpp",
    "xdebug/src/engine/service/actions/stream/stream_query.cpp",
    "xdebug/src/engine/service/actions/stream/stream_validate.cpp",
    "xdebug/src/waveform/cache/analysis_probe.cpp",
    "xdebug/src/waveform/cache/analysis_probe.h",
    "xdebug/src/waveform/cache/analysis_repository.cpp",
    "xdebug/src/waveform/stream/stream_analyzer.cpp",
    "xdebug/src/waveform/stream/stream_analyzer.h",
}
CURRENT_GENERATOR_MARKERS = (
    "testdata/fixtures",
    "fixture_build_env",
    "regenerate_",
)
ORIGINAL_CONSUMER_MARKERS = (
    ".fsdb",
    "active_trace",
    "testdata/",
    "waveform",
    "xverif_fixture",
)
ORIGINAL_TRANSITIVE_CONSUMERS = {
    "xdebug/tests/combined/run_active_driver_fixture.py": [
        "xdebug.active_driver",
        "xdebug.interface_port_root",
    ],
    "xdebug/tests/design/run_semantics.sh": [
        "xdebug.design_p3",
        "xdebug.design_uart",
    ],
}


class InventoryError(RuntimeError):
    """Raised when the inventory cannot be frozen without ambiguity."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_record(path: Path) -> dict:
    if path.is_symlink():
        target = os.readlink(path)
        payload = ("symlink\0" + target).encode("utf-8", "surrogateescape")
        return {
            "exists": True,
            "file_type": "symlink",
            "sha256": sha256_bytes(payload),
            "size_bytes": len(payload),
            "symlink_target": target,
        }
    if not path.exists():
        return {
            "exists": False,
            "file_type": "missing",
            "sha256": None,
            "size_bytes": None,
        }
    if not path.is_file():
        return {
            "exists": True,
            "file_type": "non_file",
            "sha256": None,
            "size_bytes": None,
        }
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    return {
        "exists": True,
        "file_type": "file",
        "sha256": digest.hexdigest(),
        "size_bytes": size,
    }


def git_bytes(root: Path, *args: str) -> bytes:
    env = os.environ.copy()
    env["GIT_OPTIONAL_LOCKS"] = "0"
    env["LC_ALL"] = "C"
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", "replace").strip()
        raise InventoryError(f"read-only git {' '.join(args)} failed: {message}")
    return result.stdout


def git_text(root: Path, *args: str) -> str:
    return git_bytes(root, *args).decode("utf-8", "surrogateescape").strip()


def git_paths(root: Path) -> tuple[set[str], set[str]]:
    tracked = {
        value.decode("utf-8", "surrogateescape")
        for value in git_bytes(root, "ls-files", "-z").split(b"\0")
        if value
    }
    untracked = {
        value.decode("utf-8", "surrogateescape")
        for value in git_bytes(
            root, "ls-files", "--others", "--exclude-standard", "-z"
        ).split(b"\0")
        if value
    }
    return tracked, untracked


def parse_porcelain_z(data: bytes) -> list[dict]:
    fields = data.split(b"\0")
    entries: list[dict] = []
    index = 0
    while index < len(fields):
        field = fields[index]
        index += 1
        if not field:
            continue
        text = field.decode("utf-8", "surrogateescape")
        if len(text) < 4 or text[2] != " ":
            raise InventoryError(f"unexpected git porcelain record: {text!r}")
        status = text[:2]
        entry = {
            "index_status": status[0],
            "worktree_status": status[1],
            "path": text[3:],
        }
        if status[0] in "RC" or status[1] in "RC":
            if index >= len(fields) or not fields[index]:
                raise InventoryError("rename/copy status is missing its source path")
            entry["source_path"] = fields[index].decode(
                "utf-8", "surrogateescape"
            )
            index += 1
        entries.append(entry)
    return entries


def git_status(root: Path) -> list[dict]:
    raw = git_bytes(
        root,
        "status",
        "--porcelain=v1",
        "-z",
        "--untracked-files=all",
    )
    return parse_porcelain_z(raw)


def external_snapshot(name: str, root: Path) -> dict:
    entries = git_status(root)
    enriched: list[dict] = []
    for entry in entries:
        item = dict(entry)
        item.update(file_record(root / entry["path"]))
        enriched.append(item)
    return {
        "repository": name,
        "head": git_text(root, "rev-parse", "HEAD"),
        "branch": git_text(root, "symbolic-ref", "--quiet", "--short", "HEAD"),
        "status_entries": enriched,
        "status_entry_count": len(enriched),
    }


def parse_inline_list(value: str) -> list[str]:
    value = value.strip()
    if not value.startswith("[") or not value.endswith("]"):
        return [value] if value else []
    body = value[1:-1].strip()
    if not body:
        return []
    return [item.strip().strip('"\'') for item in body.split(",")]


def parse_fixture_registry(text: str) -> list[dict]:
    blocks = re.finditer(
        r"^  - id: (?P<id>xdebug\.[^\s]+)\n(?P<body>.*?)(?=^  - id: |\Z)",
        text,
        flags=re.MULTILINE | re.DOTALL,
    )
    fixtures: list[dict] = []
    output_pattern = re.compile(
        r"^      - \{name: (?P<name>[^,]+), path: (?P<path>[^,]+), "
        r"kind: (?P<kind>[^,}]+)(?:, min_bytes: (?P<min_bytes>[^,}]+))?\}",
        flags=re.MULTILINE,
    )
    for match in blocks:
        body = match.group("body")
        source_match = re.search(r"^    source_dir: (.+)$", body, re.MULTILINE)
        capability_match = re.search(
            r"^    build_capabilities: (.+)$", body, re.MULTILINE
        )
        inputs_match = re.search(r"^    inputs: (.+)$", body, re.MULTILINE)
        argv_match = re.search(r"^      argv: (.+)$", body, re.MULTILINE)
        probe_argv = [
            parse_inline_list(value)
            for value in re.findall(r"^      - argv: (.+)$", body, re.MULTILINE)
        ]
        outputs = []
        for output in output_pattern.finditer(body):
            record = {
                "name": output.group("name").strip(),
                "path": output.group("path").strip().strip('"\''),
                "kind": output.group("kind").strip(),
            }
            if output.group("min_bytes") is not None:
                record["min_bytes"] = int(output.group("min_bytes"))
            outputs.append(record)
        fixtures.append({
            "id": match.group("id"),
            "source_dir": (
                source_match.group(1).strip().strip('"\'')
                if source_match else None
            ),
            "build_capabilities": (
                parse_inline_list(capability_match.group(1))
                if capability_match else []
            ),
            "inputs": (
                parse_inline_list(inputs_match.group(1)) if inputs_match else []
            ),
            "builder_argv": (
                parse_inline_list(argv_match.group(1)) if argv_match else []
            ),
            "probe_argv": probe_argv,
            "outputs": outputs,
        })
    return fixtures


def fixture_ids_for_path(path: str, fixtures: Iterable[dict]) -> list[str]:
    matches: list[tuple[int, str]] = []
    for fixture in fixtures:
        source_dir = fixture.get("source_dir")
        if not source_dir or source_dir == "xdebug":
            continue
        if path == source_dir or path.startswith(source_dir + "/"):
            matches.append((len(source_dir), fixture["id"]))
    if not matches:
        return []
    longest = max(length for length, _ in matches)
    return sorted(fixture_id for length, fixture_id in matches if length == longest)


def fixture_ids_referenced_by_consumer(
    text: str, fixtures: Iterable[dict],
) -> list[str]:
    """Resolve fixture references from IDs or constructed source paths.

    Original runners commonly spell a fixture source as
    ``os.path.join(ROOT, "testdata", "waveform", "name")`` instead of
    embedding its registry ID.  Matching the final source components on one
    line preserves that relationship without treating a generic ``out`` or
    ``waves.fsdb`` token as fixture evidence.
    """
    fixture_list = list(fixtures)
    known_ids = {fixture["id"] for fixture in fixture_list}
    result = set()
    imported_fixture_symbols = {
        "NONAXI_FSDB": "xdebug.ai_complex_wave",
    }
    for marker, fixture_id in imported_fixture_symbols.items():
        if marker in text and fixture_id in known_ids:
            result.add(fixture_id)
    for fixture in fixture_list:
        fixture_id = fixture["id"]
        if fixture_id in text:
            result.add(fixture_id)
            continue
        source_dir = fixture.get("source_dir", "")
        if not source_dir or source_dir == "xdebug":
            continue
        relative = source_dir.removeprefix("xdebug/")
        components = Path(relative).parts[-3:]
        if len(components) < 2:
            continue
        pattern = r"[^\n]{0,160}".join(
            re.escape(component) for component in components
        )
        if re.search(pattern, text):
            result.add(fixture_id)
    return sorted(result)


def asset_kind(path: str, roles: Iterable[str]) -> str:
    name = Path(path).name
    suffix = Path(path).suffix.lower()
    if suffix in HDL_SUFFIXES:
        return "rtl"
    if suffix == ".fst":
        return "waveform_fst"
    if suffix == ".fsdb":
        return "waveform_fsdb"
    if suffix == ".vcd":
        return "waveform_vcd"
    if suffix == ".vpd":
        return "waveform_vpd"
    if name.startswith("tb_") and suffix in {".c", ".cc", ".cpp"}:
        return "testbench"
    if name == "Makefile" or suffix in {".f", ".svi", ".tcl"}:
        return "build_or_stimulus"
    if name == "xdebug-design-db.json" or suffix in {".xddb", ".so", ".a"}:
        return "design_artifact"
    if "test_consumer" in roles:
        return "test_consumer"
    if "generator" in roles:
        return "generator"
    if "registry" in roles:
        return "registry"
    return "fixture_support"


def text_contains(path: Path, markers: Iterable[str]) -> bool:
    if path.suffix.lower() not in TEXT_SUFFIXES and path.name != "Makefile":
        return False
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    return any(marker in text for marker in markers)


def git_state(path: str, tracked: set[str], untracked: set[str], status: dict[str, dict]) -> str:
    if path in status:
        code = status[path]["index_status"] + status[path]["worktree_status"]
        return "untracked" if code == "??" else f"tracked_dirty:{code}"
    if path in tracked:
        return "tracked_clean"
    if path in untracked:
        return "untracked"
    return "unknown"


def make_asset(
    *,
    side: str,
    root: Path,
    path: str,
    roles: Iterable[str],
    fixture_ids: Iterable[str],
    state: str | None = None,
) -> dict:
    item = {
        "side": side,
        "path": path,
        "kind": asset_kind(path, roles),
        "roles": sorted(set(roles)),
        "fixture_ids": sorted(set(fixture_ids)),
    }
    item.update(file_record(root / path))
    if state is not None:
        item["git_state"] = state
    return item


def discover_original_assets(root: Path, fixtures: list[dict]) -> list[dict]:
    tracked, untracked = git_paths(root)
    status_entries = git_status(root)
    status = {entry["path"]: entry for entry in status_entries}
    all_paths = sorted(tracked | untracked)
    fixture_ids = {fixture["id"] for fixture in fixtures}
    assets: list[dict] = []
    for path in all_paths:
        roles: list[str] = []
        matched_fixture_ids = fixture_ids_for_path(path, fixtures)
        if matched_fixture_ids and path.startswith(ORIGINAL_TEST_PREFIXES):
            roles.append("fixture_source")
        if path in ORIGINAL_REGISTRY_PATHS:
            roles.append("registry")
        if path in ORIGINAL_AUDIT_SOURCE_PATHS:
            roles.append("audit_source")
        if path.startswith("xdebug/tests/"):
            if path.startswith("xdebug/tests/active_trace_chain/"):
                roles.append("test_consumer")
            elif path in ORIGINAL_TRANSITIVE_CONSUMERS:
                roles.append("test_consumer")
            elif text_contains(root / path, (*ORIGINAL_CONSUMER_MARKERS, *fixture_ids)):
                roles.append("test_consumer")
        if path == "testinfra/leaf/prepare_active_trace.py":
            roles.append("generator")
        if not roles:
            continue
        if not matched_fixture_ids and "fixture_source" in roles:
            matched_fixture_ids = ["original.unassigned"]
        if not matched_fixture_ids and "registry" in roles:
            matched_fixture_ids = ["original.registry"]
        if not matched_fixture_ids and "generator" in roles:
            matched_fixture_ids = ["original.cross_fixture"]
        if "test_consumer" in roles and not matched_fixture_ids:
            if path in ORIGINAL_TRANSITIVE_CONSUMERS:
                matched_fixture_ids = ORIGINAL_TRANSITIVE_CONSUMERS[path]
            else:
                referenced = []
                try:
                    text = (root / path).read_text(encoding="utf-8", errors="replace")
                    referenced = fixture_ids_referenced_by_consumer(text, fixtures)
                except OSError:
                    pass
                matched_fixture_ids = referenced or ["original.cross_fixture"]
        assets.append(make_asset(
            side="original",
            root=root,
            path=path,
            roles=roles,
            fixture_ids=matched_fixture_ids,
            state=git_state(path, tracked, untracked, status),
        ))
    return assets


def current_fixture_id(path: str) -> str:
    relative = path[len(CURRENT_ASSET_PREFIX):]
    if "/" not in relative:
        return "current.root"
    return "current." + relative.split("/", 1)[0]


def discover_current_assets(root: Path) -> list[dict]:
    tracked, untracked = git_paths(root)
    assets: list[dict] = []
    for path in sorted(tracked | untracked):
        roles: list[str] = []
        fixture_ids: list[str] = []
        if path.startswith(CURRENT_ASSET_PREFIX):
            roles.append("fixture_asset")
            fixture_ids.append(current_fixture_id(path))
        elif path.startswith("tests/") and Path(path).suffix.lower() in TEXT_SUFFIXES:
            roles.append("test_consumer")
            fixture_ids.append("current.cross_fixture")
        elif path == "CMakeLists.txt":
            roles.append("build_contract")
            fixture_ids.append("current.cross_fixture")
        elif path.startswith("tools/") and text_contains(
            root / path, CURRENT_GENERATOR_MARKERS
        ):
            roles.append("generator")
            fixture_ids.append("current.cross_fixture")
        if not roles:
            continue
        assets.append(make_asset(
            side="current",
            root=root,
            path=path,
            roles=roles,
            fixture_ids=fixture_ids,
        ))
    return assets


def count_kind(assets: Iterable[dict], kind: str) -> int:
    return sum(1 for asset in assets if asset["kind"] == kind)


def original_content_index(assets: Iterable[dict]) -> dict[str, tuple]:
    return {
        asset["path"]: (
            asset["exists"],
            asset["file_type"],
            asset["sha256"],
            asset["size_bytes"],
        )
        for asset in assets
        if asset["side"] == "original"
    }


def verify_frozen_original_content(
    root: Path,
    live_assets: list[dict],
    frozen_manifest: dict,
) -> None:
    live = original_content_index(live_assets)
    frozen = original_content_index(frozen_manifest.get("assets", []))
    changed = sorted(
        path for path in frozen
        if live.get(path) != frozen[path]
    )
    additions = sorted(set(live) - set(frozen))
    preview = ", ".join(changed[:8])
    if len(changed) > 8:
        preview += f", ... ({len(changed)} total)"
    if changed:
        raise InventoryError(
            "frozen original test asset content drifted; baseline replacement is forbidden: "
            + preview
        )
    goal_start = frozen_manifest.get(
        "external_read_only_goal_start_snapshot",
        frozen_manifest.get("external_read_only_snapshot", []),
    )
    original_snapshot = next(
        (
            item for item in goal_start
            if item.get("repository") == "original_xverif"
        ),
        {},
    )
    goal_start_status = {
        item["path"]: item
        for item in original_snapshot.get("status_entries", [])
    }
    for path in additions:
        if (
            path not in ORIGINAL_TRANSITIVE_CONSUMERS
            and path not in ORIGINAL_AUDIT_SOURCE_PATHS
        ):
            raise InventoryError(
                "new original asset is not an explicitly reviewed transitive consumer: "
                + path
            )
        status_record = goal_start_status.get(path)
        if status_record is not None:
            baseline_record = (
                status_record["exists"],
                status_record["file_type"],
                status_record["sha256"],
                status_record["size_bytes"],
            )
        else:
            baseline = git_bytes(
                root, "show", f"{ORIGINAL_BASELINE_HEAD}:{path}"
            )
            baseline_record = (
                True,
                "file",
                sha256_bytes(baseline),
                len(baseline),
            )
        if live[path] != baseline_record:
            raise InventoryError(
                "new original evidence does not match the Goal-start snapshot: "
                + path
            )


def relative_output_records(fixtures: list[dict]) -> list[dict]:
    records: dict[tuple[str, str], dict] = {}
    for fixture in fixtures:
        for output in fixture["outputs"]:
            suffix = Path(output["path"]).suffix.lower()
            if suffix in WAVE_SUFFIXES:
                records[(fixture["id"], output["path"])] = {
                    "fixture_id": fixture["id"],
                    **output,
                    "declarations": ["output"],
                }
        for probe in fixture["probe_argv"]:
            for index, argument in enumerate(probe[:-1]):
                if argument not in {"--fsdb-glob", "--fst-glob", "--vcd-glob"}:
                    continue
                path = probe[index + 1]
                key = (fixture["id"], path)
                if key in records:
                    records[key]["declarations"].append("probe")
                    continue
                records[key] = {
                    "fixture_id": fixture["id"],
                    "name": argument[2:].replace("-glob", ""),
                    "path": path,
                    "kind": "probe_glob",
                    "declarations": ["probe"],
                }
    return sorted(records.values(), key=lambda item: (item["fixture_id"], item["path"]))


def load_dependencies(repo_root: Path) -> dict:
    path = repo_root / "dependencies.lock.json"
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise InventoryError(f"cannot load dependencies.lock.json: {exc}") from exc


def build_manifest(
    repo_root: Path,
    original_root: Path,
    wellen_root: Path,
    verilator_root: Path,
    frozen_manifest: dict | None = None,
) -> dict:
    registry_path = original_root / "testinfra/fixtures.v1.yaml"
    catalog_path = original_root / "testinfra/catalog.v1.yaml"
    try:
        registry_text = registry_path.read_text(encoding="utf-8")
        catalog_text = catalog_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise InventoryError(f"cannot read original fixture registry: {exc}") from exc
    fixtures = parse_fixture_registry(registry_text)
    if not fixtures:
        raise InventoryError("original fixture registry contains no xdebug fixtures")
    for fixture in fixtures:
        fixture["catalog_reference_lines"] = [
            line_number
            for line_number, line in enumerate(catalog_text.splitlines(), start=1)
            if fixture["id"] in line
        ]

    original_assets = discover_original_assets(original_root, fixtures)
    current_assets = discover_current_assets(repo_root)
    dependencies = load_dependencies(repo_root)
    original_hdl = [asset for asset in original_assets if asset["kind"] == "rtl"]
    current_rtl = [asset for asset in current_assets if asset["kind"] == "rtl"]
    unassigned_original_hdl = [
        asset["path"] for asset in original_hdl
        if asset["fixture_ids"] == ["original.unassigned"]
    ]
    missing_assets = [
        {"side": asset["side"], "path": asset["path"]}
        for asset in (*original_assets, *current_assets)
        if not asset["exists"]
    ]
    external = [
        external_snapshot("original_xverif", original_root),
        external_snapshot("wellen", wellen_root),
        external_snapshot("verilator", verilator_root),
    ]
    if frozen_manifest is None and external[0]["head"] != ORIGINAL_BASELINE_HEAD:
        raise InventoryError(
            "original xverif HEAD drifted from the Goal baseline: "
            f"{external[0]['head']}"
        )
    if frozen_manifest is not None:
        verify_frozen_original_content(
            original_root, original_assets, frozen_manifest
        )
        goal_start_external = frozen_manifest.get(
            "external_read_only_goal_start_snapshot",
            frozen_manifest.get("external_read_only_snapshot"),
        )
        if not goal_start_external:
            raise InventoryError("frozen manifest lacks its goal-start external snapshot")
    else:
        goal_start_external = external

    return {
        "schema_version": SCHEMA_VERSION,
        "goal_id": GOAL_ID,
        "write_boundary": {
            "only_writable_repository": "xdebug_fst",
            "external_repositories_read_only": [
                "original_xverif", "wellen", "verilator",
            ],
        },
        "baselines": {
            "current": {
                "branch": "fix/per-session-registry-flock",
                "head": CURRENT_BASELINE_HEAD,
            },
            "original_assets": {"head": ORIGINAL_BASELINE_HEAD},
            "original_runtime": dependencies["original_xdebug"],
            "initial_counts": {
                "original_hdl": 103,
                "current_rtl": 11,
                "current_fst": 12,
                "current_vcd": 1,
            },
        },
        "discovery_contract": {
            "original_fixture_sources": "longest xdebug fixture source_dir prefix",
            "original_untracked_policy": "include non-ignored untracked fixture files",
            "original_consumers": list(ORIGINAL_CONSUMER_MARKERS),
            "original_transitive_consumers": ORIGINAL_TRANSITIVE_CONSUMERS,
            "original_audit_sources": sorted(ORIGINAL_AUDIT_SOURCE_PATHS),
            "current_fixture_prefix": CURRENT_ASSET_PREFIX,
            "current_consumers": "all text/source files below tests",
            "generated_or_ignored_outputs": "excluded unless tracked",
        },
        "summary": {
            "original": {
                "asset_count": len(original_assets),
                "fixture_count": len(fixtures),
                "rtl_count": len(original_hdl),
                "declared_waveform_output_count": len(relative_output_records(fixtures)),
                "test_consumer_count": sum(
                    "test_consumer" in asset["roles"] for asset in original_assets
                ),
            },
            "current": {
                "asset_count": len(current_assets),
                "rtl_count": len(current_rtl),
                "fst_count": count_kind(current_assets, "waveform_fst"),
                "vcd_count": count_kind(current_assets, "waveform_vcd"),
                "test_consumer_count": sum(
                    "test_consumer" in asset["roles"] for asset in current_assets
                ),
            },
            "unassigned_original_hdl": sorted(unassigned_original_hdl),
            "missing_assets": sorted(
                missing_assets, key=lambda item: (item["side"], item["path"])
            ),
        },
        "external_read_only_goal_start_snapshot": goal_start_external,
        "external_read_only_audit_snapshot": external,
        "original_fixtures": fixtures,
        "original_declared_waveform_outputs": relative_output_records(fixtures),
        "assets": sorted(
            [*original_assets, *current_assets],
            key=lambda item: (item["side"], item["path"]),
        ),
    }


def canonical_json(data: dict) -> str:
    return json.dumps(data, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def ensure_output_within_repo(repo_root: Path, output: Path) -> Path:
    resolved_root = repo_root.resolve()
    resolved_output = output.resolve(strict=False)
    try:
        resolved_output.relative_to(resolved_root)
    except ValueError as exc:
        raise InventoryError(
            f"output escapes the only writable repository: {resolved_output}"
        ) from exc
    return resolved_output


def write_atomic(output: Path, text: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=output.parent,
            prefix=output.name + ".",
            suffix=".tmp",
            delete=False,
        ) as stream:
            stream.write(text)
            temporary = Path(stream.name)
        temporary.replace(output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root)
    parser.add_argument(
        "--original-root",
        type=Path,
        default=Path(os.environ["XDEBUG_ORIGINAL_ROOT"])
        if "XDEBUG_ORIGINAL_ROOT" in os.environ else None,
    )
    parser.add_argument(
        "--wellen-root",
        type=Path,
        default=Path(os.environ["WELLEN_HOME"])
        if "WELLEN_HOME" in os.environ else None,
    )
    parser.add_argument(
        "--verilator-root",
        type=Path,
        default=Path(os.environ["VERILATOR_HOME"])
        if "VERILATOR_HOME" in os.environ else None,
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "compat/xdebug-v1/rtl-wave-assets.manifest.json",
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    parser.add_argument(
        "--accept-audited-external-drift",
        action="store_true",
        help=(
            "update only the latest external audit snapshot after independently "
            "confirming that frozen original asset content did not change"
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.original_root is None or args.wellen_root is None or args.verilator_root is None:
        print(
            "ERROR: XDEBUG_ORIGINAL_ROOT, WELLEN_HOME and VERILATOR_HOME are required",
            file=sys.stderr,
        )
        return 2
    try:
        repo_root = args.repo_root.resolve()
        output = ensure_output_within_repo(repo_root, args.output)
        if args.accept_audited_external_drift and not args.write:
            raise InventoryError(
                "--accept-audited-external-drift is valid only together with --write"
            )
        frozen_manifest = None
        if output.is_file():
            frozen_manifest = json.loads(output.read_text(encoding="utf-8"))
        manifest = build_manifest(
            repo_root,
            args.original_root.resolve(),
            args.wellen_root.resolve(),
            args.verilator_root.resolve(),
            frozen_manifest,
        )
        rendered = canonical_json(manifest)
        if args.check:
            if not output.is_file():
                raise InventoryError(f"frozen manifest is missing: {output}")
            if output.read_text(encoding="utf-8") != rendered:
                raise InventoryError(
                    "frozen manifest differs from the live read-only inventory; "
                    "run --write only after reviewing the drift"
                )
        else:
            if frozen_manifest is not None:
                previous_audit = frozen_manifest.get(
                    "external_read_only_audit_snapshot",
                    frozen_manifest.get("external_read_only_snapshot"),
                )
                if (
                    previous_audit != manifest["external_read_only_audit_snapshot"]
                    and not args.accept_audited_external_drift
                ):
                    raise InventoryError(
                        "external repository state drifted; inspect it and rerun with "
                        "--accept-audited-external-drift only when frozen original "
                        "asset hashes are unchanged"
                    )
            write_atomic(output, rendered)
    except (InventoryError, OSError, KeyError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    summary = manifest["summary"]
    print(
        "rtl/wave asset manifest: OK; "
        f"original_rtl={summary['original']['rtl_count']} "
        f"current_rtl={summary['current']['rtl_count']} "
        f"current_fst={summary['current']['fst_count']} "
        f"current_vcd={summary['current']['vcd_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
