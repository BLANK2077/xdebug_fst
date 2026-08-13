#!/usr/bin/env python3
"""Prove which frozen actions expose an explicit temporal boundary."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from check_compat_baseline import verify_frozen_files


TIME_PROPERTIES = {"time", "times", "time_range"}

EXPECTED_APPLICABLE = {
    "apb.transfer_window", "axi.channel_stall", "axi.export",
    "axi.latency_outlier", "axi.outstanding_timeline", "axi.query",
    "axi.request_response_pair", "counter.statistics", "event.export",
    "event.find", "expr.eval_at", "list.export", "list.first_change",
    "protocol.handshake.inspect", "signal.anomaly.inspect", "signal.changes",
    "signal.sampled_pulse.inspect", "signal.stability", "signal.statistics",
    "signal.xz_verify", "stream.export", "stream.query", "stream.validate",
    "trace.active_driver", "trace.active_driver_chain", "trace.x_origin",
    "value.at", "verify.conditions", "waveform.cursor.set", "window.verify",
    "batch",
}


def temporal_properties(value: Any) -> set[str]:
    result: set[str] = set()
    if isinstance(value, dict):
        properties = value.get("properties")
        if isinstance(properties, dict):
            result.update(set(properties) & TIME_PROPERTIES)
        for child in value.values():
            result.update(temporal_properties(child))
    elif isinstance(value, list):
        for child in value:
            result.update(temporal_properties(child))
    return result


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
    all_actions: set[str] = set()
    applicable: dict[str, set[str]] = {}
    for path in sorted(schema_root.glob("*.request.schema.json")):
        action = path.name.removesuffix(".request.schema.json")
        all_actions.add(action)
        schema = json.loads(path.read_text(encoding="utf-8"))
        fields = temporal_properties(
            schema.get("properties", {}).get("args", {})
        )
        if fields:
            applicable[action] = fields

    batch_schema = json.loads(
        (schema_root / "batch.request.schema.json").read_text(encoding="utf-8")
    )
    batch_items = batch_schema["properties"]["args"]["properties"][
        "requests"]["items"]
    if batch_items.get("x-deferred-action-validation") is not True:
        raise RuntimeError("batch child requests no longer use action validation")
    applicable["batch"] = {"child_request"}

    response_actions = {
        path.name.removesuffix(".response.schema.json")
        for path in schema_root.glob("*.response.schema.json")
    }
    if all_actions != response_actions or len(all_actions) != 73:
        raise RuntimeError("request/response schemas do not cover the same 73 actions")
    if set(applicable) != EXPECTED_APPLICABLE:
        raise RuntimeError(
            "boundary-time applicability changed: "
            f"expected={sorted(EXPECTED_APPLICABLE)!r}; "
            f"actual={sorted(applicable)!r}"
        )

    print(
        "boundary-time applicability: OK "
        f"({len(applicable)} applicable actions; "
        f"{len(all_actions) - len(applicable)} non-temporal actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
