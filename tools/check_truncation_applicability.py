#!/usr/bin/env python3
"""Prove truncation N/A from the exhaustive frozen success schemas."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from check_compat_baseline import verify_frozen_files


EXPECTED_NOT_APPLICABLE = {
    "actions",
    "apb.config.list",
    "apb.config.load",
    "axi.config.list",
    "axi.config.load",
    "batch",
    "event.config.load",
    "expr.eval_at",
    "expr.normalize",
    "list.add",
    "list.create",
    "list.delete",
    "list.first_change",
    "list.load",
    "list.show",
    "list.validate",
    "nwave.rc.generate",
    "schema",
    "session.close",
    "session.doctor",
    "session.gc",
    "session.kill",
    "session.list",
    "session.open",
    "signal.canonicalize",
    "stream.config.get",
    "stream.config.list",
    "stream.config.load",
    "stream.describe",
    "value.at",
    "verify.conditions",
    "waveform.cursor.delete",
    "waveform.cursor.get",
    "waveform.cursor.list",
    "waveform.cursor.set",
    "waveform.cursor.use",
}

EXPECTED_SEMANTIC_NOT_APPLICABLE = {
    "signal.resolve",
    "signal.xz_verify",
}


def schema_types(value: Any) -> set[str]:
    if not isinstance(value, dict):
        return set()
    result: set[str] = set()
    declared = value.get("type")
    if isinstance(declared, str):
        result.add(declared)
    elif isinstance(declared, list):
        result.update(item for item in declared if isinstance(item, str))
    for key in ("anyOf", "oneOf"):
        for branch in value.get(key, []):
            result.update(schema_types(branch))
    return result


def permits_exact(value: Any, expected: Any) -> bool:
    if not isinstance(value, dict):
        return False
    if value.get("const") == expected:
        return True
    if expected in value.get("enum", []):
        return True
    return any(
        permits_exact(branch, expected)
        for key in ("anyOf", "oneOf", "allOf")
        for branch in value.get(key, [])
    )


def truncation_fields(schema: dict[str, Any]) -> set[str]:
    """Return success-only fields that can encode actual truncation."""
    result: set[str] = set()

    def visit(value: Any) -> None:
        if isinstance(value, dict):
            for name, child in value.get("properties", {}).items():
                if name in {"response_truncated", "truncated"}:
                    boolean_can_be_true = (
                        "boolean" in schema_types(child)
                        and child.get("const") is not False
                    )
                    if permits_exact(child, True) or boolean_can_be_true:
                        result.add(name)
                elif name == "truncation_scopes":
                    constant = child.get("const")
                    constant_nonempty = isinstance(constant, list) and bool(constant)
                    array_can_be_nonempty = (
                        "array" in schema_types(child)
                        and child.get("maxItems") != 0
                        and constant != []
                    )
                    if constant_nonempty or array_can_be_nonempty:
                        result.add(name)
                elif name in {"termination", "reason", "kind"} and \
                        permits_exact(child, "limit"):
                    result.add(f"{name}=limit")
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    definitions = schema.get("$defs", {})
    visit(definitions.get("successData", {}))
    visit(definitions.get("successSummary", {}))
    return result


def require_timeout_only_limits(action: str, request: dict[str, Any]) -> None:
    limits = request.get("properties", {}).get("limits", {})
    properties = limits.get("properties", {})
    if set(properties) != {"timeout_ms"} or limits.get("additionalProperties") is not False:
        raise RuntimeError(f"{action} gained a public result limit")


def prove_semantic_scalar_actions(schema_root: Path) -> None:
    resolve_request = json.loads(
        (schema_root / "signal.resolve.request.schema.json").read_text(
            encoding="utf-8"
        )
    )
    require_timeout_only_limits("signal.resolve", resolve_request)
    resolve_args = resolve_request["properties"]["args"]
    if resolve_args.get("required") != ["signal"] or \
            set(resolve_args.get("properties", {})) != {"signal"} or \
            resolve_args.get("additionalProperties") is not False:
        raise RuntimeError("signal.resolve is no longer a single-leaf request")
    signal_description = resolve_args["properties"]["signal"].get("description", "")
    if "Final leaf signal path" not in signal_description or \
            "not expanded automatically" not in signal_description:
        raise RuntimeError("signal.resolve final-leaf invariant changed")

    xz_request = json.loads(
        (schema_root / "signal.xz_verify.request.schema.json").read_text(
            encoding="utf-8"
        )
    )
    require_timeout_only_limits("signal.xz_verify", xz_request)
    xz_args = xz_request["properties"]["args"]
    if set(xz_args.get("required", [])) != {
        "signal", "expected_state", "time_range"
    } or "line_limit" in xz_args.get("properties", {}):
        raise RuntimeError("signal.xz_verify gained row selection or a result limit")

    xz_response = json.loads(
        (schema_root / "signal.xz_verify.response.schema.json").read_text(
            encoding="utf-8"
        )
    )
    data_branches = xz_response["$defs"]["successData"]["anyOf"]
    for branch in data_branches:
        if set(branch.get("properties", {})) != {
            "time_range", "initial_value", "sample_time_semantics",
            "first_mismatch",
        }:
            raise RuntimeError("signal.xz_verify success data is no longer scalar")
    summary_branches = xz_response["$defs"]["successSummary"]["anyOf"]
    stop_reasons = {
        branch["properties"]["stop_reason"].get("const")
        for branch in summary_branches
    }
    if stop_reasons != {"window_end", "first_mismatch"}:
        raise RuntimeError("signal.xz_verify early-stop lifecycle changed")


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
    prove_semantic_scalar_actions(schema_root)
    not_applicable: set[str] = set()
    applicable: dict[str, set[str]] = {}
    for path in sorted(schema_root.glob("*.response.schema.json")):
        action = path.name.removesuffix(".response.schema.json")
        schema = json.loads(path.read_text(encoding="utf-8"))
        fields = truncation_fields(schema)
        if fields:
            applicable[action] = fields
        else:
            not_applicable.add(action)

    if not_applicable != EXPECTED_NOT_APPLICABLE:
        raise RuntimeError(
            "truncation schema applicability changed: "
            f"expected={sorted(EXPECTED_NOT_APPLICABLE)!r}; "
            f"actual={sorted(not_applicable)!r}"
        )
    if len(applicable) + len(not_applicable) != 73:
        raise RuntimeError("truncation applicability does not cover 73 actions")

    runtime_applicable = set(applicable) - EXPECTED_SEMANTIC_NOT_APPLICABLE
    if len(runtime_applicable) != 35:
        raise RuntimeError(
            f"expected 35 runtime-applicable truncation actions, got "
            f"{len(runtime_applicable)}"
        )

    print(
        "truncation applicability: OK "
        f"({len(runtime_applicable)} runtime-applicable actions; "
        f"{len(EXPECTED_SEMANTIC_NOT_APPLICABLE)} scalar-lifecycle actions; "
        f"{len(not_applicable)} schema-unrepresentable actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
