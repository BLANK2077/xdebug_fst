# cli.py — One-shot CLI runner (BSD-3-Clause)
"""Run the xdebug-fst binary in one-shot mode (`--json -`) and parse
the plain JSON response (no stdio-loop envelope)."""
from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

from .action_trace import record_action_exchange


@dataclass
class RunResult:
    request: Any
    returncode: int
    stdout_raw: str
    stderr_raw: str
    timed_out: bool = False
    response: Any = None

    @property
    def ok(self) -> bool:
        if isinstance(self.response, dict) and "ok" in self.response:
            return bool(self.response["ok"])
        return self.returncode == 0 and not self.timed_out


class CliRunner:
    def __init__(
        self,
        xdebug_bin: Path,
        *,
        cwd: Optional[Path] = None,
        env: Optional[Dict[str, str]] = None,
    ) -> None:
        self.command = [str(Path(xdebug_bin)), "--json", "-"]
        self.cwd = str(Path(cwd or Path.cwd()))
        self.env = dict(os.environ)
        if env:
            self.env.update(env)

    def run(self, request: Any, timeout_sec: float = 30.0) -> RunResult:
        payload = json.dumps(request) + "\n"
        try:
            proc = subprocess.run(
                self.command,
                cwd=self.cwd,
                env=self.env,
                input=payload,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=timeout_sec,
            )
            timed_out = False
        except subprocess.TimeoutExpired as exc:
            timed_out = True
            proc = exc
            stdout_raw = (exc.stdout or "").decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
            stderr_raw = (exc.stderr or "").decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
            result = RunResult(
                request, -1, stdout_raw, stderr_raw, timed_out=True
            )
            record_action_exchange(
                "one-shot", request, None, returncode=-1, timed_out=True
            )
            return result

        result = RunResult(
            request,
            proc.returncode,
            proc.stdout,
            proc.stderr,
            timed_out=timed_out,
        )
        try:
            result.response = json.loads(proc.stdout)
        except (json.JSONDecodeError, IndexError):
            result.response = None
        record_action_exchange(
            "one-shot",
            request,
            result.response,
            returncode=result.returncode,
            timed_out=result.timed_out,
        )
        return result
