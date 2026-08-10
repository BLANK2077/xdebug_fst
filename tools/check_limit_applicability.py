#!/usr/bin/env python3
"""Prove which frozen actions expose a result-cardinality limit."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from check_compat_baseline import verify_frozen_files


RESULT_LIMIT_KEYS = {
    "limit", "line_limit", "max_chains", "max_depth", "max_events",
    "max_nodes", "max_results", "max_rows", "max_time_steps",
    "max_trace_signals", "preview_limit", "result_limit", "top_n",
}

EXPECTED_APPLICABLE = {
    "apb.query", "apb.transfer_window", "axi.analysis",
    "axi.channel_stall", "axi.latency_outlier",
    "axi.outstanding_timeline", "axi.query",
    "axi.request_response_pair", "counter.statistics",
    "event.config.list", "event.export", "event.find", "expr.normalize",
    "list.export", "protocol.handshake.inspect", "scope.list",
    "signal.anomaly.inspect", "signal.changes",
    "signal.sampled_pulse.inspect", "signal.statistics", "stream.export",
    "stream.query", "stream.validate", "trace.active_driver",
    "trace.active_driver_chain", "trace.driver", "trace.load",
    "trace.x_origin", "window.verify",
}


def nested_property_names(value: Any) -> set[str]:
    result: set[str] = set()
    if isinstance(value, dict):
        properties = value.get("properties")
        if isinstance(properties, dict):
            result.update(set(properties) & RESULT_LIMIT_KEYS)
        for child in value.values():
            result.update(nested_property_names(child))
    elif isinstance(value, list):
        for child in value:
            result.update(nested_property_names(child))
    return result


def result_limit_fields(schema: dict[str, Any]) -> set[str]:
    request_properties = schema.get("properties", {})
    fields = nested_property_names(request_properties.get("args", {}))
    limits = request_properties.get("limits", {}).get("properties", {})
    fields.update(set(limits) & RESULT_LIMIT_KEYS)
    return fields


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.repo_root.is_absolute():
        raise SystemExit("--repo-root must be an absolute path")
    repo_root = args.repo_root.resolve()
    frozen_errors = verify_frozen_files(repo_root)
    if frozen_errors:
        raise RuntimeError(
            "frozen original contract is invalid: " + "; ".join(frozen_errors)
        )

    schema_root = repo_root / "compat/xdebug-v1/schemas/v1/actions"
    applicable: dict[str, set[str]] = {}
    all_actions: set[str] = set()
    for path in sorted(schema_root.glob("*.request.schema.json")):
        action = path.name.removesuffix(".request.schema.json")
        all_actions.add(action)
        schema = json.loads(path.read_text(encoding="utf-8"))
        fields = result_limit_fields(schema)
        if fields:
            applicable[action] = fields

    if all_actions != {
        path.name.removesuffix(".response.schema.json")
        for path in schema_root.glob("*.response.schema.json")
    } or len(all_actions) != 73:
        raise RuntimeError("request/response schemas do not cover the same 73 actions")
    if set(applicable) != EXPECTED_APPLICABLE:
        raise RuntimeError(
            "result-limit applicability changed: "
            f"expected={sorted(EXPECTED_APPLICABLE)!r}; "
            f"actual={sorted(applicable)!r}"
        )

    print(
        "result-limit applicability: OK "
        f"({len(applicable)} applicable actions; "
        f"{len(all_actions) - len(applicable)} timeout-only/no-limit actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
