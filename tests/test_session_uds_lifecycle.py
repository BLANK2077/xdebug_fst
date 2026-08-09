#!/usr/bin/env python3
"""Real subprocess/UDS lifecycle test; uses only open-source FST data."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor


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


def directory_tree_sha256(root: Path) -> str:
    digest = hashlib.sha256()

    def visit(directory: Path, relative: Path) -> None:
        for child in sorted(directory.iterdir(), key=lambda path: path.name):
            child_relative = relative / child.name
            relative_text = child_relative.as_posix()
            if child.is_dir() and not child.is_symlink():
                digest.update(f"D\n{relative_text}\n".encode())
                visit(child, child_relative)
            elif child.is_file() and not child.is_symlink():
                digest.update(f"F\n{relative_text}\n".encode())
                digest.update(child.read_bytes())
            else:
                raise AssertionError(f"unsupported directory member: {child}")

    visit(root, Path())
    return digest.hexdigest()


def main() -> int:
    executable = str(Path(sys.argv[1]).resolve())
    waveform = str(Path(sys.argv[2]).resolve())
    design_library = Path(sys.argv[3]).resolve()
    with tempfile.TemporaryDirectory(prefix="xdebug-fst-session-") as root:
        environment = os.environ.copy()
        environment["HOME"] = root
        environment["XVERIF_TEST_TMPDIR"] = root
        token = "1" * 64
        mismatch = "2" * 64

        # The frozen schema still advertises the original enum, but the
        # user-scoped implementation rejects TCP/file explicitly and never
        # falls back to UDS.
        for unsupported in ("tcp", "file"):
            unsupported_args = {"name": f"case_{unsupported}",
                                "transport": unsupported}
            if unsupported == "tcp":
                unsupported_args.update(
                    {"host": "127.0.0.1", "bind_host": "127.0.0.1", "port": 0}
                )
            expect_error(
                invoke(
                    executable,
                    environment,
                    request(
                        "session.open",
                        target={"fsdb": waveform},
                        args=unsupported_args,
                    ),
                ),
                "session.open",
                "TRANSPORT_UNAVAILABLE",
            )

        invalid_start_environment = environment.copy()
        invalid_start_environment["XDEBUG_SESSION_START_TIMEOUT_SEC"] = "0"
        expect_error(
            invoke(
                executable,
                invalid_start_environment,
                request(
                    "session.open",
                    target={"fsdb": waveform},
                    args={"name": "case_invalid_env"},
                ),
            ),
            "session.open",
            "INVALID_ENVIRONMENT",
        )

        provenance = Path(root) / "provenance"
        provenance.mkdir()
        manifest_waveform = provenance / "waves.fst"
        shutil.copy2(waveform, manifest_waveform)
        digest = hashlib.sha256(manifest_waveform.read_bytes()).hexdigest()
        manifest_document = {
            "schema_version": "xdebug.run-manifest.v1",
            "state": "published",
            "resources": {
                "fsdb": {
                    "path": "waves.fst",
                    "size_bytes": manifest_waveform.stat().st_size,
                    "sha256": digest,
                }
            },
        }
        manifest_path = provenance / "run.json"
        manifest_path.write_text(
            json.dumps(manifest_document), encoding="utf-8"
        )
        manifested = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={
                        "fsdb": str(manifest_waveform),
                        "run_manifest": str(manifest_path),
                    },
                    args={"name": "case_manifest"},
                ),
            ),
            "session.open",
        )
        run_manifest = manifested["data"]["run_manifest"]
        assert run_manifest["schema_version"] == "xdebug.run-manifest.v1"
        assert run_manifest["state"] == "published"
        assert run_manifest["manifest_path"] == str(manifest_path.resolve())
        assert run_manifest["resources"] == manifest_document["resources"]
        expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.close",
                    target={"session_id": "case_manifest"},
                    args={},
                ),
            ),
            "session.close",
        )

        mismatched_document = json.loads(json.dumps(manifest_document))
        mismatched_document["resources"]["fsdb"]["sha256"] = "0" * 64
        manifest_path.write_text(
            json.dumps(mismatched_document), encoding="utf-8"
        )
        provenance_error = expect_error(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={
                        "fsdb": str(manifest_waveform),
                        "run_manifest": str(manifest_path),
                    },
                    args={"name": "case_manifest_bad"},
                ),
            ),
            "session.open",
            "RESOURCE_PROVENANCE_MISMATCH",
        )
        assert provenance_error["error"]["expected_sha256"] == "0" * 64
        assert provenance_error["error"]["actual_sha256"] == digest

        combined_root = Path(root) / "combined"
        combined_bundle = combined_root / "bundle"
        combined_bundle.mkdir(parents=True)
        combined_waveform = combined_root / "waves.fst"
        shutil.copy2(waveform, combined_waveform)
        shutil.copy2(design_library, combined_bundle / "design.so")
        (combined_bundle / "xdebug-design-db.json").write_text(
            json.dumps({
                "schema_version": "xdebug.design-db-bundle.v1",
                "library": "design.so",
            }),
            encoding="utf-8",
        )
        combined_manifest = {
            "schema_version": "xdebug.run-manifest.v1",
            "state": "published",
            "resources": {
                "fsdb": {
                    "path": "waves.fst",
                    "size_bytes": combined_waveform.stat().st_size,
                    "sha256": hashlib.sha256(
                        combined_waveform.read_bytes()).hexdigest(),
                },
                "daidir": {
                    "path": "bundle",
                    "size_bytes": combined_bundle.stat().st_size,
                    "sha256": directory_tree_sha256(combined_bundle),
                },
            },
        }
        combined_manifest_path = combined_root / "run.json"
        combined_manifest_path.write_text(
            json.dumps(combined_manifest), encoding="utf-8"
        )
        combined = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={
                        "fsdb": str(combined_waveform),
                        "daidir": str(combined_bundle),
                        "run_manifest": str(combined_manifest_path),
                    },
                    args={"name": "case_combined_manifest"},
                ),
            ),
            "session.open",
        )
        assert combined["session"]["mode"] == "combined"
        assert combined["data"]["run_manifest"]["resources"] == \
            combined_manifest["resources"]
        expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.close",
                    target={"session_id": "case_combined_manifest"},
                    args={},
                ),
            ),
            "session.close",
        )

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

        same_resource = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={"fsdb": waveform},
                    args={"name": "case_same_resource"},
                ),
            ),
            "session.open",
        )
        assert same_resource["advisories"] == [{
            "code": "RESOURCE_SESSION_ALREADY_ALIVE",
            "severity": "info",
            "match_kind": "same_fsdb",
            "existing_session_id": "case_a",
            "existing_mode": "waveform",
            "message": "same resource already has an alive session; "
                       "consider closing one to save resources",
        }]
        expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.close",
                    target={"session_id": "case_same_resource"},
                    args={},
                ),
            ),
            "session.close",
        )

        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(2)
            client.connect(str(socket_path))
            malformed_control = {
                "api_version": "xdebug.internal.v1",
                "action": "server.quit",
                "args": {},
                "unexpected": True,
            }
            client.sendall(json.dumps(malformed_control).encode() + b"\n")
            received = b""
            while not received.endswith(b"\n"):
                received += client.recv(4096)
        rejected_control = json.loads(received)
        assert rejected_control["ok"] is False
        assert rejected_control["error"]["code"] == "INVALID_INTERNAL_REQUEST"

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

        # Two frontends racing on the same name must produce exactly one
        # active generation and one deterministic conflict.
        race_request = request(
            "session.open",
            target={"fsdb": waveform},
            args={"name": "case_race"},
        )
        with ThreadPoolExecutor(max_workers=2) as executor:
            race_results = list(
                executor.map(
                    lambda _: invoke(executable, environment, race_request),
                    range(2),
                )
            )
        assert sorted(result[0] for result in race_results) == [0, 1], race_results
        race_errors = [
            response["error"]["code"]
            for status, response in race_results
            if status != 0
        ]
        assert race_errors == ["SESSION_ID_EXISTS"], race_results
        expect_ok(
            invoke(
                executable,
                environment,
                request("session.close", target={"session_id": "case_race"}, args={}),
            ),
            "session.close",
        )

        # An existing but invalid FST makes the child exit before readiness;
        # the opening reservation must be compensated, not left active.
        invalid_waveform = Path(root) / "invalid.fst"
        invalid_waveform.write_text("not an fst\n", encoding="utf-8")
        failed_start = expect_error(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={"fsdb": str(invalid_waveform)},
                    args={"name": "case_bad"},
                ),
            ),
            "session.open",
            "SESSION_START_FAILED",
        )
        assert failed_start["error"]["exit_status"] != 0
        listed = expect_ok(
            invoke(executable, environment, request("session.list", args={})),
            "session.list",
        )
        assert listed["summary"]["session_count"] == 0

        # A hard engine crash is diagnosed and then reclaimed by session.gc.
        crashed = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={"fsdb": waveform},
                    args={"name": "case_crash"},
                ),
            ),
            "session.open",
        )
        os.kill(crashed["session"]["server_pid"], signal.SIGKILL)
        time.sleep(0.1)
        unhealthy = expect_error(
            invoke(
                executable,
                environment,
                request("session.doctor", target={"session_id": "case_crash"}, args={}),
            ),
            "session.doctor",
            "SESSION_UNHEALTHY",
        )
        assert unhealthy["error"]["health_status"] in {
            "process_exited", "socket_missing", "ping_failed"
        }
        collected = expect_ok(
            invoke(executable, environment, request("session.gc", args={})),
            "session.gc",
        )
        assert collected["summary"] == {
            "before_count": 1,
            "kept_count": 0,
            "removed_count": 1,
        }

        # Resource metadata is part of health, even when the server still
        # answers ping; GC must stop that proven generation before cleanup.
        mutable_waveform = Path(root) / "mutable.fst"
        shutil.copy2(waveform, mutable_waveform)
        changed = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.open",
                    target={"fsdb": str(mutable_waveform)},
                    args={"name": "case_changed"},
                ),
            ),
            "session.open",
        )
        with mutable_waveform.open("ab") as output:
            output.write(b"changed")
        changed_health = expect_error(
            invoke(
                executable,
                environment,
                request("session.doctor", target={"session_id": "case_changed"}, args={}),
            ),
            "session.doctor",
            "SESSION_UNHEALTHY",
        )
        assert changed_health["error"]["health_status"] == "fsdb_changed"
        expect_ok(
            invoke(executable, environment, request("session.gc", args={})),
            "session.gc",
        )
        assert not Path(changed["session"]["socket_path"]).exists()

        # session.list owns idle expiration and reports structured removal.
        idle_environment = environment.copy()
        idle_environment["XDEBUG_SESSION_IDLE_TIMEOUT_SEC"] = "1"
        idle = expect_ok(
            invoke(
                executable,
                idle_environment,
                request(
                    "session.open",
                    target={"fsdb": waveform},
                    args={"name": "case_idle"},
                ),
            ),
            "session.open",
        )
        time.sleep(1.1)
        expired = expect_ok(
            invoke(executable, idle_environment, request("session.list", args={})),
            "session.list",
        )
        assert expired["summary"]["session_count"] == 0
        assert expired["summary"]["expired_removed_count"] == 1
        assert expired["data"]["removed"][0]["reason"] == "idle_timeout"
        assert not Path(idle["session"]["socket_path"]).exists()

        for name in ("case_close_all_a", "case_close_all_b"):
            expect_ok(
                invoke(
                    executable,
                    environment,
                    request(
                        "session.open",
                        target={"fsdb": waveform},
                        args={"name": name},
                    ),
                ),
                "session.open",
            )
        close_all = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.close", target={"session_id": "all"}, args={}
                ),
            ),
            "session.close",
        )
        assert close_all["summary"] == {
            "requested_count": 2,
            "removed_count": 2,
        }
        assert {item["session_id"] for item in
                close_all["data"]["removed_sessions"]} == {
            "case_close_all_a", "case_close_all_b"
        }

        for name in ("case_kill_all_a", "case_kill_all_b"):
            expect_ok(
                invoke(
                    executable,
                    environment,
                    request(
                        "session.open",
                        target={"fsdb": waveform},
                        args={"name": name},
                    ),
                ),
                "session.open",
            )
        expect_error(
            invoke(
                executable,
                environment,
                request(
                    "session.kill",
                    target={"session_id": "all"},
                    args={"ownership_token": token},
                ),
            ),
            "session.kill",
            "SESSION_OWNERSHIP_TOKEN_FORBIDDEN",
        )
        kill_all = expect_ok(
            invoke(
                executable,
                environment,
                request(
                    "session.kill", target={"session_id": "all"}, args={}
                ),
            ),
            "session.kill",
        )
        assert kill_all["summary"] == {
            "requested_count": 2,
            "removed_count": 2,
        }
        assert {item["session_id"] for item in
                kill_all["data"]["removed_sessions"]} == {
            "case_kill_all_a", "case_kill_all_b"
        }

        # In stdio-loop the frontend remains the engine's parent. Closing in
        # that same process must reap the child instead of mistaking a zombie
        # for a still-running generation.
        loop = subprocess.Popen(
            [executable, "--stdio-loop", "--json"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )
        assert loop.stdin is not None and loop.stdout is not None
        ready = json.loads(loop.stdout.readline())
        assert ready["type"] == "ready"

        def loop_call(identifier: str, payload: dict) -> dict:
            payload = dict(payload)
            payload["id"] = identifier
            payload["payload_format"] = "json"
            loop.stdin.write(json.dumps(payload) + "\n")
            loop.stdin.flush()
            envelope = json.loads(loop.stdout.readline())
            assert envelope["id"] == identifier, envelope
            return envelope

        loop_open = loop_call(
            "loop-open",
            request(
                "session.open",
                target={"fsdb": waveform},
                args={"name": "case_loop"},
            ),
        )
        assert loop_open["ok"] is True, loop_open
        loop_close = loop_call(
            "loop-close",
            request("session.close", target={"session_id": "case_loop"}, args={}),
        )
        assert loop_close["ok"] is True, loop_close
        quit_envelope = loop_call("loop-quit", {"action": "stdio.quit"})
        assert quit_envelope["ok"] is True
        assert loop.wait(timeout=5) == 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
