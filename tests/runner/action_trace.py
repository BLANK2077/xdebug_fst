"""Optional NDJSON trace of public action exchanges during pytest."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


def record_action_exchange(
    transport: str,
    request: Any,
    response: Any,
    *,
    returncode: int | None = None,
    timed_out: bool = False,
) -> None:
    destination = os.environ.get("XDEBUG_ACTION_COVERAGE_LOG")
    if not destination:
        return
    path = Path(destination)
    if not path.is_absolute():
        raise RuntimeError("XDEBUG_ACTION_COVERAGE_LOG must be an absolute path")
    event = {
        "test_node": os.environ.get("PYTEST_CURRENT_TEST", "<outside-pytest>")
        .split(" (")[0],
        "transport": transport,
        "action": request.get("action") if isinstance(request, dict) else None,
        "request": request,
        "response": response,
        "returncode": returncode,
        "timed_out": timed_out,
    }
    encoded = (json.dumps(event, sort_keys=True, ensure_ascii=False) + "\n").encode(
        "utf-8"
    )
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_CLOEXEC,
        0o600,
    )
    try:
        written = os.write(descriptor, encoded)
        if written != len(encoded):
            raise RuntimeError("short write to action coverage trace")
    finally:
        os.close(descriptor)
