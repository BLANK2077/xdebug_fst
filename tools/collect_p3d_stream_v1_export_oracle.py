#!/usr/bin/env python3
"""采集锁定原版 runtime 的 stream_v1 export 与 XOUT 公开观察值。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from typing import Any

from collect_p3c_original_oracles import (
    ROOT,
    inside,
    sha256,
    strings,
    write_atomic,
)
from collect_p3d_stream_v1_public_oracle import (
    BUILD_ID,
    RUNTIME_REVISION,
    SCHEMA_REVISION,
    invoke,
    runtime_environment,
    validate_identity,
)


def invoke_xout(
    wrapper: Path,
    request: dict[str, Any],
    environment: dict[str, str],
    work_dir: Path,
) -> str:
    process = subprocess.run(
        [str(wrapper), "-"],
        input=json.dumps(request, ensure_ascii=False) + "\n",
        cwd=work_dir,
        env=environment,
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=240,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(
            f"locked xdebug XOUT 失败: {request.get('action')} "
            f"rc={process.returncode}\n{process.stdout}{process.stderr}"
        )
    expected = f"@xdebug.{request['action']}.v1\n"
    if not process.stdout.startswith(expected) or not process.stdout.endswith("\n"):
        raise RuntimeError("locked xdebug XOUT framing 漂移")
    return process.stdout


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    validate_identity(response)
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    return selected


def normalized_response(
    response: dict[str, Any], artifact_name: str | None = None
) -> dict[str, Any]:
    selected = public_response(response)
    if artifact_name is not None:
        output = selected["summary"]["output"]
        output["path"] = f"artifact/{artifact_name}"
        output["meta_path"] = f"artifact/{artifact_name}.meta.json"
    return selected


def artifact_record(path: Path) -> dict[str, Any]:
    meta = Path(str(path) + ".meta.json")
    if not path.is_file() or not meta.is_file():
        raise RuntimeError(f"stream.export 未生成完整 artifact/meta: {path}")
    payload = path.read_bytes()
    meta_payload = meta.read_bytes()
    return {
        "path": f"artifact/{path.name}",
        "size": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "text": payload.decode("utf-8"),
        "meta_path": f"artifact/{path.name}.meta.json",
        "meta_size": len(meta_payload),
        "meta_sha256": hashlib.sha256(meta_payload).hexdigest(),
        "meta": json.loads(meta_payload),
    }


def request(
    target: dict[str, str], action: str, args: dict[str, Any]
) -> dict[str, Any]:
    return {
        "api_version": "xdebug.v1",
        "action": action,
        "target": target,
        "args": args,
    }


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
    work_dir = inside(args.work_dir, ROOT, "stream_v1 export oracle work-dir")
    resources = args.resources.resolve()
    original_root = args.original_root.resolve()
    config = original_root / "xdebug/testdata/waveform/stream_v1/config/streams.json"
    fsdb = resources / "out/waves.fsdb"
    if not all(path.is_file() for path in (wrapper, executable, config, fsdb)):
        raise RuntimeError("locked runtime/stream_v1 export resources 不完整")
    work_dir.mkdir(parents=True, exist_ok=True)
    environment = runtime_environment(work_dir)

    guide = invoke(
        wrapper,
        {
            "api_version": "xdebug.v1",
            "action": "actions",
            "args": {"output": {"view": "guide"}},
        },
        environment,
        work_dir,
    )
    if (
        guide.get("ok") is not False
        or guide.get("error", {}).get("code") != "INVALID_REQUEST"
        or guide.get("error", {}).get("invalid_arg") != "args.output.view"
    ):
        raise RuntimeError("locked runtime guide capability 漂移")
    actions = invoke(
        wrapper,
        {"api_version": "xdebug.v1", "action": "actions"},
        environment,
        work_dir,
    )
    validate_identity(actions)
    if actions.get("summary", {}).get("action_count") != 73:
        raise RuntimeError("locked runtime Action catalog 漂移")
    schema = invoke(
        wrapper,
        {
            "api_version": "xdebug.v1",
            "action": "schema",
            "args": {"action": "stream.export", "kind": "request"},
        },
        environment,
        work_dir,
    )
    validate_identity(schema)
    if (
        schema.get("ok") is not True
        or schema.get("data", {}).get("schema", {}).get("$id")
        != "xdebug.stream.export.request.v1"
    ):
        raise RuntimeError("stream.export request schema 漂移")

    artifact_dir = inside(
        work_dir / f"artifacts-{os.getpid()}", ROOT,
        "stream_v1 export artifact-dir",
    )
    artifact_dir.mkdir(parents=True, exist_ok=False)
    session_id = f"p3d_stream_export_oracle_{os.getpid()}"
    opened = invoke(
        wrapper,
        request(
            {}, "session.open", {}
        ) | {"target": {"fsdb": str(fsdb)}, "args": {"name": session_id}},
        environment,
        work_dir,
    )
    validate_identity(opened)
    if opened.get("ok") is not True:
        raise RuntimeError(f"stream_v1 export session.open 失败: {opened.get('error')}")
    target = {"session_id": opened["session"]["session_id"]}
    observations: list[dict[str, Any]] = []
    closed = False
    try:
        loaded = invoke(
            wrapper,
            request(target, "stream.config.load", {
                "config_path": str(config), "mode": "replace",
            }),
            environment,
            work_dir,
        )
        if loaded.get("ok") is not True:
            raise RuntimeError(f"stream_v1 export config.load 失败: {loaded.get('error')}")

        previews = (
            ("transfer_preview", "ready_stream", "transfer"),
            ("packet_preview", "ready_packet", "packet"),
            ("packet_beats_preview", "ready_packet", "packet_beats"),
        )
        for observation_id, stream, kind in previews:
            request_args = {
                "stream": stream,
                "kind": kind,
                "cache_scope": "full",
                "time_range": {"begin": "0ns", "end": "250us"},
                "line_limit": 1,
            }
            envelope = request(target, "stream.export", request_args)
            response = invoke(wrapper, envelope, environment, work_dir)
            xout = invoke_xout(wrapper, envelope, environment, work_dir)
            observations.append({
                "observation_id": observation_id,
                "request": request_args,
                "response": normalized_response(response),
                "xout": xout,
                "xout_sha256": hashlib.sha256(xout.encode()).hexdigest(),
            })

        written = (
            (
                "transfer_written", "ready_stream", "transfer",
                {"begin": "0ns", "end": "100ns"}, "ready.tsv", "tsv",
            ),
            (
                "packet_written", "ready_packet", "packet",
                {"begin": "65ns", "end": "95ns"}, "packets.xout", "xout",
            ),
            (
                "packet_beats_written", "ready_packet", "packet_beats",
                {"begin": "65ns", "end": "95ns"}, "beats.xout", "xout",
            ),
        )
        for observation_id, stream, kind, time_range, name, file_format in written:
            path = artifact_dir / name
            request_args = {
                "stream": stream,
                "kind": kind,
                "cache_scope": "range",
                "time_range": time_range,
                "output": {"path": str(path), "file_format": file_format},
            }
            response = invoke(
                wrapper,
                request(target, "stream.export", request_args),
                environment,
                work_dir,
            )
            sanitized_request = json.loads(json.dumps(request_args))
            sanitized_request["output"]["path"] = f"artifact/{name}"
            observations.append({
                "observation_id": observation_id,
                "request": sanitized_request,
                "response": normalized_response(response, name),
                "artifact": artifact_record(path),
            })
    finally:
        closed_response = invoke(
            wrapper,
            request(target, "session.close", {}),
            environment,
            work_dir,
        )
        validate_identity(closed_response)
        closed = closed_response.get("ok") is True
    if not closed:
        raise RuntimeError("stream_v1 export locked session 未正常关闭")

    audit = {
        "schema_version": "xdebug.p3d-stream-v1-export-oracle.v1",
        "goal_id": "01a050fa-b864-7ce2-af88-56083d84ea21",
        "fixture_id": "xdebug.stream_v1",
        "locked_runtime": {
            "runtime_revision": RUNTIME_REVISION,
            "schema_revision": SCHEMA_REVISION,
            "build_id": BUILD_ID,
            "action_count": 73,
            "wrapper_sha256": sha256(wrapper),
            "binary_sha256": sha256(executable),
            "npi_version": args.npi_version,
            "fixture_version": args.fixture_version,
            "cache_reused": True,
            "fixture_rebuilt": False,
            "source_access": "read_only",
        },
        "action_discovery": {
            "guide_request_supported": False,
            "guide_error_code": "INVALID_REQUEST",
            "guide_invalid_arg": "args.output.view",
            "catalog_action_count": 73,
            "request_schema_id": "xdebug.stream.export.request.v1",
            "request_schema_path": schema["data"]["schema_path"],
        },
        "session": {
            "mode": "waveform",
            "transport": "uds",
            "opened": True,
            "closed_gracefully": True,
            "all_runtime_writes_repository_local": True,
            "fallback_used": False,
        },
        "observation_count": len(observations),
        "observations": observations,
    }
    leaked = [
        value for value in strings(audit)
        if value.startswith("/") or "/home/" in value
    ]
    if leaked:
        raise RuntimeError(f"stream_v1 export oracle 去敏失败: {leaked[:3]}")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    write_atomic(
        output,
        json.dumps(audit, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    )
    print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
