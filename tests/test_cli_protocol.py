import json
import os
import subprocess


def run_cli(xfst_bin, repo_root, arguments, payload):
    return subprocess.run(
        [str(xfst_bin), *arguments],
        input=payload,
        text=True,
        capture_output=True,
        cwd=repo_root,
        env=dict(os.environ),
        check=False,
    )


def test_one_shot_json_and_default_xout(xfst_bin, repo_root) -> None:
    request = json.dumps({
        "api_version": "xdebug.v1", "action": "actions", "args": {}
    }) + "\n"
    json_result = run_cli(xfst_bin, repo_root, ["--json", "-"], request)
    assert json_result.returncode == 0
    response = json.loads(json_result.stdout)
    assert response["ok"] is True
    assert response["data"]["actions"] == sorted(response["data"]["actions"])

    xout_result = run_cli(xfst_bin, repo_root, ["-"], request)
    assert xout_result.returncode == 0
    assert xout_result.stdout.startswith("@xdebug.actions.v1\nsummary:\n")
    assert "\nbuiltin:\n  actions\n  batch\n  schema\n" in xout_result.stdout
    assert xout_result.stdout.endswith(
        "combined:\n  trace.active_driver\n  trace.active_driver_chain\n"
        "  trace.x_origin\n"
    )


def test_one_shot_errors_use_selected_format_and_exit_one(xfst_bin, repo_root) -> None:
    invalid = "{bad\n"
    json_result = run_cli(xfst_bin, repo_root, ["--json", "-"], invalid)
    assert json_result.returncode == 1
    assert json.loads(json_result.stdout)["error"]["code"] == "INVALID_JSON"

    xout_result = run_cli(xfst_bin, repo_root, ["-"], invalid)
    assert xout_result.returncode == 1
    assert xout_result.stdout.startswith("@xdebug.error.v1\n\naction")
    assert "code       : INVALID_JSON" in xout_result.stdout


def test_stdio_loop_ready_override_error_and_quit_envelopes(xfst_bin, repo_root) -> None:
    requests = [
        {
            "api_version": "xdebug.v1",
            "request_id": "a1",
            "action": "actions",
            "args": {},
            "payload_format": "xout",
        },
        {
            "api_version": "xdebug.v1",
            "request_id": "u1",
            "action": "no.such.action",
            "payload_format": "json",
        },
    ]
    payload = "\n".join(json.dumps(item) for item in requests)
    payload += ("\n{bad\n"
                '{"api_version":"xdebug.v1","request_id":"q1",'
                '"action":"stdio.quit"}\n')
    result = run_cli(xfst_bin, repo_root, ["--stdio-loop", "--json"], payload)
    assert result.returncode == 0
    lines = [json.loads(line) for line in result.stdout.splitlines()]
    assert lines[0]["type"] == "ready"
    assert lines[0]["protocol"] == "xdebug-stdio-loop"
    assert isinstance(lines[0]["pid"], int)
    assert lines[1]["id"] == "a1"
    assert lines[1]["payload_format"] == "xout"
    assert lines[1]["xout"].startswith("@xdebug.actions.v1\n")
    assert lines[2]["id"] == "u1"
    assert lines[2]["payload_format"] == "json"
    assert lines[2]["error"]["code"] == "UNKNOWN_ACTION"
    assert lines[2]["json"]["error"] == lines[2]["error"]
    assert lines[3]["id"] == "req-3"
    assert lines[3]["error"]["code"] == "INVALID_JSON"
    assert lines[4] == {
        "id": "q1",
        "ok": True,
        "api_version": "xdebug.v1",
        "action": "stdio.quit",
        "payload_format": "json",
        "json": {"ok": True, "action": "stdio.quit"},
    }


def test_generic_xout_does_not_silently_limit_object_rows(
        xfst_bin, repo_root) -> None:
    children = [{
        "api_version": "xdebug.v1", "action": "schema",
        "args": {"action": "value.at", "kind": "request"},
    } for _ in range(25)]
    request = json.dumps({
        "api_version": "xdebug.v1", "action": "batch",
        "args": {"requests": children},
    }) + "\n"
    result = run_cli(xfst_bin, repo_root, ["-"], request)
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith("@xdebug.batch.v1\n")
    assert "(+ 5 more)" not in result.stdout
    assert result.stdout.count("xdebug.v1") >= 25
