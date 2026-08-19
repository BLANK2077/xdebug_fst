#!/usr/bin/env python3
"""Measure all representative actions with an existing FST and DesignDB bundle."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from benchmark_large_rtl_trace import benchmark_actions, tool_environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scale_dir", type=Path)
    parser.add_argument("design_bundle", type=Path)
    parser.add_argument("--build", type=Path, default=Path(__file__).resolve().parents[1] / "build")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=5)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists")
    if not args.output.is_absolute():
        parser.error("output must be an absolute path")
    if args.repetitions < 1:
        parser.error("repetitions must be positive")
    scale_dir = args.scale_dir.resolve()
    design_bundle = args.design_bundle.resolve()
    if not (scale_dir / "waves.fst").is_file() or not (
            scale_dir / "metadata.json").is_file():
        parser.error("scale_dir must contain waves.fst and metadata.json")
    if not design_bundle.is_dir():
        parser.error("design_bundle must be a directory")
    repo = Path(__file__).resolve().parents[1]
    environment, tool_info = tool_environment(repo, args.build.resolve())
    metadata = json.loads((scale_dir / "metadata.json").read_text(encoding="utf-8"))
    runtime = benchmark_actions(
        scale_dir, args.build.resolve() / "xdebug-fst", environment, metadata,
        args.repetitions, design_bundle, args.output.with_suffix(".engine.log"),
    )
    args.output.write_text(json.dumps({
        "schema_version": "xdebug.existing-trace-benchmark.v1",
        "tool_environment": tool_info,
        "target_rtl_lines": metadata["target_rtl_lines"],
        "design_bundle": design_bundle.name,
        "runtime": runtime,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
