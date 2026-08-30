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
        "fixtures": ["current.counter", "current.case"],
        "tests": [
            "test_trace_active_driver",
            "test_trace_active_driver_chain",
        ],
        "actions": ["trace.active_driver", "trace.active_driver_chain"],
        "batch": "P3-B",
    },
    "xdebug.active_semantics": {
        "fixtures": ["current.case", "current.matches", "current.output_mixed"],
        "tests": [
            "test_trace_active_driver_selects_case_item_and_default",
            "test_trace_active_driver_selects_pattern_variable_binding",
            "test_trace_active_driver_chain_reports_two_active_procedural_drivers",
        ],
        "actions": ["trace.active_driver", "trace.active_driver_chain"],
        "batch": "P3-B",
    },
    "xdebug.active_zero_evidence": {
        "fixtures": ["current.counter"],
        "tests": [
            "test_trace_active_driver_chain_distinguishes_internal_zero_evidence_from_primary_input"
        ],
        "actions": ["trace.active_driver", "trace.active_driver_chain"],
        "batch": "P3-B",
    },
    "xdebug.interface_port_root": {
        "fixtures": ["current.interface_modport"],
        "tests": ["test_trace_active_driver_chain_crosses_interface_modports"],
        "actions": ["signal.resolve", "trace.active_driver_chain"],
        "batch": "P3-B",
    },
    "xdebug.trace_x_xprop": {
        "fixtures": ["current.xprop"],
        "tests": [
            "test_trace_x_origin_x_propagation",
            "test_trace_x_origin_keeps_loop_and_normal_source_branches",
        ],
        "actions": ["trace.x_origin", "signal.xz_verify"],
        "batch": "P3-B",
    },
    "xdebug.ai_complex_wave": {
        "fixtures": ["current.counter", "current.root"],
        "tests": [
            "test_value_at_bus_signal",
            "test_signal_changes_preserves_same_time_string_deltas",
            "test_value_at_preserves_typed_real_value",
            "test_value_at_preserves_event_kind",
        ],
        "actions": [
            "scope.roots", "scope.list", "value.at", "signal.changes",
            "signal.statistics", "event.find", "window.verify",
        ],
        "batch": "P3-A",
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
        "tests": ["test_signal_resolve_contract", "test_trace_driver_contract_and_role_filter"],
        "actions": ["signal.resolve", "signal.canonicalize", "trace.driver", "trace.load"],
        "batch": "P3-B",
    },
    "xdebug.design_p3": {
        "fixtures": ["current.case", "current.matches", "current.ref_port"],
        "tests": [
            "test_trace_active_driver_selects_case_inside_pattern_and_range",
            "test_trace_active_driver_chain_crosses_ref_port",
        ],
        "actions": ["signal.resolve", "trace.driver", "trace.load", "trace.active_driver"],
        "batch": "P3-B",
    },
    "xdebug.design_hierarchy": {
        "fixtures": ["current.interface_modport", "current.output_mixed"],
        "tests": ["test_scope_list", "test_signal_canonicalize_port_connection"],
        "actions": ["scope.roots", "scope.list", "signal.resolve", "signal.canonicalize"],
        "batch": "P3-B",
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
        "fixtures": ["current.counter", "current.case", "current.interface_modport", "current.phase5"],
        "tests": [
            "test_trace_active_driver_chain_propagates_through_nba_active_time",
            "test_trace_active_driver_chain_enters_child_output_from_parent_net",
            "test_trace_active_driver_chain_crosses_interface_modports",
            "test_trace_active_driver_selects_runtime_else_branch",
        ],
    },
    "composite": {
        "fixtures": ["current.counter", "current.interface_modport", "current.phase5"],
        "tests": [
            "test_trace_active_driver_chain_enters_child_output_from_parent_net",
            "test_trace_active_driver_chain_crosses_interface_modports",
            "test_trace_active_driver_chain_reports_multi_rhs_ambiguity",
        ],
    },
    "timing": {
        "fixtures": ["current.counter"],
        "tests": [
            "test_trace_active_driver_chain_propagates_through_nba_active_time",
            "test_trace_active_driver_chain_uses_changed_event_time",
        ],
    },
    "phase4": {
        "fixtures": ["current.counter", "current.interface_modport"],
        "tests": [
            "test_trace_active_driver_chain_propagates_nba_time_through_alias_chain",
            "test_trace_active_driver_chain_crosses_interface_modports",
            "test_trace_active_driver_chain_reports_multi_rhs_ambiguity",
        ],
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
            "related capability only; exact stimulus/time/result remains gated by P2"
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
        scenarios.append({
            "scenario_id": scenario_id,
            "kind": "fixture_semantic_surface",
            "p3_batch": candidate["batch"],
            "status": status,
            "rationale": (
                "当前仅有相关能力/fixture 证据，尚未用 P2 等价公开请求逐观察点比较。"
                if status == "partial" else
                "当前没有同类 RTL/波形 fixture；相关公开语义必须在指定批次补齐或证明不可观察。"
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
        })

    phase5_by_scene = {row["scene"]: row for row in phase5_report}
    group_ordinals = Counter()
    for row in active_rows:
        group = row["group"]
        group_ordinals[group] += 1
        ordinal = group_ordinals[group]
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
        rationale = "存在相关能力代表测试，但未覆盖该原版 case 的同一 RTL 组合、刺激时间和完整响应。"
        if group == "phase5":
            scenario_id = f"active.phase5.{ordinal:02d}"
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
        if group == "phase5":
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
            "scenario_id": f"active.{group}.{ordinal:02d}",
            "kind": "active_trace_catalog_case",
            "p3_batch": "P3-C",
            "status": "partial",
            "rationale": rationale,
            "original": original,
            "current": current,
            **({"runtime_audit": runtime_evidence} if runtime_evidence else {}),
            "public_request": {
                "api_version": "xdebug.v1",
                "action": "trace.active_driver_chain",
                "args": {"signal": row["signal"], "time": row["time"]},
            },
            "public_action_contracts": {
                "trace.active_driver_chain": contracts["trace.active_driver_chain"]
            },
        })

    readme_asset = original_assets[ACTIVE_README.as_posix()]
    readme_text = validate_frozen_file(original_root, readme_asset).decode("utf-8")
    orphan_line = file_line(original_root / ACTIVE_README, r"p0_4_interface_modport")
    orphan_candidate = ACTIVE_CANDIDATES["p0"] | {
        "actions": ["trace.active_driver_chain"], "batch": "P3-C"
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
