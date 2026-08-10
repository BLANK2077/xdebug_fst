import json
from pathlib import Path

import pytest

from runner import CliRunner


REPO_ROOT = Path(__file__).resolve().parents[1]
FROZEN_ACTIONS = json.loads(
    (REPO_ROOT / "compat/xdebug-v1/catalog.response.json").read_text(
        encoding="utf-8"
    )
)["data"]["actions"]


def test_unknown_top_level_field_is_rejected_before_handler(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "unexpected": 1,
    })
    assert not result.ok
    assert result.response["error"] == {
        "code": "INVALID_REQUEST",
        "message": "public request contains unknown field: unexpected",
        "recoverable": True,
        "error_layer": "schema",
        "invalid_arg": "unexpected",
        "expected": "one of api_version, request_id, action, target, args, limits",
        "received": 1,
        "received_type": "number",
    }


@pytest.mark.parametrize("action", FROZEN_ACTIONS)
def test_every_frozen_action_rejects_unknown_top_level_field(
    cli_runner: CliRunner, action: str
) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": action,
        "unexpected": {"must": "be rejected before dispatch"},
    })
    assert result.returncode == 1, result.stderr_raw
    assert result.response["ok"] is False
    assert result.response["action"] == action
    assert result.response["error"] == {
        "code": "INVALID_REQUEST",
        "message": "public request contains unknown field: unexpected",
        "recoverable": True,
        "error_layer": "schema",
        "invalid_arg": "unexpected",
        "expected": "one of api_version, request_id, action, target, args, limits",
        "received": {"must": "be rejected before dispatch"},
        "received_type": "object",
    }


def test_every_managed_resource_action_rejects_missing_session(
    cli_runner: CliRunner,
) -> None:
    catalog = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "args": {"output": {"verbose": True}},
    })
    assert catalog.ok, catalog.stderr_raw
    specs = catalog.response["data"]["actions"]
    assert {spec["name"] for spec in specs} == set(FROZEN_ACTIONS)
    assert {spec["name"] for spec in specs if spec["requires"] == "none"} == {
        "actions",
        "batch",
        "expr.normalize",
        "schema",
        "session.gc",
        "session.list",
    }
    assert {spec["name"] for spec in specs if spec["requires"] == "any"} == {
        "scope.roots",
        "session.open",
    }

    managed_specs = [
        spec for spec in specs
        if spec["requires"] in {
            "waveform", "design", "combined", "session"
        } or spec["name"] == "scope.roots"
    ]
    assert len(managed_specs) == 66
    for spec in managed_specs:
        assert spec["request_examples"], spec["name"]
        example_path = (
            REPO_ROOT / "compat/xdebug-v1" / spec["request_examples"][0]
        )
        example = json.loads(example_path.read_text(encoding="utf-8"))
        assert example["action"] == spec["name"]
        example["target"] = {"session_id": "missing_resource_session"}

        result = cli_runner.run(example)
        assert result.returncode == 1, (spec["name"], result.stderr_raw)
        assert result.response["action"] == spec["name"]
        assert result.response["error"] == {
            "code": "SESSION_NOT_FOUND",
            "error_layer": "session_manager",
            "message": "session generation is not in registry",
            "recoverable": True,
        }


def test_nested_unknown_field_is_rejected_by_action_schema(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "args": {"output": {"verbose": False, "unexpected": True}},
    })
    assert not result.ok
    assert result.response["error"]["code"] == "INVALID_REQUEST"
    assert result.response["error"]["error_layer"] == "schema"
    assert "unexpected" in result.response["error"]["invalid_arg"]


def test_mutually_exclusive_value_sources_are_rejected(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "value.at",
        "target": {"session_id": "missing"},
        "args": {"signal": "top.a", "list": "ctrl", "time": "1ns"},
    })
    assert not result.ok
    assert result.response["error"]["code"] == "INVALID_REQUEST"
    assert result.response["error"]["error_layer"] == "schema"


def test_wrong_type_and_missing_required_field_are_schema_errors(
        cli_runner: CliRunner) -> None:
    wrong_type = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "args": {"output": {"verbose": "yes"}},
    })
    assert not wrong_type.ok
    assert wrong_type.response["error"]["error_layer"] == "schema"

    missing = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "schema",
        "args": {"kind": "request"},
    })
    assert not missing.ok
    assert missing.response["error"]["error_layer"] == "schema"
    assert missing.response["error"]["invalid_arg"] == "args.action"


def test_actions_and_schema_responses_pass_runtime_contract(cli_runner: CliRunner) -> None:
    actions = cli_runner.run({
        "api_version": "xdebug.v1", "action": "actions", "args": {}
    })
    assert actions.ok, actions.response
    schema = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "schema",
        "args": {"action": "actions", "kind": "response"},
    })
    assert schema.ok, schema.response
