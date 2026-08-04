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
                target: Optional[Json] = None) -> Json:
        assert self.proc is not None
        self._seq += 1
        req: Json = {
            "api_version": "xdebug.v1",
            "request_id": f"t-{self._seq}",
            "action": action,
        }
        if args is not None:
            req["args"] = args
        if target is not None:
            req["target"] = target
        assert self.proc.stdin is not None
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()
        env = self._read_message(60.0)
        assert env.get("id") == req["request_id"], f"id mismatch: {env}"
        assert env.get("api_version") == "xdebug.v1"
        assert env.get("payload_format") == "json"
        return env.get("json", {})

    def stop(self) -> None:
        if self.proc is None:
            return
        try:
            if self.proc.stdin:
                self.proc.stdin.write('{"action":"stdio.quit"}\n')
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
