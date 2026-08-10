#!/usr/bin/env python3
"""Freeze actions whose success contract exposes direct completeness state."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterator

from audit_action_coverage import COMPLETENESS_KEYS
from check_compat_baseline import verify_frozen_files


EXPECTED_NOT_APPLICABLE = {
    "actions", "apb.config.list", "apb.config.load", "axi.config.list",
    "axi.config.load", "batch", "event.config.load", "expr.normalize",
    "list.add", "list.create", "list.delete", "list.load", "list.show",
    "list.validate", "nwave.rc.generate", "schema", "session.close",
    "session.doctor", "session.gc", "session.kill", "session.list",
    "session.open", "signal.canonicalize", "stream.config.get",
    "stream.config.list", "stream.config.load", "stream.describe",
    "waveform.cursor.delete", "waveform.cursor.get", "waveform.cursor.list",
    "waveform.cursor.set", "waveform.cursor.use",
}


def same_level_branches(value: Any) -> Iterator[dict[str, Any]]:
    if not isinstance(value, dict):
        return
    yield value
    for keyword in ("anyOf", "oneOf", "allOf"):
        for branch in value.get(keyword, []):
            yield from same_level_branches(branch)


def permits_boolean(value: Any) -> bool:
    if not isinstance(value, dict):
        return False
    declared = value.get("type")
    if declared == "boolean" or (
            isinstance(declared, list) and "boolean" in declared):
        return True
    if isinstance(value.get("const"), bool):
        return True
    if any(isinstance(item, bool) for item in value.get("enum", [])):
        return True
    return any(
        permits_boolean(branch)
        for keyword in ("anyOf", "oneOf", "allOf")
        for branch in value.get(keyword, [])
    )


def completeness_fields(schema: dict[str, Any]) -> set[str]:
    result: set[str] = set()
    definitions = schema.get("$defs", {})
    for root_name in ("successData", "successSummary"):
        for branch in same_level_branches(definitions.get(root_name, {})):
            for name, field in branch.get("properties", {}).items():
                if name in COMPLETENESS_KEYS and permits_boolean(field):
                    result.add(f"{root_name}.{name}")
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
    actions: set[str] = set()
    applicable: dict[str, set[str]] = {}
    for path in sorted(schema_root.glob("*.response.schema.json")):
        action = path.name.removesuffix(".response.schema.json")
        actions.add(action)
        fields = completeness_fields(json.loads(path.read_text(encoding="utf-8")))
        if fields:
            applicable[action] = fields

    request_actions = {
        path.name.removesuffix(".request.schema.json")
        for path in schema_root.glob("*.request.schema.json")
    }
    if actions != request_actions or len(actions) != 73:
        raise RuntimeError("request/response schemas do not cover the same 73 actions")
    not_applicable = actions - set(applicable)
    if not_applicable != EXPECTED_NOT_APPLICABLE:
        raise RuntimeError(
            "completeness applicability changed: "
            f"expected={sorted(EXPECTED_NOT_APPLICABLE)!r}; "
            f"actual={sorted(not_applicable)!r}"
        )
    if len(applicable) != 41 or len(not_applicable) != 32:
        raise RuntimeError("completeness applicability must partition all 73 actions")
    print(
        "completeness applicability: OK "
        f"({len(applicable)} applicable actions; "
        f"{len(not_applicable)} schema-unrepresentable actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
