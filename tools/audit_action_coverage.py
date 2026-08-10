#!/usr/bin/env python3
"""Build a conservative 73-action contract-coverage matrix from a test trace."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
from typing import Any, Iterable


DIMENSIONS = (
    "success",
    "invalid_request",
    "resource_missing",
    "empty_result",
    "boundary_time",
    "multiple_results",
    "limits",
    "truncation",
    "completeness",
    "xz",
)

RESOURCE_ERROR_CODES = {
    "CONFIG_NOT_FOUND",
    "DESIGN_BUNDLE_INVALID",
    "DESIGN_NOT_LOADED",
    "RESOURCE_PROVENANCE_MISMATCH",
    "SESSION_NOT_FOUND",
    "SIGNAL_NOT_FOUND",
    "WAVEFORM_NOT_LOADED",
    "WAVEFORM_OPEN_FAILED",
}

RESULT_COUNT_KEYS = {
    "active_count",
    "change_count",
    "config_count",
    "count",
    "cursor_count",
    "event_count",
    "finding_count",
    "interval_count",
    "match_count",
    "origin_count",
    "packet_count",
    "path_count",
    "result_count",
    "row_count",
    "sample_count",
    "scope_count",
    "session_count",
    "signal_count",
    "transaction_count",
    "transfer_count",
    "total_count",
}

RESULT_LIST_KEYS = {
    "actions",
    "chains",
    "change_points",
    "changed_signals",
    "changes",
    "checks",
    "configs",
    "cursors",
    "depth_frontiers",
    "evidence",
    "events",
    "findings",
    "hops",
    "intervals",
    "matches",
    "origins",
    "outliers",
    "packets",
    "paths",
    "payloads",
    "preview",
    "ready_without_valid_intervals",
    "removed",
    "removed_sessions",
    "results",
    "rows",
    "samples",
    "scopes",
    "sessions",
    "signals",
    "kept_sessions",
    "transactions",
    "transfers",
}

LIMIT_REQUEST_KEYS = {
    "limit",
    "line_limit",
    "max_chains",
    "max_depth",
    "max_nodes",
    "max_results",
    "max_time_steps",
    "preview_limit",
    "result_limit",
}

COMPLETENESS_KEYS = {
    "analysis_complete",
    "cleanup_complete",
    "complete",
    "data_complete",
    "scan_complete",
}

VALUE_KEYS = {"binary", "bits", "raw", "value"}
TIME_KEYS = {"begin", "end", "time", "times"}
ZERO_TIME = re.compile(r"^[+-]?0(?:\.0+)?(?:as|fs|ps|ns|us|ms|s)?$")


def walk(value: Any, path: tuple[str, ...] = ()) -> Iterable[tuple[tuple[str, ...], Any]]:
    yield path, value
    if isinstance(value, dict):
        for key, child in value.items():
            yield from walk(child, path + (str(key),))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, path + (str(index),))


def error_code(response: Any) -> str:
    if not isinstance(response, dict) or not isinstance(response.get("error"), dict):
        return ""
    return str(response["error"].get("code", ""))


def has_resource_error(response: Any) -> bool:
    code = error_code(response)
    return (
        code in RESOURCE_ERROR_CODES
        or code.endswith("_NOT_FOUND")
        or code.endswith("_NOT_LOADED")
        or code.endswith("_OPEN_FAILED")
    )


def has_result_cardinality(response: Any, predicate: Any) -> bool:
    for path, value in walk(response):
        if not path:
            continue
        key = path[-1]
        if key in RESULT_COUNT_KEYS and isinstance(value, int) and predicate(value):
            return True
        if key in RESULT_LIST_KEYS and isinstance(value, list) and predicate(len(value)):
            return True
    return False


def has_empty_lookup(response: Any) -> bool:
    return any(
        bool(path) and path[-1] in {"found", "diff_found"} and value is False
        for path, value in walk(response)
    )


def has_boundary_time(request: Any) -> bool:
    for path, value in walk(request):
        if not path or path[-1] not in TIME_KEYS:
            continue
        values = value if isinstance(value, list) else [value]
        if any(isinstance(item, str) and ZERO_TIME.fullmatch(item.strip())
               for item in values):
            return True
    return False


def has_limit_request(request: Any) -> bool:
    for path, value in walk(request):
        if not path:
            continue
        if path[-1] == "limits" and isinstance(value, dict) and value:
            return True
        if path[-1] in LIMIT_REQUEST_KEYS and value is not None:
            return True
    return False


def has_truncation(response: Any) -> bool:
    for path, value in walk(response):
        if not path:
            continue
        key = path[-1]
        if key in {"truncated", "response_truncated"} and value is True:
            return True
        if key == "truncation_scopes" and isinstance(value, list) and value:
            return True
        if key in {"termination", "reason", "kind"} and value == "limit":
            return True
    return False


def has_completeness(response: Any) -> bool:
    return any(
        bool(path) and path[-1] in COMPLETENESS_KEYS and isinstance(value, bool)
        for path, value in walk(response)
    )


def has_xz(value: Any) -> bool:
    for path, child in walk(value):
        if not path:
            continue
        key = path[-1]
        if key in VALUE_KEYS and isinstance(child, str):
            literal = child.strip().lower()
            if literal in {"x", "z", "xz", "zx", "unknown"}:
                return True
            if re.fullmatch(r"[01xz_]+", literal) and re.search(r"[xz]", literal):
                return True
            if re.search(r"'[bohd][0-9a-f_xz?]*[xz]", literal):
                return True
        if key in {"kind", "state"} and child in {"unknown", "x", "z", "xz"}:
            return True
    return False


def classify(event: dict[str, Any]) -> set[str]:
    request = event.get("request")
    response = event.get("response")
    observed: set[str] = set()
    succeeded = isinstance(response, dict) and response.get("ok") is True
    truncated = has_truncation(response)
    if succeeded:
        observed.add("success")
        if has_result_cardinality(response, lambda count: count == 0) or \
                has_empty_lookup(response):
            observed.add("empty_result")
        if has_result_cardinality(response, lambda count: count > 1):
            observed.add("multiple_results")
    if error_code(response) == "INVALID_REQUEST":
        observed.add("invalid_request")
    if has_resource_error(response):
        observed.add("resource_missing")
    if succeeded and has_boundary_time(request):
        observed.add("boundary_time")
    if (succeeded or truncated) and has_limit_request(request):
        observed.add("limits")
    if truncated:
        observed.add("truncation")
    if has_completeness(response):
        observed.add("completeness")
    if succeeded and (has_xz(request) or has_xz(response)):
        observed.add("xz")
    return observed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--applicability", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-markdown", type=Path)
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="exit nonzero unless every action has every dimension",
    )
    return parser.parse_args()


def load_not_applicable(
    path: Path | None, actions: list[str]
) -> dict[tuple[str, str], dict[str, str]]:
    if path is None:
        return {}
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != \
            "xdebug.action-coverage-applicability.v1":
        raise ValueError("unsupported action applicability schema")
    result: dict[tuple[str, str], dict[str, str]] = {}
    for entry in document.get("not_applicable", []):
        if set(entry) != {"action", "dimension", "reason", "evidence"}:
            raise ValueError("invalid action applicability entry fields")
        action = entry["action"]
        dimension = entry["dimension"]
        if action not in actions:
            raise ValueError(f"unknown applicability action: {action}")
        if dimension not in DIMENSIONS:
            raise ValueError(f"unknown applicability dimension: {dimension}")
        if not entry["reason"] or not entry["evidence"]:
            raise ValueError("applicability reason and evidence must be non-empty")
        key = (action, dimension)
        if key in result:
            raise ValueError(f"duplicate applicability entry: {action}/{dimension}")
        result[key] = {
            "reason": str(entry["reason"]),
            "evidence": str(entry["evidence"]),
        }
    return result


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    trace_path = args.trace.resolve()
    if not args.repo_root.is_absolute() or not args.trace.is_absolute():
        raise SystemExit("--repo-root and --trace must be absolute paths")
    if args.applicability is not None and not args.applicability.is_absolute():
        raise SystemExit("--applicability must be an absolute path")
    for option_name, output_path in (
        ("--output-json", args.output_json),
        ("--output-markdown", args.output_markdown),
    ):
        if output_path is not None and not output_path.is_absolute():
            raise SystemExit(f"{option_name} must be an absolute path")
    catalog_path = repo_root / "compat/xdebug-v1/catalog.response.json"
    actions = json.loads(catalog_path.read_text(encoding="utf-8"))["data"]["actions"]
    if len(actions) != 73 or len(set(actions)) != 73:
        raise SystemExit("frozen catalog must contain 73 unique actions")
    try:
        not_applicable = load_not_applicable(args.applicability, actions)
    except (OSError, ValueError, json.JSONDecodeError) as exception:
        raise SystemExit(f"invalid action applicability: {exception}") from exception

    evidence: dict[str, dict[str, set[str]]] = {
        action: {dimension: set() for dimension in DIMENSIONS}
        for action in actions
    }
    event_counts: dict[str, int] = defaultdict(int)
    unknown_actions: set[str] = set()
    for line_number, line in enumerate(
        trace_path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not line.strip():
            continue
        event = json.loads(line)
        action = event.get("action")
        if action not in evidence:
            if action is not None:
                unknown_actions.add(str(action))
            continue
        event_counts[action] += 1
        node = str(event.get("test_node") or f"trace-line-{line_number}")
        for dimension in classify(event):
            evidence[action][dimension].add(node)

    rows = []
    for action in actions:
        dimensions = {
            dimension: sorted(evidence[action][dimension])
            for dimension in DIMENSIONS
        }
        rows.append({
            "action": action,
            "event_count": event_counts[action],
            "dimensions": dimensions,
            "not_applicable": {
                dimension: not_applicable[(action, dimension)]
                for dimension in DIMENSIONS
                if (action, dimension) in not_applicable
            },
            "missing": [
                dimension for dimension in DIMENSIONS
                if not dimensions[dimension]
                and (action, dimension) not in not_applicable
            ],
        })

    dimension_counts = {
        dimension: sum(bool(row["dimensions"][dimension]) for row in rows)
        for dimension in DIMENSIONS
    }
    not_applicable_counts = {
        dimension: sum(
            dimension in row["not_applicable"] for row in rows
        )
        for dimension in DIMENSIONS
    }
    report = {
        "schema_version": "xdebug.action-coverage-audit.v1",
        "action_count": len(actions),
        "trace_event_count": sum(event_counts.values()),
        "unknown_actions": sorted(unknown_actions),
        "dimension_counts": dimension_counts,
        "not_applicable_counts": not_applicable_counts,
        "complete_action_count": sum(not row["missing"] for row in rows),
        "actions": rows,
        "classification_notice": (
            "Observed trace evidence is conservative audit input, not final "
            "semantic parity proof; missing evidence remains a TODO."
        ),
    }

    rendered_json = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output_json:
        args.output_json.resolve().write_text(rendered_json, encoding="utf-8")

    header = "| action | " + " | ".join(DIMENSIONS) + " | missing |"
    separator = "|---|" + "|".join("---" for _ in DIMENSIONS) + "|---|"
    markdown = [
        "# xdebug 73-action coverage audit",
        "",
        report["classification_notice"],
        "",
        header,
        separator,
    ]
    for row in rows:
        marks = [
            "✓" if row["dimensions"][dimension]
            else "N/A" if dimension in row["not_applicable"]
            else "—"
            for dimension in DIMENSIONS
        ]
        markdown.append(
            "| " + row["action"] + " | " + " | ".join(marks) + " | "
            + ", ".join(row["missing"]) + " |"
        )
    rendered_markdown = "\n".join(markdown) + "\n"
    if args.output_markdown:
        args.output_markdown.resolve().write_text(
            rendered_markdown, encoding="utf-8"
        )
    if not args.output_json and not args.output_markdown:
        print(rendered_json, end="")

    complete = report["complete_action_count"] == len(actions)
    return 0 if complete or not args.require_complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
