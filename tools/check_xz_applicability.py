#!/usr/bin/env python3
"""Freeze actions whose public success response can expose explicit X/Z facts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from audit_action_coverage import XZ_COUNT_KEYS, XZ_STATE_VALUES
from check_compat_baseline import verify_frozen_files


EXPECTED_APPLICABLE = {
    "apb.statistics", "axi.statistics", "batch", "counter.statistics",
    "event.export", "event.find", "expr.eval_at", "list.first_change",
    "protocol.handshake.inspect", "signal.anomaly.inspect", "signal.changes",
    "signal.sampled_pulse.inspect", "signal.stability", "signal.statistics",
    "signal.xz_verify", "stream.export", "stream.query", "stream.validate",
    "trace.active_driver_chain", "trace.x_origin", "value.at",
    "verify.conditions", "window.verify",
}

# The chain schema intentionally uses recursive jsonValue for hop/frontier and
# ambiguity samples; its frozen basic response pins those fields to X literals.
SEMANTIC_JSON_VALUE_ACTIONS = {"trace.active_driver_chain"}


def explicit_xz_surface(schema: dict[str, Any]) -> bool:
    definitions = schema.get("$defs", {})
    visited_refs: set[str] = set()

    def visit(value: Any) -> bool:
        if isinstance(value, dict):
            reference = value.get("$ref")
            if isinstance(reference, str) and reference.startswith("#/$defs/"):
                name = reference.rsplit("/", 1)[-1]
                if "logicvalue" in name.lower():
                    return True
                if name not in visited_refs:
                    visited_refs.add(name)
                    if visit(definitions.get(name, {})):
                        return True
            properties = value.get("properties", {})
            if isinstance(properties, dict):
                for name, child in properties.items():
                    if name in XZ_COUNT_KEYS or name.endswith("_xz_count"):
                        return True
                    if visit(child):
                        return True
            enum = value.get("enum", [])
            if any(
                isinstance(item, str) and item.lower() in
                (XZ_STATE_VALUES - {"unknown"})
                for item in enum
            ):
                return True
            for keyword in (
                "anyOf", "oneOf", "allOf", "items", "contains",
                "additionalProperties",
            ):
                child = value.get(keyword)
                if isinstance(child, list):
                    if any(visit(item) for item in child):
                        return True
                elif isinstance(child, dict) and visit(child):
                    return True
        elif isinstance(value, list):
            return any(visit(item) for item in value)
        return False

    return any(
        visit(definitions.get(root_name, {}))
        for root_name in ("successData", "successSummary")
    )


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
    actions: set[str] = set()
    applicable: set[str] = set(SEMANTIC_JSON_VALUE_ACTIONS)
    for path in sorted(schema_root.glob("*.response.schema.json")):
        action = path.name.removesuffix(".response.schema.json")
        actions.add(action)
        schema = json.loads(path.read_text(encoding="utf-8"))
        if explicit_xz_surface(schema):
            applicable.add(action)

    request_actions = {
        path.name.removesuffix(".request.schema.json")
        for path in schema_root.glob("*.request.schema.json")
    }
    if actions != request_actions or len(actions) != 73:
        raise RuntimeError("request/response schemas do not cover the same 73 actions")
    if applicable != EXPECTED_APPLICABLE:
        raise RuntimeError(
            "X/Z applicability changed: "
            f"expected={sorted(EXPECTED_APPLICABLE)!r}; "
            f"actual={sorted(applicable)!r}"
        )
    print(
        "X/Z applicability: OK "
        f"({len(applicable)} applicable actions; "
        f"{len(actions) - len(applicable)} non-observable actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
