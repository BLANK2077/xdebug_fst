#!/usr/bin/env python3
"""Audit same-response JSON/XOUT captures for semantic loss and redundancy."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


SPECIAL_SUMMARY_PROJECTIONS = {"schema", "value.at"}
SPECIAL_COLLECTION_PROJECTIONS = {"actions", "schema"}
DOMAIN_COLLECTION_PROJECTIONS = {
    "scope.roots", "stream.query", "stream.export",
}
IGNORED_VALUE_KEYS = {
    "bits", "known", "width", "has_x", "has_z", "requested_value_format",
}
FORBIDDEN_TEXT = (
    "XOUT_BEGIN", "XOUT_END", "pointer\tkind\tvalue",
    " known=true", " known=false", " width_unknown",
)


def load_catalog(path: Path) -> list[str]:
    value = json.loads(path.read_text(encoding="utf-8"))
    actions = value["data"]["actions"]
    names = [item["name"] if isinstance(item, dict) else item for item in actions]
    if len(names) != len(set(names)):
        raise ValueError("catalog contains duplicate action names")
    return names


def load_events(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
            if not isinstance(event, dict):
                raise ValueError(f"{path}:{line_number}: event must be an object")
            events.append(event)
    return events


def is_logic_value(value: Any) -> bool:
    return (
        isinstance(value, dict)
        and isinstance(value.get("value"), str)
        and any(key in value for key in ("known", "bits", "width"))
    )


def compact_logic_literal(value: dict[str, Any]) -> str:
    literal = value["value"]
    match = re.fullmatch(r"([0-9]*)'([bBdDhH])(.+)", literal)
    if not match:
        return literal
    width, radix, body = match.groups()
    radix = radix.lower()
    if radix == "h":
        body = body.replace("_", "")
        if body and set(body.lower()) == {"x"}:
            body = "x"
        elif body and set(body.lower()) == {"z"}:
            body = "z"
        else:
            body = body.lstrip("0") or "0"
    return f"{width}'{radix}{body}"


def semantic_weight(value: Any) -> int:
    if is_logic_value(value):
        return 2
    if isinstance(value, dict):
        return sum(semantic_weight(item) for item in value.values())
    if isinstance(value, list):
        return len(value) + sum(semantic_weight(item) for item in value)
    return 1 if value is not None else 0


def richest_success(events: Iterable[dict[str, Any]]) -> dict[str, Any] | None:
    successful = [
        event for event in events
        if isinstance(event.get("response"), dict)
        and event["response"].get("ok") is True
        and isinstance(event.get("xout"), str)
    ]
    if not successful:
        return None
    return max(successful, key=lambda event: (
        semantic_weight(event["response"]), len(event["xout"])
    ))


def check_logic_values(value: Any, xout: str, path: str,
                       failures: list[str]) -> None:
    if is_logic_value(value):
        expected = compact_logic_literal(value)
        if expected not in xout:
            failures.append(f"missing logic value {path}={expected}")
        return
    if isinstance(value, dict):
        for key, item in value.items():
            if key in IGNORED_VALUE_KEYS:
                continue
            check_logic_values(item, xout, f"{path}.{key}", failures)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            check_logic_values(item, xout, f"{path}[{index}]", failures)


def check_nonempty_arrays(action: str, value: Any, xout: str, path: str,
                          failures: list[str]) -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            child_path = f"{path}.{key}" if path else key
            if isinstance(item, list) and item:
                label = re.sub(r"[^A-Za-z0-9_.-]", "_", key)
                value_matrix = action == "value.at" and child_path in {
                    "data.entries", "data.samples", "data.samples.values",
                }
                if (label not in xout and key not in {"actions", "values"}
                        and not value_matrix):
                    failures.append(f"missing non-empty collection {child_path}")
            check_nonempty_arrays(action, item, xout, child_path, failures)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            check_nonempty_arrays(action, item, xout,
                                  f"{path}[{index}]", failures)


def check_summary(action: str, response: dict[str, Any], xout: str,
                  failures: list[str]) -> None:
    if action in SPECIAL_SUMMARY_PROJECTIONS:
        return
    summary = response.get("summary")
    if not isinstance(summary, dict):
        return
    for key, value in summary.items():
        if key == "known" and value is True:
            continue
        if value is None or value == [] or value == {}:
            continue
        if isinstance(value, dict):
            if is_logic_value(value):
                expected = compact_logic_literal(value)
                if expected not in xout:
                    failures.append(f"missing summary field {key}")
                continue
            for nested_key, nested_value in value.items():
                if nested_value is None or isinstance(nested_value, (dict, list)):
                    continue
                if str(nested_value).lower() not in xout.lower():
                    failures.append(
                        f"missing summary field {key}.{nested_key}"
                    )
            continue
        if isinstance(value, list):
            continue
        rendered_key = re.sub(r"[^A-Za-z0-9_.-]", "_", key)
        if rendered_key not in xout:
            failures.append(f"missing summary field {key}")


def check_actions_projection(response: dict[str, Any], xout: str,
                             failures: list[str]) -> None:
    """Validate the intentionally compact verbose action-catalog table."""
    actions = response.get("data", {}).get("actions", [])
    if not isinstance(actions, list) or not actions:
        return
    if "actions:" not in xout:
        failures.append("missing actions table")
    for item in actions:
        name = item.get("name") if isinstance(item, dict) else item
        if not isinstance(name, str):
            continue
        row = re.compile(rf"^\s{{2}}{re.escape(name)}(?:\s|$)", re.MULTILINE)
        if not row.search(xout):
            failures.append(f"missing action catalog row {name}")


def check_schema_projection(response: dict[str, Any], xout: str,
                            failures: list[str]) -> None:
    """Validate schema's human-oriented synopsis, not its raw JSON Schema tree."""
    summary = response.get("summary", {})
    if isinstance(summary, dict):
        for key in ("action", "kind", "schema_path"):
            value = summary.get(key)
            if value is not None and str(value) not in xout:
                failures.append(f"missing schema summary value {key}")
    data = response.get("data", {})
    if not isinstance(data, dict):
        return
    schema = data.get("schema", {})
    properties = schema.get("properties", {}) if isinstance(schema, dict) else {}
    section = "arguments:" if summary.get("kind") == "request" else "fields:"
    if isinstance(properties, dict) and properties and section not in xout:
        failures.append(f"missing schema {section[:-1]} table")
    for key in ("constraints", "examples"):
        if isinstance(data.get(key), list) and data[key] and f"{key}:" not in xout:
            failures.append(f"missing schema {key} synopsis")


def check_session_projection(response: dict[str, Any], xout: str,
                             failures: list[str]) -> None:
    session = response.get("session")
    if not isinstance(session, dict):
        return
    for key in ("session_id", "mode", "transport"):
        value = session.get(key)
        if value is not None and str(value) not in xout:
            failures.append(f"missing session field {key}")


def audit_event(action: str, event: dict[str, Any]) -> tuple[list[str], list[str]]:
    response = event["response"]
    xout = event["xout"]
    failures: list[str] = []
    observations: list[str] = []
    expected_header = f"@xdebug.{action}.v1\n"
    if not xout.startswith(expected_header):
        failures.append("wrong XOUT header")
    if not xout.endswith("\n") or xout.endswith("\n\n"):
        failures.append("XOUT must end with exactly one newline")
    for forbidden in FORBIDDEN_TEXT:
        if forbidden in xout:
            failures.append(f"forbidden text {forbidden!r}")
    if re.search(r"\bbits=[01_]+\b", xout):
        failures.append("known value redundantly includes bits")
    if '{"api_version"' in xout or '"summary": {' in xout:
        failures.append("raw JSON leaked into XOUT")
    check_summary(action, response, xout, failures)
    if action == "actions":
        check_actions_projection(response, xout, failures)
    elif action == "schema":
        check_schema_projection(response, xout, failures)
    if action.startswith("session."):
        check_session_projection(response, xout, failures)
    if action not in SPECIAL_COLLECTION_PROJECTIONS | DOMAIN_COLLECTION_PROJECTIONS:
        check_nonempty_arrays(action, response.get("data", {}), xout,
                              "data", failures)
        check_nonempty_arrays(action, response.get("findings", []), xout,
                              "findings", failures)
    if action not in SPECIAL_COLLECTION_PROJECTIONS:
        check_logic_values(response.get("data", {}), xout, "data", failures)
        check_logic_values(response.get("findings", []), xout,
                           "findings", failures)
    summary = response.get("summary", {})
    if isinstance(summary, dict):
        if summary.get("response_truncated") is True and "truncated" not in xout:
            failures.append("response_truncated fact is not visible")
        if summary.get("analysis_complete") is False and "analysis_complete" not in xout:
            failures.append("incomplete analysis fact is not visible")
    if len(xout) > 16384:
        observations.append("XOUT exceeds 16 KiB; review density")
    return sorted(set(failures)), observations


def classify_variants(events: Iterable[dict[str, Any]]) -> set[str]:
    variants: set[str] = set()
    for event in events:
        response = event.get("response", {})
        request = event.get("request", {})
        if not response.get("ok"):
            variants.add("error")
            continue
        summary = response.get("summary", {})
        args = request.get("args", {}) if isinstance(request, dict) else {}
        if isinstance(args, dict) and args.get("value_format") in {"bin", "dec"}:
            variants.add("value_format")
        if isinstance(summary, dict):
            if summary.get("response_truncated") is True:
                variants.add("truncated")
            if summary.get("analysis_complete") is False:
                variants.add("incomplete")
            total = summary.get("total_count")
            if total == 0:
                variants.add("empty")
            if isinstance(total, int) and total > 1:
                variants.add("multiple")
        response_text = json.dumps(response, ensure_ascii=False).lower()
        if re.search(r"(?:'h|'b)[0-9a-f_]*[xz]", response_text):
            variants.add("x/z")
    return variants


def audit(catalog: list[str], events: list[dict[str, Any]]) -> dict[str, Any]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for event in events:
        action = event.get("action")
        if isinstance(action, str):
            grouped[action].append(event)
    rows: list[dict[str, Any]] = []
    for action in catalog:
        primary = richest_success(grouped[action])
        if primary is None:
            rows.append({
                "action": action, "status": "FAIL",
                "failures": ["missing successful JSON/XOUT capture"],
                "observations": [], "variants": sorted(classify_variants(grouped[action])),
            })
            continue
        failures, observations = audit_event(action, primary)
        rows.append({
            "action": action,
            "status": "PASS" if not failures else "FAIL",
            "failures": failures,
            "observations": observations,
            "variants": sorted(classify_variants(grouped[action])),
            "test_node": primary.get("test_node"),
            "request_sha256": hashlib.sha256(json.dumps(
                primary.get("request"), sort_keys=True,
                ensure_ascii=False,
            ).encode("utf-8")).hexdigest(),
            "xout_sha256": hashlib.sha256(primary["xout"].encode("utf-8")).hexdigest(),
        })
    return {
        "action_count": len(catalog),
        "passing_action_count": sum(row["status"] == "PASS" for row in rows),
        "failing_action_count": sum(row["status"] == "FAIL" for row in rows),
        "actions": rows,
    }


def markdown(report: dict[str, Any]) -> str:
    lines = [
        "# xdebug_fst XOUT 语义审计",
        "",
        f"- action：{report['passing_action_count']}/{report['action_count']} 通过",
        f"- 失败：{report['failing_action_count']}",
        "",
        "| Action | 结论 | 关键变体 | 发现 |",
        "| --- | --- | --- | --- |",
    ]
    for row in report["actions"]:
        findings = "; ".join(row["failures"] + row["observations"]) or "无"
        lines.append(
            f"| `{row['action']}` | {row['status']} | "
            f"{', '.join(row['variants']) or '-'} | {findings} |"
        )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument(
        "--catalog", type=Path,
        default=Path(__file__).resolve().parents[1] /
        "compat/xdebug-v1/catalog.response.json",
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    report = audit(load_catalog(args.catalog), load_events(args.trace))
    rendered_json = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    rendered_markdown = markdown(report)
    if args.json_out:
        args.json_out.write_text(rendered_json, encoding="utf-8")
    else:
        print(rendered_json, end="")
    if args.markdown_out:
        args.markdown_out.write_text(rendered_markdown, encoding="utf-8")
    if args.require_complete and report["failing_action_count"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
