#!/usr/bin/env python3
"""Real subprocess/UDS lifecycle test; uses only open-source FST data."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(executable: str, environment: dict[str, str], request: dict) -> tuple[int, dict]:
    completed = subprocess.run(
        [executable, "--json", "-"],
        input=json.dumps(request),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment,
        timeout=15,
        check=False,
    )
    try:
        response = json.loads(completed.stdout)
    except json.JSONDecodeError as exception:
        raise AssertionError(
            f"invalid JSON response; rc={completed.returncode}; "
            f"stdout={completed.stdout!r}; stderr={completed.stderr!r}"
        ) from exception
    return completed.returncode, response


def request(action: str, *, target: dict | None = None,
            args: dict | None = None) -> dict:
    value = {"api_version": "xdebug.v1", "action": action}
    if target is not None:
        value["target"] = target
    if args is not None:
        value["args"] = args
    return value


def expect_ok(result: tuple[int, dict], action: str) -> dict:
    status, response = result
    assert status == 0, response
    assert response["ok"] is True and response["action"] == action, response
    assert response["error"] is None, response
    return response


def expect_error(result: tuple[int, dict], action: str, code: str) -> dict:
    status, response = result
    assert status == 1, response
    assert response["ok"] is False and response["action"] == action, response
    assert response["error"]["code"] == code, response
    return response


def main() -> int:
    executable = str(Path(sys.argv[1]).resolve())
    waveform = str(Path(sys.argv[2]).resolve())
    with tempfile.TemporaryDirectory(prefix="xdebug-fst-session-") as root:
        environment = os.environ.copy()
        environment["HOME"] = root
        environment["XVERIF_TEST_TMPDIR"] = root
        token = "1" * 64
        mismatch = "2" * 64

        open_request = request(
            "session.open",
            target={"fsdb": waveform},
            args={"name": "case_a", "ownership_token": token},
        )
        opened = expect_ok(invoke(executable, environment, open_request), "session.open")
        assert opened["session"]["mode"] == "waveform"
        assert opened["session"]["transport"] == "uds"
        socket_path = Path(opened["session"]["socket_path"])
        assert socket_path.is_socket()
        assert socket_path.stat().st_mode & 0o777 == 0o600

        expect_error(
            invoke(executable, environment, open_request),
            "session.open",
            "SESSION_ID_EXISTS",
        )
        listed = expect_ok(
            invoke(executable, environment, request("session.list", args={})),
            "session.list",
        )
        assert listed["summary"] == {"session_count": 1, "expired_removed_count": 0}

        expect_ok(
            invoke(
                executable,
                environment,
                request("session.doctor", target={"session_id": "case_a"}, args={}),
            ),
            "session.doctor",
        )
        # The error is produced by the persistent child engine, proving that
        # non-lifecycle public requests traverse the UDS route.
        expect_error(
            invoke(
                executable,
                environment,
                request(
                    "trace.active_driver",
                    target={"session_id": "case_a"},
                    args={"signal": "top.u.ready", "time": "120ns"},
                ),
            ),
            "trace.active_driver",
            "DESIGN_NOT_LOADED",
        )
        expect_error(
            invoke(
                executable,
                environment,
                request(
                    "session.kill",
                    target={"session_id": "case_a"},
                    args={"ownership_token": mismatch},
                ),
            ),
            "session.kill",
            "SESSION_OWNERSHIP_TOKEN_MISMATCH",
        )
        expect_ok(
            invoke(
                executable,
                environment,
                request("session.doctor", target={"session_id": "case_a"}, args={}),
            ),
            "session.doctor",
        )
        expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.kill",
                    target={"session_id": "case_a"},
                    args={"ownership_token": token},
                ),
            ),
            "session.kill",
        )
        assert not socket_path.exists()
        listed = expect_ok(
            invoke(executable, environment, request("session.list", args={})),
            "session.list",
        )
        assert listed["summary"] == {"session_count": 0, "expired_removed_count": 0}
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
