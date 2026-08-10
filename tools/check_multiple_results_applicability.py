#!/usr/bin/env python3
"""Freeze which public actions can return more than one primary result."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterator

from audit_action_coverage import RESULT_COUNT_KEYS, RESULT_LIST_KEYS
from check_compat_baseline import verify_frozen_files


EXPECTED_SCHEMA_UNREPRESENTABLE = {
    "apb.config.load", "axi.config.load", "event.config.load",
    "expr.eval_at", "expr.normalize", "list.add", "list.delete",
    "list.load", "schema", "session.doctor", "session.open",
    "signal.canonicalize", "stream.config.get", "stream.config.load",
    "stream.describe", "waveform.cursor.delete", "waveform.cursor.get",
    "waveform.cursor.set", "waveform.cursor.use",
}

# This response can represent a collection for shared design machinery, but the
# frozen public request contract makes cardinality > 1 unreachable: resolve
# accepts one exact final-leaf signal path and does not expand aggregates or
# patterns.  session.close/session.kill remain applicable because the public
# scalar session_id has the reserved value "all" for batch cleanup.
EXPECTED_SEMANTIC_NOT_APPLICABLE = {
    "signal.resolve": "args.signal is one exact final-leaf signal path",
}


def same_level_branches(value: Any) -> Iterator[dict[str, Any]]:
    """Traverse only schema combiners for one response object level."""
    if not isinstance(value, dict):
        return
    yield value
    for keyword in ("anyOf", "oneOf", "allOf"):
        for branch in value.get(keyword, []):
            yield from same_level_branches(branch)


def schema_types(value: Any) -> set[str]:
    if not isinstance(value, dict):
        return set()
    result: set[str] = set()
    declared = value.get("type")
    if isinstance(declared, str):
        result.add(declared)
    elif isinstance(declared, list):
        result.update(item for item in declared if isinstance(item, str))
    for keyword in ("anyOf", "oneOf", "allOf"):
        for branch in value.get(keyword, []):
            result.update(schema_types(branch))
    return result


def permits_more_than_one(value: dict[str, Any], kind: str) -> bool:
    def qualifies(candidate: Any) -> bool:
        if kind == "integer":
            return (isinstance(candidate, int) and
                    not isinstance(candidate, bool) and candidate >= 2)
        return isinstance(candidate, list) and len(candidate) >= 2

    if "const" in value:
        return qualifies(value["const"])
    if "enum" in value:
        return any(qualifies(candidate) for candidate in value["enum"])
    upper_bound = value.get("maximum" if kind == "integer" else "maxItems")
    return upper_bound is None or upper_bound >= 2


def multiple_result_fields(schema: dict[str, Any]) -> set[str]:
    result: set[str] = set()
    definitions = schema.get("$defs", {})
    for root_name in ("successData", "successSummary"):
        for branch in same_level_branches(definitions.get(root_name, {})):
            for name, field in branch.get("properties", {}).items():
                types = schema_types(field)
                if (name in RESULT_COUNT_KEYS and "integer" in types and
                        permits_more_than_one(field, "integer")):
                    result.add(f"{root_name}.{name}")
                if (name in RESULT_LIST_KEYS and "array" in types and
                        permits_more_than_one(field, "array")):
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
    representable: dict[str, set[str]] = {}
    for path in sorted(schema_root.glob("*.response.schema.json")):
        action = path.name.removesuffix(".response.schema.json")
        actions.add(action)
        fields = multiple_result_fields(json.loads(path.read_text(encoding="utf-8")))
        if fields:
            representable[action] = fields

    request_actions = {
        path.name.removesuffix(".request.schema.json")
        for path in schema_root.glob("*.request.schema.json")
    }
    if actions != request_actions or len(actions) != 73:
        raise RuntimeError("request/response schemas do not cover the same 73 actions")
    unrepresentable = actions - set(representable)
    if unrepresentable != EXPECTED_SCHEMA_UNREPRESENTABLE:
        raise RuntimeError(
            "multiple-results schema applicability changed: "
            f"expected={sorted(EXPECTED_SCHEMA_UNREPRESENTABLE)!r}; "
            f"actual={sorted(unrepresentable)!r}"
        )
    semantic = set(EXPECTED_SEMANTIC_NOT_APPLICABLE)
    if not semantic <= set(representable):
        raise RuntimeError("semantic N/A actions must remain schema-representable")

    applicable = set(representable) - semantic
    if len(applicable) != 53 or len(unrepresentable | semantic) != 20:
        raise RuntimeError("multiple-results applicability must partition all 73 actions")
    print(
        "multiple-results applicability: OK "
        f"({len(applicable)} applicable actions; "
        f"{len(unrepresentable)} schema-unrepresentable actions; "
        f"{len(semantic)} semantic-scalar actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
