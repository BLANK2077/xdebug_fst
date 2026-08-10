#!/usr/bin/env python3
"""Prove empty_result N/A from the exhaustive frozen success schemas."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from audit_action_coverage import RESULT_COUNT_KEYS, RESULT_LIST_KEYS
from check_compat_baseline import verify_frozen_files


EXPECTED_NOT_APPLICABLE = {
    "apb.config.load",
    "axi.config.load",
    "event.config.load",
    "expr.eval_at",
    "expr.normalize",
    "list.add",
    "list.delete",
    "schema",
    "session.doctor",
    "session.open",
    "signal.canonicalize",
    "stream.config.get",
    "stream.config.load",
    "stream.describe",
    "waveform.cursor.delete",
    "waveform.cursor.get",
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


def primary_cardinality_fields(schema: dict[str, Any]) -> set[str]:
    result: set[str] = set()

    def visit(value: Any) -> None:
        if isinstance(value, dict):
            for name, child in value.get("properties", {}).items():
                types = schema_types(child)
                if name in RESULT_COUNT_KEYS and "integer" in types:
                    result.add(name)
                if name in RESULT_LIST_KEYS and "array" in types:
                    result.add(name)
                if name in {"found", "diff_found"} and "boolean" in types:
                    result.add(name)
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
    unrepresentable: set[str] = set()
    for path in sorted(schema_root.glob("*.response.schema.json")):
        action = path.name.removesuffix(".response.schema.json")
        schema = json.loads(path.read_text(encoding="utf-8"))
        if not primary_cardinality_fields(schema):
            unrepresentable.add(action)

    if unrepresentable != EXPECTED_NOT_APPLICABLE:
        raise RuntimeError(
            "empty-result schema applicability changed: "
            f"expected={sorted(EXPECTED_NOT_APPLICABLE)!r}; "
            f"actual={sorted(unrepresentable)!r}"
        )
    print(
        "empty-result applicability: OK "
        f"({len(unrepresentable)} schema-unrepresentable actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
