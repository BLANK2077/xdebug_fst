from __future__ import annotations

import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "audit_xout_semantics", ROOT / "tools/audit_xout_semantics.py"
)
assert SPEC is not None and SPEC.loader is not None
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def event(action: str, response: dict, xout: str) -> dict:
    return {
        "action": action,
        "request": {"api_version": "xdebug.v1", "action": action},
        "response": response,
        "xout": xout,
        "test_node": "unit",
    }


def test_audit_accepts_compact_logic_value_and_complete_summary() -> None:
    report = AUDIT.audit(["signal.changes"], [event(
        "signal.changes",
        {
            "ok": True,
            "summary": {
                "signal": "top.bus", "analysis_complete": True,
                "response_truncated": False, "total_count": 1,
            },
            "data": {"changes": [{
                "time": "10ns",
                "value": {"value": "8'h000b", "bits": "00001011",
                          "known": True, "width": 8},
            }]},
        },
        "@xdebug.signal.changes.v1\nsummary:\n"
        "  signal: top.bus\n  analysis_complete: true\n"
        "  response_truncated: false\n  total_count: 1\n\n"
        "changes:\n  time  value\n  10ns  8'hb\n",
    )])
    assert report["failing_action_count"] == 0, report


def test_audit_rejects_missing_nested_values_and_silent_truncation() -> None:
    report = AUDIT.audit(["value.at"], [event(
        "value.at",
        {"ok": True, "summary": {"response_truncated": True}, "data": {
            "samples": [{"time": "10ns", "values": [{
                "key": "top.bus", "status": "ok",
                "value": {"value": "8'h0b", "known": True, "width": 8},
            }]}],
        }},
        "@xdebug.value.at.v1\nsamples:\n  time\n  10ns\n",
    )])
    failures = report["actions"][0]["failures"]
    assert any("values" in item for item in failures)
    assert any("8'hb" in item for item in failures)
    assert any("response_truncated" in item for item in failures)


def test_audit_reports_missing_catalog_action() -> None:
    report = AUDIT.audit(["actions", "schema"], [event(
        "actions", {"ok": True, "summary": {"action_count": 2}, "data": {}},
        "@xdebug.actions.v1\nsummary:\n  action_count: 2\n",
    )])
    assert report["passing_action_count"] == 1
    assert report["actions"][1]["failures"] == [
        "missing successful JSON/XOUT capture"
    ]


def test_actions_projection_requires_every_catalog_row() -> None:
    response = {
        "ok": True,
        "summary": {"action_count": 2, "total_action_count": 2},
        "data": {"actions": [
            {"name": "actions", "alternatives": ["schema"]},
            {"name": "schema", "examples": ["request"]},
        ]},
    }
    report = AUDIT.audit(["actions"], [event(
        "actions", response,
        "@xdebug.actions.v1\nsummary:\n  action_count: 2\n"
        "  total_action_count: 2\n\nactions:\n"
        "  actions  builtin\n  schema   builtin\n",
    )])
    assert report["failing_action_count"] == 0, report


def test_schema_projection_requires_summary_fields_and_sections() -> None:
    response = {
        "ok": True,
        "summary": {
            "action": "value.at", "kind": "request",
            "schema_path": "schemas/value-at.json",
        },
        "data": {
            "schema": {"properties": {"signal": {"type": "string"}}},
            "constraints": ["one signal is required"],
            "examples": [{"path": "examples/value-at.json", "value": {}}],
        },
    }
    report = AUDIT.audit(["schema"], [event(
        "schema", response,
        "@xdebug.schema.v1\nsummary:\n  action: value.at\n"
        "  kind: request\n  schema_path: schemas/value-at.json\n\n"
        "arguments:\n  name  type\n  signal string\n\n"
        "constraints:\n  one signal is required\n\n"
        "examples:\n  examples/value-at.json\n",
    )])
    assert report["failing_action_count"] == 0, report
