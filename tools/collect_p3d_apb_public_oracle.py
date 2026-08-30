#!/usr/bin/env python3
"""采集锁定原版 runtime 的 APB SVT/XAMBA 公开 Action 与 cache 边界。"""

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
from collect_p3d_stream_v1_export_oracle import invoke_xout
from collect_p3d_stream_v1_public_oracle import (
    BUILD_ID,
    RUNTIME_REVISION,
    SCHEMA_REVISION,
    invoke,
    runtime_environment,
    validate_identity,
)


GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
APB_ACTIONS = (
    "apb.config.list",
    "apb.config.load",
    "apb.query",
    "apb.statistics",
    "apb.transaction.cursor",
    "apb.transfer_window",
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


def canonical_sha256(value: Any) -> str:
    payload = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


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


def public_response(
    response: dict[str, Any],
    *,
    action: str,
    expected_ok: bool,
    original_root: Path,
    resources: Path,
) -> dict[str, Any]:
    validate_identity(response)
    if response.get("action") != action:
        raise RuntimeError(f"APB Action identity 漂移: {response.get('action')}")
    if response.get("ok") is not expected_ok:
        raise RuntimeError(
            f"APB Action 成功状态漂移: {action}: {response.get('error')}"
        )
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if not expected_ok:
        selected["error"] = response.get("error", {})
    if expected_ok and action in {
        "apb.query",
        "apb.statistics",
        "apb.transaction.cursor",
        "apb.transfer_window",
    }:
        summary = selected["summary"]
        if (
            summary.get("scan_complete") is not True
            or summary.get("analysis_complete") is not True
        ):
            raise RuntimeError(f"{action} 未完成扫描/分析: {summary}")
        scopes = summary.get("truncation_scopes", [])
        if any(scope != "response_transactions" for scope in scopes):
            raise RuntimeError(f"{action} 出现分析侧裁剪: {scopes}")
    return sanitize(selected, original_root, resources)


def make_environment(
    work_dir: Path,
    original_root: Path,
    context: str,
    *,
    soft_bytes: str | None = None,
    hard_bytes: str | None = None,
    probe: bool = False,
) -> tuple[dict[str, str], Path | None]:
    context_dir = inside(work_dir / context, ROOT, f"APB {context} work-dir")
    context_dir.mkdir(parents=True, exist_ok=True)
    environment = runtime_environment(context_dir)
    # Do not set XVERIF_HOME here.  The locked wrapper must resolve schemas and
    # runtime resources from its own frozen clone, never from the live original
    # worktree passed only for read-only fixture/source identity.
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
    if probe:
        probe_path = inside(
            Path(environment["XVERIF_TEST_TMPDIR"]) / "apb-probe.jsonl",
            ROOT,
            f"APB {context} probe",
        )
        probe_path.unlink(missing_ok=True)
        environment["XDEBUG_TEST_ANALYSIS_PROBE_PATH"] = str(probe_path)
    return environment, probe_path


def open_session(
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
    fsdb: Path,
    name: str,
) -> dict[str, str]:
    response = invoke(
        wrapper,
        request("session.open", {"name": name}, {"fsdb": str(fsdb)}),
        environment,
        work_dir,
    )
    validate_identity(response)
    if response.get("ok") is not True:
        raise RuntimeError(f"APB session.open 失败: {response.get('error')}")
    return {"session_id": response["session"]["session_id"]}


def close_session(
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
    target: dict[str, str],
) -> None:
    response = invoke(
        wrapper,
        request("session.close", {}, target),
        environment,
        work_dir,
    )
    validate_identity(response)
    if response.get("ok") is not True:
        raise RuntimeError(f"APB session.close 失败: {response.get('error')}")


def observation(
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
    with_xout: bool = False,
) -> dict[str, Any]:
    envelope = request(action, args, target)
    response = invoke(wrapper, envelope, environment, work_dir)
    row = {
        "observation_id": observation_id,
        "action": action,
        "request": sanitize(args, original_root, resources),
        "response": public_response(
            response,
            action=action,
            expected_ok=expected_ok,
            original_root=original_root,
            resources=resources,
        ),
    }
    if with_xout:
        if not expected_ok:
            raise RuntimeError("失败响应不采集 XOUT")
        xout = invoke_xout(wrapper, envelope, environment, work_dir)
        if "/home/" in xout:
            raise RuntimeError(f"{observation_id} XOUT 泄漏绝对路径")
        row["xout"] = xout
        row["xout_sha256"] = hashlib.sha256(xout.encode()).hexdigest()
    return row


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
        if row.get("protocol") == "apb"
    ]


def probe_summary(path: Path | None) -> dict[str, Any]:
    rows = read_probe(path)
    if not rows:
        return {"row_count": 0, "event_counts": {}, "last": None}
    last = rows[-1]
    return {
        "row_count": len(rows),
        "event_counts": dict(sorted(Counter(row["event"] for row in rows).items())),
        "last": {field: last.get(field) for field in PRIVATE_PROBE_FIELDS},
    }


def source_record(original_root: Path, relative: str) -> dict[str, Any]:
    path = original_root / relative
    if not path.is_file():
        raise RuntimeError(f"APB 原版源码缺失: {relative}")
    return {
        "path": relative,
        "size": path.stat().st_size,
        "sha256": sha256(path),
    }


def resource_record(resources: Path, relative: str) -> dict[str, Any]:
    path = resources / relative
    if not path.is_file():
        raise RuntimeError(f"APB cache resource 缺失: {relative}")
    return {
        "path": relative,
        "size": path.stat().st_size,
        "sha256": sha256(path),
    }


def svt_stimulus() -> list[dict[str, Any]]:
    raw = (
        (True, 0x00, 0x11223344, 0xF, False),
        (True, 0x04, 0x55667788, 0xF, False),
        (True, 0x08, 0xA5A55A5A, 0xF, False),
        (True, 0x0C, 0xDEADBEEF, 0xF, False),
        (True, 0x04, 0x0000ABCD, 0x3, False),
        (False, 0x00, 0x11223344, 0x0, False),
        (False, 0x04, 0x5566ABCD, 0x0, False),
        (False, 0x08, 0xA5A55A5A, 0x0, False),
        (False, 0x0C, 0xDEADBEEF, 0x0, False),
        (False, 0xF0, 0xBAD000F0, 0x0, True),
    )
    return [
        {
            "index": index,
            "is_write": write,
            "address": f"32'h{address:08x}",
            "public_data": f"32'h{data:08x}",
            "pstrb": f"4'h{strobe:x}",
            "wait_cycles": (address >> 2) & 0x3,
            "has_error": error,
        }
        for index, (write, address, data, strobe, error) in enumerate(raw)
    ]


def xamba_stimulus() -> list[dict[str, Any]]:
    rows = []
    for index in range(64):
        write = index % 2 == 0
        rows.append({
            "index": index,
            "is_write": write,
            "address": f"32'h{0x1000 + index * 4:08x}",
            "write_data": f"32'h{0xA5000000 | index:08x}",
            "read_data": f"32'h{0x5A000000 | index:08x}",
            "public_data": (
                f"32'h{0xA5000000 | index:08x}"
                if write else f"32'h{0x5A000000 | index:08x}"
            ),
            "pstrb": f"4'h{(1 << (index % 4)) if write else 0:x}",
            "pprot": index % 8,
            "pnse": (index // 8) % 2,
            "wait_cycles": index % 4,
            "has_error": index % 11 == 0,
        })
    return rows


def fixture_spec(kind: str, original_root: Path, resources: Path) -> dict[str, Any]:
    if kind == "svt":
        source_dir = "xdebug/testdata/waveform/apb_vip_real"
        manifest_path = original_root / source_dir / "manifest.json"
        config_path = original_root / source_dir / "config/apb0.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        config = json.loads(config_path.read_text(encoding="utf-8"))
        source_paths = (
            f"{source_dir}/Makefile",
            f"{source_dir}/README.md",
            f"{source_dir}/apb_vip_pkg.sv",
            f"{source_dir}/config/apb0.json",
            f"{source_dir}/env/apb_vip_env.sv",
            f"{source_dir}/manifest.json",
            f"{source_dir}/seq/apb_vip_sequence.sv",
            f"{source_dir}/tb/apb_slave_dut.sv",
            f"{source_dir}/tb/apb_vip_fixture_top.sv",
            f"{source_dir}/tests/apb_vip_test.sv",
        )
        resource_paths = (
            manifest["resources"]["fsdb"],
            manifest["resources"]["simulation_log"],
        )
        return {
            "kind": kind,
            "fixture_id": "xdebug.apb_vip",
            "source_dir": source_dir,
            "manifest": manifest,
            "manifest_path": manifest_path,
            "config": config,
            "config_path": config_path,
            "fsdb": resources / manifest["resources"]["fsdb"],
            "full_range": {"begin": "0ns", "end": "1us"},
            "transaction_count": 10,
            "write_count": 5,
            "read_count": 5,
            "preview_limit": 4,
            "exact_address": "32'h00000004",
            "range_address": {"begin": "32'h00000004", "end": "32'h0000000c"},
            "mask_address": {"value": "32'h00000008", "mask": "32'h00000008"},
            "source_paths": source_paths,
            "resource_paths": resource_paths,
            "stimulus": svt_stimulus(),
            "test_path": "xdebug/tests/synthetic/test_apb_vip_real.py",
            "log_markers": (
                "UVM_ERROR :    0",
                "UVM_FATAL :    0",
                "APB VIP fixture completed: writes=5 reads=5 errors=1",
            ),
        }
    source_dir = "xdebug/testdata/waveform/apb_xamba_vip_real"
    manifest_path = original_root / source_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    prefix = manifest["interface"]
    config = {
        "paddr": prefix + ".paddr",
        "pwdata": prefix + ".pwdata",
        "prdata": prefix + ".prdata",
        "pwrite": prefix + ".pwrite",
        "penable": prefix + ".penable",
        "psel": prefix + ".psel",
        "pready": prefix + ".pready",
        "pslverr": prefix + ".pslverr",
        "clock": prefix + ".pclk",
        "reset": {"signal": prefix + ".presetn", "polarity": "active_low"},
        "edge": "posedge",
    }
    source_paths = (
        f"{source_dir}/Makefile",
        f"{source_dir}/README.md",
        f"{source_dir}/manifest.json",
        f"{source_dir}/tb/xdebug_apb_xamba_fixture_pkg.sv",
        f"{source_dir}/tb/xdebug_apb_xamba_fixture_top.sv",
    )
    resource_paths = tuple(manifest["resources"][key] for key in (
        "fsdb",
        "simulation_log",
        "run_manifest",
        "extra_sources_manifest",
        "resolved_filelist",
    ))
    return {
        "kind": kind,
        "fixture_id": "xdebug.apb_xamba_vip",
        "source_dir": source_dir,
        "manifest": manifest,
        "manifest_path": manifest_path,
        "config": config,
        "config_path": None,
        "fsdb": resources / manifest["resources"]["fsdb"],
        "full_range": {"begin": "0ns", "end": "20us"},
        "transaction_count": 64,
        "write_count": 32,
        "read_count": 32,
        "preview_limit": 20,
        "exact_address": "32'h00001008",
        "range_address": {"begin": "32'h00001010", "end": "32'h00001020"},
        "mask_address": {"value": "32'h00001000", "mask": "32'h0000ffc0"},
        "source_paths": source_paths,
        "resource_paths": resource_paths,
        "stimulus": xamba_stimulus(),
        "test_path": "xdebug/tests/synthetic/test_apb_xamba_vip_real.py",
        "log_markers": (
            "UVM_ERROR :    0",
            "UVM_FATAL :    0",
            manifest["expected"]["pass_marker"],
        ),
    }


def collect_action_authority(
    wrapper: Path,
    environment: dict[str, str],
    work_dir: Path,
    original_root: Path,
    resources: Path,
    spec: dict[str, Any],
) -> dict[str, Any]:
    catalog = invoke(wrapper, request("actions"), environment, work_dir)
    validate_identity(catalog)
    names = catalog.get("data", {}).get("actions", [])
    apb_names = tuple(name for name in names if name.startswith("apb."))
    if (
        catalog.get("summary", {}).get("action_count") != 73
        or len(names) != 73
        or apb_names != APB_ACTIONS
    ):
        raise RuntimeError(f"冻结 APB Action catalog 漂移: {apb_names}")

    guide = invoke(
        wrapper,
        request("actions", {"output": {"view": "guide"}}),
        environment,
        work_dir,
    )
    guide_public = public_response(
        guide,
        action="actions",
        expected_ok=False,
        original_root=original_root,
        resources=resources,
    )
    if (
        guide_public.get("error", {}).get("code") != "INVALID_REQUEST"
        or guide_public.get("error", {}).get("invalid_arg") != "args.output.view"
    ):
        raise RuntimeError("冻结 runtime guide 能力边界漂移")

    schemas = []
    request_schema_text = ""
    response_schema_text = ""
    runtime_root = wrapper.parent.parent
    for action in APB_ACTIONS:
        record: dict[str, Any] = {"action": action}
        for kind in ("request", "response"):
            response = invoke(
                wrapper,
                request("schema", {"action": action, "kind": kind}),
                environment,
                work_dir,
            )
            validate_identity(response)
            schema = response.get("data", {}).get("schema")
            schema_path = response.get("data", {}).get("schema_path")
            if response.get("ok") is not True or not isinstance(schema, dict):
                raise RuntimeError(f"{action} {kind} schema 缺失")
            path = runtime_root / "xdebug" / schema_path
            if not path.is_file():
                raise RuntimeError(f"冻结 schema 文件缺失: {schema_path}")
            record[kind] = {
                "schema_id": schema.get("$id"),
                "schema_path": schema_path,
                "schema_sha256": sha256(path),
                "canonical_sha256": canonical_sha256(schema),
            }
            serialized = json.dumps(schema, ensure_ascii=False, sort_keys=True)
            if kind == "request":
                request_schema_text += serialized
            else:
                response_schema_text += serialized
        schemas.append(record)
    request_leaks = [
        field for field in PRIVATE_PROBE_FIELDS if field in request_schema_text
    ]
    response_fields = [
        field for field in PRIVATE_PROBE_FIELDS if field in response_schema_text
    ]
    if request_leaks or response_fields != ["key_summary"]:
        raise RuntimeError(
            "APB probe/public cache-error schema 边界漂移: "
            f"request={request_leaks} response={response_fields}"
        )

    excluded = invoke(
        wrapper,
        request("apb.export", {"name": "apb0"}),
        environment,
        work_dir,
    )
    excluded_public = public_response(
        excluded,
        action="apb.export",
        expected_ok=False,
        original_root=original_root,
        resources=resources,
    )
    if excluded_public.get("error", {}).get("code") != "UNKNOWN_ACTION":
        raise RuntimeError("冻结 runtime 对 apb.export 的拒绝边界漂移")

    current_test = original_root / spec["test_path"]
    frozen_test = runtime_root / spec["test_path"]
    if not current_test.is_file():
        raise RuntimeError("APB fixture consumer 源码身份文件缺失")
    current_text = current_test.read_text(encoding="utf-8")
    frozen_text = (
        frozen_test.read_text(encoding="utf-8")
        if frozen_test.is_file() else None
    )
    return {
        "catalog_action_count": 73,
        "apb_actions": list(APB_ACTIONS),
        "schemas": schemas,
        "private_probe_fields": list(PRIVATE_PROBE_FIELDS),
        "private_probe_fields_in_public_request_schema": [],
        "private_probe_fields_in_public_response_schema": ["key_summary"],
        "public_response_field_note": (
            "key_summary is an opaque public hard-limit error identity; all "
            "other analysis probe counters remain private"
        ),
        "guide_request": guide_public,
        "excluded_apb_export": {
            "classification": "not-in-frozen-public-surface",
            "dynamic_oracle_eligible": False,
            "fallback_to_live_runtime_allowed": False,
            "response": excluded_public,
            "fixture_consumer_path": spec["test_path"],
            "fixture_consumer_sha256": sha256(current_test),
            "fixture_consumer_reference_count": current_text.count("apb.export"),
            "frozen_consumer_present": frozen_text is not None,
            "frozen_consumer_sha256": (
                sha256(frozen_test) if frozen_text is not None else None
            ),
            "frozen_consumer_reference_count": (
                frozen_text.count("apb.export")
                if frozen_text is not None else None
            ),
        },
    }


def collect_base_observations(
    wrapper: Path,
    original_root: Path,
    resources: Path,
    work_dir: Path,
    spec: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    environment, probe_path = make_environment(
        work_dir, original_root, "base", probe=True
    )
    target = open_session(
        wrapper, environment, work_dir, spec["fsdb"],
        f"p3d_apb_{spec['kind']}_base",
    )
    rows: list[dict[str, Any]] = []

    def add(
        observation_id: str,
        action: str,
        args: dict[str, Any],
        *,
        expected_ok: bool = True,
        with_xout: bool = False,
    ) -> dict[str, Any]:
        row = observation(
            wrapper, environment, work_dir, target, original_root, resources,
            observation_id=observation_id,
            action=action,
            args=args,
            expected_ok=expected_ok,
            with_xout=with_xout,
        )
        rows.append(row)
        return row

    try:
        add("config.load", "apb.config.load", {
            "name": "apb0", "config": spec["config"]
        })
        add("config.list.named", "apb.config.list", {"name": "apb0"})
        add("config.list.all", "apb.config.list", {})
        for direction in ("all", "write", "read"):
            add(
                f"query.count.{direction}",
                "apb.query",
                {"name": "apb0", "direction": direction},
            )
        full = add(
            "query.full",
            "apb.query",
            {"name": "apb0", "query": {"line_limit": spec["transaction_count"]}},
        )
        add(
            "query.preview",
            "apb.query",
            {"name": "apb0", "query": {"line_limit": spec["preview_limit"]}},
            with_xout=True,
        )
        add(
            "query.index.first",
            "apb.query",
            {"name": "apb0", "query": {"index": 1}},
        )
        add(
            "query.index.middle",
            "apb.query",
            {"name": "apb0", "query": {"index": spec["transaction_count"] // 2}},
        )
        add("query.last", "apb.query", {"name": "apb0", "last": True})
        add(
            "query.index.oob",
            "apb.query",
            {"name": "apb0", "query": {"index": spec["transaction_count"] + 1}},
        )
        exact = {"mode": "exact", "values": [spec["exact_address"]]}
        address_range = {
            "mode": "range",
            **spec["range_address"],
        }
        address_mask = {
            "mode": "mask",
            **spec["mask_address"],
        }
        add(
            "query.address.exact",
            "apb.query",
            {
                "name": "apb0",
                "address": exact,
                "query": {"line_limit": spec["transaction_count"]},
            },
        )
        add(
            "query.address.range.write",
            "apb.query",
            {
                "name": "apb0",
                "direction": "write",
                "address": address_range,
                "query": {"line_limit": spec["transaction_count"]},
            },
        )
        add(
            "query.address.mask.read",
            "apb.query",
            {
                "name": "apb0",
                "direction": "read",
                "address": address_mask,
                "query": {"line_limit": spec["transaction_count"]},
            },
        )
        add(
            "query.value.decimal",
            "apb.query",
            {
                "name": "apb0",
                "address": exact,
                "value_format": "dec",
                "query": {"line_limit": spec["transaction_count"]},
            },
        )
        add(
            "query.value.binary",
            "apb.query",
            {
                "name": "apb0",
                "address": exact,
                "value_format": "bin",
                "query": {"line_limit": spec["transaction_count"]},
            },
        )
        add(
            "statistics.all", "apb.statistics", {"name": "apb0"},
            with_xout=True,
        )
        add(
            "statistics.write",
            "apb.statistics",
            {"name": "apb0", "filter": {"direction": "write"}},
        )
        add(
            "statistics.address.exact",
            "apb.statistics",
            {"name": "apb0", "filter": {"address": exact}},
        )
        add(
            "statistics.address.range",
            "apb.statistics",
            {"name": "apb0", "filter": {"address": address_range}},
        )
        add(
            "statistics.address.mask",
            "apb.statistics",
            {"name": "apb0", "filter": {"address": address_mask}},
        )
        add(
            "window.full",
            "apb.transfer_window",
            {
                "name": "apb0",
                "time_range": spec["full_range"],
                "line_limit": spec["transaction_count"],
            },
        )
        add(
            "window.preview",
            "apb.transfer_window",
            {
                "name": "apb0",
                "time_range": spec["full_range"],
                "line_limit": 3,
            },
            with_xout=True,
        )
        transactions = full["response"]["data"]["transactions"]
        selected_time = transactions[len(transactions) // 2]["time"]
        add(
            "window.single_completion",
            "apb.transfer_window",
            {
                "name": "apb0",
                "time_range": {"begin": selected_time, "end": selected_time},
                "line_limit": 2,
            },
        )
        add(
            "window.invalid_range",
            "apb.transfer_window",
            {
                "name": "apb0",
                "time_range": {"begin": "1us", "end": "0ns"},
                "line_limit": 1,
            },
            expected_ok=False,
        )
        add(
            "cursor.begin",
            "apb.transaction.cursor",
            {"name": "apb0", "op": "begin", "direction": "all"},
            with_xout=True,
        )
        add(
            "cursor.next",
            "apb.transaction.cursor",
            {"name": "apb0", "op": "next", "direction": "all"},
        )
        add(
            "cursor.prev",
            "apb.transaction.cursor",
            {"name": "apb0", "op": "prev", "direction": "all"},
        )
        add(
            "cursor.pre.at_begin",
            "apb.transaction.cursor",
            {"name": "apb0", "op": "pre", "direction": "all"},
            expected_ok=False,
        )
        add(
            "cursor.last",
            "apb.transaction.cursor",
            {"name": "apb0", "op": "last", "direction": "all"},
        )
        add(
            "cursor.next.at_end",
            "apb.transaction.cursor",
            {"name": "apb0", "op": "next", "direction": "all"},
        )
        add(
            "query.missing_config",
            "apb.query",
            {"name": "missing"},
            expected_ok=False,
        )
        invalid = dict(spec["config"])
        invalid["edge"] = "negedge"
        invalid["sample_point"] = "after"
        add(
            "config.invalid_negedge_sample_point",
            "apb.config.load",
            {"name": "invalid", "config": invalid},
            expected_ok=False,
        )
        if spec["kind"] == "svt":
            for missing_signal in ("pready", "pslverr"):
                incomplete = dict(spec["config"])
                incomplete.pop(missing_signal)
                add(
                    f"config.optional_without_{missing_signal}",
                    "apb.config.load",
                    {
                        "name": f"apb_without_{missing_signal}",
                        "config": incomplete,
                    },
                )
    finally:
        close_session(wrapper, environment, work_dir, target)

    probe = probe_summary(probe_path)
    if (
        probe["last"] is None
        or probe["last"]["scanner_invocations"] != 1
        or probe["event_counts"].get("build") != 1
        or probe["event_counts"].get("scan") != 1
        or probe["event_counts"].get("index_build", 0) < 1
    ):
        raise RuntimeError(f"APB base cache/hit/index 合同漂移: {probe}")
    return rows, probe


def collect_cache_contract(
    wrapper: Path,
    original_root: Path,
    resources: Path,
    work_dir: Path,
    spec: dict[str, Any],
) -> dict[str, Any]:
    environment, probe_path = make_environment(
        work_dir,
        original_root,
        "soft",
        soft_bytes="1",
        hard_bytes="2147483648",
        probe=True,
    )
    target = open_session(
        wrapper, environment, work_dir, spec["fsdb"],
        f"p3d_apb_{spec['kind']}_soft",
    )
    soft_rows = []
    try:
        before = dict(spec["config"])
        before["sample_point"] = "before"
        after = dict(spec["config"])
        after["sample_point"] = "after"
        sequence = (
            ("config.before", "apb.config.load", {"name": "apb_before", "config": before}),
            ("config.after", "apb.config.load", {"name": "apb_after", "config": after}),
            ("cursor.before.begin", "apb.transaction.cursor", {"name": "apb_before", "op": "begin", "direction": "all"}),
            ("cursor.before.next", "apb.transaction.cursor", {"name": "apb_before", "op": "next", "direction": "all"}),
            ("query.after.write", "apb.query", {"name": "apb_after", "direction": "write"}),
            ("cursor.before.resume", "apb.transaction.cursor", {"name": "apb_before", "op": "next", "direction": "all"}),
        )
        for observation_id, action, args in sequence:
            soft_rows.append(observation(
                wrapper, environment, work_dir, target,
                original_root, resources,
                observation_id=observation_id,
                action=action,
                args=args,
            ))
    finally:
        close_session(wrapper, environment, work_dir, target)
    soft_probe = probe_summary(probe_path)
    if (
        soft_probe["last"] is None
        or soft_probe["last"]["scanner_invocations"] != 3
        or soft_probe["last"]["evictions"] < 2
        or soft_probe["event_counts"].get("scan") != 3
    ):
        raise RuntimeError(f"APB soft-LRU 合同漂移: {soft_probe}")

    environment, probe_path = make_environment(
        work_dir,
        original_root,
        "hard",
        soft_bytes="1",
        hard_bytes="1",
        probe=True,
    )
    target = open_session(
        wrapper, environment, work_dir, spec["fsdb"],
        f"p3d_apb_{spec['kind']}_hard",
    )
    hard_rows = []
    try:
        hard_rows.append(observation(
            wrapper, environment, work_dir, target,
            original_root, resources,
            observation_id="config.load",
            action="apb.config.load",
            args={"name": "apb0", "config": spec["config"]},
        ))
        hard_rows.append(observation(
            wrapper, environment, work_dir, target,
            original_root, resources,
            observation_id="query.write.rejected",
            action="apb.query",
            args={"name": "apb0", "direction": "write"},
            expected_ok=False,
        ))
    finally:
        close_session(wrapper, environment, work_dir, target)
    hard_probe = probe_summary(probe_path)
    error = hard_rows[-1]["response"].get("error", {})
    if (
        error.get("code") != "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
        or error.get("protocol") != "apb"
        or error.get("hard_max_bytes") != 1
        or error.get("recoverable") is not True
        or hard_probe["event_counts"].get("build_failed") != 1
        or hard_probe.get("last", {}).get("scanner_invocations") != 0
    ):
        raise RuntimeError(
            f"APB hard-limit 公开/私有合同漂移: {error} {hard_probe}"
        )
    return {
        "base_hit_index": {
            "private_probe_classification": "proven-unobservable",
        },
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
            "allowed_current_projection": {
                "error.key_summary": (
                    "opaque 16-hex cache identity may differ because the current "
                    "fixture is native FST rather than the original FSDB"
                )
            },
            "public_observations": hard_rows,
            "private_probe": hard_probe,
        },
    }


def validate_transactions(spec: dict[str, Any], rows: list[dict[str, Any]]) -> None:
    full = next(row for row in rows if row["observation_id"] == "query.full")
    response = full["response"]
    summary = response["summary"]
    transactions = response["data"]["transactions"]
    if (
        summary.get("total_count") != spec["transaction_count"]
        or summary.get("returned_count") != spec["transaction_count"]
        or len(transactions) != spec["transaction_count"]
    ):
        raise RuntimeError("APB 全量 transaction 未完整采集")
    expected = spec["stimulus"]
    for index, (actual, contract) in enumerate(zip(transactions, expected)):
        projection = {
            "is_write": actual.get("is_write"),
            "address": actual.get("addr"),
            "public_data": actual.get("data"),
            "has_error": actual.get("has_error"),
        }
        wanted = {
            key: contract[key]
            for key in ("is_write", "address", "public_data", "has_error")
        }
        if projection != wanted or not isinstance(actual.get("time"), str):
            raise RuntimeError(
                f"APB stimulus/public transaction 差异 index={index}: "
                f"{projection} != {wanted}"
            )


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
    work_dir = inside(args.work_dir, ROOT, "APB oracle work-dir")
    original_root = args.original_root.resolve()
    resources = args.resources.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    spec = fixture_spec(args.fixture, original_root, resources)
    cache_manifest = resources.parent / "manifest.json"
    for path in (
        wrapper,
        executable,
        spec["manifest_path"],
        spec["fsdb"],
        cache_manifest,
    ):
        if not path.is_file():
            raise RuntimeError(f"APB oracle 输入缺失: {path}")
    log_path = resources / spec["manifest"]["resources"]["simulation_log"]
    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    if any(marker not in log_text for marker in spec["log_markers"]):
        raise RuntimeError("APB fixture simulation log 门禁失败")

    authority_environment, _ = make_environment(
        work_dir, original_root, "authority"
    )
    authority = collect_action_authority(
        wrapper,
        authority_environment,
        work_dir,
        original_root,
        resources,
        spec,
    )
    rows, base_probe = collect_base_observations(
        wrapper, original_root, resources, work_dir, spec
    )
    validate_transactions(spec, rows)
    cache_contract = collect_cache_contract(
        wrapper, original_root, resources, work_dir, spec
    )
    cache_contract["base_hit_index"]["private_probe"] = base_probe

    original_fixture: dict[str, Any] = {
        "source_dir": spec["source_dir"],
        "manifest_sha256": sha256(spec["manifest_path"]),
        "cache_manifest_sha256": sha256(cache_manifest),
        "fsdb_path": spec["manifest"]["resources"]["fsdb"],
        "fsdb_sha256": sha256(spec["fsdb"]),
        "fsdb_size": spec["fsdb"].stat().st_size,
        "top": spec["manifest"]["top"],
        "interface": spec["manifest"]["interface"],
        "seed": spec["manifest"]["seed"],
        "expected": spec["manifest"]["expected"],
        "config": spec["config"],
        "source_files": [
            source_record(original_root, relative)
            for relative in spec["source_paths"]
        ],
        "resource_files": [
            resource_record(resources, relative)
            for relative in spec["resource_paths"]
        ],
        "stimulus_contract": spec["stimulus"],
        "field_observability": {
            "direct_transaction_fields": [
                "time", "is_write", "addr", "data", "has_error"
            ],
            "indirect_source_fields": ["pstrb", "wait_cycles"],
            "xamba_source_only_fields": (
                ["pprot", "pnse"] if spec["kind"] == "xamba" else []
            ),
            "rule": (
                "PSTRB/PPROT/PNSE/wait count are not direct fields of the six "
                "frozen APB Action responses; preserve them in the source "
                "stimulus contract and compare completion time/data/error."
            ),
        },
    }
    if spec["config_path"] is not None:
        original_fixture["config_path"] = (
            f"{spec['source_dir']}/config/apb0.json"
        )
        original_fixture["config_sha256"] = sha256(spec["config_path"])

    audit = {
        "schema_version": "xdebug.p3d-apb-public-oracle.v1",
        "goal_id": GOAL_ID,
        "fixture_id": spec["fixture_id"],
        "producer_kind": spec["kind"],
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
        "original_fixture": original_fixture,
        "session": {
            "mode": "waveform",
            "transport": "uds",
            "session_count": 3,
            "all_sessions_closed_gracefully": True,
            "all_runtime_writes_repository_local": True,
            "source_access": "read_only",
            "fixture_rebuilt": False,
            "fallback_used": False,
        },
        "observation_count": len(rows),
        "observations": rows,
        "cache_contract": cache_contract,
    }
    leaked = [
        value for value in strings(audit)
        if value.startswith("/") or "/home/" in value
    ]
    if leaked:
        raise RuntimeError(f"APB oracle 去敏失败: {leaked[:3]}")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output = inside(output, ROOT, "APB oracle output")
    write_atomic(
        output,
        json.dumps(audit, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    )
    print(f"wrote {output.resolve().relative_to(ROOT.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
