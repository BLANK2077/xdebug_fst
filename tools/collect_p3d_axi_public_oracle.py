#!/usr/bin/env python3
"""采集锁定原版 runtime 的 AXI SVT/XAMBA 公开语义与导出证据。"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from typing import Any

from collect_p3c_original_oracles import (
    ROOT,
    inside,
    sanitize,
    sha256,
    strings,
    write_atomic,
)
from collect_p3d_apb_public_oracle import (
    canonical_sha256,
    close_session,
    make_environment,
    open_session,
    request,
    resource_record,
    source_record,
)
from collect_p3d_stream_v1_export_oracle import invoke_xout
from collect_p3d_stream_v1_public_oracle import (
    BUILD_ID,
    RUNTIME_REVISION,
    SCHEMA_REVISION,
    invoke,
    validate_identity,
)


GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
AXI_ACTIONS = (
    "axi.analysis",
    "axi.channel_stall",
    "axi.config.list",
    "axi.config.load",
    "axi.export",
    "axi.latency_outlier",
    "axi.outstanding_timeline",
    "axi.query",
    "axi.request_response_pair",
    "axi.statistics",
    "axi.transaction.cursor",
)
PRIVATE_PROBE_FIELDS = (
    "access_sequence", "build_bytes", "entry_count", "evictions", "hits",
    "index_count", "key_summary", "misses", "resident_bytes",
    "scanner_invocations",
)
ROW_FIELDS = {
    "transactions", "findings", "outliers", "change_points",
    "pending_transactions",
}


def scrub_repo_paths(value: Any) -> Any:
    if isinstance(value, str):
        try:
            path = Path(value)
            if path.is_absolute() and (path == ROOT.resolve() or ROOT.resolve() in path.parents):
                return "<repo-local-output>/" + path.name
        except (OSError, ValueError):
            pass
        return value
    if isinstance(value, list):
        return [scrub_repo_paths(item) for item in value]
    if isinstance(value, dict):
        return {key: scrub_repo_paths(item) for key, item in value.items()}
    return value


def axi_config(prefix: str, top: str) -> dict[str, Any]:
    names = (
        "awaddr", "awid", "awlen", "awsize", "awburst", "awvalid",
        "awready", "wdata", "wstrb", "wlast", "wvalid", "wready",
        "bid", "bresp", "bvalid", "bready", "araddr", "arid", "arlen",
        "arsize", "arburst", "arvalid", "arready", "rid", "rdata",
        "rresp", "rlast", "rvalid", "rready",
    )
    return {
        **{name: f"{prefix}.{name}" for name in names},
        "clock": f"{top}.clk" if prefix.startswith("axi_vip_fixture_top")
        else f"{prefix}.aclk",
        "reset": {
            "signal": f"{top}.rst_n" if prefix.startswith("axi_vip_fixture_top")
            else f"{prefix}.aresetn",
            "polarity": "active_low",
        },
        "edge": "posedge",
    }


def compact_response(
    response: dict[str, Any],
    original_root: Path,
    resources: Path,
) -> dict[str, Any]:
    """Keep full public fields, replacing only large row arrays with digest/anchors."""

    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    selected = scrub_repo_paths(sanitize(selected, original_root, resources))
    data = selected.get("data")
    if not isinstance(data, dict):
        return selected
    for field in sorted(ROW_FIELDS & set(data)):
        rows = data[field]
        if not isinstance(rows, list) or len(rows) <= 64:
            continue
        selected["data"][field] = {
            "canonical_sha256": canonical_sha256(rows),
            "count": len(rows),
            "anchors": [rows[0], rows[len(rows) // 2], rows[-1]],
        }
    return selected


def checked_invoke(
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
    original_root: Path,
    resources: Path,
    action: str,
    args: dict[str, Any],
    target: dict[str, str],
    *,
    expected_ok: bool = True,
) -> dict[str, Any]:
    response = invoke(wrapper, request(action, args, target), environment, work_dir)
    validate_identity(response)
    if response.get("action") != action or response.get("ok") is not expected_ok:
        raise RuntimeError(f"AXI Action 漂移: {action}: {response.get('error')}")
    return compact_response(response, original_root, resources)


def action_authority(
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
) -> dict[str, Any]:
    catalog = invoke(wrapper, request("actions"), environment, work_dir)
    validate_identity(catalog)
    names = catalog.get("data", {}).get("actions", [])
    axi_names = tuple(name for name in names if name.startswith("axi."))
    if len(names) != 73 or axi_names != AXI_ACTIONS:
        raise RuntimeError(f"冻结 AXI Action catalog 漂移: {axi_names}")
    runtime_root = wrapper.parent.parent
    schemas = []
    request_text = ""
    response_text = ""
    for action in AXI_ACTIONS:
        record: dict[str, Any] = {"action": action}
        for kind in ("request", "response"):
            response = invoke(
                wrapper, request("schema", {"action": action, "kind": kind}),
                environment, work_dir,
            )
            validate_identity(response)
            schema = response.get("data", {}).get("schema")
            schema_path = response.get("data", {}).get("schema_path")
            if response.get("ok") is not True or not isinstance(schema, dict):
                raise RuntimeError(f"{action} {kind} schema 缺失")
            path = runtime_root / "xdebug" / schema_path
            record[kind] = {
                "schema_id": schema.get("$id"),
                "schema_path": schema_path,
                "schema_sha256": sha256(path),
                "canonical_sha256": canonical_sha256(schema),
            }
            if kind == "request":
                request_text += json.dumps(schema, sort_keys=True)
            else:
                response_text += json.dumps(schema, sort_keys=True)
        schemas.append(record)
    request_leaks = [field for field in PRIVATE_PROBE_FIELDS if field in request_text]
    response_fields = [field for field in PRIVATE_PROBE_FIELDS if field in response_text]
    if request_leaks or response_fields != ["key_summary"]:
        raise RuntimeError(
            f"AXI probe/public schema 边界漂移: {request_leaks} {response_fields}"
        )
    return {
        "catalog_action_count": 73,
        "axi_actions": list(AXI_ACTIONS),
        "schemas": schemas,
        "private_probe_fields": list(PRIVATE_PROBE_FIELDS),
        "private_probe_fields_in_public_request_schema": request_leaks,
        "private_probe_fields_in_public_response_schema": response_fields,
    }


def fixture_spec(kind: str, original_root: Path, resources: Path) -> dict[str, Any]:
    if kind == "svt":
        source_dir = "xdebug/testdata/waveform/axi_vip_real"
        manifest_path = original_root / source_dir / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        source_paths = [
            path.relative_to(original_root).as_posix()
            for path in sorted((original_root / source_dir).rglob("*"))
            if path.is_file() and path.name != ".gitignore"
        ]
        runs = []
        for run in manifest["runs"]:
            runs.append({
                **run,
                "expected_direction_count":
                    run["num_ids"] * run["transactions_per_id"],
                "fsdb_path": run["fsdb"],
                "fsdb": resources / run["fsdb"],
                "log_path": run["simulation_log"],
                "handshake_path": run["handshake_oracle"],
            })
        return {
            "fixture_id": "xdebug.axi_vip",
            "producer_kind": "svt",
            "source_dir": source_dir,
            "manifest": manifest,
            "manifest_path": manifest_path,
            "source_paths": source_paths,
            "runs": runs,
            "config": axi_config(manifest["interface"], manifest["top"]),
        }
    source_dir = "xdebug/testdata/waveform/axi_xamba_vip_real"
    manifest_path = original_root / source_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    source_paths = [
        path.relative_to(original_root).as_posix()
        for path in sorted((original_root / source_dir).rglob("*"))
        if path.is_file()
    ]
    run = {
        "name": "xamba",
        "seed": manifest["seed"],
        "expected_direction_count": 32,
        "fsdb_path": manifest["resources"]["fsdb"],
        "fsdb": resources / manifest["resources"]["fsdb"],
        "log_path": manifest["resources"]["simulation_log"],
        "handshake_path": manifest["resources"]["handshake_oracle"],
    }
    return {
        "fixture_id": "xdebug.axi_xamba_vip",
        "producer_kind": "xamba",
        "source_dir": source_dir,
        "manifest": manifest,
        "manifest_path": manifest_path,
        "source_paths": source_paths,
        "runs": [run],
        "config": axi_config(manifest["interface"], manifest["top"]),
    }


def artifact_records(prefix: Path) -> list[dict[str, Any]]:
    paths = sorted(prefix.parent.glob(prefix.name + ".*"))
    if not paths:
        raise RuntimeError("AXI export 未生成 artifact")
    records = []
    for path in paths:
        if not path.is_file():
            continue
        payload = path.read_bytes()
        if path.suffix == ".json":
            normalized = scrub_repo_paths(json.loads(payload.decode("utf-8")))
            payload = (
                json.dumps(normalized, ensure_ascii=False, indent=2, sort_keys=True)
                + "\n"
            ).encode("utf-8")
        records.append({
            "name": path.name,
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        })
    return records


def probe_summary(path: Path | None) -> dict[str, Any]:
    rows = []
    if path is not None and path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("protocol") == "axi":
                rows.append(row)
    return {
        "row_count": len(rows),
        "event_counts": dict(sorted(Counter(row["event"] for row in rows).items())),
        "last": (
            {field: rows[-1].get(field) for field in PRIVATE_PROBE_FIELDS}
            if rows else None
        ),
    }


def collect_cache_contract(
    wrapper: Path,
    original_root: Path,
    resources: Path,
    work_dir: Path,
    spec: dict[str, Any],
) -> dict[str, Any]:
    run = next(
        (item for item in spec["runs"] if item["name"] == "fixed_delay"),
        spec["runs"][0],
    )

    def public(action: str, args: dict[str, Any], target: dict[str, str],
               environment: dict[str, str], expected_ok: bool = True) -> dict[str, Any]:
        return {
            "action": action,
            "request": scrub_repo_paths(sanitize(args, original_root, resources)),
            "response": checked_invoke(
                wrapper, environment, work_dir, original_root, resources,
                action, args, target, expected_ok=expected_ok,
            ),
        }

    soft_env, soft_probe_path = make_environment(
        work_dir, original_root, "cache-soft", soft_bytes="1",
        hard_bytes="2147483648", probe=True,
    )
    soft_target = open_session(
        wrapper, soft_env, work_dir, run["fsdb"],
        f"p3d_axi_{spec['producer_kind']}_soft",
    )
    soft_rows = []
    try:
        base = spec["config"]
        before = {**base, "edge": "dual", "sample_point": "before"}
        sequence = (
            ("axi.config.load", {"name": "axi0", "config": base}),
            ("axi.query", {"name": "axi0", "direction": "write"}),
            ("axi.query", {"name": "axi0", "direction": "read"}),
            ("axi.config.load", {"name": "axi_before", "config": before}),
            ("axi.query", {"name": "axi_before", "direction": "write"}),
            ("axi.query", {"name": "axi0", "direction": "write"}),
        )
        for action, args in sequence:
            soft_rows.append(public(action, args, soft_target, soft_env))
    finally:
        close_session(wrapper, soft_env, work_dir, soft_target)
    soft_probe = probe_summary(soft_probe_path)
    if (
        soft_probe["last"] is None
        or soft_probe["last"]["scanner_invocations"] < 2
        or soft_probe["last"]["evictions"] < 1
    ):
        raise RuntimeError(f"AXI soft-LRU 合同漂移: {soft_probe}")

    hard_env, hard_probe_path = make_environment(
        work_dir, original_root, "cache-hard", soft_bytes="1",
        hard_bytes="1", probe=True,
    )
    hard_target = open_session(
        wrapper, hard_env, work_dir, run["fsdb"],
        f"p3d_axi_{spec['producer_kind']}_hard",
    )
    hard_rows = []
    try:
        hard_rows.append(public(
            "axi.config.load", {"name": "axi0", "config": spec["config"]},
            hard_target, hard_env,
        ))
        hard_rows.append(public(
            "axi.query", {"name": "axi0", "direction": "write"},
            hard_target, hard_env, expected_ok=False,
        ))
    finally:
        close_session(wrapper, hard_env, work_dir, hard_target)
    hard_probe = probe_summary(hard_probe_path)
    error = hard_rows[-1]["response"].get("error", {})
    if (
        error.get("code") != "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
        or error.get("protocol") != "axi"
        or error.get("hard_max_bytes") != 1
        or hard_probe["event_counts"].get("build_failed") != 1
    ):
        raise RuntimeError(f"AXI hard-limit 合同漂移: {error} {hard_probe}")
    return {
        "soft_lru": {
            "soft_max_bytes": 1,
            "hard_max_bytes": 2147483648,
            "private_eviction_classification": "proven-unobservable",
            "public_observations": soft_rows,
            "private_probe": soft_probe,
        },
        "hard_limit": {
            "soft_max_bytes": 1,
            "hard_max_bytes": 1,
            "classification": "publicly-observable",
            "public_observations": hard_rows,
            "private_probe": hard_probe,
            "allowed_current_projection": {
                "error.key_summary": "opaque 16-hex cache identity may differ for native FST"
            },
        },
    }


def collect_run(
    wrapper: Path,
    original_root: Path,
    resources: Path,
    work_dir: Path,
    spec: dict[str, Any],
    run: dict[str, Any],
) -> dict[str, Any]:
    environment, probe_path = make_environment(
        work_dir, original_root, f"base-{run['name']}", probe=True
    )
    target = open_session(
        wrapper, environment, work_dir, run["fsdb"],
        f"p3d_axi_{spec['producer_kind']}_{run['name']}",
    )
    observations = []

    def add(
        observation_id: str,
        action: str,
        args: dict[str, Any],
        *,
        xout: bool = False,
        expected_ok: bool = True,
    ) -> dict[str, Any]:
        response = checked_invoke(
            wrapper, environment, work_dir, original_root, resources,
            action, args, target, expected_ok=expected_ok,
        )
        row = {
            "observation_id": observation_id,
            "action": action,
            "request": scrub_repo_paths(sanitize(args, original_root, resources)),
            "response": response,
        }
        if xout:
            text = invoke_xout(
                wrapper, request(action, args, target), environment, work_dir
            )
            if "/home/" in text:
                raise RuntimeError("AXI XOUT 泄漏绝对路径")
            row["xout"] = text
            row["xout_sha256"] = hashlib.sha256(text.encode()).hexdigest()
        observations.append(row)
        return row

    count = run["expected_direction_count"]
    full_limit = 2 * count
    export_prefix = inside(
        work_dir / f"export-{spec['producer_kind']}-{run['name']}" / "axi0",
        ROOT,
        "AXI export prefix",
    )
    export_prefix.parent.mkdir(parents=True, exist_ok=True)
    try:
        add("config.load", "axi.config.load", {"name": "axi0", "config": spec["config"]})
        add("config.list", "axi.config.list", {"name": "axi0"})
        add("query.count.write", "axi.query", {"name": "axi0", "direction": "write"})
        add("query.count.read", "axi.query", {"name": "axi0", "direction": "read"})
        add(
            "query.first.detailed", "axi.query",
            {"name": "axi0", "direction": "write", "query": {"index": 1},
             "output": {"include_data": True}}, xout=True,
        )
        add("statistics.all", "axi.statistics", {"name": "axi0"}, xout=True)
        add(
            "pair.full", "axi.request_response_pair",
            {"name": "axi0", "direction": "all", "line_limit": full_limit},
        )
        add(
            "analysis.latency", "axi.analysis",
            {"name": "axi0", "analysis": "latency", "direction": "all"},
            xout=True,
        )
        add(
            "analysis.osd", "axi.analysis",
            {"name": "axi0", "analysis": "osd", "direction": "all"},
        )
        add(
            "analysis.pending", "axi.analysis",
            {"name": "axi0", "analysis": "pending", "direction": "all"},
        )
        add(
            "latency.preview", "axi.latency_outlier",
            {"name": "axi0", "direction": "all", "method": "top_n",
             "top_n": 5, "line_limit": 5}, xout=True,
        )
        add(
            "outstanding.preview", "axi.outstanding_timeline",
            {"name": "axi0", "direction": "all", "line_limit": 20}, xout=True,
        )
        add(
            "stall.r.preview", "axi.channel_stall",
            {"name": "axi0", "channel": "r", "rules": {"max_wait_cycles": 2},
             "line_limit": 20}, xout=True,
        )
        add(
            "cursor.begin", "axi.transaction.cursor",
            {"name": "axi0", "op": "begin", "direction": "all"}, xout=True,
        )
        add(
            "cursor.last", "axi.transaction.cursor",
            {"name": "axi0", "op": "last", "direction": "all"},
        )
        export = add(
            "export.full", "axi.export",
            {"name": "axi0", "time_range": {"begin": "0ns", "end": "200ms"},
             "output": {"path": str(export_prefix), "file_format": "tsv"}},
        )
        export["artifacts"] = artifact_records(export_prefix)
        add("query.missing", "axi.query", {"name": "missing"}, expected_ok=False)
    finally:
        close_session(wrapper, environment, work_dir, target)

    by_id = {row["observation_id"]: row for row in observations}
    for direction in ("write", "read"):
        summary = by_id[f"query.count.{direction}"]["response"]["summary"]
        if summary.get("total_count") != count or summary.get("scan_complete") is not True:
            raise RuntimeError(f"AXI {run['name']} {direction} count 漂移: {summary}")
    pair = by_id["pair.full"]["response"]
    if (
        pair["summary"].get("total_count") != 2 * count
        or pair["summary"].get("returned_count") != 2 * count
        or pair["summary"].get("response_truncated") is not False
        or pair["summary"].get("analysis_complete") is not True
    ):
        raise RuntimeError(f"AXI {run['name']} full pair 不完整")
    pending = by_id["analysis.pending"]["response"]
    if pending["summary"].get("total_count") != 0:
        raise RuntimeError(f"AXI {run['name']} 存在未完成事务")
    probe_rows = []
    if probe_path is not None and probe_path.is_file():
        probe_rows = [
            json.loads(line) for line in probe_path.read_text().splitlines()
            if line.strip() and json.loads(line).get("protocol") == "axi"
        ]
    if not probe_rows or probe_rows[-1].get("scanner_invocations") != 1:
        raise RuntimeError(f"AXI {run['name']} cache 扫描合同漂移")
    handshake = resources / run["handshake_path"]
    records = [json.loads(line) for line in handshake.read_text().splitlines() if line]
    channels = Counter(str(row["channel"]).upper() for row in records)
    return {
        "name": run["name"],
        "seed": run["seed"],
        "expected_direction_count": count,
        "fsdb_path": run["fsdb_path"],
        "fsdb_sha256": sha256(run["fsdb"]),
        "fsdb_size": run["fsdb"].stat().st_size,
        "simulation_log": resource_record(resources, run["log_path"]),
        "handshake_oracle": resource_record(resources, run["handshake_path"]),
        "handshake_line_count": len(records),
        "handshake_channel_counts": dict(sorted(channels.items())),
        "observation_count": len(observations),
        "observations": observations,
        "cache_probe": {
            "classification": "private-proven-unobservable",
            "row_count": len(probe_rows),
            "event_counts": dict(sorted(Counter(row["event"] for row in probe_rows).items())),
            "last": {field: probe_rows[-1].get(field) for field in PRIVATE_PROBE_FIELDS},
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", choices=("svt", "xamba"), required=True)
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
    work_dir = inside(args.work_dir, ROOT, "AXI oracle work-dir")
    resources = args.resources.resolve()
    original_root = args.original_root.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    spec = fixture_spec(args.fixture, original_root, resources)
    cache_manifest = resources.parent / "manifest.json"
    for path in (wrapper, executable, spec["manifest_path"], cache_manifest):
        if not path.is_file():
            raise RuntimeError(f"AXI oracle 输入缺失: {path}")
    for run in spec["runs"]:
        for key in ("fsdb",):
            if not run[key].is_file():
                raise RuntimeError(f"AXI cache resource 缺失: {run[key]}")

    authority_env, _ = make_environment(work_dir, original_root, "authority")
    authority = action_authority(wrapper, authority_env, work_dir)
    runs = [
        collect_run(wrapper, original_root, resources, work_dir, spec, run)
        for run in spec["runs"]
    ]
    cache_contract = collect_cache_contract(
        wrapper, original_root, resources, work_dir, spec
    )
    audit = {
        "schema_version": "xdebug.p3d-axi-public-oracle.v1",
        "goal_id": GOAL_ID,
        "fixture_id": spec["fixture_id"],
        "producer_kind": spec["producer_kind"],
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
        "action_authority": authority,
        "original_fixture": {
            "source_dir": spec["source_dir"],
            "manifest_sha256": sha256(spec["manifest_path"]),
            "cache_manifest_sha256": sha256(cache_manifest),
            "top": spec["manifest"]["top"],
            "interface": spec["manifest"]["interface"],
            "config": spec["config"],
            "source_files": [
                source_record(original_root, path) for path in spec["source_paths"]
            ],
        },
        "run_count": len(runs),
        "runs": runs,
        "cache_contract": cache_contract,
        "session": {
            "mode": "waveform",
            "transport": "uds",
            "session_count": len(runs) + 2,
            "all_sessions_closed_gracefully": True,
            "all_runtime_writes_repository_local": True,
            "source_access": "read_only",
            "fixture_rebuilt": False,
            "fallback_used": False,
        },
    }
    leaked = [value for value in strings(audit) if value.startswith("/") or "/home/" in value]
    if leaked:
        raise RuntimeError(f"AXI oracle 去敏失败: {leaked[:3]}")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output = inside(output, ROOT, "AXI oracle output")
    write_atomic(output, json.dumps(audit, ensure_ascii=False, indent=2, sort_keys=True) + "\n")
    print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
