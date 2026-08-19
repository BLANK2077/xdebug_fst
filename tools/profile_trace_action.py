#!/usr/bin/env python3
"""Profile one large-RTL trace action against an existing benchmark directory."""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from benchmark_large_rtl_trace import StdioClient, response_contract, tool_environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scale_dir", type=Path)
    parser.add_argument("--build", type=Path, default=TOOLS_DIR.parent / "build")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists")
    metadata = json.loads((args.scale_dir / "metadata.json").read_text())
    repo = TOOLS_DIR.parent
    environment, _ = tool_environment(repo, args.build)
    with tempfile.TemporaryDirectory(prefix="xfst-trace-profile-home.") as home:
        environment["HOME"] = home
        client = StdioClient(
            args.build / "xdebug-fst", args.scale_dir, environment,
            args.output.with_suffix(".engine.log"),
        )
        profiler: subprocess.Popen[str] | None = None
        try:
            client.start()
            opened, _ = client.request(
                "session.open",
                target={"fsdb": str(args.scale_dir / "waves.fst"),
                        "daidir": str(args.scale_dir / "obj_dir")},
                args={"name": "trace_profile"}, timeout=600,
            )
            response_contract("session.open", opened)
            server_pid = int(opened["session"]["server_pid"])
            anchors = metadata["query_anchors"]
            warmed, _ = client.request("value.at", args={
                "signal": anchors["value"], "time": anchors["query_time"],
                "render_time_unit": "ps"}, timeout=600)
            response_contract("value.at", warmed)
            profiler = subprocess.Popen(
                ["perf", "record", "-q", "-e", "task-clock", "-g",
                 "-p", str(server_pid),
                 "-o", str(args.output), "--", "sleep", "600"],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
            )
            time.sleep(0.25)
            if profiler.poll() is not None:
                raise RuntimeError(f"perf failed to attach: {profiler.stderr.read()}")
            response, elapsed = client.request(
                "trace.active_driver_chain",
                args={"signal": anchors["active_chain"],
                      "time": anchors["query_time"], "render_time_unit": "ps"},
                limits={"max_depth": 12, "max_nodes": 128,
                        "max_trace_signals": 128}, timeout=600,
            )
            response_contract("trace.active_driver_chain", response)
            print(json.dumps({"elapsed_ms": round(elapsed, 3),
                              "server_pid": server_pid}, sort_keys=True))
        finally:
            if profiler is not None and profiler.poll() is None:
                profiler.send_signal(signal.SIGINT)
                profiler.wait(timeout=30)
            client.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
