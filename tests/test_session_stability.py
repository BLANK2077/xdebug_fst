#!/usr/bin/env python3
"""Concurrent and repeated UDS lifecycle stability gate."""

from __future__ import annotations

import atexit
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

from test_session_uds_lifecycle import expect_ok, invoke, request


PARALLEL_SESSION_COUNT = 8
WARMUP_CYCLE_COUNT = 3
MEASURED_CYCLE_COUNT = 24
# AddressSanitizer intentionally retains freed allocations in its quarantine.
# LeakSanitizer remains enabled for the ASan gate; the wider RSS bound covers
# instrumentation overhead rather than replacing leak detection.
RSS_GROWTH_LIMIT_KIB = 65536 if os.environ.get("ASAN_OPTIONS") else 4096
TRACKED_PIDS: set[int] = set()


def cleanup_tracked_processes() -> None:
    for pid in tuple(TRACKED_PIDS):
        try:
            command = Path(f"/proc/{pid}/cmdline").read_bytes()
            if b"xdebug-fst" in command:
                os.kill(pid, signal.SIGTERM)
        except (FileNotFoundError, ProcessLookupError):
            pass


atexit.register(cleanup_tracked_processes)


def process_exists(pid: int) -> bool:
    return Path(f"/proc/{pid}").exists()


def wait_process_gone(pid: int) -> None:
    for _ in range(200):
        if not process_exists(pid):
            TRACKED_PIDS.discard(pid)
            return
        time.sleep(0.01)
    raise AssertionError(f"process {pid} remains after session cleanup")


def fd_count(pid: int) -> int:
    return len(list(Path(f"/proc/{pid}/fd").iterdir()))


def rss_kib(pid: int) -> int:
    for line in Path(f"/proc/{pid}/status").read_text(
        encoding="utf-8"
    ).splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    raise AssertionError(f"VmRSS is missing for process {pid}")


def main() -> int:
    executable = str(Path(sys.argv[1]).resolve())
    waveform = str(Path(sys.argv[2]).resolve())
    assert Path(waveform).suffix == ".fst", waveform

    with tempfile.TemporaryDirectory(prefix="xdebug-fst-stability-") as root:
        environment = os.environ.copy()
        environment["HOME"] = root
        environment["XVERIF_TEST_TMPDIR"] = root

        names = [f"parallel_{index}" for index in range(PARALLEL_SESSION_COUNT)]

        def open_parallel(name: str) -> dict:
            response = expect_ok(
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
            TRACKED_PIDS.add(response["session"]["server_pid"])
            return response

        with ThreadPoolExecutor(max_workers=PARALLEL_SESSION_COUNT) as executor:
            opened = list(executor.map(open_parallel, names))

        pids = [response["session"]["server_pid"] for response in opened]
        socket_paths = [response["session"]["socket_path"] for response in opened]
        assert len(set(pids)) == PARALLEL_SESSION_COUNT
        assert len(set(socket_paths)) == PARALLEL_SESSION_COUNT
        assert all(Path(path).is_socket() for path in socket_paths)

        registry = Path(root) / ".xdebug" / "engine" / "registry.json"
        registry_document = json.loads(registry.read_text(encoding="utf-8"))
        records = registry_document["sessions"]
        assert {record["session_id"] for record in records} == set(names)
        assert (
            len({record["generation"] for record in records})
            == PARALLEL_SESSION_COUNT
        )

        listed = expect_ok(
            invoke(executable, environment, request("session.list", args={})),
            "session.list",
        )
        assert listed["summary"] == {
            "session_count": PARALLEL_SESSION_COUNT,
            "expired_removed_count": 0,
        }

        def doctor_parallel(name: str) -> dict:
            return expect_ok(
                invoke(
                    executable,
                    environment,
                    request(
                        "session.doctor",
                        target={"session_id": name},
                        args={},
                    ),
                ),
                "session.doctor",
            )

        with ThreadPoolExecutor(max_workers=PARALLEL_SESSION_COUNT) as executor:
            diagnosed = list(executor.map(doctor_parallel, names))
        assert all(response["summary"] == {"healthy": True}
                   for response in diagnosed)

        def close_parallel(name: str) -> dict:
            return expect_ok(
                invoke(
                    executable,
                    environment,
                    request(
                        "session.close",
                        target={"session_id": name},
                        args={},
                    ),
                ),
                "session.close",
            )

        with ThreadPoolExecutor(max_workers=PARALLEL_SESSION_COUNT) as executor:
            closed = list(executor.map(close_parallel, names))
        assert all(response["summary"] == {"removed": True}
                   for response in closed)
        for pid in pids:
            wait_process_gone(pid)
        assert all(not Path(path).exists() for path in socket_paths)

        listed = expect_ok(
            invoke(executable, environment, request("session.list", args={})),
            "session.list",
        )
        assert listed["summary"] == {
            "session_count": 0,
            "expired_removed_count": 0,
        }

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
        assert ready["type"] == "ready", ready
        TRACKED_PIDS.add(loop.pid)

        sequence = 0

        def loop_call(payload: dict) -> dict:
            nonlocal sequence
            sequence += 1
            identifier = f"stability-{sequence}"
            message = dict(payload)
            message["id"] = identifier
            message["payload_format"] = "json"
            loop.stdin.write(json.dumps(message) + "\n")
            loop.stdin.flush()
            envelope = json.loads(loop.stdout.readline())
            assert envelope["id"] == identifier, envelope
            assert envelope["ok"] is True, envelope
            return envelope

        def cycle(index: int) -> None:
            name = "repeated"
            opened_envelope = loop_call(
                request(
                    "session.open",
                    target={"fsdb": waveform},
                    args={"name": name},
                )
            )
            session = opened_envelope["json"]["session"]
            TRACKED_PIDS.add(session["server_pid"])
            assert session["session_id"] == name
            loop_call(
                request(
                    "session.doctor",
                    target={"session_id": name},
                    args={},
                )
            )
            loop_call(
                request(
                    "session.close",
                    target={"session_id": name},
                    args={},
                )
            )
            wait_process_gone(session["server_pid"])
            assert not Path(session["socket_path"]).exists(), index

        for index in range(WARMUP_CYCLE_COUNT):
            cycle(index)
        baseline_fd_count = fd_count(loop.pid)
        baseline_rss_kib = rss_kib(loop.pid)

        for index in range(MEASURED_CYCLE_COUNT):
            cycle(index + WARMUP_CYCLE_COUNT)

        final_list = loop_call(request("session.list", args={}))
        assert final_list["json"]["summary"] == {
            "session_count": 0,
            "expired_removed_count": 0,
        }
        assert fd_count(loop.pid) == baseline_fd_count
        assert rss_kib(loop.pid) <= baseline_rss_kib + RSS_GROWTH_LIMIT_KIB

        quit_envelope = loop_call({"action": "stdio.quit"})
        assert quit_envelope["ok"] is True
        assert loop.wait(timeout=5) == 0
        TRACKED_PIDS.discard(loop.pid)

        assert json.loads(registry.read_text(encoding="utf-8")) == {
            "sessions": [],
            "version": 2,
        }
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
