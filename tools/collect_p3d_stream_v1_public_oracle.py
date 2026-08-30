#!/usr/bin/env python3
"""采集锁定原版 runtime 的 stream_v1 公开 Action 观察值。"""

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
STREAM_NAMES = (
    "valid_only",
    "ready_stream",
    "bp_stream",
    "ready_packet",
    "bp_packet",
    "ready_bp_packet_negedge",
    "interleaved_packet",
)
FULL_RANGE = {"begin": "0ns", "end": "250us"}


def invoke(
    wrapper: Path,
    request: dict[str, Any],
    environment: dict[str, str],
    work_dir: Path,
) -> dict[str, Any]:
    process = subprocess.run(
        [str(wrapper), "--json", "-"],
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
    start = process.stdout.find("{")
    if start < 0:
        raise RuntimeError(
            f"locked xdebug 未返回 JSON: {request.get('action')} "
            f"rc={process.returncode}\n{process.stdout}{process.stderr}"
        )
    response, _ = json.JSONDecoder().raw_decode(process.stdout[start:])
    if not isinstance(response, dict):
        raise RuntimeError("locked xdebug response 不是对象")
    if process.returncode != 0 and response.get("ok") is not False:
        raise RuntimeError(
            f"locked xdebug 进程失败但未返回公开错误: "
            f"{request.get('action')} rc={process.returncode}\n"
            f"{process.stdout}{process.stderr}"
        )
    return response


def validate_identity(response: dict[str, Any]) -> None:
    tool = response.get("tool", {})
    if (
        tool.get("build_id") != BUILD_ID
        or tool.get("git_revision") != RUNTIME_REVISION[:12]
        or tool.get("schema_revision") != SCHEMA_REVISION
    ):
        raise RuntimeError(f"locked runtime identity 漂移: {tool}")


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


def observable_response(
    response: dict[str, Any],
    *,
    expected_action: str,
    original_root: Path,
    resources: Path,
    expected_ok: bool = True,
) -> dict[str, Any]:
    validate_identity(response)
    if response.get("action") != expected_action:
        raise RuntimeError(
            f"公开 Action identity 漂移: {response.get('action')}"
        )
    if response.get("ok") is not expected_ok:
        raise RuntimeError(
            f"公开 Action 成功状态漂移: {expected_action}: "
            f"{response.get('error')}"
        )
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if not expected_ok:
        selected["error"] = response.get("error", {})
    summary = selected["summary"]
    if expected_ok and expected_action in {"stream.query", "stream.validate"}:
        if (
            summary.get("scan_complete") is not True
            or summary.get("analysis_complete") is not True
        ):
            raise RuntimeError(
                f"{expected_action} 未完成扫描/分析: {summary}"
            )
        scopes = summary.get("truncation_scopes", [])
        if any(not str(scope).startswith("response_") for scope in scopes):
            raise RuntimeError(
                f"{expected_action} 出现分析侧裁剪: {scopes}"
            )
    return sanitize(selected, original_root, resources)


def request_row(
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
    target: dict[str, str],
    original_root: Path,
    resources: Path,
    *,
    observation_id: str,
    action: str,
    args: dict[str, Any],
    expected_ok: bool = True,
) -> dict[str, Any]:
    response = invoke(
        wrapper,
        {
            "api_version": "xdebug.v1",
            "action": action,
            "target": target,
            "args": args,
        },
        environment,
        work_dir,
    )
    return {
        "observation_id": observation_id,
        "action": action,
        "request": sanitize(args, original_root, resources),
        "response": observable_response(
            response,
            expected_action=action,
            original_root=original_root,
            resources=resources,
            expected_ok=expected_ok,
        ),
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
    work_dir = inside(args.work_dir, ROOT, "stream_v1 oracle work-dir")
    resources = args.resources.resolve()
    original_root = args.original_root.resolve()
    fixture_dir = original_root / "xdebug/testdata/waveform/stream_v1"
    config = fixture_dir / "config/streams.json"
    rtl = fixture_dir / "tb/stream_v1_top.sv"
    fsdb = resources / "out/waves.fsdb"
    expected_path = resources / "out/stream_expected.json"
    if not all(path.is_file() for path in (
        wrapper, executable, config, rtl, fsdb, expected_path
    )):
        raise RuntimeError("locked runtime/stream_v1 resources 不完整")
    work_dir.mkdir(parents=True, exist_ok=True)
    environment = runtime_environment(work_dir)

    actions = invoke(
        wrapper,
        {"api_version": "xdebug.v1", "action": "actions"},
        environment,
        work_dir,
    )
    validate_identity(actions)
    if (
        actions.get("ok") is not True
        or actions.get("summary", {}).get("action_count") != 73
        or len(actions.get("data", {}).get("actions", [])) != 73
    ):
        raise RuntimeError("locked runtime 73-Action identity gate 失败")

    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    expected_streams = expected.get("streams", {})
    if tuple(expected_streams) != STREAM_NAMES:
        raise RuntimeError("stream_v1 expected stream 顺序/集合漂移")

    session_id = f"p3d_stream_v1_oracle_{os.getpid()}"
    opened = invoke(
        wrapper,
        {
            "api_version": "xdebug.v1",
            "action": "session.open",
            "target": {"fsdb": str(fsdb)},
            "args": {"name": session_id},
        },
        environment,
        work_dir,
    )
    validate_identity(opened)
    if opened.get("ok") is not True:
        raise RuntimeError(f"stream_v1 session.open 失败: {opened.get('error')}")
    target = {"session_id": opened["session"]["session_id"]}
    rows: list[dict[str, Any]] = []
    closed = False
    try:
        rows.append(request_row(
            wrapper, environment, work_dir, target, original_root, resources,
            observation_id="config.load", action="stream.config.load",
            args={"config_path": str(config), "mode": "replace"},
        ))
        rows.append(request_row(
            wrapper, environment, work_dir, target, original_root, resources,
            observation_id="config.list", action="stream.config.list", args={},
        ))
        for stream_name in STREAM_NAMES:
            rows.append(request_row(
                wrapper, environment, work_dir, target, original_root, resources,
                observation_id=f"{stream_name}.describe",
                action="stream.describe", args={"stream": stream_name},
            ))
            rows.append(request_row(
                wrapper, environment, work_dir, target, original_root, resources,
                observation_id=f"{stream_name}.validate",
                action="stream.validate",
                args={
                    "stream": stream_name,
                    "time_range": FULL_RANGE,
                    "line_limit": 512,
                },
            ))
            for query, extra in (
                ("summary", {}),
                ("first_transfer", {}),
                ("last_transfer", {}),
                ("transfer_window", {"line_limit": 8}),
            ):
                rows.append(request_row(
                    wrapper, environment, work_dir, target,
                    original_root, resources,
                    observation_id=f"{stream_name}.{query}",
                    action="stream.query",
                    args={
                        "stream": stream_name,
                        "query": query,
                        "time_range": FULL_RANGE,
                        **extra,
                    },
                ))

        specific_queries = (
            (
                "ready_packet.partial_summary",
                "ready_packet", "summary",
                {"time_range": {"begin": "65ns", "end": "75ns"}},
            ),
            (
                "ready_packet.first_packet",
                "ready_packet", "first_packet", {},
            ),
            (
                "ready_packet.packet_at_3",
                "ready_packet", "packet_at",
                {"packet_index": 3, "line_limit": 1},
            ),
            (
                "ready_packet.packet_at_oob",
                "ready_packet", "packet_at", {"packet_index": 999999},
            ),
            (
                "bp_packet.first_packet",
                "bp_packet", "first_packet", {},
            ),
            (
                "ready_stream.stall_window",
                "ready_stream", "stall_window", {"line_limit": 4},
            ),
            (
                "ready_bp_packet_negedge.packet_window",
                "ready_bp_packet_negedge", "packet_window", {"line_limit": 4},
            ),
            (
                "interleaved_packet.packet_window",
                "interleaved_packet", "packet_window", {"line_limit": 4},
            ),
            (
                "ready_stream.beat_filter",
                "ready_stream", "transfer_window",
                {
                    "line_limit": 8,
                    "filter": {"fields": {
                        "low8": {
                            "mode": "exact", "values": ["8'h5a", "8'h5b"]
                        },
                        "is_wr": {
                            "mode": "range", "begin": "1'b0", "end": "1'b1"
                        },
                        "data": {
                            "mode": "mask",
                            "value": "32'h0000005a",
                            "mask": "32'h000000ff",
                        },
                    }},
                },
            ),
            (
                "valid_only.scalar_filter",
                "valid_only", "transfer_window",
                {
                    "line_limit": 2,
                    "filter": {"fields": {
                        "data": {
                            "mode": "exact", "values": ["32'h1000005a"]
                        },
                    }},
                },
            ),
            (
                "ready_packet.packet_filter",
                "ready_packet", "packet_window",
                {
                    "line_limit": 1,
                    "filter": {
                        "position": "sop",
                        "fields": {
                            "opcode": {
                                "mode": "exact", "values": ["8'ha3"]
                            },
                            "seq": {
                                "mode": "range",
                                "begin": "16'd12", "end": "16'd12",
                            },
                            "data": {
                                "mode": "mask", "value": "32'h0c",
                                "mask": "32'hff",
                            },
                        },
                    },
                },
            ),
            (
                "ready_packet.partial_filter",
                "ready_packet", "summary",
                {
                    "time_range": {"begin": "65ns", "end": "75ns"},
                    "filter": {
                        "position": "eop",
                        "fields": {
                            "opcode": {
                                "mode": "exact", "values": ["8'ha0"]
                            },
                        },
                    },
                },
            ),
            (
                "ready_stream.channel_3",
                "ready_stream", "transfer_window",
                {"channel": "3", "line_limit": 8},
            ),
        )
        for observation_id, stream_name, query, extra in specific_queries:
            time_range = extra.get("time_range", FULL_RANGE)
            query_extra = {
                key: value for key, value in extra.items()
                if key != "time_range"
            }
            rows.append(request_row(
                wrapper, environment, work_dir, target, original_root, resources,
                observation_id=observation_id,
                action="stream.query",
                args={
                    "stream": stream_name,
                    "query": query,
                    "time_range": time_range,
                    **query_extra,
                },
            ))

        rows.append(request_row(
            wrapper, environment, work_dir, target, original_root, resources,
            observation_id="config.invalid_interleaving",
            action="stream.config.load",
            args={
                "mode": "append",
                "config": {"streams": [{
                    "name": "bad_interleave_channel_valid",
                    "signals": {
                        "clk": "stream_v1_top.clk",
                        "vld": "stream_v1_top.ipkt_vld",
                        "rdy": "stream_v1_top.ipkt_rdy",
                        "sop": "stream_v1_top.ipkt_sop",
                        "eop": "stream_v1_top.ipkt_eop",
                        "chid": "stream_v1_top.ipkt_chid",
                        "data": "stream_v1_top.ipkt_data",
                    },
                    "clock": "clk",
                    "vld": "vld",
                    "rdy": "rdy",
                    "sop": "sop",
                    "eop": "eop",
                    "channel_id": "chid",
                    "channel_id_valid": "sop",
                    "allow_interleaving": True,
                    "beat_fields": {"data": "data"},
                }]},
            },
            expected_ok=False,
        ))
    finally:
        closed_response = invoke(
            wrapper,
            {
                "api_version": "xdebug.v1",
                "action": "session.close",
                "target": target,
                "args": {},
            },
            environment,
            work_dir,
        )
        validate_identity(closed_response)
        closed = closed_response.get("ok") is True
    if not closed:
        raise RuntimeError("stream_v1 locked session 未正常关闭")

    audit = {
        "schema_version": "xdebug.p3d-stream-v1-public-oracle.v1",
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
        "original_fixture": {
            "rtl_path": "xdebug/testdata/waveform/stream_v1/tb/stream_v1_top.sv",
            "rtl_sha256": sha256(rtl),
            "config_path": "xdebug/testdata/waveform/stream_v1/config/streams.json",
            "config_sha256": sha256(config),
            "fsdb_sha256": sha256(fsdb),
            "fsdb_size": fsdb.stat().st_size,
            "expected_sha256": sha256(expected_path),
            "expected": expected,
        },
        "session": {
            "mode": "waveform",
            "transport": "uds",
            "opened": True,
            "closed_gracefully": True,
            "all_runtime_writes_repository_local": True,
            "fallback_used": False,
        },
        "observation_count": len(rows),
        "observations": rows,
    }
    leaked = [
        value for value in strings(audit)
        if value.startswith("/") or "/home/" in value
    ]
    if leaked:
        raise RuntimeError(f"stream_v1 oracle 去敏失败: {leaked[:3]}")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    write_atomic(
        output,
        json.dumps(audit, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    )
    print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
