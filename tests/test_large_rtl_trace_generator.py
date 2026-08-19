"""Fast contracts for the local large-RTL trace benchmark generator."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = REPO_ROOT / "tools" / "large_rtl_trace_generator.py"
SPEC = importlib.util.spec_from_file_location("large_rtl_trace_generator", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


@pytest.mark.parametrize("target", [1024, 2048])
def test_generator_has_exact_semantic_scale_and_required_features(target: int) -> None:
    rtl, metadata = GENERATOR.render(target)
    assert len(rtl.splitlines()) == target
    assert metadata["rtl_line_count"] == target
    assert metadata["target_rtl_lines"] == target
    assert metadata["tile_module_count"] > 0
    assert metadata["interface_instance_count"] == metadata["tile_module_count"]
    assert metadata["max_hierarchy_depth"] >= 6
    assert metadata["control_nesting_depth"] >= 5
    assert metadata["expected_minimum_instance_count"] > metadata["tile_module_count"]
    required = {
        "interface", "modport", "generate_for", "generate_if",
        "always_comb", "always_ff", "nested_if", "case", "casez",
        "cross_module_chain",
    }
    assert required.issubset(metadata["features"])
    assert "interface scale_bus_if" in rtl
    assert "modport producer" in rtl
    assert "scale_chain_5 u_chain" in rtl
    assert "casez (opcode[3:0])" in rtl
    assert "localparam logic [31:0] SCALE_PAD_" in rtl
    assert metadata["rtl_sha256"] == hashlib.sha256(rtl.encode()).hexdigest()


def test_generator_is_deterministic_and_scale_adds_semantic_tiles() -> None:
    first_rtl, first = GENERATOR.render(4096)
    second_rtl, second = GENERATOR.render(4096)
    larger_rtl, larger = GENERATOR.render(8192)
    assert first_rtl == second_rtl
    assert first == second
    assert larger["tile_module_count"] > first["tile_module_count"]
    assert larger["expected_minimum_instance_count"] > first["expected_minimum_instance_count"]
    assert len(larger_rtl.splitlines()) == 8192


def test_generator_rejects_non_benchmark_scale() -> None:
    with pytest.raises(ValueError, match="at least"):
        GENERATOR.render(1000)
