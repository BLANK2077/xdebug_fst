#!/usr/bin/env python3
"""Compare the P1 catalog and every schema response through stdio-loop."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


class LoopClient:
    def __init__(self, executable: Path, cwd: Path) -> None:
        self.process = subprocess.Popen(
            [str(executable), "--stdio-loop", "--json"],
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        ready = self._read()
        if ready.get("type") != "ready" or ready.get("protocol") != "xdebug-stdio-loop":
            raise RuntimeError(f"invalid ready envelope from {executable}: {ready}")

    def _read(self) -> dict[str, Any]:
        assert self.process.stdout is not None
        line = self.process.stdout.readline()
        if not line:
            stderr = self.process.stderr.read() if self.process.stderr else ""
            raise RuntimeError(f"stdio-loop exited before response: {stderr}")
        return json.loads(line)

    def request(self, request: dict[str, Any]) -> dict[str, Any]:
        assert self.process.stdin is not None
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        envelope = self._read()
        if envelope.get("id") != request["request_id"]:
            raise RuntimeError(f"stdio-loop id mismatch: {envelope}")
        if "json" not in envelope:
            raise RuntimeError(f"stdio-loop response lacks JSON payload: {envelope}")
        return envelope["json"]

    def close(self) -> None:
        if self.process.poll() is not None:
            return
        assert self.process.stdin is not None
        self.process.stdin.write(
            '{"api_version":"xdebug.v1","request_id":"quit","action":"stdio.quit"}\n'
        )
        self.process.stdin.flush()
        self._read()
        self.process.wait(timeout=10)


def first_difference(expected: Any, actual: Any, path: str = "$") -> str | None:
    if type(expected) is not type(actual):
        return f"{path}: type {type(expected).__name__} != {type(actual).__name__}"
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return f"{path}: keys {sorted(expected)} != {sorted(actual)}"
        for key in expected:
            difference = first_difference(expected[key], actual[key], f"{path}.{key}")
            if difference:
                return difference
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}: length {len(expected)} != {len(actual)}"
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
        return None
    return None if expected == actual else f"{path}: {expected!r} != {actual!r}"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument(
        "--original-root",
        type=Path,
        default=os.environ.get("XDEBUG_ORIGINAL_ROOT"),
        required="XDEBUG_ORIGINAL_ROOT" not in os.environ,
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    original_root = args.original_root.resolve()
    candidate = LoopClient(repo_root / "build/xdebug-fst", repo_root)
    original = LoopClient(original_root / "xdebug/xdebug", original_root)
    try:
        actions_request = {
            "api_version": "xdebug.v1",
            "request_id": "actions",
            "action": "actions",
            "args": {},
        }
        expected_catalog = original.request(actions_request)
        actual_catalog = candidate.request(actions_request)
        difference = first_difference(expected_catalog, actual_catalog)
        if difference:
            print(f"actions mismatch: {difference}", file=sys.stderr)
            return 1

        actions = expected_catalog["data"]["actions"]
        if len(actions) != 73 or len(set(actions)) != 73:
            print("original catalog is not the frozen 73-action contract", file=sys.stderr)
            return 1
        checked = 0
        for action in actions:
            for kind in ("request", "response"):
                request = {
                    "api_version": "xdebug.v1",
                    "request_id": f"schema-{checked}",
                    "action": "schema",
                    "args": {"action": action, "kind": kind},
                }
                expected = original.request(request)
                actual = candidate.request(request)
                difference = first_difference(expected, actual)
                if difference:
                    print(f"schema mismatch for {action} {kind}: {difference}", file=sys.stderr)
                    return 1
                checked += 1
    finally:
        candidate.close()
        original.close()
    print(f"P1 protocol parity: OK ({len(actions)} actions, {checked} schemas)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
