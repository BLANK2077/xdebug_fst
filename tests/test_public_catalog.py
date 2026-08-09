import json
from pathlib import Path

from runner import CliRunner


REPO_ROOT = Path(__file__).resolve().parents[1]


def test_compact_catalog_matches_frozen_original(cli_runner: CliRunner) -> None:
    original = json.loads(
        (REPO_ROOT / "compat/xdebug-v1/catalog.response.json").read_text(encoding="utf-8")
    )
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "actions", "args": {}})
    assert result.ok, result.stderr_raw
    assert result.response["summary"] == original["summary"]
    assert result.response["data"] == original["data"]


def test_catalog_filter_and_verbose_metadata(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "args": {
            "filter": {"category": ["combined"], "purposes": ["trace"]},
            "output": {"verbose": True},
        },
    })
    assert result.ok, result.stderr_raw
    assert result.response["summary"] == {
        "action_count": 3,
        "total_action_count": 73,
        "verbose": True,
        "filtered": True,
    }
    assert [item["name"] for item in result.response["data"]["actions"]] == [
        "trace.active_driver",
        "trace.active_driver_chain",
        "trace.x_origin",
    ]
    for item in result.response["data"]["actions"]:
        assert item["category"] == "combined"
        assert item["requires"] == "combined"
        assert item["request_schema"].startswith("schemas/v1/actions/")


def test_removed_clock_point_query_is_rejected(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "clock_point_query",
        "args": {},
    })
    assert not result.ok
    assert result.response["error"]["code"] == "UNKNOWN_ACTION"
