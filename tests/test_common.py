# test_common.py — common/session/batch actions (BSD-3-Clause)
"""actions, schema, batch, session.open/close/list/doctor/gc/kill."""
from __future__ import annotations

from pathlib import Path

import pytest

from conftest import open_session
from runner import CliRunner, StdioLoopRunner


ALL_ACTIONS = [
    "actions", "schema", "batch",
    "session.open", "session.close", "session.list", "session.doctor",
    "session.gc", "session.kill",
    "apb.config.list", "apb.config.load", "apb.query", "apb.statistics",
    "apb.transaction.cursor", "apb.transfer_window",
    "axi.analysis", "axi.channel_stall", "axi.config.list", "axi.config.load",
    "axi.export", "axi.latency_outlier", "axi.outstanding_timeline",
    "axi.query", "axi.request_response_pair", "axi.statistics",
    "axi.transaction.cursor",
    "counter.statistics", "event.config.list", "event.config.load",
    "event.export", "event.find", "expr.eval_at", "expr.normalize",
    "list.add", "list.create", "list.delete", "list.export",
    "list.first_change", "list.load", "list.show", "list.validate",
    "nwave.rc.generate", "protocol.handshake.inspect",
    "scope.list", "scope.roots",
    "signal.anomaly.inspect", "signal.canonicalize", "signal.changes",
    "signal.resolve", "signal.sampled_pulse.inspect", "signal.stability",
    "signal.statistics", "signal.xz_verify",
    "stream.config.get", "stream.config.list", "stream.config.load",
    "stream.describe", "stream.export", "stream.query", "stream.validate",
    "trace.active_driver", "trace.active_driver_chain", "trace.driver",
    "trace.load", "trace.x_origin", "value.at", "verify.conditions",
    "waveform.cursor.delete", "waveform.cursor.get", "waveform.cursor.list",
    "waveform.cursor.set", "waveform.cursor.use", "window.verify",
]


def test_actions_catalog(cli_runner: CliRunner) -> None:
    """actions returns the full catalog; every action from the reference list
    is present (xdebug parity)."""
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "actions"})
    assert result.ok, result.stderr_raw
    got = result.response["data"]["actions"]
    assert got == sorted(ALL_ACTIONS)
    assert result.response["summary"]["action_count"] == 73
    assert result.response["summary"]["total_action_count"] == 73
    assert "clock_point_query" not in got


def test_actions_entries_have_category_and_requires(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "args": {"output": {"verbose": True}},
    })
    assert result.ok
    for entry in result.response["data"]["actions"]:
        assert entry["name"]
        assert entry["category"] in {"waveform", "design", "combined", "builtin", "session"}
        assert entry["requires"] in {"waveform", "design", "combined", "any",
                                     "none", "session"}
        assert entry["request_schema"].endswith(".request.schema.json")
        assert entry["response_schema"].endswith(".response.schema.json")


def test_actions_catalog_empty_filter(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "actions",
        "args": {"filter": {"keyword": "no_such_catalog_keyword_7f4d"}},
    })
    assert result.ok, result.stderr_raw
    assert result.response["summary"]["action_count"] == 0
    assert result.response["data"]["actions"] == []


def test_schema_action(cli_runner: CliRunner) -> None:
    result = cli_runner.run({
        "api_version": "xdebug.v1",
        "action": "schema",
        "args": {"action": "value.at", "kind": "request"},
    })
    assert result.ok
    assert result.response["summary"]["action"] == "value.at"
    assert result.response["summary"]["kind"] == "request"
    assert result.response["data"]["schema"]["$id"] == "xdebug.value.at.request.v1"
    assert result.response["data"]["schema_path"] == \
        "schemas/v1/actions/value.at.request.schema.json"
    assert result.response["data"]["examples"]


def test_batch_aggregates_responses(loop_runner: StdioLoopRunner,
                                    counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("batch", args={
        "requests": [
             {"api_version": "xdebug.v1", "action": "value.at",
             "target": {"session_id": "test"},
             "args": {"signal": "top.clk", "time": "100ps",
                      "render_time_unit": "ps"}},
            {"api_version": "xdebug.v1", "action": "value.at",
             "target": {"session_id": "test"},
             "args": {"signal": "top.clk", "time": "200ps",
                      "render_time_unit": "ps"}},
        ]
    })
    assert rsp.get("ok"), rsp
    results = rsp["data"]["results"]
    assert rsp["summary"] == {"count": 2, "all_ok": True,
                              "failed_count": 0, "failed_indexes": [],
                              "failed_codes": [], "failed_layers": []}
    assert len(results) == 2
    assert all(result["ok"] for result in results)
    assert results[0]["data"]["samples"][0]["time"] == "100ps"
    assert results[1]["data"]["samples"][0]["time"] == "200ps"


def test_batch_aggregates_child_failure(loop_runner: StdioLoopRunner,
                                        counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("batch", args={"requests": [
        {"api_version": "xdebug.v1", "action": "value.at",
         "target": {"session_id": "test"},
         "args": {"signal": "top.clk", "time": "100ps"}},
        {"api_version": "xdebug.v1", "action": "value.at",
         "target": {"session_id": "test"},
         "args": {"time": "100ps"}},
    ]})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["all_ok"] is False
    assert rsp["summary"]["failed_count"] == 1
    assert rsp["summary"]["failed_indexes"] == [1]
    assert rsp["summary"]["failed_codes"] == ["INVALID_REQUEST"]
    assert rsp["summary"]["failed_layers"] == ["schema"]


def test_batch_missing_requests(cli_runner: CliRunner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "batch"})
    assert not result.ok
    assert result.response["error"]["code"] == "INVALID_REQUEST"
    assert result.response["error"]["error_layer"] == "schema"


def test_session_open_and_close(loop_runner: StdioLoopRunner,
                                counter_fst) -> None:
    rsp = open_session(loop_runner, counter_fst)
    assert rsp["session"]["mode"] == "waveform"
    assert rsp["session"]["fsdb"] == str(counter_fst)
    rsp = loop_runner.request("session.close")
    assert rsp.get("ok")
    assert rsp["summary"]["removed"] is True
    assert rsp["session"] is None
    assert rsp["data"]["removed_session"]["session_id"] == "test"


def test_session_open_with_design_db(loop_runner: StdioLoopRunner,
                                     counter_fst, counter_design_db) -> None:
    rsp = open_session(loop_runner, counter_fst, counter_design_db)
    assert rsp["session"]["mode"] == "combined"
    assert rsp["session"]["daidir"] == str(counter_design_db)


def test_session_open_missing_file(loop_runner: StdioLoopRunner) -> None:
    rsp = loop_runner.request("session.open", target={
        "fsdb": "/nonexistent/waves.fst"}, args={"name": "bad"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "WAVEFORM_OPEN_FAILED"


def test_session_list_and_doctor(loop_runner: StdioLoopRunner,
                                 counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("session.list")
    assert rsp.get("ok")
    assert rsp["data"]["sessions"][0]["session_id"] == "test"
    rsp = loop_runner.request("session.doctor")
    assert rsp.get("ok")
    assert rsp["summary"]["healthy"] is True


def test_session_list_empty(cli_runner: CliRunner, tmp_path) -> None:
    environment = dict(cli_runner.env)
    environment["HOME"] = str(tmp_path)
    environment["XVERIF_TEST_TMPDIR"] = str(tmp_path)
    isolated = CliRunner(
        Path(cli_runner.command[0]), cwd=Path(cli_runner.cwd), env=environment
    )
    result = isolated.run({
        "api_version": "xdebug.v1", "action": "session.list", "args": {}
    })
    assert result.ok, result.stderr_raw
    assert result.response["summary"]["session_count"] == 0
    assert result.response["data"] == {"sessions": []}


def test_session_gc_and_kill(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("session.gc")
    assert rsp.get("ok")
    rsp = loop_runner.request("session.kill")
    assert rsp.get("ok")


def test_session_multi_result_lifecycle_on_direct_raw_fst(
        loop_runner: StdioLoopRunner, counter_fst) -> None:
    cleared = loop_runner.request(
        "session.close", target={"session_id": "all"}, args={})
    assert cleared.get("ok"), cleared
    for name in ("multiple_close_a", "multiple_close_b"):
        opened = loop_runner.request(
            "session.open", target={"fsdb": str(counter_fst)},
            args={"name": name})
        assert opened.get("ok"), opened

    listed = loop_runner.request("session.list")
    assert listed.get("ok"), listed
    assert listed["summary"]["session_count"] == 2
    assert len(listed["data"]["sessions"]) == 2

    collected = loop_runner.request("session.gc")
    assert collected.get("ok"), collected
    assert collected["summary"]["before_count"] == 2
    assert collected["summary"]["kept_count"] == 2
    assert len(collected["data"]["kept_sessions"]) == 2

    closed = loop_runner.request(
        "session.close", target={"session_id": "all"}, args={})
    assert closed.get("ok"), closed
    assert closed["summary"] == {"requested_count": 2, "removed_count": 2}
    assert len(closed["data"]["removed_sessions"]) == 2

    for name in ("multiple_kill_a", "multiple_kill_b"):
        opened = loop_runner.request(
            "session.open", target={"fsdb": str(counter_fst)},
            args={"name": name})
        assert opened.get("ok"), opened
    killed = loop_runner.request(
        "session.kill", target={"session_id": "all"}, args={})
    assert killed.get("ok"), killed
    assert killed["summary"] == {"requested_count": 2, "removed_count": 2}
    assert len(killed["data"]["removed_sessions"]) == 2


def test_unknown_action(cli_runner: CliRunner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1", "action": "no.such.action"})
    assert not result.ok
    assert result.response["error"]["code"] == "UNKNOWN_ACTION"


def test_missing_action(cli_runner: CliRunner) -> None:
    result = cli_runner.run({"api_version": "xdebug.v1"})
    assert not result.ok
    assert result.response["error"]["code"] == "INVALID_REQUEST"
    assert result.response["error"]["error_layer"] == "internal"
