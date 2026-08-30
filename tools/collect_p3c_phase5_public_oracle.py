#!/usr/bin/env python3
"""Collect sanitized Phase5 public-Action responses from the locked runtime."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
from typing import Any

from collect_p3c_original_oracles import (
    ROOT,
    inside,
    load_catalog,
    rtl_mirrors,
    sanitize,
    sha256,
    strings,
    write_atomic,
)


RUNTIME_REVISION = "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
SCHEMA_REVISION = (
    "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
)
BUILD_ID = f"{RUNTIME_REVISION[:12]}-{SCHEMA_REVISION}"


def executable_sha256(wrapper: Path, executable: Path) -> dict[str, str]:
    return {
        "wrapper_sha256": sha256(wrapper),
        "binary_sha256": sha256(executable),
    }


def invoke(wrapper: Path, request: dict[str, Any], environment: dict[str, str],
           work_dir: Path) -> dict[str, Any]:
    process = subprocess.run(
        [str(wrapper), "--json", "-"],
        input=json.dumps(request, ensure_ascii=False) + "\n",
        cwd=work_dir,
        env=environment,
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(
            f"locked xdebug 请求失败: {request.get('action')}\n"
            f"{process.stdout}{process.stderr}"
        )
    start = process.stdout.find("{")
    if start < 0:
        raise RuntimeError(
            f"locked xdebug 未返回 JSON: {request.get('action')}"
        )
    response, _ = json.JSONDecoder().raw_decode(process.stdout[start:])
    if not isinstance(response, dict):
        raise RuntimeError("locked xdebug response 不是对象")
    return response


def validate_identity(response: dict[str, Any]) -> None:
    tool = response.get("tool", {})
    if (
        tool.get("build_id") != BUILD_ID
        or tool.get("git_revision") != RUNTIME_REVISION[:12]
        or tool.get("schema_revision") != SCHEMA_REVISION
    ):
        raise RuntimeError(f"locked runtime identity 漂移: {tool}")


def observable_response(response: dict[str, Any], original_root: Path,
                        resources: Path) -> dict[str, Any]:
    if response.get("ok") is not True:
        raise RuntimeError(f"public Action 失败: {response.get('error')}")
    if response.get("action") != "trace.active_driver_chain":
        raise RuntimeError("public Action identity 漂移")
    validate_identity(response)
    summary = response.get("summary", {})
    data = response.get("data", {})
    if (
        summary.get("scan_complete") is not True
        or summary.get("analysis_complete") is not True
        or summary.get("response_truncated") is not False
        or summary.get("truncation_scopes") != []
        or not isinstance(data, dict)
    ):
        raise RuntimeError("Phase5 public response 不完整")
    return sanitize(
        {"summary": summary, "data": data}, original_root, resources
    )


def runtime_environment(work_dir: Path) -> dict[str, str]:
    environment = dict(os.environ)
    for name, leaf in (
        ("HOME", "home"),
        ("TMPDIR", "tmp"),
        ("XDG_CACHE_HOME", "cache"),
        ("XVERIF_TEST_TMPDIR", "socket"),
    ):
        target = inside(work_dir / leaf, ROOT, name)
        target.mkdir(parents=True, exist_ok=True)
        environment[name] = str(target)
    if not environment.get("VERDI_HOME"):
        raise RuntimeError("VERDI_HOME 未设置；禁止替换 NPI 工具链")
    return environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xdebug-wrapper", type=Path, required=True)
    parser.add_argument("--xdebug-binary", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--original-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--fixture-version", required=True)
    parser.add_argument("--npi-version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    wrapper = inside(args.xdebug_wrapper, ROOT, "locked xdebug wrapper")
    executable = inside(args.xdebug_binary, ROOT, "locked xdebug binary")
    work_dir = inside(args.work_dir, ROOT, "Phase5 oracle work-dir")
    resources = args.resources.resolve()
    original_root = args.original_root.resolve()
    fsdb = resources / "cases/phase5/out/waves.fsdb"
    daidir = resources / "cases/phase5/out/simv.daidir"
    if (
        not wrapper.is_file()
        or not executable.is_file()
        or not fsdb.is_file()
        or not daidir.is_dir()
        or not original_root.is_dir()
    ):
        raise RuntimeError("locked runtime/resources/original root 不完整")
    work_dir.mkdir(parents=True, exist_ok=True)
    environment = runtime_environment(work_dir)

    actions = invoke(wrapper, {
        "api_version": "xdebug.v1",
        "action": "actions",
    }, environment, work_dir)
    validate_identity(actions)
    if (
        actions.get("ok") is not True
        or actions.get("summary", {}).get("action_count") != 73
        or len(actions.get("data", {}).get("actions", [])) != 73
    ):
        raise RuntimeError("locked runtime 73-Action identity gate 失败")

    catalog_version, groups = load_catalog()
    phase5 = groups["phase5"]
    if len(phase5) != 10:
        raise RuntimeError("Phase5 catalog 必须恰好十行")

    session_id = f"p3c_phase5_oracle_{os.getpid()}"
    opened = invoke(wrapper, {
        "api_version": "xdebug.v1",
        "action": "session.open",
        "target": {"daidir": str(daidir), "fsdb": str(fsdb)},
        "args": {"name": session_id},
    }, environment, work_dir)
    if opened.get("ok") is not True:
        raise RuntimeError(f"Phase5 session.open 失败: {opened.get('error')}")
    validate_identity(opened)
    rows = []
    closed = False
    try:
        for index, case in enumerate(phase5, 1):
            request_args = {
                "signal": case["signal"],
                "time": case["time"],
                "render_time_unit": "ns",
            }
            response = invoke(wrapper, {
                "api_version": "xdebug.v1",
                "action": "trace.active_driver_chain",
                "target": {"session_id": session_id},
                "args": request_args,
                "limits": {"max_depth": 64, "max_nodes": 64},
            }, environment, work_dir)
            rows.append({
                "scenario_id": f"active.phase5.{index:02d}",
                "catalog_index": index,
                "case": case["case"],
                "request": request_args,
                "limits": {"max_depth": 64, "max_nodes": 64},
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
                    original_root, "phase5", str(case["case"])
                ),
                "response": observable_response(
                    response, original_root, resources
                ),
            })
    finally:
        close_response = invoke(wrapper, {
            "api_version": "xdebug.v1",
            "action": "session.close",
            "target": {"session_id": session_id},
            "args": {},
        }, environment, work_dir)
        closed = close_response.get("ok") is True
    if not closed:
        raise RuntimeError("Phase5 locked session 未正常关闭")

    audit = {
        "schema_version": "xdebug.p3c-phase5-public-oracle.v1",
        "goal_id": "01a050fa-b864-7ce2-af88-56083d84ea21",
        "group": "phase5",
        "catalog": {
            "schema_version": catalog_version,
            "sha256": sha256(
                ROOT / "testdata/fixtures/active_trace/original-cases.v1.yaml"
            ),
            "row_count": len(rows),
        },
        "locked_runtime": {
            "runtime_revision": RUNTIME_REVISION,
            "schema_revision": SCHEMA_REVISION,
            "build_id": BUILD_ID,
            "action_count": 73,
            **executable_sha256(wrapper, executable),
            "npi_version": args.npi_version,
            "fixture_version": args.fixture_version,
            "cache_reused": True,
            "fixture_rebuilt": False,
            "source_access": "read_only",
        },
        "session": {
            "mode": "combined",
            "transport": "uds",
            "opened": True,
            "closed_gracefully": True,
            "all_runtime_writes_repository_local": True,
            "fallback_used": False,
        },
        "rows": rows,
    }
    leaked = [
        text for text in strings(audit)
        if text.startswith("/") or "/home/" in text
    ]
    if leaked:
        raise RuntimeError(f"Phase5 oracle 去敏失败: {leaked[:3]}")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    write_atomic(output, json.dumps(
        audit, ensure_ascii=False, indent=2, sort_keys=True
    ) + "\n")
    print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
