#!/usr/bin/env python3
"""Build the auditable original-RTL/current-FST semantic mapping matrix.

The original xverif checkout is an immutable input.  This program validates
every original file it reads against the P0 asset manifest and can only write
the generated JSON below the current repository root.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = "xdebug.rtl-wave-semantic-matrix.v1"
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
ASSET_MANIFEST = Path("compat/xdebug-v1/rtl-wave-assets.manifest.json")
DEFAULT_OUTPUT = Path("tests/coverage/rtl_wave_semantic_matrix.json")
ACTIVE_CATALOG = Path("xdebug/tests/active_trace_chain/cases.v1.yaml")
ACTIVE_README = Path("xdebug/tests/active_trace_chain/README.md")
PHASE5_REPORT = Path(
    "xdebug/tests/active_trace_chain/reports/phase5_lane_select_report.md"
)
PHASE5_RUNTIME_AUDIT = Path(
    "tests/data/rtl_wave_differential/phase5.runtime-audit.json"
)
AI_COMPLEX_RUNTIME_AUDIT = Path(
    "tests/data/rtl_wave_differential/ai_complex.runtime-audit.json"
)
P3B_RUNTIME_AUDIT = Path(
    "tests/data/rtl_wave_differential/p3b.runtime-audit.json"
)
P3C_P0_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-p0.original-oracle.json"
)
P3C_COMPOSITE_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-composite.original-oracle.json"
)
P3C_PHASE4_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-phase4.original-oracle.json"
)
AI_COMPLEX_RUNNER_SHA256 = (
    "2c8f34c48d675d2e82b9edfd470a084fd17f84bc373ba26a98f0ab7cef848724"
)
AI_COMPLEX_COUNTER_RUNNER_SHA256 = (
    "fe0bafa4ff50d36d1fc283da07f915981ce613aae618341324edfb683c29ead5"
)
P3C_P0_RUNNER_SHA256 = (
    "f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237"
)
P3C_P0_FIXTURE_VERSION = (
    "c2bff810935847ad3debd15d1facb671d4943bc209ba25f38cb4b92d3d1ca06c-"
    "prepare-98pbtd3b"
)
P3C_COMPOSITE_FIXTURE_VERSION = (
    "93b0b714ac405932d426e9f03c88daaacad2d7c3a1595777d4c080f9a0c1d676-"
    "prepare-fpjmfmeo"
)
P3C_PHASE4_FIXTURE_VERSION = (
    "6a9d0fea1e9c68057e2ef12906dfd23f73a8a14622fc09b3be9798ee859b09ae-"
    "prepare-m3d4_3z7"
)

ALLOWED_STATUSES = {
    "exact",
    "semantic-equivalent",
    "partial",
    "missing",
    "proven-unobservable",
}

CONSTRUCT_PATTERNS = {
    "module": r"\bmodule\b",
    "interface": r"\binterface\b",
    "modport": r"\bmodport\b",
    "continuous_assign": r"\bassign\b",
    "always_ff": r"\balways_ff\b",
    "always_comb": r"\balways_comb\b",
    "always": r"\balways\s*@",
    "initial": r"\binitial\b",
    "generate": r"\b(?:generate|genvar)\b",
    "procedural_for": r"\bfor\s*\(",
    "if": r"\bif\s*\(",
    "case": r"\bcase\s*\(",
    "casez": r"\bcasez\s*\(",
    "casex": r"\bcasex\s*\(",
    "matches": r"\bmatches\b",
    "inside": r"\binside\b",
    "nonblocking_assignment": r"<=",
    "force": r"\bforce\b",
    "release": r"\brelease\b",
    "function": r"\bfunction\b",
    "task": r"\btask\b",
    "assertion": r"\b(?:assert|assume|cover)\s+(?:property|final)\b",
    "property": r"\bproperty\b",
    "sequence": r"\bsequence\b",
    "event": r"\bevent\b|->",
    "real": r"\breal\b",
    "string": r"\bstring\b",
    "four_state_literal": r"(?:'[bBoOhH][0-9a-fA-F_xXzZ?]+|\b[01]*[xXzZ?][01xXzZ?]*\b)",
    "delay": r"#[0-9(]",
    "clock": r"\b(?:clk|clock|aclk|pclk)\b",
    "reset": r"\b(?:rst|reset|aresetn|presetn)\b",
    "apb": r"\bP(?:ADDR|SEL|ENABLE|WRITE|WDATA|RDATA|READY|SLVERR)\b",
    "axi": r"\b(?:AW|AR|W|R|B)(?:VALID|READY|ADDR|DATA|ID|RESP|LAST)\b",
    "valid_ready": r"\b(?:valid|ready|tvalid|tready)\b",
}

# A candidate only proves that a related capability exists.  Until P2 runs an
# equal request against both waveforms, every entry below remains partial.
FIXTURE_CANDIDATES = {
    "xdebug.active_driver": {
        "fixtures": ["current.active_driver"],
        "tests": [
            "test_locked_active_driver_assignment_and_force",
            "test_locked_active_driver_recurses_and_preserves_limits",
        ],
        "actions": ["trace.active_driver"],
        "batch": "P3-B",
        "evidence_scope": (
            "原版 RTL 字节相同；原版 runner 先在锁定 FSDB/runtime 上通过，"
            "同一公开请求与完整断言移植到当前原生 FST/DesignDB 后通过"
        ),
    },
    "xdebug.active_semantics": {
        "fixtures": ["current.case", "current.matches", "current.output_mixed"],
        "tests": [
            "test_trace_active_driver_selects_case_item_and_default",
            "test_trace_active_driver_selects_pattern_variable_binding",
            "test_trace_active_driver_chain_reports_two_active_procedural_drivers",
            "test_signal_canonicalize_port_connection",
        ],
        "actions": [
            "signal.canonicalize",
            "trace.active_driver",
            "trace.active_driver_chain",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "锁定原版完整 oracle 通过；当前按公开 Action、分支、时间、完整性字段"
            "逐项归一映射，而非宣称复用同一原版 runner"
        ),
    },
    "xdebug.active_zero_evidence": {
        "fixtures": ["current.active_zero_evidence"],
        "tests": [
            "test_locked_active_zero_scope_roots_discovers_combined_top",
            "test_locked_active_zero_precise_active_time",
            "test_locked_active_zero_reduction_zero_evidence",
            "test_locked_active_zero_module_input_follows_parent",
            "test_locked_active_zero_expression_outputs_have_zero_evidence",
        ],
        "actions": [
            "scope.list", "scope.roots", "trace.active_driver",
            "trace.active_driver_chain", "trace.driver",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "原版 RTL 字节相同；完整移植 scope/driver/chain/零证据与时间尺度 oracle"
            "在当前原生 FST/DesignDB 上通过"
        ),
    },
    "xdebug.interface_port_root": {
        "fixtures": ["current.interface_port_root"],
        "tests": [
            "test_locked_interface_scope_classification",
            "test_locked_interface_active_driver_aliases",
        ],
        "actions": ["scope.list", "trace.active_driver"],
        "batch": "P3-B",
        "evidence_scope": (
            "原版 RTL 字节相同；interface 分类、modport alias 和活动驱动公开 oracle"
            "在当前原生 FST/DesignDB 上通过"
        ),
    },
    "xdebug.trace_x_xprop": {
        "fixtures": ["current.xprop"],
        "tests": [
            "test_batch_preserves_nested_x_value_from_direct_raw_fst",
            "test_trace_x_origin_x_propagation",
            "test_trace_x_origin_branch_chain_ids_are_consistent",
            "test_trace_x_origin_keeps_loop_and_normal_source_branches",
        ],
        "actions": [
            "list.load", "trace.active_driver_chain", "trace.x_origin", "value.at",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "锁定原版 X-prop oracle 通过；当前 FST 逐项覆盖四态点读、list、"
            "X origin、alias/loop/limits 和 active chain 公开结果"
        ),
    },
    "xdebug.ai_complex_wave": {
        "fixtures": ["current.ai_complex"],
        "tests": [
            "test_ai_complex_fixture_is_frozen_and_four_state",
            "test_ai_complex_scope_value_and_four_state_contract",
            "test_ai_complex_event_reset_and_sampling_contract",
            "test_ai_complex_stream_before_after_contract",
            "test_ai_complex_expression_window_and_xz_contract",
            "test_ai_complex_counter_statistics_contract",
            "test_ai_complex_changes_statistics_anomaly_and_handshake_contract",
        ],
        "actions": [
            "counter.statistics", "event.config.list", "event.config.load", "event.export",
            "event.find", "expr.eval_at", "list.add", "list.create",
            "list.delete", "list.export", "list.first_change", "list.load",
            "list.show", "list.validate", "protocol.handshake.inspect",
            "scope.list", "signal.anomaly.inspect", "signal.changes",
            "signal.sampled_pulse.inspect", "signal.stability",
            "signal.statistics", "signal.xz_verify", "stream.config.load",
            "stream.query", "value.at", "verify.conditions", "waveform.cursor.set",
            "window.verify",
        ],
        "batch": "P3-A",
        "status": "semantic-equivalent",
        "rationale": (
            "锁定 8eec runtime 的同一 nonaxi oracle 已分别在原版 FSDB 与当前四态 FST 上完整通过；"
            "当前仓库另有逐 Action 回归，格式差异不改变公开可观察语义。"
        ),
        "evidence_scope": (
            "same locked original oracle on original FSDB and current FST; "
            "repository-local regression covers the closed differences"
        ),
    },
    "xdebug.stream_v1": {
        "fixtures": ["current.stream"],
        "tests": [
            "test_stream_query",
            "test_stream_query_stall_window",
            "test_stream_validate_packet_dynamic",
        ],
        "actions": [
            "stream.config.load", "stream.describe", "stream.query",
            "stream.validate", "stream.export",
        ],
        "batch": "P3-D",
    },
    "xdebug.stream_differential_tool": {
        "fixtures": ["current.stream"],
        "tests": ["test_stream_all_query_kinds"],
        "actions": ["stream.query", "stream.export"],
        "batch": "P3-D",
    },
    "xdebug.npi_fsdb_sva": {
        "fixtures": [],
        "tests": [],
        "actions": [
            "scope.list", "value.at", "signal.changes", "event.find",
        ],
        "batch": "P3-E",
        "status": "missing",
    },
    "xdebug.apb_vip": {
        "fixtures": ["current.apb"],
        "tests": [
            "test_apb_query", "test_apb_statistics",
            "test_apb_transaction_cursor", "test_apb_transfer_window",
        ],
        "actions": [
            "apb.config.load", "apb.query", "apb.statistics",
            "apb.transaction.cursor", "apb.transfer_window",
        ],
        "batch": "P3-D",
    },
    "xdebug.apb_xamba_vip": {
        "fixtures": ["current.apb"],
        "tests": ["test_apb_query", "test_apb_statistics"],
        "actions": ["apb.query", "apb.statistics"],
        "batch": "P3-D",
    },
    "xdebug.axi_vip": {
        "fixtures": ["current.axi"],
        "tests": [
            "test_axi_query", "test_axi_analysis", "test_axi_statistics",
            "test_axi_outstanding_timeline", "test_axi_request_response_pair",
        ],
        "actions": [
            "axi.config.load", "axi.query", "axi.analysis", "axi.statistics",
            "axi.channel_stall", "axi.latency_outlier",
            "axi.outstanding_timeline", "axi.request_response_pair",
        ],
        "batch": "P3-D",
    },
    "xdebug.axi_xamba_vip": {
        "fixtures": ["current.axi"],
        "tests": ["test_axi_query", "test_axi_analysis", "test_axi_statistics"],
        "actions": ["axi.query", "axi.analysis", "axi.statistics"],
        "batch": "P3-D",
    },
    "xdebug.design_uart": {
        "fixtures": ["current.counter", "current.output_mixed"],
        "tests": [
            "test_batch_aggregates_responses",
            "test_expr_normalize_contract",
            "test_expr_normalize_parse_error",
            "test_signal_canonicalize_port_connection",
            "test_signal_resolve_contract",
            "test_trace_driver_contract_and_role_filter",
        ],
        "actions": [
            "batch", "expr.normalize", "signal.canonicalize",
            "signal.resolve", "trace.driver",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "锁定 UART DesignDB runner 通过；当前以同一公开设计 Action 和完整响应合同"
            "完成归一映射，不比较不同设计数据库二进制"
        ),
    },
    "xdebug.design_p3": {
        "fixtures": [],
        "tests": ["test_design_p3_unobservable_proof_is_hash_anchored"],
        "actions": ["session.open"],
        "batch": "P3-B",
        "evidence_scope": (
            "Goal-start 与锁定 run_semantics 只对 P3 DesignDB 执行 session.open，"
            "没有任何 P3 语义查询；由 runner 哈希静态门禁证明不可观察"
        ),
    },
    "xdebug.design_hierarchy": {
        "fixtures": [],
        "tests": [
            "test_design_hierarchy_unobservable_proof_matches_locked_schema",
        ],
        "actions": ["scope.list"],
        "batch": "P3-B",
        "evidence_scope": (
            "Goal-start hierarchy test 不存在于锁定 runtime；所需 kind/data groups 又被"
            "冻结 scope.list schema 排除，由静态合同门禁证明不可观察"
        ),
    },
    "xdebug.active_trace_runner": {
        "fixtures": ["current.counter"],
        "tests": ["test_trace_active_driver_chain"],
        "actions": ["trace.active_driver_chain"],
        "batch": "P3-C",
    },
    "xdebug.xif_event": {
        "fixtures": [],
        "tests": ["test_value_at_preserves_event_kind"],
        "actions": ["value.at", "event.find", "event.export"],
        "batch": "P3-E",
    },
}

ACTIVE_CANDIDATES = {
    "p0": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_p0_oracle_is_locked_complete_and_sanitized",
            "test_p3c_p0_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_p0_matches_locked_native_chain_semantics",
            "test_p3c_p0_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "六份原版 RTL 字节相同；锁定 native NPI oracle 与当前原始 FST/"
            "binary-v1 DesignDB 按 hop、时间、值、候选、终止和完整性逐项通过"
        ),
    },
    "composite": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_composite_oracle_is_locked_complete_and_sanitized",
            "test_p3c_composite_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_composite_matches_locked_native_chain_semantics",
            "test_p3c_composite_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "二十份原版 case RTL 与共享 DUT 字节相同；锁定 native NPI oracle 与"
            "当前原始 FST/binary-v1 DesignDB 按完整复合链逐项通过"
        ),
    },
    "timing": {
        "fixtures": ["current.counter"],
        "tests": [
            "test_trace_active_driver_chain_propagates_through_nba_active_time",
            "test_trace_active_driver_chain_uses_changed_event_time",
        ],
    },
    "phase4": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_phase4_oracle_is_locked_complete_and_sanitized",
            "test_p3c_phase4_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_phase4_matches_locked_native_chain_semantics",
            "test_p3c_phase4_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "二十份原版 case RTL 及两份共享 DUT 字节相同；锁定 native NPI oracle 与"
            "当前原始 FST/binary-v1 DesignDB 按完整复合链逐项通过"
        ),
    },
    "phase5": {
        "fixtures": ["current.phase5"],
        "tests": ["test_trace_active_driver_chain_matches_original_phase5_public_semantics"],
    },
}

ACTIVE_FIXTURE = {
    "p0": "xdebug.active_trace_p0",
    "composite": "xdebug.active_trace_composite",
    "timing": "xdebug.active_trace_timing",
    "phase4": "xdebug.active_trace_phase4",
    "phase5": "xdebug.active_trace_phase5",
}


class MatrixError(RuntimeError):
    """Raised when the matrix cannot be built without losing evidence."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def ensure_within_repo(repo_root: Path, path: Path) -> Path:
    root = repo_root.resolve()
    resolved = path.resolve(strict=False)
    if resolved != root and root not in resolved.parents:
        raise MatrixError(f"output escapes the only writable repository: {path}")
    return resolved


def parse_scalar(value: str):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    if value == "true":
        return True
    if value == "false":
        return False
    if re.fullmatch(r"[0-9]+", value):
        return int(value)
    return value


def parse_inline_mapping(body: str) -> dict:
    fields = next(csv.reader([body], skipinitialspace=True))
    result = {}
    for field in fields:
        if ":" not in field:
            raise MatrixError(f"invalid inline catalog field: {field!r}")
        key, value = field.split(":", 1)
        result[key.strip()] = parse_scalar(value)
    return result


def parse_active_catalog(text: str) -> list[dict]:
    group = None
    rows = []
    for line_number, line in enumerate(text.splitlines(), 1):
        group_match = re.fullmatch(r"  ([a-z0-9_]+):", line)
        if group_match:
            group = group_match.group(1)
            continue
        item_match = re.fullmatch(r"    - \{(.*)\}", line)
        if item_match:
            if group is None:
                raise MatrixError(f"catalog item without group at line {line_number}")
            item = parse_inline_mapping(item_match.group(1))
            item["group"] = group
            item["catalog_line"] = line_number
            rows.append(item)
    expected = {"p0": 6, "composite": 20, "timing": 12, "phase4": 20, "phase5": 10}
    counts = Counter(row["group"] for row in rows)
    if dict(counts) != expected:
        raise MatrixError(f"active catalog count drift: {dict(counts)!r}")
    return rows


def parse_phase5_report_terminations(text: str) -> list[dict]:
    rows = []
    pattern = re.compile(
        r"^\| S(?P<scene>[0-9]+) \| (?P<target>[^|]+) \| "
        r"(?P<time>[^|]+) \| (?P<hops>[0-9]+) \| "
        r"(?P<termination>[a-z_]+) \|",
        re.MULTILINE,
    )
    lines = text.splitlines()
    for match in pattern.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        rows.append({
            "scene": int(match.group("scene")),
            "target": match.group("target").strip(),
            "time": match.group("time").strip(),
            "hops": int(match.group("hops")),
            "termination": match.group("termination"),
            "report_line": line,
        })
    if len(rows) != 10:
        raise MatrixError(f"phase5 report table drift: expected 10 rows, got {len(rows)}")
    return rows


def validate_phase5_runtime_audit(
    audit: dict,
    runtime_revision: str,
    schema_revision: str,
) -> dict[str, dict]:
    if audit.get("schema_version") != "xdebug.phase5-runtime-audit.v1":
        raise MatrixError("Phase5 runtime audit has the wrong schema_version")
    if audit.get("goal_id") != GOAL_ID:
        raise MatrixError("Phase5 runtime audit belongs to a different Goal")
    locked = audit.get("locked_original_runtime", {})
    if locked.get("git_revision") != runtime_revision:
        raise MatrixError("Phase5 runtime audit does not use the locked runtime revision")
    if locked.get("schema_revision") != schema_revision:
        raise MatrixError("Phase5 runtime audit does not use the locked schema revision")
    if locked.get("action_count") != 73:
        raise MatrixError("Phase5 runtime audit does not prove the 73-Action identity gate")
    if locked.get("fixture", {}).get("fixture_rebuilt") is not False:
        raise MatrixError("Phase5 runtime audit must reuse, not rebuild, the fixture cache")
    if locked.get("external_write_audit", {}).get("unchanged") is not True:
        raise MatrixError("Phase5 runtime audit does not prove zero external writes")

    rows = audit.get("scene_results")
    if not isinstance(rows, list) or len(rows) != 10:
        raise MatrixError("Phase5 runtime audit must contain exactly ten scenes")
    result = {}
    for index, row in enumerate(rows, 1):
        scenario_id = f"active.phase5.{index:02d}"
        if row.get("scenario_id") != scenario_id:
            raise MatrixError(f"Phase5 runtime audit scene order drift at {scenario_id}")
        if row.get("status") != "partial" or row.get("p3_batch") != "P3-C":
            raise MatrixError(f"Phase5 runtime audit closes {scenario_id} prematurely")
        locked_result = row.get("locked_runtime", {})
        if (
            locked_result.get("scan_complete") is not True
            or locked_result.get("analysis_complete") is not True
            or locked_result.get("response_truncated") is not False
        ):
            raise MatrixError(f"Phase5 locked response is incomplete: {scenario_id}")
        if scenario_id in result:
            raise MatrixError(f"duplicate Phase5 runtime audit scene: {scenario_id}")
        result[scenario_id] = row
    verdict = audit.get("verdict", {})
    if (
        verdict.get("status") != "partial"
        or verdict.get("termination_and_ambiguity_subset_equivalent_scene_count") != 10
        or verdict.get("full_response_equivalent_scene_count") != 0
    ):
        raise MatrixError("Phase5 runtime audit verdict drifted")
    return result


def validate_p3c_p0_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C P0 oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "p0":
        raise MatrixError("P3-C P0 oracle belongs to a different Goal/group")
    locked = oracle.get("locked_runtime", {})
    if locked != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_P0_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError("P3-C P0 locked native runtime/cache identity drifted")
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C P0 write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 6
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C P0 catalog identity drifted")

    rows = oracle.get("rows")
    expected_results = [
        ("ambiguous", 3),
        ("primary_input", 4),
        ("control_only", 1),
        ("control_only", 1),
        ("ambiguous", 1),
        ("primary_input", 1),
    ]
    if not isinstance(rows, list) or len(rows) != len(expected_results):
        raise MatrixError("P3-C P0 oracle must contain exactly six rows")

    digest_pattern = re.compile(r"[0-9a-f]{64}")
    result = {}
    has_source_less_control = False
    has_control_only_candidates = False
    for ordinal, (row, expected_result) in enumerate(
            zip(rows, expected_results), 1):
        scenario_id = f"active.p0.{ordinal:02d}"
        case = row.get("case")
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or not isinstance(case, str)
            or not case
        ):
            raise MatrixError(f"P3-C P0 row identity drifted: {scenario_id}")

        original_path = (
            "xdebug/tests/active_trace_chain/p0_composability/"
            f"{case}/tb.sv"
        )
        current_path = f"testdata/fixtures/active_trace/rtl/p0/{case}/tb.sv"
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != 1:
            raise MatrixError(f"P3-C P0 RTL mirror inventory drifted: {scenario_id}")
        mirror = mirrors[0]
        original_asset = original_assets.get(original_path)
        current_asset = current_assets.get(current_path)
        if (
            mirror.get("byte_identical") is not True
            or mirror.get("original_path") != original_path
            or mirror.get("current_path") != current_path
            or original_asset is None
            or current_asset is None
            or mirror.get("sha256") != original_asset["sha256"]
            or current_asset["sha256"] != original_asset["sha256"]
            or mirror.get("size") != original_asset["size_bytes"]
            or current_asset["size_bytes"] != original_asset["size_bytes"]
        ):
            raise MatrixError(f"P3-C P0 RTL mirror drifted: {scenario_id}")

        fixture_root = f"testdata/fixtures/active_trace/p0/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(f"P3-C P0 fixture lock is missing: {scenario_id}")
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            current_path,
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(f"P3-C P0 fixture lock inventory drifted: {scenario_id}")
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C P0 fixture evidence drifted: {scenario_id}: {path}"
                )

        request = row.get("request", {})
        if (
            not isinstance(request.get("signal"), str)
            or not isinstance(request.get("time"), str)
            or request.get("stop_on_temporal") is not False
        ):
            raise MatrixError(f"P3-C P0 request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(f"P3-C P0 original fixture proof drifted: {scenario_id}")

        native = row.get("native_result", {})
        termination, hop_count = expected_result
        chain = native.get("chain")
        if (
            native.get("termination") != termination
            or native.get("total_hops") != hop_count
            or not isinstance(chain, list)
            or len(chain) != hop_count
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or native.get("temporal_boundaries") != sum(
                hop.get("hop_type") == "temporal_boundary" for hop in chain
            )
        ):
            raise MatrixError(f"P3-C P0 native result is incomplete: {scenario_id}")
        has_source_less_control |= any(
            hop.get("file") == "" and hop.get("line") == 0 for hop in chain
        )
        branches = native.get("branch_evidence")
        if not isinstance(branches, list):
            raise MatrixError(f"P3-C P0 branch evidence drifted: {scenario_id}")
        has_control_only_candidates |= termination == "control_only" and bool(branches)
        result[scenario_id] = row

    if not has_source_less_control or not has_control_only_candidates:
        raise MatrixError("P3-C P0 frozen schema representability boundaries disappeared")
    return result


def validate_p3c_composite_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C composite oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "composite":
        raise MatrixError(
            "P3-C composite oracle belongs to a different Goal/group"
        )
    if oracle.get("locked_runtime") != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_COMPOSITE_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError(
            "P3-C composite locked native runtime/cache identity drifted"
        )
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C composite write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 20
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C composite catalog identity drifted")

    expected_results = [
        ("primary_input", 11), ("primary_input", 11),
        ("primary_input", 11), ("primary_input", 12),
        ("primary_input", 12), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 7),
        ("primary_input", 6), ("primary_input", 6),
        ("ambiguous", 7), ("primary_input", 7),
        ("primary_input", 7), ("primary_input", 6),
    ]
    rows = oracle.get("rows")
    if not isinstance(rows, list) or len(rows) != len(expected_results):
        raise MatrixError("P3-C composite oracle must contain exactly twenty rows")

    digest_pattern = re.compile(r"[0-9a-f]{64}")
    expected_before = ["1", "0", "1", "0", "0", "1", "0", "1"]
    expected_after = ["0", "1", "0", "1", "1", "0", "1", "0"]
    result = {}
    source_less_hops = 0
    for ordinal, (row, expected_result) in enumerate(
            zip(rows, expected_results), 1):
        scenario_id = f"active.composite.{ordinal:02d}"
        case = f"case_{ordinal:02d}"
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or row.get("case") != case
        ):
            raise MatrixError(
                f"P3-C composite row identity drifted: {scenario_id}"
            )

        mirror_paths = [
            (
                f"xdebug/tests/active_trace_chain/composite/{case}/tb.sv",
                f"testdata/fixtures/active_trace/rtl/composite/{case}/tb.sv",
            ),
            (
                "xdebug/tests/active_trace_chain/composite/chain_dut.sv",
                "testdata/fixtures/active_trace/rtl/composite/chain_dut.sv",
            ),
        ]
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != len(mirror_paths):
            raise MatrixError(
                f"P3-C composite RTL mirror inventory drifted: {scenario_id}"
            )
        by_paths = {
            (mirror.get("original_path"), mirror.get("current_path")): mirror
            for mirror in mirrors
        }
        for original_path, current_path in mirror_paths:
            mirror = by_paths.get((original_path, current_path), {})
            original_asset = original_assets.get(original_path)
            current_asset = current_assets.get(current_path)
            if (
                mirror.get("byte_identical") is not True
                or original_asset is None
                or current_asset is None
                or mirror.get("sha256") != original_asset["sha256"]
                or current_asset["sha256"] != original_asset["sha256"]
                or mirror.get("size") != original_asset["size_bytes"]
                or current_asset["size_bytes"] != original_asset["size_bytes"]
            ):
                raise MatrixError(
                    f"P3-C composite RTL mirror drifted: {scenario_id}"
                )

        fixture_root = f"testdata/fixtures/active_trace/composite/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(
                f"P3-C composite fixture lock is missing: {scenario_id}"
            )
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            *(current_path for _, current_path in mirror_paths),
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(
                f"P3-C composite fixture lock inventory drifted: {scenario_id}"
            )
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C composite fixture evidence drifted: "
                    f"{scenario_id}: {path}"
                )

        request = row.get("request", {})
        if (
            request != {
                "signal": "top.data_out",
                "stop_on_temporal": False,
                "time": "50ns",
            }
        ):
            raise MatrixError(f"P3-C composite request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(
                f"P3-C composite original fixture proof drifted: {scenario_id}"
            )

        native = row.get("native_result", {})
        termination, hop_count = expected_result
        chain = native.get("chain")
        if (
            native.get("termination") != termination
            or native.get("total_hops") != hop_count
            or native.get("active_trace_calls") != hop_count
            or not isinstance(chain, list)
            or len(chain) != hop_count
            or native.get("temporal_boundaries") != 1
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or not all(hop.get("value_known") is True for hop in chain)
            or row.get("catalog_expectation") != {
                "hops": hop_count,
                "temporal_boundaries": 1,
                "termination": termination,
            }
        ):
            raise MatrixError(
                f"P3-C composite native result is incomplete: {scenario_id}"
            )
        source_less_hops += sum(
            hop.get("file") == "" and hop.get("line") == 0 for hop in chain
        )

        branches = native.get("branch_evidence")
        if not isinstance(branches, list):
            raise MatrixError(
                f"P3-C composite branch evidence drifted: {scenario_id}"
            )
        if termination == "ambiguous":
            branch = branches[0] if len(branches) == 1 else {}
            candidates = branch.get("candidates", [])
            expected_names = [
                "top.u_dut.g_g.u_gen.in[" + str(index) + "]"
                for index in range(8)
            ]
            if (
                branch.get("signal") != "top.u_dut.gen_out"
                or branch.get("time") != "35.0n"
                or branch.get("reason") != "8 signals toggled simultaneously"
                or [item.get("name") for item in candidates] != expected_names
                or [item.get("role") for item in candidates] != ["data"] * 8
                or [item.get("toggled") for item in candidates] != [True] * 8
                or [item.get("before") for item in candidates] != expected_before
                or [item.get("after") for item in candidates] != expected_after
            ):
                raise MatrixError(
                    f"P3-C composite branch evidence drifted: {scenario_id}"
                )
        elif branches:
            raise MatrixError(
                f"P3-C composite primary-input row has branch evidence: "
                f"{scenario_id}"
            )
        result[scenario_id] = row

    if source_less_hops != 5:
        raise MatrixError("P3-C composite source-less endpoint count drifted")
    return result


def validate_p3c_phase4_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C Phase4 oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "phase4":
        raise MatrixError("P3-C Phase4 oracle belongs to a different Goal/group")
    if oracle.get("locked_runtime") != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_PHASE4_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError("P3-C Phase4 locked native runtime/cache identity drifted")
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C Phase4 write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 20
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C Phase4 catalog identity drifted")

    expected_results = [
        ("primary_input", 16), ("primary_input", 16),
        ("primary_input", 16), ("primary_input", 17),
        ("primary_input", 16), ("ambiguous", 11),
        ("ambiguous", 11), ("ambiguous", 11),
        ("ambiguous", 10), ("ambiguous", 11),
        ("ambiguous", 11), ("ambiguous", 11),
        ("ambiguous", 11), ("ambiguous", 12),
        ("primary_input", 11), ("primary_input", 11),
        ("ambiguous", 11), ("primary_input", 12),
        ("primary_input", 12), ("primary_input", 11),
    ]
    rows = oracle.get("rows")
    if not isinstance(rows, list) or len(rows) != len(expected_results):
        raise MatrixError("P3-C Phase4 oracle must contain exactly twenty rows")

    digest_pattern = re.compile(r"[0-9a-f]{64}")
    expected_toggles = [True, False, True, False, False, True, False, True]
    result = {}
    source_less_hops = 0
    for ordinal, (row, expected_result) in enumerate(
            zip(rows, expected_results), 1):
        scenario_id = f"active.phase4.{ordinal:02d}"
        case = f"case_{ordinal:02d}"
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or row.get("case") != case
        ):
            raise MatrixError(f"P3-C Phase4 row identity drifted: {scenario_id}")

        mirror_paths = [
            (
                f"xdebug/tests/active_trace_chain/phase4/{case}/tb.sv",
                f"testdata/fixtures/active_trace/rtl/phase4/{case}/tb.sv",
            ),
            (
                "xdebug/tests/active_trace_chain/phase4/phase4_dut.sv",
                "testdata/fixtures/active_trace/rtl/phase4/phase4_dut.sv",
            ),
            (
                "xdebug/tests/active_trace_chain/composite/chain_dut.sv",
                "testdata/fixtures/active_trace/rtl/composite/chain_dut.sv",
            ),
        ]
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != len(mirror_paths):
            raise MatrixError(
                f"P3-C Phase4 RTL mirror inventory drifted: {scenario_id}"
            )
        by_paths = {
            (mirror.get("original_path"), mirror.get("current_path")): mirror
            for mirror in mirrors
        }
        for original_path, current_path in mirror_paths:
            mirror = by_paths.get((original_path, current_path), {})
            original_asset = original_assets.get(original_path)
            current_asset = current_assets.get(current_path)
            if (
                mirror.get("byte_identical") is not True
                or original_asset is None
                or current_asset is None
                or mirror.get("sha256") != original_asset["sha256"]
                or current_asset["sha256"] != original_asset["sha256"]
                or mirror.get("size") != original_asset["size_bytes"]
                or current_asset["size_bytes"] != original_asset["size_bytes"]
            ):
                raise MatrixError(
                    f"P3-C Phase4 RTL mirror drifted: {scenario_id}"
                )

        fixture_root = f"testdata/fixtures/active_trace/phase4/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(
                f"P3-C Phase4 fixture lock is missing: {scenario_id}"
            )
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            *(current_path for _, current_path in mirror_paths),
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(
                f"P3-C Phase4 fixture lock inventory drifted: {scenario_id}"
            )
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C Phase4 fixture evidence drifted: {scenario_id}: {path}"
                )

        request = row.get("request", {})
        if (
            not isinstance(request.get("signal"), str)
            or not isinstance(request.get("time"), str)
            or request.get("stop_on_temporal") is not False
        ):
            raise MatrixError(f"P3-C Phase4 request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(
                f"P3-C Phase4 original fixture proof drifted: {scenario_id}"
            )

        native = row.get("native_result", {})
        termination, hop_count = expected_result
        chain = native.get("chain")
        if (
            native.get("termination") != termination
            or native.get("total_hops") != hop_count
            or native.get("active_trace_calls") != hop_count
            or not isinstance(chain, list)
            or len(chain) != hop_count
            or native.get("temporal_boundaries") != 2
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or not all(hop.get("value_known") is True for hop in chain)
            or row.get("catalog_expectation") != {
                "hops": hop_count,
                "temporal_boundaries": 2,
                "termination": termination,
            }
        ):
            raise MatrixError(
                f"P3-C Phase4 native result is incomplete: {scenario_id}"
            )
        source_less_hops += sum(
            hop.get("file") == "" and hop.get("line") == 0 for hop in chain
        )

        branches = native.get("branch_evidence")
        if not isinstance(branches, list):
            raise MatrixError(
                f"P3-C Phase4 branch evidence drifted: {scenario_id}"
        )
        if termination == "ambiguous":
            branch = branches[0] if len(branches) == 1 else {}
            candidates = branch.get("candidates", [])
            expected_names = [
                "top.u_dut.u_pre.g_g.u_gen.in[" + str(index) + "]"
                for index in range(8)
            ]
            if (
                branch.get("signal") != "top.u_dut.u_pre.gen_out"
                or branch.get("time") != "15.0n"
                or branch.get("reason") != "4 signals toggled simultaneously"
                or [item.get("name") for item in candidates] != expected_names
                or [item.get("role") for item in candidates] != ["data"] * 8
                or [item.get("toggled") for item in candidates] != expected_toggles
                or [item.get("before") for item in candidates] != ["0"] * 8
                or [item.get("after") for item in candidates] != [
                    "1" if toggled else "0" for toggled in expected_toggles
                ]
            ):
                raise MatrixError(
                    f"P3-C Phase4 branch evidence drifted: {scenario_id}"
                )
        elif branches:
            raise MatrixError(
                f"P3-C Phase4 primary-input row has branch evidence: {scenario_id}"
            )
        result[scenario_id] = row

    if source_less_hops != 5:
        raise MatrixError("P3-C Phase4 source-less endpoint count drifted")
    return result


def validate_ai_complex_runtime_audit(
    audit: dict,
    runtime_revision: str,
    schema_revision: str,
) -> None:
    if audit.get("schema_version") != "xdebug.ai-complex-runtime-audit.v1":
        raise MatrixError("ai_complex runtime audit has the wrong schema_version")
    if audit.get("goal_id") != GOAL_ID:
        raise MatrixError("ai_complex runtime audit belongs to a different Goal")
    locked = audit.get("locked_original_runtime", {})
    if locked.get("git_revision") != runtime_revision:
        raise MatrixError("ai_complex audit does not use the locked runtime revision")
    if locked.get("schema_revision") != schema_revision:
        raise MatrixError("ai_complex audit does not use the locked schema revision")
    if locked.get("action_count") != 73:
        raise MatrixError("ai_complex audit does not prove the 73-Action identity gate")
    runner = locked.get("runner", {})
    if (
        runner.get("sha256") != AI_COMPLEX_RUNNER_SHA256
        or runner.get("mode") != "nonaxi"
    ):
        raise MatrixError("ai_complex audit does not use the locked nonaxi oracle")
    counter_runner = locked.get("counter_runner", {})
    if counter_runner.get("sha256") != AI_COMPLEX_COUNTER_RUNNER_SHA256:
        raise MatrixError("ai_complex audit does not use the locked counter oracle")
    original_fixture = locked.get("fixture", {})
    if (
        original_fixture.get("cache_reused") is not True
        or original_fixture.get("fixture_rebuilt") is not False
        or original_fixture.get("source_access") != "read_only"
    ):
        raise MatrixError("ai_complex audit did not reuse the original fixture read-only")
    original_gate = locked.get("suite_gate", {})
    original_counter_gate = locked.get("counter_suite_gate", {})
    current = audit.get("current_runtime", {})
    current_gate = current.get("locked_suite_gate", {})
    current_counter_gate = current.get("locked_counter_suite_gate", {})
    if (
        original_gate.get("result") != "passed"
        or original_gate.get("session_closed_gracefully") is not True
        or original_counter_gate.get("result") != "passed"
        or original_counter_gate.get("session_closed_gracefully") is not True
        or current_gate.get("result") != "passed"
        or current_gate.get("same_runner_sha256") != runner.get("sha256")
        or current_gate.get("same_mode") != runner.get("mode")
        or current_gate.get("session_closed_gracefully") is not True
        or current_counter_gate.get("result") != "passed"
        or current_counter_gate.get("same_runner_sha256") !=
            counter_runner.get("sha256")
        or current_counter_gate.get("session_closed_gracefully") is not True
    ):
        raise MatrixError("ai_complex two-sided locked suite gate is incomplete")
    current_fixture = current.get("fixture", {})
    if (
        current_fixture.get("deterministic_second_build") is not True
        or current_fixture.get("vcd_or_json_conversion_used") is not False
    ):
        raise MatrixError("ai_complex FST regeneration contract is incomplete")
    observable = audit.get("observable_contract", {})
    if (
        observable.get("same_locked_oracle_passed_both_sides") is not True
        or observable.get("four_state_x_and_z_preserved") is not True
        or observable.get("binary_waveform_comparison_used") is not False
    ):
        raise MatrixError("ai_complex public observable comparison drifted")
    boundary = audit.get("write_boundary_audit", {})
    if (
        boundary.get("only_writable_repository") != "xdebug_fst"
        or boundary.get("external_inputs_read_only") is not True
        or boundary.get("fallback_used") is not False
    ):
        raise MatrixError("ai_complex write/fallback boundary is not proven")
    for repository in ("original_xverif", "wellen", "verilator"):
        if boundary.get(f"{repository}_snapshot_sha256_before") != \
                boundary.get(f"{repository}_snapshot_sha256_after"):
            raise MatrixError(f"ai_complex audit changed external {repository}")
    verdict = audit.get("verdict", {})
    if (
        verdict.get("scenario_id") != "fixture.ai_complex_wave"
        or verdict.get("status") != "semantic-equivalent"
        or verdict.get("p3_batch") != "P3-A"
        or verdict.get("remaining_observable_gap_count") != 0
    ):
        raise MatrixError("ai_complex runtime audit verdict drifted")


def validate_p3b_runtime_audit(
    audit: dict,
    runtime_revision: str,
    schema_revision: str,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if audit.get("schema_version") != "xdebug.p3b-runtime-audit.v1":
        raise MatrixError("P3-B runtime audit has the wrong schema_version")
    if audit.get("goal_id") != GOAL_ID:
        raise MatrixError("P3-B runtime audit belongs to a different Goal")
    policy = audit.get("authority_policy", {})
    if (
        policy.get("behavior_authority") != "locked_original_runtime"
        or policy.get("asset_authority") != "goal_start_original_assets"
        or policy.get("authority_conflicts_preserved") is not True
        or policy.get("binary_waveform_comparison_used") is not False
    ):
        raise MatrixError("P3-B audit collapsed or changed the two original authorities")

    locked = audit.get("locked_original_runtime", {})
    if locked.get("git_revision") != runtime_revision:
        raise MatrixError("P3-B audit does not use the locked runtime revision")
    if locked.get("schema_revision") != schema_revision:
        raise MatrixError("P3-B audit does not use the locked schema revision")
    if locked.get("action_count") != 73:
        raise MatrixError("P3-B audit does not prove the 73-Action identity gate")
    if (
        locked.get("source_access") != "read_only"
        or locked.get("fixture_cache_reused") is not True
        or locked.get("fixture_rebuilt") is not False
    ):
        raise MatrixError("P3-B original fixture/cache policy drifted")

    gates = {
        item.get("gate_id"): item
        for item in locked.get("runner_gates", [])
    }
    expected_gates = {
        "active_driver_and_interface": 10,
        "active_semantics": 1,
        "active_zero_evidence": 16,
        "trace_x_xprop": 1,
        "design_semantics": 1,
    }
    if set(gates) != set(expected_gates):
        raise MatrixError("P3-B original runner gate inventory drifted")
    for gate_id, passed in expected_gates.items():
        gate = gates[gate_id]
        if (
            gate.get("result") != "passed"
            or gate.get("passed") != passed
            or gate.get("failed") != 0
            or gate.get("skipped") != 0
            or gate.get("session_closed_gracefully") is not True
        ):
            raise MatrixError(f"P3-B original runner gate is incomplete: {gate_id}")
        path = gate.get("relative_path")
        asset = original_assets.get(path)
        if asset is None or asset["sha256"] != gate.get("goal_start_asset_sha256"):
            raise MatrixError(f"P3-B Goal-start runner evidence drifted: {path}")
        if "transitive_runner_relative_path" in gate:
            transitive = gate["transitive_runner_relative_path"]
            transitive_asset = original_assets.get(transitive)
            if (
                transitive_asset is None
                or transitive_asset["sha256"] !=
                    gate.get("goal_start_transitive_runner_sha256")
            ):
                raise MatrixError(
                    f"P3-B Goal-start transitive runner drifted: {transitive}"
                )

    caches = locked.get("fixture_caches", [])
    expected_caches = {
        "xdebug.active_driver", "xdebug.interface_port_root",
        "xdebug.active_semantics", "xdebug.active_zero_evidence",
        "xdebug.trace_x_xprop", "xdebug.design_uart", "xdebug.design_p3",
    }
    if {item.get("fixture_id") for item in caches} != expected_caches:
        raise MatrixError("P3-B locked fixture cache inventory drifted")
    digest_pattern = re.compile(r"[0-9a-f]{64}")
    for fixture in caches:
        fingerprint = fixture.get("cache_fingerprint", "")
        if (
            not digest_pattern.fullmatch(fingerprint)
            or not fixture.get("cache_version", "").startswith(
                fingerprint + "-prepare-"
            )
            or not digest_pattern.fullmatch(fixture.get("manifest_sha256", ""))
            or fixture.get("tool_identity") != "X-2025.06"
        ):
            raise MatrixError(
                f"P3-B cache evidence is incomplete: {fixture.get('fixture_id')}"
            )

    current = audit.get("current_runtime", {})
    generation = current.get("fixture_generation", {})
    if (
        current.get("implementation_commit") != "b4b810e"
        or generation.get("deterministic_second_build") is not True
        or generation.get("vcd_or_json_conversion_used") is not False
        or generation.get("fallback_used") is not False
    ):
        raise MatrixError("P3-B current fixture generation contract drifted")
    for path_key, hash_key in (
        ("script_path", "script_sha256"),
        ("verilator_patch_path", "verilator_patch_sha256"),
    ):
        path = generation.get(path_key)
        expected_hash = generation.get(hash_key)
        if (
            not isinstance(path, str)
            or not isinstance(expected_hash, str)
            or sha256_bytes((repo_root / path).read_bytes()) != expected_hash
        ):
            raise MatrixError(f"P3-B fixture generator evidence drifted: {path}")

    fixture_ids = set()
    for fixture in current.get("fixtures", []):
        fixture_id = fixture.get("fixture_id")
        if not isinstance(fixture_id, str) or fixture_id in fixture_ids:
            raise MatrixError("P3-B current fixture identity is missing or duplicated")
        fixture_ids.add(fixture_id)
        for path_key, hash_key in (
            ("rtl_path", "rtl_sha256"),
            ("harness_path", "harness_sha256"),
            ("design_db_path", "design_db_sha256"),
            ("fst_path", "fst_sha256"),
            ("hash_record_path", "hash_record_sha256"),
        ):
            if path_key not in fixture:
                continue
            path = fixture[path_key]
            asset = current_assets.get(path)
            if asset is None or asset["sha256"] != fixture.get(hash_key):
                raise MatrixError(f"P3-B current fixture evidence drifted: {path}")
            validate_frozen_file(repo_root, asset)

    repository_gates = {
        item.get("gate_id"): item
        for item in current.get("repository_gates", [])
    }
    if set(repository_gates) != {
        "ported_original_active_oracles", "design_contracts",
        "adjacent_regression", "static_differential_contracts", "ctest",
    }:
        raise MatrixError("P3-B current repository gate inventory drifted")
    expected_repository_passes = {
        "ported_original_active_oracles": 18,
        "design_contracts": 13,
        "adjacent_regression": 146,
        "static_differential_contracts": 27,
        "ctest": 7,
    }
    for gate_id, passed in expected_repository_passes.items():
        gate = repository_gates[gate_id]
        if gate.get("passed") != passed or gate.get("failed") != 0:
            raise MatrixError(f"P3-B current repository gate failed: {gate_id}")
        if "path" in gate:
            asset = current_assets.get(gate["path"])
            if asset is None or asset["sha256"] != gate.get("sha256"):
                raise MatrixError(f"P3-B current test evidence drifted: {gate['path']}")

    rows = audit.get("comparisons")
    if not isinstance(rows, list) or len(rows) != 8:
        raise MatrixError("P3-B audit must contain exactly eight fixture comparisons")
    row_map = {}
    for row in rows:
        scenario_id = row.get("scenario_id")
        if scenario_id in row_map:
            raise MatrixError(f"duplicate P3-B scenario: {scenario_id}")
        if (
            row.get("p3_batch") != "P3-B"
            or row.get("status") not in {
                "semantic-equivalent", "proven-unobservable",
            }
            or row.get("remaining_observable_gap_count") != 0
        ):
            raise MatrixError(f"P3-B scenario is not closed: {scenario_id}")
        if row.get("status") == "semantic-equivalent":
            if row.get("same_locked_oracle_executable_used_on_current") is not False:
                raise MatrixError(f"P3-B overclaims original runner reuse: {scenario_id}")
            if not (
                row.get("ported_public_oracle_complete") is True
                or row.get("normalized_contract_mapping_complete") is True
            ):
                raise MatrixError(f"P3-B observable mapping is incomplete: {scenario_id}")
        row_map[scenario_id] = row

    expected_rows = {
        "fixture.active_driver", "fixture.active_semantics",
        "fixture.active_zero_evidence", "fixture.interface_port_root",
        "fixture.trace_x_xprop", "fixture.design_uart",
        "fixture.design_p3", "fixture.design_hierarchy",
    }
    if set(row_map) != expected_rows:
        raise MatrixError("P3-B scenario inventory drifted")
    counts = Counter(row["status"] for row in rows)
    if counts != {"semantic-equivalent": 6, "proven-unobservable": 2}:
        raise MatrixError("P3-B status count drifted")

    for scenario_id in (
        "fixture.active_driver", "fixture.interface_port_root",
        "fixture.active_zero_evidence",
    ):
        row = row_map[scenario_id]
        original_asset = original_assets.get(row.get("original_rtl_path"))
        current_asset = current_assets.get(row.get("current_rtl_path"))
        if (
            row.get("comparison_method") != "exact_rtl_and_ported_public_oracle"
            or row.get("exact_rtl_match") is not True
            or original_asset is None
            or current_asset is None
            or original_asset["sha256"] != row.get("original_rtl_sha256")
            or current_asset["sha256"] != row.get("current_rtl_sha256")
            or row.get("original_rtl_sha256") != row.get("current_rtl_sha256")
        ):
            raise MatrixError(f"P3-B exact RTL proof drifted: {scenario_id}")

    hierarchy = row_map["fixture.design_hierarchy"]
    request_schema = json.loads((
        repo_root / "compat/xdebug-v1/schemas/v1/actions/"
        "scope.list.request.schema.json"
    ).read_text(encoding="utf-8"))
    response_schema = json.loads((
        repo_root / "compat/xdebug-v1/schemas/v1/actions/"
        "scope.list.response.schema.json"
    ).read_text(encoding="utf-8"))
    locked_kinds = request_schema["properties"]["args"]["properties"]["kind"][
        "enum"
    ]
    locked_groups = sorted(response_schema["$defs"]["successData"]["properties"])
    hierarchy_asset = original_assets.get(hierarchy.get("goal_start_test_path"))
    if (
        hierarchy.get("comparison_method") !=
            "locked_runtime_and_schema_negative_proof"
        or hierarchy.get("test_present_at_locked_runtime") is not False
        or hierarchy.get("locked_scope_list_kind_enum") != locked_kinds
        or hierarchy.get("locked_scope_list_data_groups") != locked_groups
        or hierarchy_asset is None
        or hierarchy_asset["sha256"] != hierarchy.get("goal_start_test_sha256")
        or not set(
            hierarchy.get("goal_start_requested_unsupported_kinds", [])
        ).isdisjoint(locked_kinds)
        or not set(
            hierarchy.get("goal_start_requested_unsupported_groups", [])
        ).isdisjoint(locked_groups)
    ):
        raise MatrixError("P3-B design hierarchy negative schema proof drifted")

    design_p3 = row_map["fixture.design_p3"]
    p3_runner = original_assets.get(design_p3.get("goal_start_runner_path"))
    if (
        design_p3.get("comparison_method") !=
            "goal_start_and_locked_runner_request_inventory"
        or design_p3.get("public_actions") != ["session.open"]
        or design_p3.get("p3_session_opened") is not True
        or design_p3.get("p3_semantic_query_count") != 0
        or p3_runner is None
        or p3_runner["sha256"] != design_p3.get("goal_start_runner_sha256")
    ):
        raise MatrixError("P3-B design P3 unobservable proof drifted")

    boundary = audit.get("write_boundary_audit", {})
    if (
        boundary.get("only_writable_repository") != "xdebug_fst"
        or boundary.get("external_inputs_read_only") is not True
        or boundary.get("fallback_used") is not False
        or boundary.get(
            "all_runtime_home_tmp_socket_and_artifacts_repository_local"
        ) is not True
    ):
        raise MatrixError("P3-B write/fallback boundary is not proven")
    for repository in ("original_xverif", "wellen", "verilator"):
        if boundary.get(f"{repository}_snapshot_sha256_before") != \
                boundary.get(f"{repository}_snapshot_sha256_after"):
            raise MatrixError(f"P3-B audit changed external {repository}")

    def strings(value: object) -> Iterable[str]:
        if isinstance(value, str):
            yield value
        elif isinstance(value, dict):
            for key, item in value.items():
                yield from strings(key)
                yield from strings(item)
        elif isinstance(value, list):
            for item in value:
                yield from strings(item)

    if any(value.startswith("/") for value in strings(audit)):
        raise MatrixError("P3-B audit contains an absolute path")

    verdict = audit.get("verdict", {})
    if verdict != {
        "p3_batch": "P3-B",
        "scenario_count": 8,
        "semantic_equivalent_count": 6,
        "proven_unobservable_count": 2,
        "partial_count": 0,
        "missing_count": 0,
        "remaining_observable_gap_count": 0,
    }:
        raise MatrixError("P3-B runtime audit verdict drifted")
    return row_map


def scan_constructs(text: str) -> dict[str, list[int]]:
    found: dict[str, list[int]] = {}
    in_block_comment = False
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw
        if in_block_comment:
            if "*/" not in line:
                continue
            line = line.split("*/", 1)[1]
            in_block_comment = False
        while "/*" in line:
            before, after = line.split("/*", 1)
            if "*/" in after:
                line = before + " " + after.split("*/", 1)[1]
            else:
                line = before
                in_block_comment = True
                break
        line = line.split("//", 1)[0]
        if not line.strip():
            continue
        for name, pattern in CONSTRUCT_PATTERNS.items():
            if re.search(pattern, line, re.IGNORECASE):
                found.setdefault(name, []).append(line_number)
    return found


def first_content_line(text: str) -> int:
    for line_number, line in enumerate(text.splitlines(), 1):
        if line.strip() and not line.lstrip().startswith(("//", "/*", "*")):
            return line_number
    return 1


def file_line(path: Path, pattern: str) -> int:
    regex = re.compile(pattern)
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if regex.search(line):
            return line_number
    raise MatrixError(f"evidence pattern {pattern!r} not found in {path}")


def public_actions(repo_root: Path) -> list[str]:
    catalog = json.loads(
        (repo_root / "compat/xdebug-v1/catalog.response.json").read_text(encoding="utf-8")
    )
    actions = [
        item["name"] if isinstance(item, dict) else item
        for item in catalog["data"]["actions"]
    ]
    if len(actions) != 73 or len(set(actions)) != 73:
        raise MatrixError("frozen public Action catalog is not exactly 73 unique entries")
    return actions


def completeness_fields(schema: object, prefix: str = "") -> list[str]:
    names = {
        "analysis_complete", "scan_complete", "response_truncated",
        "total_count", "returned_count", "termination", "termination_detail",
        "omitted_count", "truncated", "complete",
    }
    result = set()
    if isinstance(schema, dict):
        properties = schema.get("properties")
        if isinstance(properties, dict):
            for key, value in properties.items():
                field = f"{prefix}.{key}" if prefix else key
                if key in names:
                    result.add(field)
                result.update(completeness_fields(value, field))
        for key in ("$defs", "definitions"):
            values = schema.get(key)
            if isinstance(values, dict):
                for value in values.values():
                    result.update(completeness_fields(value, prefix))
        for key in ("allOf", "anyOf", "oneOf"):
            values = schema.get(key)
            if isinstance(values, list):
                for value in values:
                    result.update(completeness_fields(value, prefix))
        if "items" in schema:
            result.update(completeness_fields(schema["items"], prefix))
    elif isinstance(schema, list):
        for item in schema:
            result.update(completeness_fields(item, prefix))
    return sorted(result)


def action_contract(repo_root: Path, action: str) -> dict:
    request_dir = repo_root / "compat/xdebug-v1/examples/requests"
    candidates = sorted(request_dir.glob(f"{action}.*.json"))
    if not candidates:
        raise MatrixError(f"no frozen request example for Action {action}")
    basic = request_dir / f"{action}.basic.json"
    request = basic if basic in candidates else candidates[0]
    response = repo_root / f"compat/xdebug-v1/schemas/v1/actions/{action}.response.schema.json"
    if not response.is_file():
        raise MatrixError(f"no frozen response schema for Action {action}")
    schema = json.loads(response.read_text(encoding="utf-8"))
    note_fields = set()
    notes = schema.get("x-output_notes", "")
    for path in re.findall(r"\b(?:summary|data)\.([A-Za-z0-9_]+)", notes):
        if path in {
            "analysis_complete", "scan_complete", "response_truncated",
            "total_count", "returned_count", "termination", "termination_detail",
            "omitted_count", "truncated", "complete", "truncation_scopes",
            "value_width_complete",
        }:
            note_fields.add(f"summary.{path}")
    fields = note_fields or set(completeness_fields(schema))
    return {
        "request_example": request.relative_to(repo_root).as_posix(),
        "response_schema": response.relative_to(repo_root).as_posix(),
        "completeness_fields": sorted(fields),
    }


def asset_lookup(manifest: dict, side: str) -> dict[str, dict]:
    return {
        asset["path"]: asset
        for asset in manifest["assets"]
        if asset["side"] == side
    }


def validate_frozen_file(root: Path, asset: dict) -> bytes:
    path = root / asset["path"]
    data = path.read_bytes()
    digest = sha256_bytes(data)
    if digest != asset["sha256"]:
        raise MatrixError(
            f"frozen asset content drift: {asset['path']} "
            f"expected {asset['sha256']}, got {digest}"
        )
    return data


def source_record(root: Path, asset: dict, *, scan: bool) -> dict:
    data = validate_frozen_file(root, asset)
    text = data.decode("utf-8", "replace")
    record = {
        "path": asset["path"],
        "sha256": asset["sha256"],
        "line_anchor": first_content_line(text),
        "line_count": len(text.splitlines()),
    }
    if scan:
        record["construct_lines"] = scan_constructs(text)
    return record


def index_current_tests(repo_root: Path) -> dict[str, dict]:
    result = {}
    pattern = re.compile(r"^def (test_[A-Za-z0-9_]+)\(", re.MULTILINE)
    for path in sorted((repo_root / "tests").rglob("*.py")):
        text = path.read_text(encoding="utf-8")
        digest = sha256_bytes(path.read_bytes())
        for match in pattern.finditer(text):
            name = match.group(1)
            if name in result:
                raise MatrixError(f"duplicate current test evidence name: {name}")
            result[name] = {
                "test": name,
                "path": path.relative_to(repo_root).as_posix(),
                "line": text.count("\n", 0, match.start()) + 1,
                "sha256": digest,
            }
    return result


def current_evidence(
    repo_root: Path,
    current_assets: dict[str, dict],
    tests: dict[str, dict],
    candidate: dict,
) -> dict:
    fixture_ids = candidate.get("fixtures", [])
    source_assets = [
        asset
        for asset in current_assets.values()
        if set(asset["fixture_ids"]) & set(fixture_ids)
        and asset["kind"] in {"rtl", "testbench", "waveform_fst", "waveform_vcd"}
    ]
    sources = [
        source_record(repo_root, asset, scan=asset["kind"] == "rtl")
        | {"kind": asset["kind"], "fixture_ids": asset["fixture_ids"]}
        for asset in sorted(source_assets, key=lambda item: item["path"])
    ]
    test_evidence = []
    for name in candidate.get("tests", []):
        if name not in tests:
            raise MatrixError(f"configured current test evidence does not exist: {name}")
        test_evidence.append(tests[name])
    return {
        "candidate_fixture_ids": fixture_ids,
        "candidate_sources": sources,
        "test_evidence": test_evidence,
        "evidence_scope": (
            candidate.get(
                "evidence_scope",
                "related capability only; exact stimulus/time/result remains gated by P2",
            )
        ),
    }


def active_source_paths(group: str, case: str, original_assets: dict[str, dict]) -> list[str]:
    base = "xdebug/tests/active_trace_chain"
    candidates: list[str]
    if group == "p0":
        candidates = [f"{base}/p0_composability/{case}/tb.sv"]
    elif group == "composite":
        candidates = [
            f"{base}/composite/{case}/tb.sv",
            f"{base}/composite/chain_dut.sv",
        ]
    elif group == "timing":
        candidates = [
            f"{base}/timing/{case}/tb.sv",
            f"{base}/timing/timing_boundary_dut.sv",
        ]
    elif group == "phase4":
        candidates = [
            f"{base}/phase4/{case}/tb.sv",
            f"{base}/phase4/phase4_dut.sv",
            f"{base}/composite/chain_dut.sv",
        ]
    elif group == "phase5":
        candidates = [f"{base}/phase5/dut.sv", f"{base}/phase5/tb.sv"]
    else:
        raise MatrixError(f"unknown active trace group: {group}")
    missing = [path for path in candidates if path not in original_assets]
    if missing:
        raise MatrixError(f"active scenario source not frozen: {missing!r}")
    return candidates


def consumer_catalog(
    original_root: Path,
    original_assets: dict[str, dict],
    actions: set[str],
) -> dict[str, dict]:
    result = {}
    action_pattern = re.compile(
        r"(?<![A-Za-z0-9_.])(" + "|".join(
            re.escape(action) for action in sorted(actions, key=len, reverse=True)
        ) + r")(?![A-Za-z0-9_.])"
    )
    for asset in original_assets.values():
        if "test_consumer" not in asset["roles"]:
            continue
        data = validate_frozen_file(original_root, asset)
        text = data.decode("utf-8", "replace")
        action_lines: dict[str, list[int]] = defaultdict(list)
        for line_number, line in enumerate(text.splitlines(), 1):
            for match in action_pattern.finditer(line):
                action_lines[match.group(1)].append(line_number)
        result[asset["path"]] = {
            "sha256": asset["sha256"],
            "fixture_ids": asset["fixture_ids"],
            "action_lines": {key: sorted(set(value)) for key, value in sorted(action_lines.items())},
        }
    return result


def fixture_consumers(catalog: dict[str, dict], fixture_id: str) -> list[str]:
    return sorted(path for path, value in catalog.items() if fixture_id in value["fixture_ids"])


def fixture_actions(catalog: dict[str, dict], paths: Iterable[str]) -> list[str]:
    return sorted({action for path in paths for action in catalog[path]["action_lines"]})


def build_matrix(repo_root: Path, original_root: Path, manifest_path: Path) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["goal_id"] != GOAL_ID:
        raise MatrixError("P0 manifest belongs to a different Goal")
    original_assets = asset_lookup(manifest, "original")
    current_assets = asset_lookup(manifest, "current")

    runtime_baseline = manifest["baselines"]["original_runtime"]
    audit_asset = current_assets.get(PHASE5_RUNTIME_AUDIT.as_posix())
    if audit_asset is None:
        raise MatrixError("P0 manifest does not freeze the Phase5 runtime audit")
    phase5_runtime_audit = json.loads(
        validate_frozen_file(repo_root, audit_asset).decode("utf-8")
    )
    phase5_runtime_rows = validate_phase5_runtime_audit(
        phase5_runtime_audit,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
    )
    ai_audit_asset = current_assets.get(AI_COMPLEX_RUNTIME_AUDIT.as_posix())
    if ai_audit_asset is None:
        raise MatrixError("P0 manifest does not freeze the ai_complex runtime audit")
    ai_complex_runtime_audit = json.loads(
        validate_frozen_file(repo_root, ai_audit_asset).decode("utf-8")
    )
    validate_ai_complex_runtime_audit(
        ai_complex_runtime_audit,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
    )
    ai_current = ai_complex_runtime_audit["current_runtime"]
    ai_fixture = ai_current["fixture"]
    ai_current_hashes = {
        "testdata/fixtures/ai_complex/ai_complex_top.sv":
            ai_fixture["rtl_sha256"],
        "testdata/fixtures/ai_complex/generate_ai_complex_fst.cpp":
            ai_fixture["generator_sha256"],
        "testdata/fixtures/ai_complex/fstcpp-four-state-vector.patch":
            ai_fixture["writer_patch_sha256"],
        "testdata/fixtures/ai_complex/waves.fst": ai_fixture["fst_sha256"],
        ai_current["repository_gate"]["path"]:
            ai_current["repository_gate"]["sha256"],
        ai_current["focused_regression"]["path"]:
            ai_current["focused_regression"]["sha256"],
    }
    for path, expected_hash in ai_current_hashes.items():
        asset = current_assets.get(path)
        if asset is None or asset["sha256"] != expected_hash:
            raise MatrixError(f"ai_complex current evidence hash drifted: {path}")
    ai_original_rtl = ai_complex_runtime_audit["observable_contract"][
        "original_rtl"
    ]
    original_rtl_asset = original_assets.get(ai_original_rtl["path"])
    if (
        original_rtl_asset is None
        or original_rtl_asset["sha256"] != ai_original_rtl["sha256"]
    ):
        raise MatrixError("ai_complex frozen original RTL evidence drifted")

    p3b_audit_asset = current_assets.get(P3B_RUNTIME_AUDIT.as_posix())
    if p3b_audit_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-B runtime audit")
    p3b_runtime_audit = json.loads(
        validate_frozen_file(repo_root, p3b_audit_asset).decode("utf-8")
    )
    p3b_runtime_rows = validate_p3b_runtime_audit(
        p3b_runtime_audit,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_p0_oracle_asset = current_assets.get(P3C_P0_ORACLE.as_posix())
    if p3c_p0_oracle_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-C P0 oracle")
    p3c_p0_oracle = json.loads(
        validate_frozen_file(repo_root, p3c_p0_oracle_asset).decode("utf-8")
    )
    p3c_p0_rows = validate_p3c_p0_oracle(
        p3c_p0_oracle,
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_composite_oracle_asset = current_assets.get(
        P3C_COMPOSITE_ORACLE.as_posix()
    )
    if p3c_composite_oracle_asset is None:
        raise MatrixError(
            "P0 manifest does not freeze the P3-C composite oracle"
        )
    p3c_composite_oracle = json.loads(
        validate_frozen_file(
            repo_root, p3c_composite_oracle_asset
        ).decode("utf-8")
    )
    p3c_composite_rows = validate_p3c_composite_oracle(
        p3c_composite_oracle,
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_phase4_oracle_asset = current_assets.get(
        P3C_PHASE4_ORACLE.as_posix()
    )
    if p3c_phase4_oracle_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-C Phase4 oracle")
    p3c_phase4_oracle = json.loads(
        validate_frozen_file(
            repo_root, p3c_phase4_oracle_asset
        ).decode("utf-8")
    )
    p3c_phase4_rows = validate_p3c_phase4_oracle(
        p3c_phase4_oracle,
        repo_root,
        original_assets,
        current_assets,
    )

    # Validate all frozen original assets, including consumers that do not end
    # up as HDL sources.  P1 must fail closed on any P0 evidence drift.
    for asset in original_assets.values():
        validate_frozen_file(original_root, asset)

    action_names = public_actions(repo_root)
    action_set = set(action_names)
    contracts = {action: action_contract(repo_root, action) for action in action_names}
    consumers = consumer_catalog(original_root, original_assets, action_set)
    current_tests = index_current_tests(repo_root)
    fixture_map = {item["id"]: item for item in manifest["original_fixtures"]}

    catalog_asset = original_assets[ACTIVE_CATALOG.as_posix()]
    catalog_text = validate_frozen_file(original_root, catalog_asset).decode("utf-8")
    active_rows = parse_active_catalog(catalog_text)
    report_asset = original_assets[PHASE5_REPORT.as_posix()]
    phase5_report = parse_phase5_report_terminations(
        validate_frozen_file(original_root, report_asset).decode("utf-8")
    )

    scenarios = []
    active_fixture_ids = set(ACTIVE_FIXTURE.values())
    special_fixtures = active_fixture_ids | {"xdebug.active_trace_runner"}

    for fixture_id, fixture in sorted(fixture_map.items()):
        if fixture_id in active_fixture_ids:
            continue
        candidate = FIXTURE_CANDIDATES[fixture_id]
        status = candidate.get("status", "partial")
        paths = fixture_consumers(consumers, fixture_id)
        hdl_assets = sorted(
            (
                asset for asset in original_assets.values()
                if fixture_id in asset["fixture_ids"] and asset["kind"] == "rtl"
            ),
            key=lambda item: item["path"],
        )
        scenario_id = "fixture." + fixture_id.removeprefix("xdebug.")
        scenario = {
            "scenario_id": scenario_id,
            "kind": "fixture_semantic_surface",
            "p3_batch": candidate["batch"],
            "status": status,
            "rationale": candidate.get(
                "rationale",
                (
                    "当前仅有相关能力/fixture 证据，尚未用 P2 等价公开请求逐观察点比较。"
                    if status == "partial" else
                    "当前没有同类 RTL/波形 fixture；相关公开语义必须在指定批次补齐或证明不可观察。"
                ),
            ),
            "original": {
                "fixture_id": fixture_id,
                "fixture_source_dir": fixture["source_dir"],
                "catalog_reference_lines": fixture["catalog_reference_lines"],
                "sources": [source_record(original_root, asset, scan=True) for asset in hdl_assets],
                "declared_waveform_outputs": [
                    item for item in manifest["original_declared_waveform_outputs"]
                    if item["fixture_id"] == fixture_id
                ],
                "consumer_paths": paths,
                "observed_public_actions": fixture_actions(consumers, paths),
            },
            "current": current_evidence(repo_root, current_assets, current_tests, candidate),
            "public_action_contracts": {
                action: contracts[action] for action in candidate["actions"]
            },
        }
        if fixture_id == "xdebug.ai_complex_wave":
            verdict = ai_complex_runtime_audit["verdict"]
            observable = ai_complex_runtime_audit["observable_contract"]
            if sorted(candidate["actions"]) != observable["public_actions"]:
                raise MatrixError("ai_complex audited public Action set drifted")
            scenario["runtime_audit"] = {
                "path": AI_COMPLEX_RUNTIME_AUDIT.as_posix(),
                "sha256": ai_audit_asset["sha256"],
                "status": verdict["status"],
                "p3_batch": verdict["p3_batch"],
                "same_locked_oracle_passed_both_sides":
                    observable["same_locked_oracle_passed_both_sides"],
                "remaining_observable_gap_count":
                    verdict["remaining_observable_gap_count"],
            }
        if scenario_id in p3b_runtime_rows:
            comparison = p3b_runtime_rows[scenario_id]
            if sorted(candidate["actions"]) != comparison["public_actions"]:
                raise MatrixError(
                    f"P3-B audited public Action set drifted: {scenario_id}"
                )
            status = comparison["status"]
            scenario["status"] = status
            if status == "semantic-equivalent":
                if comparison["comparison_method"] == \
                        "exact_rtl_and_ported_public_oracle":
                    scenario["rationale"] = (
                        "锁定原版 runtime/FSDB oracle 已通过；当前使用字节相同 RTL、"
                        "确定性原生 FST/DesignDB 和完整移植的公开请求断言通过。"
                        "原版 runner 未直接用于当前侧，审计明确保留该差别。"
                    )
                else:
                    scenario["rationale"] = (
                        "锁定原版完整 oracle 已通过；当前按相同公开 Action、刺激/时间、"
                        "响应字段与完整性语义逐项归一映射通过，未宣称复用同一 runner。"
                    )
            else:
                scenario["rationale"] = (
                    "冻结 73 Action/runtime/schema 与 Goal-start runner 哈希共同证明"
                    "该资产差异没有可执行公开语义观察点；静态合同门禁禁止将其泛化为免测。"
                )
            scenario["runtime_audit"] = {
                "path": P3B_RUNTIME_AUDIT.as_posix(),
                "sha256": p3b_audit_asset["sha256"],
                "status": status,
                "p3_batch": comparison["p3_batch"],
                "comparison_method": comparison["comparison_method"],
                "remaining_observable_gap_count":
                    comparison["remaining_observable_gap_count"],
            }
        scenarios.append(scenario)

    phase5_by_scene = {row["scene"]: row for row in phase5_report}
    group_ordinals = Counter()
    for row in active_rows:
        group = row["group"]
        group_ordinals[group] += 1
        ordinal = group_ordinals[group]
        scenario_id = f"active.{group}.{ordinal:02d}"
        fixture_id = ACTIVE_FIXTURE[group]
        candidate = ACTIVE_CANDIDATES[group] | {
            "actions": ["trace.active_driver_chain"], "batch": "P3-C"
        }
        source_paths = active_source_paths(group, str(row["case"]), original_assets)
        paths = fixture_consumers(consumers, fixture_id)
        oracle = {key: value for key, value in row.items() if key not in {"group", "case", "catalog_line"}}
        original = {
            "fixture_id": fixture_id,
            "catalog": {"path": ACTIVE_CATALOG.as_posix(), "line": row["catalog_line"]},
            "case": row["case"],
            "query": {"signal": row["signal"], "time": row["time"]},
            "oracle": oracle,
            "sources": [source_record(original_root, original_assets[path], scan=True) for path in source_paths],
            "declared_waveform_outputs": [
                item for item in manifest["original_declared_waveform_outputs"]
                if item["fixture_id"] == fixture_id
            ],
            "consumer_paths": paths,
            "observed_public_actions": fixture_actions(consumers, paths),
        }
        status = "partial"
        rationale = "存在相关能力代表测试，但未覆盖该原版 case 的同一 RTL 组合、刺激时间和完整响应。"
        p0_runtime_row = None
        composite_runtime_row = None
        phase4_runtime_row = None
        if group == "p0":
            p0_runtime_row = p3c_p0_rows[scenario_id]
            if (
                p0_runtime_row["case"] != row["case"]
                or p0_runtime_row["request"]["signal"] != original["query"]["signal"]
                or p0_runtime_row["request"]["time"] != original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C P0 oracle differs from frozen catalog: {scenario_id}"
                )
            native = p0_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_P0_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256": p0_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 RTL 与当前镜像逐字节相同；锁定 native NPI oracle 和当前原始 FST/"
                "binary-v1 DesignDB 已按请求、hop、源码行、时间、值、候选、termination 与"
                "完整性逐项通过。冻结 v1 schema 的两项表达限制由显式哨兵/value.at 门禁裁决。"
            )
        if group == "composite":
            composite_runtime_row = p3c_composite_rows[scenario_id]
            if (
                composite_runtime_row["case"] != row["case"]
                or composite_runtime_row["request"]["signal"] !=
                    original["query"]["signal"]
                or composite_runtime_row["request"]["time"] !=
                    original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C composite oracle differs from frozen catalog: "
                    f"{scenario_id}"
                )
            native = composite_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_COMPOSITE_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256":
                    composite_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 case RTL 与共享 DUT 均和当前镜像逐字节相同；锁定 native NPI "
                "oracle 和当前原始 FST/binary-v1 DesignDB 已按完整复合链、时间边界、"
                "源码行、值、generate 逐位候选、termination 与完整性逐项通过。"
            )
        if group == "phase4":
            phase4_runtime_row = p3c_phase4_rows[scenario_id]
            if (
                phase4_runtime_row["case"] != row["case"]
                or phase4_runtime_row["request"]["signal"] !=
                    original["query"]["signal"]
                or phase4_runtime_row["request"]["time"] !=
                    original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C Phase4 oracle differs from frozen catalog: {scenario_id}"
                )
            native = phase4_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_PHASE4_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256":
                    phase4_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 case RTL 与两份共享 DUT 均和当前镜像逐字节相同；锁定 native NPI "
                "oracle 和当前原始 FST/binary-v1 DesignDB 已按完整复合链、两次时序边界、"
                "源码行、值、generate 逐位候选、termination 与完整性逐项通过。"
            )
        if group == "phase5":
            runtime_row = phase5_runtime_rows[scenario_id]
            if runtime_row["locked_request"] != original["query"]:
                raise MatrixError(
                    f"Phase5 runtime request differs from frozen catalog: {scenario_id}"
                )
            report = phase5_by_scene[ordinal]
            original["report_oracle"] = {
                "path": PHASE5_REPORT.as_posix(),
                "line": report["report_line"],
                "termination": report["termination"],
                "hops": report["hops"],
            }
            if report["termination"] != row["termination"]:
                original["authority_conflict"] = {
                    "field": "termination",
                    "catalog_value": row["termination"],
                    "report_value": report["termination"],
                    "resolution": (
                        "P2 已保留双值；冻结 runtime 实测为 ambiguous。catalog/report 作为"
                        "历史 oracle 漂移继续保留，禁止静默改写。"
                    ),
                }
            original["locked_runtime_oracle"] = {
                "path": PHASE5_RUNTIME_AUDIT.as_posix(),
                "scenario_id": scenario_id,
                "termination": runtime_row["locked_runtime"]["termination"],
                "termination_detail": runtime_row["locked_runtime"]["termination_detail"],
                "scan_complete": runtime_row["locked_runtime"]["scan_complete"],
                "analysis_complete": runtime_row["locked_runtime"]["analysis_complete"],
                "response_truncated": runtime_row["locked_runtime"]["response_truncated"],
            }
            rationale = (
                "P2 锁定 runtime 实测与当前门禁在 termination/ambiguity 子集上一致；"
                "S1 完整响应仍有宽度诊断、RHS 顺序、statement 和源码证据差异，"
                "且资产 catalog/report 已确认漂移，因此继续保持 partial 并进入 P3-C。"
            )
        current = current_evidence(repo_root, current_assets, current_tests, candidate)
        runtime_evidence = None
        public_args = {"signal": row["signal"], "time": row["time"]}
        public_limits = None
        if group == "p0":
            native = p0_runtime_row["native_result"]
            exact_current_paths = {
                p0_runtime_row["rtl_mirrors"][0]["current_path"],
                (
                    "testdata/fixtures/active_trace/p0/"
                    f"{p0_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C P0 exact current fixture evidence is missing: {scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C P0 六场景逐项差分已通过；不是以共享 DUT 或代表 case 替代"
            )
            schema_projections = []
            if any(
                    hop.get("file") == "" and hop.get("line") == 0
                    for hop in native["chain"]):
                schema_projections.append({
                    "kind": "source_location_sentinel",
                    "native_shape": "source-less file='' and line=0",
                    "public_shape": "file='<unknown>', line=1, source_context=[]",
                })
            if native["termination"] == "control_only" and \
                    native["branch_evidence"]:
                schema_projections.append({
                    "kind": "control_only_candidate_sampling",
                    "native_shape": "control_only with branch candidates",
                    "public_shape": (
                        "control_only without schema-invalid ambiguity_evidence; "
                        "candidate before/after values independently gated by value.at"
                    ),
                })
            runtime_evidence = {
                "path": P3C_P0_ORACLE.as_posix(),
                "sha256": p3c_p0_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "remaining_observable_gap_count": 0,
                "schema_projections": schema_projections,
            }
            public_args = dict(p0_runtime_row["request"])
        elif group == "composite":
            native = composite_runtime_row["native_result"]
            exact_current_paths = {
                *(mirror["current_path"]
                  for mirror in composite_runtime_row["rtl_mirrors"]),
                (
                    "testdata/fixtures/active_trace/composite/"
                    f"{composite_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C composite exact current fixture evidence is missing: "
                    f"{scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C composite 二十场景逐项完整差分已通过；"
                "没有用共享 DUT 代替 case 级参数与刺激"
            )
            schema_projections = []
            if any(
                    hop.get("file") == "" and hop.get("line") == 0
                    for hop in native["chain"]):
                schema_projections.append({
                    "kind": "source_location_sentinel",
                    "native_shape": "source-less file='' and line=0",
                    "public_shape": (
                        "file='<unknown>', line=1, source_context=[]"
                    ),
                })
            runtime_evidence = {
                "path": P3C_COMPOSITE_ORACLE.as_posix(),
                "sha256": p3c_composite_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "locked_temporal_boundaries": native["temporal_boundaries"],
                "remaining_observable_gap_count": 0,
                "schema_projections": schema_projections,
            }
            public_args = {
                "signal": composite_runtime_row["request"]["signal"],
                "time": composite_runtime_row["request"]["time"],
            }
            public_limits = {"max_depth": 11}
        elif group == "phase4":
            native = phase4_runtime_row["native_result"]
            exact_current_paths = {
                *(mirror["current_path"]
                  for mirror in phase4_runtime_row["rtl_mirrors"]),
                (
                    "testdata/fixtures/active_trace/phase4/"
                    f"{phase4_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C Phase4 exact current fixture evidence is missing: {scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C Phase4 二十场景逐项完整差分已通过；没有用共享 DUT 代替 case 级刺激"
            )
            schema_projections = []
            if any(
                    hop.get("file") == "" and hop.get("line") == 0
                    for hop in native["chain"]):
                schema_projections.append({
                    "kind": "source_location_sentinel",
                    "native_shape": "source-less file='' and line=0",
                    "public_shape": (
                        "file='<unknown>', line=1, source_context=[]"
                    ),
                })
            runtime_evidence = {
                "path": P3C_PHASE4_ORACLE.as_posix(),
                "sha256": p3c_phase4_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "locked_temporal_boundaries": native["temporal_boundaries"],
                "remaining_observable_gap_count": 0,
                "schema_projections": schema_projections,
            }
            public_args = {
                "signal": phase4_runtime_row["request"]["signal"],
                "time": phase4_runtime_row["request"]["time"],
            }
            public_limits = {"max_depth": 16}
        elif group == "phase5":
            current["evidence_scope"] = (
                "P2 已证明十个场景的 termination/ambiguity 子集一致；完整响应仍由 P3-C 关闭"
            )
            runtime_evidence = {
                "path": PHASE5_RUNTIME_AUDIT.as_posix(),
                "sha256": audit_asset["sha256"],
                "scenario_id": scenario_id,
                "status": runtime_row["status"],
                "p3_batch": runtime_row["p3_batch"],
                "locked_termination": runtime_row["locked_runtime"]["termination"],
                "current_gate_termination": runtime_row["current_gate"]["termination"],
                "full_response_equivalent": False,
            }
        scenarios.append({
            "scenario_id": scenario_id,
            "kind": "active_trace_catalog_case",
            "p3_batch": "P3-C",
            "status": status,
            "rationale": rationale,
            "original": original,
            "current": current,
            **({"runtime_audit": runtime_evidence} if runtime_evidence else {}),
            "public_request": {
                "api_version": "xdebug.v1",
                "action": "trace.active_driver_chain",
                "args": public_args,
                **({"limits": public_limits} if public_limits else {}),
            },
            "public_action_contracts": {
                "trace.active_driver_chain": contracts["trace.active_driver_chain"]
            },
        })

    readme_asset = original_assets[ACTIVE_README.as_posix()]
    readme_text = validate_frozen_file(original_root, readme_asset).decode("utf-8")
    orphan_line = file_line(original_root / ACTIVE_README, r"p0_4_interface_modport")
    orphan_candidate = {
        "fixtures": ["current.interface_modport"],
        "tests": ["test_trace_active_driver_chain_crosses_interface_modports"],
        "actions": ["trace.active_driver_chain"],
        "batch": "P3-C",
        "evidence_scope": (
            "仅证明当前存在 interface/modport active-chain 能力；原版 p0_4 没有"
            "RTL、catalog request 或动态 oracle，不能据此关闭 missing"
        ),
    }
    p0_paths = fixture_consumers(consumers, "xdebug.active_trace_p0")
    scenarios.append({
        "scenario_id": "active.p0.declared_only_p0_4",
        "kind": "declared_only_orphan",
        "p3_batch": "P3-C",
        "status": "missing",
        "rationale": (
            "README 声明 interface/modport case，但冻结目录只有 .gitignore，cases.v1.yaml 也无条目；"
            "当前相关能力测试不能替代缺失的原版刺激/oracle，必须显式补建差分合同。"
        ),
        "original": {
            "fixture_id": "xdebug.active_trace_p0",
            "declaration": {"path": ACTIVE_README.as_posix(), "line": orphan_line},
            "case": "p0_4_interface_modport",
            "catalog_presence": False,
            "sources": [],
            "declared_waveform_outputs": [
                item for item in manifest["original_declared_waveform_outputs"]
                if item["fixture_id"] == "xdebug.active_trace_p0"
            ],
            "consumer_paths": p0_paths,
            "observed_public_actions": fixture_actions(consumers, p0_paths),
        },
        "current": current_evidence(repo_root, current_assets, current_tests, orphan_candidate),
        "public_action_contracts": {
            "trace.active_driver_chain": contracts["trace.active_driver_chain"]
        },
    })

    cross_paths = fixture_consumers(consumers, "original.cross_fixture")
    scenarios.append({
        "scenario_id": "cross_fixture.public_contract_consumers",
        "kind": "cross_fixture_contract_surface",
        "p3_batch": "P3-E",
        "status": "partial",
        "rationale": (
            "跨 fixture consumer 已完整归档，但其合同/Action 断言要在 P2/P4 由 73 Action 门禁裁决。"
        ),
        "original": {
            "fixture_id": None,
            "sources": [],
            "declared_waveform_outputs": [],
            "consumer_paths": cross_paths,
            "observed_public_actions": fixture_actions(consumers, cross_paths),
        },
        "current": {
            "candidate_fixture_ids": [],
            "candidate_sources": [],
            "test_evidence": [],
            "evidence_scope": "P2/P4 73 Action cross-fixture contract gate",
        },
        "public_action_contracts": {},
    })

    scenarios.sort(key=lambda item: item["scenario_id"])
    scenario_ids = {item["scenario_id"] for item in scenarios}
    if len(scenario_ids) != len(scenarios):
        raise MatrixError("duplicate scenario_id")
    if any(item["status"] not in ALLOWED_STATUSES for item in scenarios):
        raise MatrixError("matrix contains an invalid status")

    fixture_index = {}
    for fixture_id in sorted(fixture_map):
        matches = [
            item["scenario_id"] for item in scenarios
            if item["original"].get("fixture_id") == fixture_id
        ]
        if not matches:
            raise MatrixError(f"original fixture has no scenario: {fixture_id}")
        fixture_index[fixture_id] = matches

    hdl_index = {}
    for path, asset in sorted(original_assets.items()):
        if asset["kind"] != "rtl":
            continue
        matches = [
            item["scenario_id"] for item in scenarios
            if path in {source["path"] for source in item["original"]["sources"]}
        ]
        if not matches:
            raise MatrixError(f"original HDL has no scenario: {path}")
        hdl_index[path] = matches

    output_index = {}
    for output in manifest["original_declared_waveform_outputs"]:
        key = f"{output['fixture_id']}::{output['path']}"
        matches = fixture_index.get(output["fixture_id"], [])
        if not matches:
            raise MatrixError(f"declared waveform output has no scenario: {key}")
        output_index[key] = matches

    consumer_index = {}
    for path in sorted(consumers):
        matches = [
            item["scenario_id"] for item in scenarios
            if path in item["original"]["consumer_paths"]
        ]
        if not matches:
            raise MatrixError(f"original consumer has no scenario: {path}")
        consumer_index[path] = matches

    status_counts = Counter(item["status"] for item in scenarios)
    p3_queue = defaultdict(list)
    for item in scenarios:
        if item["status"] in {"partial", "missing"}:
            p3_queue[item["p3_batch"]].append(item["scenario_id"])

    return {
        "schema_version": SCHEMA_VERSION,
        "goal_id": GOAL_ID,
        "policy": {
            "comparison_unit": (
                "fixture + RTL construct + stimulus/time + observable path + public Action + complete response"
            ),
            "candidate_evidence_is_not_equivalence": True,
            "unclassified_status_forbidden": True,
            "only_writable_repository": "xdebug_fst",
            "external_inputs_read_only": True,
        },
        "authority": {
            "runtime_revision": manifest["baselines"]["original_runtime"]["runtime_revision"],
            "schema_revision": manifest["baselines"]["original_runtime"]["schema_revision"],
            "asset_goal_start_head": manifest["baselines"]["original_assets"]["head"],
            "active_catalog": ACTIVE_CATALOG.as_posix(),
        },
        "action_contracts": contracts,
        "original_consumers": consumers,
        "scenarios": scenarios,
        "coverage": {
            "original_fixtures": fixture_index,
            "original_hdl": hdl_index,
            "declared_waveform_outputs": output_index,
            "original_consumers": consumer_index,
        },
        "p3_queue": {key: sorted(value) for key, value in sorted(p3_queue.items())},
        "summary": {
            "scenario_count": len(scenarios),
            "active_catalog_case_count": len(active_rows),
            "declared_only_orphan_count": 1,
            "original_fixture_count": len(fixture_index),
            "original_hdl_count": len(hdl_index),
            "declared_waveform_output_count": len(output_index),
            "original_consumer_count": len(consumer_index),
            "status_counts": dict(sorted(status_counts.items())),
            "unclassified_count": 0,
            "unqueued_gap_count": 0,
        },
    }


def write_atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.",
        delete=False,
    ) as stream:
        stream.write(content)
        temporary = Path(stream.name)
    os.replace(temporary, path)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ASSET_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--original-root", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    repo_root = Path(__file__).resolve().parents[1]
    manifest = args.manifest if args.manifest.is_absolute() else repo_root / args.manifest
    output = args.output if args.output.is_absolute() else repo_root / args.output
    output = ensure_within_repo(repo_root, output)
    original_value = args.original_root or (
        Path(os.environ["XDEBUG_ORIGINAL_ROOT"])
        if os.environ.get("XDEBUG_ORIGINAL_ROOT") else None
    )
    if original_value is None:
        raise MatrixError("set XDEBUG_ORIGINAL_ROOT or pass --original-root; no fallback is allowed")
    original_root = original_value.resolve()
    matrix = build_matrix(repo_root, original_root, manifest)
    content = canonical_json(matrix)
    if args.check:
        if not output.is_file() or output.read_text(encoding="utf-8") != content:
            raise MatrixError(f"semantic matrix is stale: {output.relative_to(repo_root)}")
        print(f"OK: {output.relative_to(repo_root)} is reproducible")
        return 0
    write_atomic(output, content)
    print(f"wrote {output.relative_to(repo_root)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except MatrixError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
