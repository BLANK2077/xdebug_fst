from __future__ import annotations

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tests" / "coverage" / "p6_capability_applicability.json"
TEST_SOURCES = tuple((ROOT / "tests").glob("test_*.py"))


def test_p6_capability_applicability_is_exhaustive_and_evidenced() -> None:
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert document["schema_version"] == \
        "xdebug.p6-capability-applicability.v1"
    assert document["policy"] == "capability_and_information_semantics"

    covered = document["covered_semantic_families"]
    assert set(covered) == {
        "activation_predicates_and_patterns",
        "sequential_and_driver_precedence",
        "module_interface_ref_and_alias",
        "multi_driver_ambiguity",
        "x_origin_branch_loop_and_source",
        "x_origin_limits_and_time",
        "typed_and_delta_waveform_facts",
    }
    test_names = {
        match.group(1)
        for source in TEST_SOURCES
        for match in re.finditer(
            r"^def (test_[A-Za-z0-9_]+)\(",
            source.read_text(encoding="utf-8"), re.MULTILINE)
    }
    for family in covered.values():
        evidence = family["evidence"]
        assert len(evidence) >= 2
        assert set(evidence) <= test_names

    upstream = document[
        "upstream_language_variants_not_separate_xdebug_capabilities"]
    assert set(upstream) == {
        "tagged_union_expression_and_pattern_syntax",
        "nested_or_arrayed_interface_syntax_shapes",
        "primitive_strength_and_tristate_syntax_combinations",
        "cartesian_products_of_existing_limits",
    }
    assert all(item["reason"] for item in upstream.values())
