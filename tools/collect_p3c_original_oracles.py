#!/usr/bin/env python3
"""Run the locked native active-trace runner and print sanitized oracles."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "testdata/fixtures/active_trace/original-cases.v1.yaml"
CURRENT_RTL = ROOT / "testdata/fixtures/active_trace/rtl"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def scalar(text: str) -> Any:
    value = text.strip()
    if value.startswith('"') and value.endswith('"'):
        return json.loads(value)
    if value == "true":
        return True
    if value == "false":
        return False
    if value.isdigit():
        return int(value)
    return value


def load_catalog() -> tuple[str, dict[str, list[dict[str, Any]]]]:
    version = ""
    groups: dict[str, list[dict[str, Any]]] = {}
    current = ""
    for raw in CATALOG.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        if line.startswith("version:"):
            version = line.split(":", 1)[1].strip()
        elif line.startswith("  ") and not line.startswith("    ") \
                and line.endswith(":"):
            current = line.strip()[:-1]
            groups[current] = []
        elif line.startswith("    - {") and line.endswith("}"):
            if not current:
                raise RuntimeError("catalog case 没有所属 group")
            body = line[len("    - {"):-1]
            row: dict[str, Any] = {}
            for field in body.split(","):
                key, value = field.split(":", 1)
                row[key.strip()] = scalar(value)
            groups[current].append(row)
    if version != "xdebug-active-trace-cases.v1":
        raise RuntimeError(f"catalog version 漂移: {version}")
    if {group: len(rows) for group, rows in groups.items()} != {
        "p0": 6, "composite": 20, "timing": 12,
        "phase4": 20, "phase5": 10,
    }:
        raise RuntimeError("catalog group/cardinality 漂移")
    return version, groups


def inside(path: Path, root: Path, label: str) -> Path:
    resolved = path.resolve()
    expected = root.resolve()
    if resolved != expected and expected not in resolved.parents:
        raise RuntimeError(f"{label} 必须位于当前仓库: {resolved}")
    return resolved


def write_atomic(path: Path, content: str) -> None:
    output = inside(path, ROOT, "oracle output")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=output.parent,
            prefix=f".{output.name}.", suffix=".tmp", delete=False,
        ) as stream:
            stream.write(content)
            temporary = Path(stream.name)
        os.replace(temporary, output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def sanitize(value: Any, original_root: Path, resources: Path) -> Any:
    if isinstance(value, str):
        replacements = (
            (str(original_root.resolve()) + "/", "xdebug/"),
            (str(resources.resolve()) + "/", "locked-fixture/"),
        )
        result = value
        for source, target in replacements:
            result = result.replace(source, target)
        return result
    if isinstance(value, list):
        return [sanitize(item, original_root, resources) for item in value]
    if isinstance(value, dict):
        return {
            key: sanitize(item, original_root, resources)
            for key, item in value.items()
        }
    return value


def strings(value: Any):
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from strings(key)
            yield from strings(item)


def validate_result(case: dict[str, Any], result: dict[str, Any]) -> None:
    if result.get("truncated") is not False:
        raise RuntimeError(f"{case['case']} native response 被截断")
    if result.get("active_trace_calls") != result.get("total_hops"):
        raise RuntimeError(f"{case['case']} native call/hop 计数不一致")
    for catalog_field, result_field in (
        ("hops", "total_hops"),
        ("termination", "termination"),
        ("temporal_boundaries", "temporal_boundaries"),
    ):
        if catalog_field in case and case[catalog_field] != result.get(result_field):
            raise RuntimeError(
                f"{case['case']} catalog {catalog_field} 与锁定运行时冲突: "
                f"{case[catalog_field]!r} != {result.get(result_field)!r}"
            )


def rtl_mirrors(original_root: Path, group: str,
                case: str) -> list[dict[str, Any]]:
    active = original_root / "tests/active_trace_chain"
    if group == "p0":
        pairs = [
            (active / "p0_composability" / case / "tb.sv",
             CURRENT_RTL / group / case / "tb.sv"),
        ]
    elif group == "composite":
        pairs = [
            (active / group / case / "tb.sv",
             CURRENT_RTL / group / case / "tb.sv"),
            (active / group / "chain_dut.sv",
             CURRENT_RTL / group / "chain_dut.sv"),
        ]
    elif group == "timing":
        pairs = [
            (active / group / case / "tb.sv",
             CURRENT_RTL / group / case / "tb.sv"),
            (active / group / "timing_boundary_dut.sv",
             CURRENT_RTL / group / "timing_boundary_dut.sv"),
        ]
    elif group == "phase4":
        pairs = [
            (active / group / case / "tb.sv",
             CURRENT_RTL / group / case / "tb.sv"),
            (active / group / "phase4_dut.sv",
             CURRENT_RTL / group / "phase4_dut.sv"),
            (active / "composite/chain_dut.sv",
             CURRENT_RTL / "composite/chain_dut.sv"),
        ]
    elif group == "phase5":
        pairs = [
            (active / group / name, CURRENT_RTL / group / name)
            for name in ("dut.sv", "tb.sv")
        ]
    else:
        raise RuntimeError(f"unsupported group: {group}")
    rows = []
    for original, current in pairs:
        if not original.is_file() or not current.is_file():
            raise RuntimeError(f"缺少 RTL mirror: {original} / {current}")
        original_digest = sha256(original)
        current_digest = sha256(current)
        if original_digest != current_digest:
            raise RuntimeError(f"RTL mirror 非逐字节一致: {current}")
        rows.append({
            "original_path": "xdebug/" + original.relative_to(
                original_root).as_posix(),
            "current_path": current.relative_to(ROOT).as_posix(),
            "sha256": original_digest,
            "size": original.stat().st_size,
            "byte_identical": True,
        })
    return rows


def run_case(runner: Path, resources: Path, work_dir: Path,
             case: dict[str, Any]) -> tuple[dict[str, Any], Path]:
    output = resources / "cases" / str(case["case"]) / "out"
    fsdb = output / "waves.fsdb"
    daidir = output / "simv.daidir"
    if not fsdb.is_file() or not daidir.is_dir():
        raise RuntimeError(f"锁定 fixture 不完整: {case['case']}")
    command = [
        str(runner), "-dbdir", str(daidir), "-ssf", str(fsdb),
        "-signal", str(case["signal"]), "-time", str(case["time"]),
    ]
    if case.get("stop_on_temporal"):
        command.append("--stop-on-temporal")
    process = subprocess.run(
        command, cwd=work_dir, text=True, encoding="utf-8",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=180, check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(
            f"native runner 失败: {case['case']}\n"
            f"{process.stdout}{process.stderr}"
        )
    start = process.stdout.find("{")
    if start < 0:
        raise RuntimeError(f"native runner 未返回 JSON: {case['case']}")
    result, _ = json.JSONDecoder().raw_decode(process.stdout[start:])
    validate_result(case, result)
    return result, fsdb


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--group", required=True,
        choices=("p0", "composite", "timing", "phase4", "phase5"),
    )
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--original-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--fixture-version", required=True)
    parser.add_argument("--npi-version", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    runner = args.runner.resolve()
    resources = args.resources.resolve()
    original_root = args.original_root.resolve()
    work_dir = inside(args.work_dir, ROOT, "NPI work-dir")
    work_dir.mkdir(parents=True, exist_ok=True)
    if not runner.is_file() or not resources.is_dir() or not original_root.is_dir():
        raise SystemExit("runner/resources/original-root 不完整")
    catalog_version, groups = load_catalog()
    rows = []
    for index, case in enumerate(groups[args.group], start=1):
        result, fsdb = run_case(runner, resources, work_dir, case)
        rows.append({
            "scenario_id": f"active.{args.group}.{index:02d}",
            "catalog_index": index,
            "request": {
                "signal": case["signal"],
                "time": case["time"],
                "stop_on_temporal": bool(case.get("stop_on_temporal", False)),
            },
            "case": case["case"],
            "catalog_expectation": {
                key: case[key] for key in (
                    "hops", "termination", "temporal_boundaries"
                ) if key in case
            },
            "fixture": {
                "fsdb_sha256": sha256(fsdb),
                "fsdb_size": fsdb.stat().st_size,
            },
            "rtl_mirrors": rtl_mirrors(
                original_root, args.group, str(case["case"])
            ),
            "native_result": sanitize(result, original_root, resources),
        })
    audit = {
        "schema_version": "xdebug.p3c-original-active-trace-oracle.v1",
        "goal_id": "01a050fa-b864-7ce2-af88-56083d84ea21",
        "group": args.group,
        "catalog": {
            "schema_version": catalog_version,
            "sha256": sha256(CATALOG),
            "row_count": len(rows),
        },
        "locked_runtime": {
            "runner_sha256": sha256(runner),
            "npi_version": args.npi_version,
            "fixture_version": args.fixture_version,
            "cache_reused": True,
            "fixture_rebuilt": False,
            "source_access": "read_only",
        },
        "session": {
            "mode": "native_chain_test",
            "all_runtime_writes_repository_local": True,
            "fallback_used": False,
        },
        "rows": rows,
    }
    leaked = [text for text in strings(audit) if text.startswith("/") or "/home/" in text]
    if leaked:
        raise RuntimeError(f"去敏失败: {leaked[:3]}")
    rendered = json.dumps(
        audit, indent=2, ensure_ascii=False, sort_keys=True
    ) + "\n"
    if args.output is None:
        print(rendered, end="")
    else:
        output = args.output if args.output.is_absolute() else ROOT / args.output
        write_atomic(output, rendered)
        print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
