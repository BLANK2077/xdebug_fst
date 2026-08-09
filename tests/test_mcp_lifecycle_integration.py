#!/usr/bin/env python3
"""Exercise the real xverif MCP direct/fake-LSF loop around xdebug-fst."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile


def require(condition: bool, message: object) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    executable = Path(sys.argv[1]).resolve()
    waveform = Path(sys.argv[2]).resolve()
    mcp_source = Path(sys.argv[3]).resolve()
    mode = sys.argv[4]
    require(mode in {"direct", "fake-lsf"}, f"unknown mode: {mode}")
    sys.path.insert(0, str(mcp_source))
    current_pythonpath = os.environ.get("PYTHONPATH", "")
    os.environ["PYTHONPATH"] = str(mcp_source) + (
        os.pathsep + current_pythonpath if current_pythonpath else ""
    )

    from xverif_mcp.adapters.xdebug import XverifDebugAdapter

    with tempfile.TemporaryDirectory(prefix=f"xdebug-fst-mcp-{mode}-") as root_text:
        root = Path(root_text)
        tools = root / "tools"
        home = root / "home"
        temporary = root / "tmp"
        tools.mkdir()
        home.mkdir()
        temporary.mkdir()
        (tools / "xdebug").symlink_to(executable)

        os.environ["XVERIF_HOME"] = str(root)
        os.environ["HOME"] = str(home)
        os.environ["XVERIF_TEST_TMPDIR"] = str(temporary)
        os.environ["XVERIF_MCP_LOG_DIR"] = str(root / "logs")
        os.environ["XVERIF_MCP_BACKEND"] = "lsf" if mode == "fake-lsf" else "direct"
        os.environ["XVERIF_MCP_FAKE_LSF"] = "1" if mode == "fake-lsf" else "0"
        if mode == "fake-lsf":
            os.environ["FAKE_BSUB_STDOUT_NOISE_BEFORE_READY"] = "1"
        else:
            os.environ.pop("FAKE_BSUB_STDOUT_NOISE_BEFORE_READY", None)

        name = "mcp_lsf" if mode == "fake-lsf" else "mcp_direct"
        adapter = XverifDebugAdapter(
            mode="lsf" if mode == "fake-lsf" else "direct",
            startup_timeout_sec=10,
            request_timeout_sec=10,
        )
        try:
            if mode == "direct":
                catalog = adapter.actions()
                require(catalog.get("ok") is True, catalog)
                require(catalog["summary"]["action_count"] == 73, catalog)

            opened = adapter.session_open(name, fsdb=str(waveform))
            require(opened.get("ok") is True, opened)
            require(opened["session"]["session_id"] == name, opened)
            require(opened["session"]["launcher"] ==
                    ("lsf" if mode == "fake-lsf" else "direct"), opened)

            doctor = adapter.session_doctor(name)
            require(doctor.get("ok") is True, doctor)
            require(doctor["summary"]["backend_healthy"] is True, doctor)
            listed = adapter.session_list()
            require(listed.get("ok") is True, listed)
            require(listed["summary"]["active_count"] == 1, listed)

            if mode == "direct":
                routed = adapter.query(
                    session_id=name,
                    action="trace.active_driver",
                    args={"signal": "top.u.ready", "time": "120ns"},
                    output_format="json",
                )
                require(routed.get("ok") is False, routed)
                require(routed["error"]["code"] == "DESIGN_NOT_LOADED", routed)

            closed = adapter.session_close(name)
            require(closed.get("ok") is True, closed)
            require(closed["summary"]["cleanup_complete"] is True, closed)
        finally:
            adapter.close_all()

        registry = home / ".xdebug" / "engine" / "registry.json"
        document = json.loads(registry.read_text(encoding="utf-8"))
        require(document == {"sessions": [], "version": 2}, document)
        if mode == "fake-lsf":
            lsf_log = root / "logs" / "sessions" / name / "lsf.ndjson"
            events = [json.loads(line) for line in lsf_log.read_text(
                encoding="utf-8").splitlines()]
            phases = {event["phase"] for event in events}
            require("bsub.start" in phases, phases)
            require("job_id.detected" in phases, phases)
            require(any(phase.startswith("bkill.") for phase in phases), phases)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
