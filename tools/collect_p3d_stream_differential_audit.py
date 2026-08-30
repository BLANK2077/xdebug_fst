#!/usr/bin/env python3
"""冻结 stream differential 专用工具、公开回放和 cache 可观察边界。"""

from __future__ import annotations

import argparse
from collections import Counter
import copy
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


GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
DIFFERENTIAL_RUNTIME_REVISION = "846edd6800bd"
DIFFERENTIAL_SCHEMA_REVISION = (
    "6ace27b232adefe5a896cb4222f9ea539e6127e600baf1761e560ec103872574"
)
DIFFERENTIAL_BUILD_ID = (
    f"{DIFFERENTIAL_RUNTIME_REVISION}-{DIFFERENTIAL_SCHEMA_REVISION}"
)
PRIVATE_PROBE_FIELDS = (
    "access_sequence",
    "build_bytes",
    "entry_count",
    "evictions",
    "hits",
    "index_count",
    "key_summary",
    "misses",
    "resident_bytes",
    "scanner_invocations",
)
RANGE_A = {"begin": "0ns", "end": "5us"}
RANGE_B = {"begin": "5us", "end": "10us"}


def canonical_sha256(value: Any) -> str:
    payload = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def invoke(
    binary: Path,
    request: dict[str, Any],
    environment: dict[str, str],
    work_dir: Path,
) -> dict[str, Any]:
    process = subprocess.run(
        [str(binary), "--json", "-"],
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
            f"differential xdebug 未返回 JSON: {request.get('action')} "
            f"rc={process.returncode}\n{process.stdout}{process.stderr}"
        )
    response, _ = json.JSONDecoder().raw_decode(process.stdout[start:])
    if not isinstance(response, dict):
        raise RuntimeError("differential xdebug response 不是对象")
    if process.returncode != 0 and response.get("ok") is not False:
        raise RuntimeError(
            f"differential xdebug 异常退出且无公开错误: "
            f"{request.get('action')} rc={process.returncode}"
        )
    validate_identity(response)
    return response


def invoke_xout(
    binary: Path,
    request: dict[str, Any],
    environment: dict[str, str],
    work_dir: Path,
) -> str:
    process = subprocess.run(
        [str(binary), "-"],
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
    expected = f"@xdebug.{request['action']}.v1\n"
    if (
        process.returncode != 0
        or not process.stdout.startswith(expected)
        or not process.stdout.endswith("\n")
    ):
        raise RuntimeError(
            f"differential XOUT 失败: {request.get('action')} "
            f"rc={process.returncode}\n{process.stdout}{process.stderr}"
        )
    return process.stdout


def validate_identity(response: dict[str, Any]) -> None:
    tool = response.get("tool", {})
    if (
        tool.get("build_id") != DIFFERENTIAL_BUILD_ID
        or tool.get("git_revision") != DIFFERENTIAL_RUNTIME_REVISION
        or tool.get("schema_revision") != DIFFERENTIAL_SCHEMA_REVISION
    ):
        raise RuntimeError(f"differential runtime identity 漂移: {tool}")


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    return selected


def request(
    action: str,
    args: dict[str, Any] | None = None,
    target: dict[str, Any] | None = None,
) -> dict[str, Any]:
    envelope: dict[str, Any] = {
        "api_version": "xdebug.v1",
        "action": action,
    }
    if args is not None:
        envelope["args"] = args
    if target is not None:
        envelope["target"] = target
    return envelope


def runtime_environment(
    work_dir: Path,
    original_root: Path,
    *,
    soft_bytes: str | None = None,
    hard_bytes: str | None = None,
    probe_name: str | None = None,
) -> tuple[dict[str, str], Path | None]:
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
    environment["XVERIF_HOME"] = str(original_root)
    if not environment.get("VERDI_HOME"):
        raise RuntimeError("VERDI_HOME 未设置；禁止替换 differential NPI 工具链")
    for key in (
        "XDEBUG_ANALYSIS_CACHE_MAX_BYTES",
        "XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES",
        "XDEBUG_TEST_ANALYSIS_PROBE_PATH",
    ):
        environment.pop(key, None)
    if soft_bytes is not None:
        environment["XDEBUG_ANALYSIS_CACHE_MAX_BYTES"] = soft_bytes
    if hard_bytes is not None:
        environment["XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES"] = hard_bytes
    probe_path: Path | None = None
    if probe_name is not None:
        probe_path = inside(
            Path(environment["XVERIF_TEST_TMPDIR"]) / probe_name,
            ROOT,
            "analysis probe",
        )
        environment["XDEBUG_TEST_ANALYSIS_PROBE_PATH"] = str(probe_path)
    return environment, probe_path


def open_session(
    binary: Path,
    environment: dict[str, str],
    work_dir: Path,
    fsdb: Path,
    name: str,
) -> dict[str, str]:
    opened = invoke(
        binary,
        request("session.open", {"name": name}, {"fsdb": str(fsdb)}),
        environment,
        work_dir,
    )
    if opened.get("ok") is not True:
        raise RuntimeError(f"differential session.open 失败: {opened.get('error')}")
    return {"session_id": opened["session"]["session_id"]}


def close_session(
    binary: Path,
    environment: dict[str, str],
    work_dir: Path,
    target: dict[str, str],
) -> None:
    closed = invoke(
        binary,
        request("session.close", {}, target),
        environment,
        work_dir,
    )
    if closed.get("ok") is not True:
        raise RuntimeError("differential session 未正常关闭")


def read_probe(path: Path | None) -> list[dict[str, Any]]:
    if path is None or not path.is_file():
        return []
    return [
        row
        for row in (
            json.loads(line)
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
        if row.get("protocol") == "stream"
    ]


def probe_summary(path: Path | None) -> dict[str, Any]:
    rows = read_probe(path)
    if not rows:
        return {"row_count": 0, "event_counts": {}, "last": None}
    last = rows[-1]
    return {
        "row_count": len(rows),
        "event_counts": dict(sorted(Counter(row["event"] for row in rows).items())),
        "last": {field: last[field] for field in PRIVATE_PROBE_FIELDS},
    }


def replay_locked_public_oracles(
    binary: Path,
    environment: dict[str, str],
    work_dir: Path,
    fsdb: Path,
    config: Path,
    public_oracle: dict[str, Any],
    export_oracle: dict[str, Any],
) -> dict[str, Any]:
    target = open_session(
        binary, environment, work_dir, fsdb,
        "p3d_diff_public",
    )
    public_differences: list[str] = []
    try:
        for observation in public_oracle["observations"]:
            args = copy.deepcopy(observation["request"])
            if "config_path" in args:
                args["config_path"] = str(config)
            actual = invoke(
                binary,
                request(observation["action"], args, target),
                environment,
                work_dir,
            )
            if public_response(actual) != observation["response"]:
                public_differences.append(observation["observation_id"])
    finally:
        close_session(binary, environment, work_dir, target)

    artifact_dir = inside(
        work_dir / "export-artifacts",
        ROOT,
        "differential export artifacts",
    )
    artifact_dir.mkdir(parents=True, exist_ok=False)
    target = open_session(
        binary, environment, work_dir, fsdb,
        "p3d_diff_export",
    )
    export_differences: list[str] = []
    artifact_checks = 0
    xout_checks = 0
    try:
        loaded = invoke(
            binary,
            request(
                "stream.config.load",
                {"config_path": str(config), "mode": "replace"},
                target,
            ),
            environment,
            work_dir,
        )
        if loaded.get("ok") is not True:
            raise RuntimeError("differential export config.load 失败")
        for observation in export_oracle["observations"]:
            args = copy.deepcopy(observation["request"])
            artifact = observation.get("artifact")
            artifact_path: Path | None = None
            if artifact is not None:
                artifact_path = artifact_dir / Path(artifact["path"]).name
                args["output"]["path"] = str(artifact_path)
            envelope = request("stream.export", args, target)
            actual = invoke(binary, envelope, environment, work_dir)
            selected = public_response(actual)
            if artifact is None:
                if selected != observation["response"]:
                    export_differences.append(observation["observation_id"])
                xout = invoke_xout(binary, envelope, environment, work_dir)
                if xout != observation["xout"]:
                    export_differences.append(
                        observation["observation_id"] + ".xout"
                    )
                xout_checks += 1
                continue
            assert artifact_path is not None
            output = selected["summary"]["output"]
            name = artifact_path.name
            output["path"] = f"artifact/{name}"
            output["meta_path"] = f"artifact/{name}.meta.json"
            if selected != observation["response"]:
                export_differences.append(observation["observation_id"])
            meta_path = Path(str(artifact_path) + ".meta.json")
            if (
                artifact_path.read_bytes().decode("utf-8") != artifact["text"]
                or json.loads(meta_path.read_text(encoding="utf-8"))
                != artifact["meta"]
            ):
                export_differences.append(
                    observation["observation_id"] + ".artifact"
                )
            artifact_checks += 1
    finally:
        close_session(binary, environment, work_dir, target)

    if public_differences or export_differences:
        raise RuntimeError(
            "differential public replay 漂移: "
            f"query={public_differences}, export={export_differences}"
        )
    return {
        "query_config_observation_count": public_oracle["observation_count"],
        "query_config_difference_count": 0,
        "query_config_oracle_sha256": "",
        "export_observation_count": export_oracle["observation_count"],
        "export_difference_count": 0,
        "export_oracle_sha256": "",
        "artifact_check_count": artifact_checks,
        "xout_check_count": xout_checks,
        "comparator_failure_count": 0,
    }


def cache_observation(
    binary: Path,
    environment: dict[str, str],
    work_dir: Path,
    target: dict[str, str],
    observation_id: str,
    action: str,
    args: dict[str, Any],
) -> dict[str, Any]:
    response = invoke(
        binary, request(action, args, target), environment, work_dir
    )
    return {
        "observation_id": observation_id,
        "action": action,
        "request": args,
        "response": public_response(response),
    }


def normalized_batch(response: dict[str, Any]) -> dict[str, Any]:
    return {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "results": [
            public_response(child)
            for child in response.get("data", {}).get("results", [])
        ],
        "error": response.get("error"),
    }


def collect_cache_contract(
    binary: Path,
    original_root: Path,
    work_dir: Path,
    fsdb: Path,
    ready_packet: dict[str, Any],
) -> dict[str, Any]:
    base_dir = inside(work_dir / "cache-base", ROOT, "cache base work-dir")
    base_dir.mkdir(parents=True, exist_ok=True)
    environment, probe = runtime_environment(
        base_dir, original_root, probe_name="stream-cache.jsonl"
    )
    target = open_session(
        binary, environment, base_dir, fsdb,
        "p3d_diff_cache",
    )
    observations: list[dict[str, Any]] = []
    checkpoints: dict[str, Any] = {}
    try:
        loaded = invoke(
            binary,
            request(
                "stream.config.load",
                {"config": {"streams": [ready_packet]}, "mode": "replace"},
                target,
            ),
            environment,
            base_dir,
        )
        if loaded.get("ok") is not True:
            raise RuntimeError("cache contract config.load 失败")
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "static_validate", "stream.validate",
            {"stream": "ready_packet", "dynamic": False},
        ))
        if read_probe(probe):
            raise RuntimeError("static validate 不应触发私有 analysis probe")
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "range_a_query", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "range", "time_range": RANGE_A},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "range_a_export", "stream.export",
            {"stream": "ready_packet", "kind": "packet",
             "cache_scope": "range", "time_range": RANGE_A,
             "line_limit": 2},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "range_a_validate", "stream.validate",
            {"stream": "ready_packet", "dynamic": True,
             "cache_scope": "range", "time_range": RANGE_A},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "range_b_query", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "range", "time_range": RANGE_B},
        ))
        checkpoints["two_ranges"] = probe_summary(probe)
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "full_from_range", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "full", "time_range": RANGE_A},
        ))
        checkpoints["full_build"] = probe_summary(probe)
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "derived_range", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "range", "time_range": RANGE_B},
        ))

        same_semantics = copy.deepcopy(ready_packet)
        same_semantics["description"] = "description-only replacement"
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "description_load", "stream.config.load",
            {"config": {"streams": [same_semantics]}, "mode": "replace"},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "description_query", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "full", "time_range": RANGE_A},
        ))
        changed_semantics = copy.deepcopy(same_semantics)
        changed_semantics["sample_point"] = "after"
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "semantic_load", "stream.config.load",
            {"config": {"streams": [changed_semantics]}, "mode": "replace"},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "semantic_query", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "full", "time_range": RANGE_A},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "invalid_range", "stream.query",
            {"stream": "ready_packet", "query": "summary",
             "cache_scope": "range"},
        ))
        observations.append(cache_observation(
            binary, environment, base_dir, target,
            "invalid_static", "stream.validate",
            {"stream": "ready_packet", "dynamic": False,
             "cache_scope": "full"},
        ))
        checkpoints["final"] = probe_summary(probe)
    finally:
        close_session(binary, environment, base_dir, target)

    if (
        checkpoints["two_ranges"]["last"]["scanner_invocations"] != 2
        or checkpoints["two_ranges"]["event_counts"].get("build") != 2
        or checkpoints["full_build"]["last"]["scanner_invocations"] != 3
        or checkpoints["full_build"]["event_counts"].get("invalidate") != 2
        or checkpoints["final"]["last"]["scanner_invocations"] != 5
    ):
        raise RuntimeError(f"cache base 私有 probe 合同漂移: {checkpoints}")

    batch_dir = inside(work_dir / "cache-batch", ROOT, "cache batch work-dir")
    batch_dir.mkdir(parents=True, exist_ok=True)
    environment, batch_probe = runtime_environment(
        batch_dir, original_root, probe_name="stream-batch.jsonl"
    )
    target = open_session(
        binary, environment, batch_dir, fsdb,
        "p3d_diff_batch",
    )
    try:
        invoke(
            binary,
            request(
                "stream.config.load",
                {"config": {"streams": [ready_packet]}, "mode": "replace"},
                target,
            ),
            environment,
            batch_dir,
        )
        batch_requests = [
            request("stream.query", {
                "stream": "ready_packet", "query": "summary",
                "cache_scope": "range", "time_range": RANGE_A,
            }, target),
            request("stream.export", {
                "stream": "ready_packet", "kind": "packet",
                "cache_scope": "range", "time_range": RANGE_A,
                "line_limit": 2,
            }, target),
            request("stream.validate", {
                "stream": "ready_packet", "dynamic": True,
                "cache_scope": "range", "time_range": RANGE_A,
            }, target),
            request("stream.query", {
                "stream": "ready_packet", "query": "summary",
                "cache_scope": "range", "time_range": RANGE_B,
            }, target),
            request("stream.query", {
                "stream": "ready_packet", "query": "summary",
                "cache_scope": "full", "time_range": RANGE_A,
            }, target),
            request("stream.query", {
                "stream": "ready_packet", "query": "summary",
                "cache_scope": "range", "time_range": RANGE_B,
            }, target),
        ]
        batch = invoke(
            binary,
            request("batch", {
                "mode": "continue_on_error", "requests": batch_requests,
            }),
            environment,
            batch_dir,
        )
        batch_public = normalized_batch(batch)
        batch_private = probe_summary(batch_probe)
    finally:
        close_session(binary, environment, batch_dir, target)
    if (
        batch_public["summary"].get("all_ok") is not True
        or batch_private["last"]["scanner_invocations"] != 4
        or batch_private["event_counts"].get("build") != 4
        or batch_private["event_counts"].get("invalidate") != 2
    ):
        raise RuntimeError("cache batch 合同漂移")

    soft_dir = inside(work_dir / "cache-soft", ROOT, "cache soft work-dir")
    soft_dir.mkdir(parents=True, exist_ok=True)
    environment, soft_probe = runtime_environment(
        soft_dir, original_root, soft_bytes="1", hard_bytes="2147483648",
        probe_name="stream-soft.jsonl",
    )
    target = open_session(
        binary, environment, soft_dir, fsdb,
        "p3d_diff_soft",
    )
    soft_public: list[dict[str, Any]] = []
    try:
        invoke(
            binary,
            request(
                "stream.config.load",
                {"config": {"streams": [ready_packet]}, "mode": "replace"},
                target,
            ),
            environment,
            soft_dir,
        )
        for index, time_range in enumerate((RANGE_A, RANGE_B, RANGE_A)):
            soft_public.append(cache_observation(
                binary, environment, soft_dir, target,
                f"soft_range_{index}", "stream.query",
                {"stream": "ready_packet", "query": "summary",
                 "cache_scope": "range", "time_range": time_range},
            ))
        soft_private = probe_summary(soft_probe)
    finally:
        close_session(binary, environment, soft_dir, target)
    if (
        soft_private["last"]["scanner_invocations"] != 3
        or soft_private["last"]["evictions"] < 2
    ):
        raise RuntimeError("cache soft-LRU 私有 probe 合同漂移")

    hard_dir = inside(work_dir / "cache-hard", ROOT, "cache hard work-dir")
    hard_dir.mkdir(parents=True, exist_ok=True)
    environment, hard_probe = runtime_environment(
        hard_dir, original_root, soft_bytes="1", hard_bytes="1",
        probe_name="stream-hard.jsonl",
    )
    target = open_session(
        binary, environment, hard_dir, fsdb,
        "p3d_diff_hard",
    )
    try:
        invoke(
            binary,
            request(
                "stream.config.load",
                {"config": {"streams": [ready_packet]}, "mode": "replace"},
                target,
            ),
            environment,
            hard_dir,
        )
        hard_child = request("stream.query", {
            "stream": "ready_packet", "query": "summary",
            "cache_scope": "full", "time_range": RANGE_A,
        }, target)
        hard = invoke(
            binary,
            request("batch", {
                "mode": "continue_on_error",
                "requests": [hard_child, copy.deepcopy(hard_child)],
            }),
            environment,
            hard_dir,
        )
        hard_public = normalized_batch(hard)
        hard_private = probe_summary(hard_probe)
    finally:
        close_session(binary, environment, hard_dir, target)
    hard_errors = [item.get("error", {}) for item in hard_public["results"]]
    if (
        hard_public["summary"].get("all_ok") is not False
        or len(hard_errors) != 2
        or any(
            error.get("code") != "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
            or error.get("protocol") != "stream"
            or error.get("hard_max_bytes") != 1
            or error.get("recoverable") is not True
            or len(error.get("next_actions", [])) != 2
            for error in hard_errors
        )
        or hard_private["last"]["scanner_invocations"] != 0
        or hard_private["event_counts"].get("build_failed") != 2
    ):
        raise RuntimeError("cache hard-limit 公开/私有合同漂移")

    return {
        "ready_packet_config": ready_packet,
        "public_observations": observations,
        "public_observation_count": len(observations),
        "base_private_probe": {
            "classification": "proven-unobservable",
            "checkpoints": checkpoints,
        },
        "batch": {"public": batch_public, "private_probe": batch_private},
        "soft_lru": {
            "public_observations": soft_public,
            "private_probe": soft_private,
            "private_eviction_classification": "proven-unobservable",
        },
        "hard_limit": {
            "classification": "publicly-observable",
            "allowed_current_projection": {
                "error.key_summary": (
                    "opaque 16-hex cache identity may differ because the current "
                    "fixture is native FST rather than the original FSDB"
                ),
            },
            "soft_max_bytes": 1,
            "hard_max_bytes": 1,
            "public": hard_public,
            "private_probe": hard_private,
        },
    }


def source_record(original_root: Path, relative: str) -> dict[str, Any]:
    path = original_root / relative
    if not path.is_file():
        raise RuntimeError(f"原版 differential 源文件缺失: {relative}")
    return {"path": relative, "size": path.stat().st_size, "sha256": sha256(path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--differential-binary", type=Path, required=True)
    parser.add_argument("--differential-engine", type=Path, required=True)
    parser.add_argument("--differential-object", type=Path, required=True)
    parser.add_argument("--differential-manifest", type=Path, required=True)
    parser.add_argument("--stream-resources", type=Path, required=True)
    parser.add_argument("--original-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--fixture-version", required=True)
    parser.add_argument("--stream-fixture-version", required=True)
    parser.add_argument("--npi-version", required=True)
    parser.add_argument("--public-oracle", type=Path, required=True)
    parser.add_argument("--export-oracle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    binary = args.differential_binary.resolve()
    engine = args.differential_engine.resolve()
    differential_object = args.differential_object.resolve()
    cache_manifest = args.differential_manifest.resolve()
    resources = args.stream_resources.resolve()
    original_root = args.original_root.resolve()
    work_dir = inside(args.work_dir, ROOT, "differential audit work-dir")
    work_dir.mkdir(parents=True, exist_ok=True)
    config = original_root / "xdebug/testdata/waveform/stream_v1/config/streams.json"
    fsdb = resources / "out/waves.fsdb"
    for path in (
        binary, engine, differential_object, cache_manifest, config, fsdb,
        args.public_oracle, args.export_oracle,
    ):
        if not path.is_file():
            raise RuntimeError(f"differential audit 输入缺失: {path}")

    environment, _ = runtime_environment(work_dir, original_root)
    actions = invoke(binary, request("actions"), environment, work_dir)
    action_names = actions.get("data", {}).get("actions", [])
    if actions.get("summary", {}).get("action_count") != 73 or len(action_names) != 73:
        raise RuntimeError("differential 73-Action catalog 漂移")
    schema_records: list[dict[str, Any]] = []
    schema_text = ""
    for action in ("stream.query", "stream.export", "stream.validate"):
        response = invoke(
            binary,
            request("schema", {"action": action, "kind": "request"}),
            environment,
            work_dir,
        )
        schema = response.get("data", {}).get("schema")
        if not isinstance(schema, dict):
            raise RuntimeError(f"differential {action} request schema 缺失")
        serialized = json.dumps(schema, ensure_ascii=False, sort_keys=True)
        schema_text += serialized
        schema_records.append({
            "action": action,
            "schema_id": schema.get("$id"),
            "schema_path": response["data"]["schema_path"],
            "schema_sha256": canonical_sha256(schema),
        })
    leaked_private_fields = [
        field for field in PRIVATE_PROBE_FIELDS if field in schema_text
    ]
    if leaked_private_fields or any(
        "differential" in name or "probe" in name for name in action_names
    ):
        raise RuntimeError("私有 comparator/probe 泄漏到公开 Action/schema")

    public_oracle = json.loads(args.public_oracle.read_text(encoding="utf-8"))
    export_oracle = json.loads(args.export_oracle.read_text(encoding="utf-8"))
    replay = replay_locked_public_oracles(
        binary, environment, work_dir, fsdb, config,
        public_oracle, export_oracle,
    )
    replay["query_config_oracle_sha256"] = sha256(args.public_oracle)
    replay["export_oracle_sha256"] = sha256(args.export_oracle)
    stream_configs = json.loads(config.read_text(encoding="utf-8"))["streams"]
    ready_packet = next(
        item for item in stream_configs if item["name"] == "ready_packet"
    )
    cache_contract = collect_cache_contract(
        binary, original_root, work_dir, fsdb, ready_packet
    )

    engine_strings = subprocess.run(
        ["strings", str(engine)],
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=60,
        check=True,
    ).stdout
    comparator_sentinels = [
        "legacy stream differential oracle failed: ",
        "stream columnar differential mismatch for ",
    ]
    if any(value not in engine_strings for value in comparator_sentinels):
        raise RuntimeError("differential engine comparator 哨兵缺失")

    source_paths = (
        "testinfra/fixtures.v1.yaml",
        "testinfra/catalog.v1.yaml",
        "xdebug/Makefile",
        "xdebug/tests/stream_differential/test_stream_differential.py",
        "xdebug/tests/stream_differential/legacy_stream_oracle.h",
        "xdebug/tests/stream_differential/legacy_stream_oracle.cpp",
        "xdebug/tests/synthetic/test_stream_v1_real_waveform.py",
        "xdebug/src/engine/service/actions/stream/stream_query.cpp",
        "xdebug/src/engine/service/actions/stream/stream_export.cpp",
        "xdebug/src/engine/service/actions/stream/stream_validate.cpp",
        "xdebug/src/waveform/cache/analysis_probe.h",
        "xdebug/src/waveform/cache/analysis_probe.cpp",
        "xdebug/src/waveform/cache/analysis_repository.cpp",
        "xdebug/src/waveform/stream/stream_analyzer.cpp",
        "xdebug/src/waveform/stream/stream_analyzer.h",
    )
    cache_manifest_json = json.loads(cache_manifest.read_text(encoding="utf-8"))
    if cache_manifest_json.get("fixture_id") != "xdebug.stream_differential_tool":
        raise RuntimeError("differential cache manifest fixture identity 漂移")

    audit = {
        "schema_version": "xdebug.p3d-stream-differential-closure-audit.v1",
        "goal_id": GOAL_ID,
        "fixture_id": "xdebug.stream_differential_tool",
        "classification": "proven-unobservable",
        "fixture_contract": {
            "source_dir": "xdebug",
            "builder_argv": [
                "make", "stream-differential-test-dist",
                "STREAM_DIFFERENTIAL_DIST={resources}/out",
            ],
            "outputs": [
                {"name": "frontend", "path": "out/xdebug", "kind": "file", "min_bytes": 1024},
                {"name": "engine", "path": "out/libexec/xdebug-engine", "kind": "file", "min_bytes": 1024},
            ],
            "rtl_input_count": 0,
            "waveform_output_count": 0,
            "reused_waveform_fixture_id": "xdebug.stream_v1",
        },
        "locked_runtime": {
            "runtime_revision": DIFFERENTIAL_RUNTIME_REVISION,
            "schema_revision": DIFFERENTIAL_SCHEMA_REVISION,
            "build_id": DIFFERENTIAL_BUILD_ID,
            "action_count": 73,
            "frontend_sha256": sha256(binary),
            "engine_sha256": sha256(engine),
            "legacy_object_sha256": sha256(differential_object),
            "fixture_version": args.fixture_version,
            "stream_fixture_version": args.stream_fixture_version,
            "cache_manifest_sha256": sha256(cache_manifest),
            "cache_fingerprint": cache_manifest_json["fingerprint"],
            "npi_version": args.npi_version,
            "cache_reused": True,
            "fixture_rebuilt": False,
            "source_access": "read_only",
        },
        "comparator": {
            "compile_guard": "XDEBUG_STREAM_DIFFERENTIAL_TEST_BUILD",
            "linked_object": "obj/tests/stream_differential/legacy_stream_oracle.o",
            "public_action": False,
            "interposed_actions": [
                "stream.query", "stream.export", "stream.validate"
            ],
            "engine_failure_sentinels": comparator_sentinels,
            "public_replay": replay,
            "remaining_public_difference_count": 0,
        },
        "public_boundary": {
            "action_count": 73,
            "private_action_count": 0,
            "request_schemas": schema_records,
            "private_probe_fields": list(PRIVATE_PROBE_FIELDS),
            "private_probe_fields_in_public_schema": [],
            "public_cache_error_code": "ANALYSIS_MEMORY_LIMIT_EXCEEDED",
        },
        "cache_contract": cache_contract,
        "source_files": [
            source_record(original_root, relative) for relative in source_paths
        ],
        "session": {
            "mode": "waveform",
            "transport": "uds",
            "all_runtime_writes_repository_local": True,
            "source_access": "read_only",
            "fixture_rebuilt": False,
            "fallback_used": False,
        },
        "closure": {
            "stream_v1_classification_candidate": "semantic-equivalent",
            "differential_tool_classification_candidate": "proven-unobservable",
            "private_cache_metrics_classification": "proven-unobservable",
            "public_hard_limit_requires_current_gate": True,
            "remaining_unmapped_public_observation_count": 0,
        },
    }
    leaked = [
        value for value in strings(audit)
        if value.startswith("/") or "/home/" in value
    ]
    if leaked:
        raise RuntimeError(f"differential closure audit 去敏失败: {leaked[:3]}")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output = inside(output, ROOT, "differential audit output")
    write_atomic(
        output,
        json.dumps(audit, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    )
    print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
