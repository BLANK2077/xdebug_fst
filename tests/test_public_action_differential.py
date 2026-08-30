from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from tools.compare_public_action_responses import (
    DifferentialError,
    canonical_sv_literal,
    canonical_time,
    compare_bundles,
    ensure_output_within_repo,
)


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "tests/data/rtl_wave_differential"


def load(name: str) -> dict:
    return json.loads((DATA / name).read_text(encoding="utf-8"))


@pytest.fixture
def documents() -> tuple[dict, dict, dict]:
    return (
        load("equivalent.plan.json"),
        load("equivalent.original.bundle.json"),
        load("equivalent.current.bundle.json"),
    )


def observation(bundle: dict, observation_id: str) -> dict:
    return next(
        item for item in bundle["observations"]
        if item["observation_id"] == observation_id
    )


def difference_pointers(report: dict) -> set[str]:
    return {
        difference["pointer"]
        for item in report["observations"]
        for difference in item["differences"]
    }


def test_checked_fsdb_fst_bundle_is_semantically_equivalent(
    documents: tuple[dict, dict, dict],
) -> None:
    report = compare_bundles(*documents)
    assert report["equivalent"] is True
    assert report["difference_count"] == 0
    assert [item["observation_id"] for item in report["observations"]] == [
        "typed_wave", "axi_transactions", "phase5_trace",
    ]
    assert all(item["equivalent"] for item in report["observations"])
    typed = report["observations"][0]["normalization"]
    assert typed["original"]["canonical_time_count"] == 7
    assert typed["current"]["canonical_time_count"] == 7
    assert typed["original"]["canonical_logic_count"] == 3
    assert typed["original"]["ignored_field_count"] == 2
    assert typed["original"]["explicit_rewrite_count"] == 3


@pytest.mark.parametrize(
    ("mutation", "expected_pointer"),
    [
        ("missing_signal", "/response/data/signals"),
        ("signal_type", "/response/data/signals/0/type"),
        ("signal_width", "/response/data/signals/0/width"),
        ("xz_value", "/response/data/changes/1/value/bits"),
        ("delta", "/response/data/changes/1/delta"),
        ("typed_real", "/response/data/typed_values/0/value"),
        ("typed_string", "/response/data/typed_values/1/value"),
        ("typed_event", "/response/data/typed_values/2/value"),
        ("protocol_id", "/response/data/transactions/1/id"),
        ("protocol_latency", "/response/data/transactions/1/latency"),
        ("protocol_outstanding", "/response/data/transactions/1/outstanding"),
        ("protocol_stall", "/response/data/transactions/1/stalled_cycles"),
        ("trace_termination", "/response/summary/termination"),
    ],
)
def test_negative_mutations_cannot_pass_as_equivalent(
    documents: tuple[dict, dict, dict],
    mutation: str,
    expected_pointer: str,
) -> None:
    plan, original, current = documents
    current = copy.deepcopy(current)
    typed = observation(current, "typed_wave")["response"]["data"]
    axi = observation(current, "axi_transactions")["response"]["data"]["transactions"][1]
    trace = observation(current, "phase5_trace")["response"]
    if mutation == "missing_signal":
        typed["signals"].pop()
    elif mutation == "signal_type":
        typed["signals"][0]["type"] = "wire"
    elif mutation == "signal_width":
        typed["signals"][0]["width"] = 5
    elif mutation == "xz_value":
        typed["changes"][1]["value"]["bits"] = "10X1"
    elif mutation == "delta":
        typed["changes"][1]["delta"] = 2
    elif mutation == "typed_real":
        typed["typed_values"][0]["value"] = "1.5001"
    elif mutation == "typed_string":
        typed["typed_values"][1]["value"] = "波形漂移"
    elif mutation == "typed_event":
        typed["typed_values"][2]["value"] = "idle"
    elif mutation == "protocol_id":
        axi["id"] = "4'h3"
    elif mutation == "protocol_latency":
        axi["latency"] = "3ns"
    elif mutation == "protocol_outstanding":
        axi["outstanding"] = 1
    elif mutation == "protocol_stall":
        axi["stalled_cycles"] = 0
    elif mutation == "trace_termination":
        trace["summary"]["termination"] = "primary_input"
    report = compare_bundles(plan, original, current)
    assert report["equivalent"] is False
    assert any(
        pointer == expected_pointer or pointer.startswith(expected_pointer + "/")
        for pointer in difference_pointers(report)
    )


def test_same_time_delta_order_is_not_sorted_away(
    documents: tuple[dict, dict, dict],
) -> None:
    plan, original, current = documents
    current = copy.deepcopy(current)
    changes = observation(current, "typed_wave")["response"]["data"]["changes"]
    changes[0], changes[1] = changes[1], changes[0]
    report = compare_bundles(plan, original, current)
    assert report["equivalent"] is False
    pointers = difference_pointers(report)
    assert "/response/data/changes/0/delta" in pointers
    assert "/response/data/changes/1/delta" in pointers


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("scan_complete", False),
        ("analysis_complete", False),
        ("response_truncated", True),
        ("value_width_complete", False),
        ("width_diagnostics", [{
            "signal": "top.u_fst.bus",
            "role": "changes[0].value",
            "reason": "derived_width_unavailable",
        }]),
    ],
)
def test_incomplete_or_truncated_response_fails_before_equivalence(
    documents: tuple[dict, dict, dict],
    field: str,
    value: object,
) -> None:
    plan, original, current = documents
    current = copy.deepcopy(current)
    observation(current, "typed_wave")["response"]["summary"][field] = value
    with pytest.raises(DifferentialError, match="completeness requirement failed"):
        compare_bundles(plan, original, current)


def test_explicit_source_line_mapping_has_a_strict_precondition(
    documents: tuple[dict, dict, dict],
) -> None:
    plan, original, current = documents
    current = copy.deepcopy(current)
    observation(current, "phase5_trace")["response"]["data"]["hops"][0]["line"] = 40
    with pytest.raises(DifferentialError, match="rewrite precondition failed"):
        compare_bundles(plan, original, current)


def test_ignore_and_rewrite_policies_require_exact_audited_entries(
    documents: tuple[dict, dict, dict],
) -> None:
    plan, original, current = documents
    bad_ignore = copy.deepcopy(plan)
    bad_ignore["observations"][0]["ignore"][0]["reason"] = ""
    with pytest.raises(DifferentialError, match="ignored field needs"):
        compare_bundles(bad_ignore, original, current)

    bad_pointer = copy.deepcopy(plan)
    bad_pointer["observations"][0]["ignore"][0]["pointer"] = "/response/no_such_field"
    with pytest.raises(DifferentialError, match="JSON pointer does not exist"):
        compare_bundles(bad_pointer, original, current)


def test_time_and_logic_normalization_preserve_width_x_and_z() -> None:
    assert canonical_time("1ns") == canonical_time("1000ps") == {
        "$xdebug_time_fs": "1000000"
    }
    assert canonical_sv_literal("8'hx") == canonical_sv_literal("8'bXXXXXXXX")
    assert canonical_sv_literal("4'b10z1") == {
        "$xdebug_logic": {"width": 4, "bits": "10Z1", "signed": False}
    }
    assert canonical_sv_literal("4'b10x1") != canonical_sv_literal("4'b10z1")
    assert canonical_sv_literal("4'b10?1") == canonical_sv_literal("4'b10z1")
    with pytest.raises(DifferentialError, match="invalid base-b SV literal"):
        canonical_sv_literal("4'b1021")
    with pytest.raises(DifferentialError, match="invalid decimal SV literal"):
        canonical_sv_literal("8'd1x")
    with pytest.raises(DifferentialError, match="integer femtoseconds"):
        canonical_time("0.1fs")


@pytest.mark.parametrize(
    ("original_text", "current_text"),
    [
        ("1ns", "1000ps"),
        ("4'hf", "4'b1111"),
    ],
)
def test_typed_string_payload_is_never_reinterpreted_as_time_or_logic(
    documents: tuple[dict, dict, dict],
    original_text: str,
    current_text: str,
) -> None:
    plan, original, current = documents
    original = copy.deepcopy(original)
    current = copy.deepcopy(current)
    observation(original, "typed_wave")["response"]["data"]["typed_values"][1][
        "value"
    ] = original_text
    observation(current, "typed_wave")["response"]["data"]["typed_values"][1][
        "value"
    ] = current_text
    report = compare_bundles(plan, original, current)
    assert report["equivalent"] is False
    assert "/response/data/typed_values/1/value" in difference_pointers(report)


def test_report_output_boundary_rejects_parent_and_symlink_escape(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    outside = tmp_path / "outside"
    repo.mkdir()
    outside.mkdir()
    assert ensure_output_within_repo(repo, repo / "report.json") == repo / "report.json"
    with pytest.raises(DifferentialError, match="escapes the only writable repository"):
        ensure_output_within_repo(repo, outside / "report.json")
    (repo / "escape").symlink_to(outside, target_is_directory=True)
    with pytest.raises(DifferentialError, match="escapes the only writable repository"):
        ensure_output_within_repo(repo, repo / "escape/report.json")
