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

    print(
        "truncation applicability: OK "
        f"({len(applicable)} applicable actions; "
        f"{len(not_applicable)} schema-unrepresentable actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
