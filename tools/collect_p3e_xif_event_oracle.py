#!/usr/bin/env python3
"""从冻结原版 runtime/cache 采集 XIF event 公开 Action oracle。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from collect_p3c_original_oracles import (  # noqa: E402
    ROOT,
    inside,
    sanitize,
    sha256,
    strings,
    write_atomic,
)
from collect_p3c_phase5_public_oracle import (  # noqa: E402
    BUILD_ID,
    RUNTIME_REVISION,
    SCHEMA_REVISION,
    executable_sha256,
    invoke,
    runtime_environment,
    validate_identity,
)
from collect_p3d_stream_v1_export_oracle import invoke_xout  # noqa: E402


GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
CONFIG_NAMES = (
    "rdy",
    "bp",
    "none",
    "pair_master",
    "pair_slave",
    "xz",
)
DIRECT_CASES = (
    ("rdy", "vld && rdy", "5a", "3", "2", "a55a"),
    ("rdy", "vld && rdy", "10", "1", "1", "1000"),
    ("bp", "vld && !bp", "b2", "2", "2", "2002"),
    ("bp", "vld && !bp", "b3", "3", "3", "2003"),
    ("none", "vld", "c0", "0", "1", "3000"),
    ("none", "vld", "c2", "2", "3", "3002"),
    ("pair_master", "vld && rdy", "d0", "0", "0", "4000"),
    ("pair_master", "vld && rdy", "d2", "2", "2", "4002"),
    ("pair_slave", "vld && rdy", "d0", "0", "0", "4000"),
    ("pair_slave", "vld && rdy", "d2", "2", "2", "4002"),
)


def request(action: str, args: dict[str, Any], target: dict[str, str]) -> dict:
    return {
        "api_version": "xdebug.v1",
        "action": action,
        "target": target,
        "args": args,
    }


def invoke_expected_error(
    wrapper: Path,
    envelope: dict[str, Any],
    environment: dict[str, str],
    work_dir: Path,
) -> dict[str, Any]:
    process = subprocess.run(
        [str(wrapper), "--json", "-"],
        input=json.dumps(envelope, ensure_ascii=False) + "\n",
        cwd=work_dir,
        env=environment,
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    start = process.stdout.find("{")
    if process.returncode == 0 or start < 0:
        raise RuntimeError(
            f"XIF expected-error 请求退出状态漂移: {process.returncode}: "
            f"{process.stdout}{process.stderr}"
        )
    response, _ = json.JSONDecoder().raw_decode(process.stdout[start:])
    if not isinstance(response, dict):
        raise RuntimeError("XIF expected-error response 不是对象")
    return response


def public_response(
    response: dict[str, Any],
    *,
    action: str,
    expected_ok: bool,
    expected_response_truncated: bool,
    expected_truncation_scopes: list[str],
    original_root: Path,
    resources: Path,
) -> dict[str, Any]:
    validate_identity(response)
    if response.get("action") != action or response.get("ok") is not expected_ok:
        raise RuntimeError(f"XIF {action} response identity/status 漂移: {response}")
    result = {
        "ok": response["ok"],
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if not expected_ok:
        result["error"] = response.get("error", {})
    if expected_ok and action in {"event.find", "event.export"}:
        summary = result["summary"]
        if (
            summary.get("scan_complete") is not True
            or summary.get("analysis_complete") is not True
            or summary.get("response_truncated") is not expected_response_truncated
            or summary.get("truncation_scopes") != expected_truncation_scopes
        ):
            raise RuntimeError(f"XIF {action} response 不完整: {summary}")
    return sanitize(result, original_root, resources)


def replace_exact_path(value: Any, source: Path, replacement: str) -> Any:
    if isinstance(value, str):
        return replacement if value == str(source.resolve()) else value
    if isinstance(value, list):
        return [replace_exact_path(item, source, replacement) for item in value]
    if isinstance(value, dict):
        return {
            key: replace_exact_path(item, source, replacement)
            for key, item in value.items()
        }
    return value


def add_observation(
    rows: list[dict[str, Any]],
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
    target: dict[str, str],
    original_root: Path,
    resources: Path,
    observation_id: str,
    action: str,
    args: dict[str, Any],
    *,
    expected_ok: bool = True,
    with_xout: bool = False,
    expected_response_truncated: bool = False,
    expected_truncation_scopes: list[str] | None = None,
    artifact_path: Path | None = None,
) -> None:
    envelope = request(action, args, target)
    response = (
        invoke(wrapper, envelope, environment, work_dir)
        if expected_ok
        else invoke_expected_error(wrapper, envelope, environment, work_dir)
    )
    public_request = sanitize(args, original_root, resources)
    public_result = public_response(
        response,
        action=action,
        expected_ok=expected_ok,
        expected_response_truncated=expected_response_truncated,
        expected_truncation_scopes=expected_truncation_scopes or [],
        original_root=original_root,
        resources=resources,
    )
    if artifact_path is not None:
        artifact_label = f"<artifact>/{artifact_path.name}"
        public_request = replace_exact_path(public_request, artifact_path, artifact_label)
        public_result = replace_exact_path(public_result, artifact_path, artifact_label)
    row = {
        "observation_id": observation_id,
        "action": action,
        "request": public_request,
        "response": public_result,
    }
    if with_xout:
        xout = invoke_xout(wrapper, envelope, environment, work_dir)
        if "/home/" in xout:
            raise RuntimeError("XIF XOUT 泄漏绝对路径")
        row["xout"] = xout
        row["xout_sha256"] = hashlib.sha256(xout.encode()).hexdigest()
    if artifact_path is not None:
        if not artifact_path.is_file():
            raise RuntimeError(f"XIF export artifact 缺失: {artifact_path}")
        artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
        row["artifact"] = sanitize(artifact, original_root, resources)
        row["artifact_sha256"] = sha256(artifact_path)
    rows.append(row)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xdebug-wrapper", type=Path, required=True)
    parser.add_argument("--xdebug-binary", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--fixture-version", required=True)
    parser.add_argument("--original-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--npi-version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    wrapper = inside(args.xdebug_wrapper, ROOT, "XIF locked wrapper")
    executable = inside(args.xdebug_binary, ROOT, "XIF locked binary")
    work_dir = inside(args.work_dir, ROOT, "XIF oracle work-dir")
    output = inside(args.output, ROOT, "XIF oracle output")
    resources = args.resources.resolve()
    original_root = args.original_root.resolve()
    fsdb = resources / "out/waves/xif_event_multi_if_test.fsdb"
    cache_manifest = resources.parent / "manifest.json"
    if not all((wrapper.is_file(), executable.is_file(), fsdb.is_file(), cache_manifest.is_file())):
        raise RuntimeError("XIF locked runtime/cache chain 不完整")
    manifest = json.loads(cache_manifest.read_text(encoding="utf-8"))
    if (
        manifest.get("schema_version") != "xverif-fixture-manifest.v1"
        or manifest.get("fixture_id") != "xdebug.xif_event"
        or manifest.get("fingerprint") != args.fixture_version.split("-prepare-", 1)[0]
    ):
        raise RuntimeError("XIF cache manifest/version 漂移")

    work_dir.mkdir(parents=True, exist_ok=True)
    environment = runtime_environment(work_dir)
    actions = invoke(wrapper, {
        "api_version": "xdebug.v1",
        "action": "actions",
    }, environment, work_dir)
    validate_identity(actions)
    if actions.get("summary", {}).get("action_count") != 73:
        raise RuntimeError("XIF locked runtime 非 73 Action")

    session_id = f"p3e_xif_{os.getpid()}"
    opened = invoke(wrapper, request(
        "session.open", {"name": session_id}, {"fsdb": str(fsdb)}
    ), environment, work_dir)
    validate_identity(opened)
    if opened.get("ok") is not True:
        raise RuntimeError(f"XIF session.open 失败: {opened}")
    target = {"session_id": session_id}
    rows: list[dict[str, Any]] = []
    closed = False
    try:
        config_root = original_root / "xdebug/testdata/waveform/xif_agent_event"
        for name in CONFIG_NAMES:
            add_observation(
                rows, wrapper, environment, work_dir, target,
                original_root, resources, f"config.{name}",
                "event.config.load", {
                    "name": name,
                    "config_path": str(config_root / f"event_{name}.json"),
                },
            )

        for name, expr in (
            ("rdy", "vld && rdy"),
            ("bp", "vld && !bp"),
            ("none", "vld"),
            ("pair_master", "vld && rdy"),
            ("pair_slave", "vld && rdy"),
            ("xz", "vld"),
        ):
            add_observation(
                rows, wrapper, environment, work_dir, target,
                original_root, resources, f"full.{name}",
                "event.export", {"name": name, "expr": expr},
            )

        for index, (name, prefix, opcode, channel, item_id, data) in enumerate(DIRECT_CASES, 1):
            expr = (
                f"{prefix} && opcode == 'h{opcode} && channel == 'h{channel} "
                f"&& id == 'h{item_id} && data == 'h{data}"
            )
            add_observation(
                rows, wrapper, environment, work_dir, target,
                original_root, resources, f"direct.{index:02d}",
                "event.export", {"name": name, "expr": expr},
            )

        add_observation(
            rows, wrapper, environment, work_dir, target,
            original_root, resources, "relational.rdy", "event.export", {
                "name": "rdy",
                "expr": "vld && rdy && opcode >= 8'h10 && data <= 16'h1002",
            },
        )
        add_observation(
            rows, wrapper, environment, work_dir, target,
            original_root, resources, "xz.unknown", "event.export", {
                "name": "xz", "expr": "vld && data != 0",
            },
        )
        add_observation(
            rows, wrapper, environment, work_dir, target,
            original_root, resources, "find.rdy.limit2", "event.find", {
                "name": "rdy", "expr": "vld && rdy", "mode": "all", "line_limit": 2,
            }, with_xout=True, expected_response_truncated=True,
            expected_truncation_scopes=["response_events"],
        )
        add_observation(
            rows, wrapper, environment, work_dir, target,
            original_root, resources, "find.unknown_alias", "event.find", {
                "name": "rdy", "expr": "vld && missing_alias",
            }, expected_ok=False,
        )
        for index, (signal, time) in enumerate((
            ("xif_event_top.if_rdy.pd.opcode", "85ns"),
            ("xif_event_top.if_rdy.pd.channel", "85ns"),
            ("xif_event_top.if_rdy.pd.id", "85ns"),
            ("xif_event_top.if_rdy.pd.data", "85ns"),
            ("xif_event_top.xz_data", "70ns"),
        ), 1):
            add_observation(
                rows, wrapper, environment, work_dir, target,
                original_root, resources, f"value.{index:02d}", "value.at", {
                    "signal": signal, "time": time, "value_format": "hex",
                },
            )
        artifact_path = inside(
            work_dir / "artifacts/rdy-full.json", ROOT, "XIF export artifact"
        )
        artifact_path.parent.mkdir(parents=True, exist_ok=True)
        artifact_path.unlink(missing_ok=True)
        add_observation(
            rows, wrapper, environment, work_dir, target,
            original_root, resources, "artifact.rdy.full", "event.export", {
                "name": "rdy",
                "expr": "vld && rdy",
                "output": {"path": str(artifact_path), "file_format": "json"},
            }, artifact_path=artifact_path,
        )
    finally:
        closed_response = invoke(wrapper, request(
            "session.close", {}, target
        ), environment, work_dir)
        validate_identity(closed_response)
        closed = closed_response.get("ok") is True
    if not closed:
        raise RuntimeError("XIF session 未正常关闭")

    source_root = original_root / "xdebug/testdata/waveform/xif_agent_event"
    source_paths = [
        "tb/xif_event_pkg.sv",
        "tb/xif_event_top.sv",
        *(f"event_{name}.json" for name in CONFIG_NAMES),
    ]
    document = {
        "schema_version": "xdebug.p3e-xif-event-public-oracle.v1",
        "goal_id": GOAL_ID,
        "locked_runtime": {
            "runtime_revision": RUNTIME_REVISION,
            "schema_revision": SCHEMA_REVISION,
            "build_id": BUILD_ID,
            "action_count": 73,
            **executable_sha256(wrapper, executable),
            "npi_version": args.npi_version,
        },
        "original_fixture": {
            "fixture_id": "xdebug.xif_event",
            "fixture_version": args.fixture_version,
            "cache_manifest_sha256": sha256(cache_manifest),
            "fsdb_sha256": sha256(fsdb),
            "fsdb_size": fsdb.stat().st_size,
            "sources": [
                {"path": path, "sha256": sha256(source_root / path)}
                for path in source_paths
            ],
            "cache_reused": True,
            "fixture_rebuilt": False,
            "source_access": "read_only",
        },
        "session": {
            "mode": "waveform",
            "transport": "uds",
            "opened": True,
            "closed_gracefully": True,
            "all_runtime_writes_repository_local": True,
            "fallback_used": False,
        },
        "observations": rows,
    }
    leaked = [value for value in strings(document) if value.startswith("/") or "/home/" in value]
    if leaked:
        raise RuntimeError(f"XIF oracle 泄漏绝对路径: {leaked[:3]}")
    write_atomic(
        output,
        json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
