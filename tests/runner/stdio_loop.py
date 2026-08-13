# stdio_loop.py — Persistent stdio-loop runner (BSD-3-Clause)
"""Drive the xdebug-fst `--stdio-loop --json` protocol: ready envelope,
request/response envelopes with request_id echo."""
from __future__ import annotations

import json
import os
import queue
import signal
import subprocess
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional

from .action_trace import record_action_exchange

Json = Dict[str, Any]


class StdioLoopError(RuntimeError):
    pass


class StdioLoopRunner:
    def __init__(
        self,
        xdebug_bin: Path,
        *,
        cwd: Optional[Path] = None,
        env: Optional[Dict[str, str]] = None,
    ) -> None:
        self.command = [str(Path(xdebug_bin)), "--stdio-loop", "--json"]
        self.cwd = str(Path(cwd or Path.cwd()))
        self.env = dict(os.environ)
        if env:
            self.env.update(env)
        self.proc: Optional[subprocess.Popen[str]] = None
        self._out_queue: "queue.Queue[str]" = queue.Queue()
        self._seq = 0
        self._session_id: Optional[str] = None

    @property
    def has_current_session(self) -> bool:
        return self._session_id is not None

    def start(self, timeout_sec: float = 30.0) -> Json:
        self.proc = subprocess.Popen(
            self.command,
            cwd=self.cwd,
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        threading.Thread(target=self._read_stdout, daemon=True).start()
        ready = self._read_message(timeout_sec)
        if ready.get("type") != "ready" or ready.get("protocol") != "xdebug-stdio-loop":
            self.stop()
            raise StdioLoopError(f"unexpected ready envelope: {ready!r}")
        return ready

    def _read_stdout(self) -> None:
        assert self.proc is not None and self.proc.stdout is not None
        for line in self.proc.stdout:
            self._out_queue.put(line.rstrip("\n"))

    def _read_message(self, timeout_sec: float) -> Json:
        deadline = time.monotonic() + timeout_sec
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise StdioLoopError("timed out waiting for message")
            try:
                line = self._out_queue.get(timeout=remaining)
            except queue.Empty:
                raise StdioLoopError("timed out waiting for message")
            line = line.strip()
            if not line:
                continue
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                # noise on stdout (e.g. stray prints) — keep waiting
                continue

    def request(self, action: str, args: Optional[Json] = None,
                target: Optional[Json] = None,
                limits: Optional[Json] = None) -> Json:
        env, req = self._request_envelope(
            action, args=args, target=target, limits=limits,
            payload_format="json")
        assert env.get("payload_format") == "json"
        response = env.get("json", {})
        self._record_xout_audit(req, response, env.get("xout_audit"))
        if action == "session.open" and response.get("ok"):
            self._session_id = args.get("name") if args else None
        if action in {"session.close", "session.kill"} and response.get("ok"):
            self._session_id = None
        record_action_exchange("stdio-loop", req, response, returncode=0)
        return response

    def _record_xout_audit(self, request: Json, response: Json,
                           xout: Any) -> None:
        destination = os.environ.get("XDEBUG_XOUT_AUDIT_LOG")
        if not destination:
            return
        if not isinstance(xout, str):
            raise AssertionError("XDEBUG XOUT audit capture is missing")
        path = Path(destination)
        if not path.is_absolute():
            raise RuntimeError("XDEBUG_XOUT_AUDIT_LOG must be an absolute path")
        event = {
            "test_node": os.environ.get(
                "PYTEST_CURRENT_TEST", "<outside-pytest>"
            ).split(" (")[0],
            "action": request.get("action"),
            "request": request,
            "response": response,
            "xout": xout,
        }
        encoded = (json.dumps(
            event, sort_keys=True, ensure_ascii=False
        ) + "\n").encode("utf-8")
        descriptor = os.open(
            path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_CLOEXEC,
            0o600,
        )
        try:
            written = os.write(descriptor, encoded)
            if written != len(encoded):
                raise RuntimeError("short write to XOUT audit trace")
        finally:
            os.close(descriptor)

    def request_xout(self, action: str, args: Optional[Json] = None,
                     target: Optional[Json] = None,
                     limits: Optional[Json] = None) -> str:
        env, _ = self._request_envelope(
            action, args=args, target=target, limits=limits,
            payload_format="xout")
        assert env.get("payload_format") == "xout"
        assert env.get("ok"), env
        xout = env.get("xout")
        assert isinstance(xout, str)
        return xout

    def _request_envelope(self, action: str, args: Optional[Json] = None,
                          target: Optional[Json] = None,
                          limits: Optional[Json] = None,
                          payload_format: str = "json") -> tuple[Json, Json]:
        assert self.proc is not None
        self._seq += 1
        req: Json = {
            "api_version": "xdebug.v1",
            "request_id": f"t-{self._seq}",
            "action": action,
            "payload_format": payload_format,
        }
        if args is not None:
            req["args"] = args
        if limits is not None:
            req["limits"] = limits
        if target is not None:
            req["target"] = target
        elif self._session_id is not None and action not in {
            "actions", "schema", "batch", "session.open", "session.list",
            "session.gc",
        }:
            req["target"] = {"session_id": self._session_id}
        assert self.proc.stdin is not None
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()
        env = self._read_message(60.0)
        assert env.get("id") == req["request_id"], f"id mismatch: {env}"
        assert env.get("api_version") == "xdebug.v1"
        return env, req

    def stop(self) -> None:
        if self.proc is None:
            return
        try:
            if self.proc.stdin:
                self.proc.stdin.write(
                    '{"api_version":"xdebug.v1","action":"stdio.quit"}\n'
                )
                self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)
        self.proc = None

    def __enter__(self) -> "StdioLoopRunner":
        self.start()
        return self

    def __exit__(self, *exc: Any) -> None:
        self.stop()
