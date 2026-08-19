#!/usr/bin/env python3
"""Reject developer-home paths and tracked Codex configuration."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys


FORBIDDEN_CONTENT = (
    (re.compile(rb"/home/[A-Za-z0-9._-]+/"), "Linux home directory"),
    (re.compile(rb"/Users/[A-Za-z0-9._-]+/"), "macOS home directory"),
    (
        re.compile(rb"[A-Za-z]:\\\\Users\\\\[A-Za-z0-9._-]+\\\\"),
        "Windows home directory",
    ),
    (re.compile(re.escape(b"~/" + b"xdebug_oc")), "legacy tilde workspace"),
)


def tracked_paths(repo_root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=repo_root,
        check=True,
        capture_output=True,
    )
    return [repo_root / raw.decode() for raw in result.stdout.split(b"\0") if raw]


def scan_repository(repo_root: Path) -> list[str]:
    errors: list[str] = []
    for path in tracked_paths(repo_root):
        relative = path.relative_to(repo_root)
        if relative.parts and relative.parts[0] == ".codex":
            errors.append(f"tracked Codex configuration: {relative}")
            continue
        if not path.is_file():
            continue
        content = path.read_bytes()
        for pattern, label in FORBIDDEN_CONTENT:
            if pattern.search(content):
                errors.append(f"{relative}: {label}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    args = parser.parse_args(argv)
    errors = scan_repository(args.repo_root.resolve())
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("local path audit: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
