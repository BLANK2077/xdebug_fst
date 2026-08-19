"""Fast parser contracts for the local large-RTL performance runner."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "benchmark_large_rtl_trace.py"
SPEC = importlib.util.spec_from_file_location("benchmark_large_rtl_trace", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
BENCHMARK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCHMARK)


def test_parse_scales_preserves_explicit_matrix() -> None:
    assert BENCHMARK.parse_scales("1024,2048,4096") == (1024, 2048, 4096)
    with pytest.raises(ValueError, match="duplicates"):
        BENCHMARK.parse_scales("1024,1024")
    with pytest.raises(ValueError, match="at least"):
        BENCHMARK.parse_scales("512")


def test_parse_time_metrics_requires_complete_gnu_time_contract() -> None:
    metrics = BENCHMARK.parse_time_metrics(
        "wall_seconds=1.25\nuser_seconds=2.5\nsys_seconds=0.1\n"
        "max_rss_kib=4096\nexit_code=0\n"
    )
    assert metrics == {
        "wall_seconds": 1.25,
        "user_seconds": 2.5,
        "sys_seconds": 0.1,
        "max_rss_kib": 4096,
        "exit_code": 0,
    }
    with pytest.raises(BENCHMARK.BenchmarkError, match="incomplete"):
        BENCHMARK.parse_time_metrics("wall_seconds=1.0\n")


def test_latency_summary_keeps_first_and_distribution() -> None:
    summary = BENCHMARK.latency_summary([5.0, 1.0, 2.0, 3.0, 4.0])
    assert summary == {
        "samples": 5,
        "first_ms": 5.0,
        "median_ms": 3.0,
        "p95_ms": 5.0,
        "min_ms": 1.0,
        "max_ms": 5.0,
    }


def test_query_index_metrics_requires_structured_engine_record(tmp_path: Path) -> None:
    log = tmp_path / "engine.log"
    log.write_text(
        "[engine] design query index: build_ms=12.375 estimated_bytes=4096 "
        "signals_scanned=887 port_records_scanned=650\n",
        encoding="utf-8",
    )
    assert BENCHMARK.query_index_metrics(log) == {
        "build_ms": 12.375,
        "estimated_bytes": 4096,
        "signals_scanned": 887,
        "port_records_scanned": 650,
    }
    log.write_text("[engine] no metrics\n", encoding="utf-8")
    with pytest.raises(BENCHMARK.BenchmarkError, match="lacks"):
        BENCHMARK.query_index_metrics(log)


def test_binary_manifest_uses_explicit_non_fallback_schema() -> None:
    assert "binary-v1" in SCRIPT.read_text(encoding="utf-8")
    assert "xdebug.design-db-bundle.v2" in SCRIPT.read_text(encoding="utf-8")
