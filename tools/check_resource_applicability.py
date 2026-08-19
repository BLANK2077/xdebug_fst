#!/usr/bin/env python3
"""Differentially prove resource_missing N/A for resource-free actions."""

from __future__ import annotations

import argparse
import json
import os
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any


REQUEST_EXAMPLES = {
    "actions": "actions.basic.json",
    "batch": "batch.basic.json",
    "expr.normalize": "expr.normalize.basic.json",
    "schema": "schema.basic.json",
    "session.gc": "session.gc.basic.json",
    "session.list": "session.list.basic.json",
}

EXPECTED_INVALID_ARGS = {
    action: "$" if action == "expr.normalize" else "target.session_id"
    for action in REQUEST_EXAMPLES
}


def invoke(
    executable: Path,
    cwd: Path,
    home: Path,
    request: dict[str, Any],
) -> tuple[int, dict[str, Any]]:
    environment = os.environ.copy()
    environment["HOME"] = str(home)
    environment["XVERIF_TEST_TMPDIR"] = str(home)
    completed = subprocess.run(
        [str(executable), "--json", "-"],
        cwd=cwd,
        env=environment,
        input=json.dumps(request, ensure_ascii=False) + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
        check=False,
    )
    try:
        response = json.loads(completed.stdout)
    except json.JSONDecodeError as exception:
        raise RuntimeError(
            f"invalid JSON from {executable}: rc={completed.returncode}; "
            f"stdout={completed.stdout!r}; stderr={completed.stderr!r}"
        ) from exception
    return completed.returncode, response


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument(
        "--original-root",
        type=Path,
        default=os.environ.get("XDEBUG_ORIGINAL_ROOT"),
        required="XDEBUG_ORIGINAL_ROOT" not in os.environ,
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    if not args.repo_root.is_absolute() or not args.original_root.is_absolute():
        raise SystemExit("--repo-root and --original-root must be absolute paths")
    repo_root = args.repo_root.resolve()
    original_root = args.original_root.resolve()
    candidate = repo_root / "build/xdebug-fst"
    original = original_root / "xdebug/xdebug"
    examples = repo_root / "compat/xdebug-v1/examples/requests"

    with tempfile.TemporaryDirectory(prefix="xdebug-app-original-") as original_home, \
            tempfile.TemporaryDirectory(prefix="xdebug-app-candidate-") as candidate_home:
        for action, filename in REQUEST_EXAMPLES.items():
            request = json.loads((examples / filename).read_text(encoding="utf-8"))
            if request.get("action") != action:
                raise RuntimeError(f"request example action mismatch: {filename}")
            request["target"] = {"session_id": "missing_resource_session"}
            expected = invoke(
                original, original_root, Path(original_home), request
            )
            actual = invoke(candidate, repo_root, Path(candidate_home), request)
            if expected != actual:
                raise RuntimeError(
                    f"resource applicability differs for {action}: "
                    f"original={expected!r}; candidate={actual!r}"
                )
            status, response = actual
            error = response.get("error") or {}
            if status != 1 or error.get("code") != "INVALID_REQUEST" or \
                    error.get("error_layer") != "schema" or \
                    error.get("invalid_arg") != EXPECTED_INVALID_ARGS[action]:
                raise RuntimeError(
                    f"unexpected resource-free contract for {action}: {actual!r}"
                )
            if action == "expr.normalize" and not any(
                issue.get("path") == "target"
                and "session_id" in issue.get("message", "")
                for issue in error.get("validation_issues", [])
            ):
                raise RuntimeError(
                    "expr.normalize oneOf error does not identify the forbidden "
                    f"session target: {actual!r}"
                )
    print(
        "resource applicability differential: OK "
        f"({len(REQUEST_EXAMPLES)} actions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
