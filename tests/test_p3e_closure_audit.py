from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest

from tools.build_p3e_closure_audit import (
    ClosureError,
    DEFAULT_MANIFEST,
    DEFAULT_OUTPUT,
    build_audit,
    validate_audit,
    write_document,
)


ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / DEFAULT_OUTPUT
MANIFEST = ROOT / DEFAULT_MANIFEST


def load_audit() -> dict:
    return json.loads(AUDIT.read_text(encoding="utf-8"))


def test_p3e_closure_audit_is_reproducible_and_repository_local() -> None:
    audit = load_audit()
    assert build_audit(ROOT, MANIFEST) == audit
    validate_audit(audit, ROOT, MANIFEST)
    assert audit["session"] == {
        "all_writes_repository_local": True,
        "external_sources_read_only": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
    }
    assert audit["verdict"] == {
        "missing_count": 0,
        "p3_batch": "P3-E",
        "partial_count": 0,
        "proven_unobservable_count": 2,
        "remaining_observable_gap_count": 0,
        "scenario_count": 3,
        "semantic_equivalent_count": 1,
    }
    assert not any(
        isinstance(value, str) and (value.startswith("/") or "/home/" in value)
        for value in _strings(audit)
    )


def _strings(value: object):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for item in value.values():
            yield from _strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from _strings(item)


def test_p3e_xif_closure_prefers_direct_reuse_and_gates_content_equivalence() -> None:
    xif = load_audit()["xif_event"]
    assert xif["classification"] == "semantic-equivalent"
    assert xif["oracle"]["observation_count"] == 32
    assert xif["remaining_observable_gap_count"] == 0
    policy = xif["asset_reuse_policy"]
    assert policy["direct_copy_preferred"] is True
    assert len(policy["directly_reused_configs"]) == 6
    assert {row["reuse"] for row in policy["directly_reused_configs"]} == {
        "byte-identical-copy"
    }
    assert policy["cross_side_hash_equality_required"] is False
    assert policy["public_content_equivalence_required"] is True
    assert policy["original_rtl_directly_executable_open_source"] is False
    assert policy["rtl_replacement_scope"] == "minimal-pin-level-semantic-mirror"
    assert len(xif["requirements"]) == 12
    assert all(xif["requirements"].values())
    assert xif["fixture"]["proprietary_vip_used"] is False
    assert xif["fixture"]["fsdb_conversion_used"] is False


def test_p3e_sva_and_cross_fixture_proofs_are_strictly_bounded() -> None:
    audit = load_audit()
    sva = audit["sva_npi"]
    assert sva["classification"] == "proven-unobservable"
    assert sva["observed_public_action_count"] == 0
    assert sva["public_exposure_count"] == 0
    assert sva["remaining_distinct_public_observation_count"] == 0
    assert "frozen 73-action public schemas" in sva["proof_scope"]

    cross = audit["cross_fixture"]
    assert cross["classification"] == "proven-unobservable"
    assert cross["consumer_count"] == 37
    assert cross["observed_public_action_count"] == 72
    assert cross["contract_count"] == 72
    assert cross["catalog_actions_not_observed"] == ["session.kill"]
    assert cross["remaining_unmapped_consumer_action_count"] == 0
    assert cross["remaining_distinct_public_observation_count"] == 0
    assert "each waveform fixture" in cross["proof_scope"]


@pytest.mark.parametrize(
    ("path", "replacement"),
    [
        (("xif_event", "remaining_observable_gap_count"), 1),
        (("sva_npi", "public_exposure_count"), 1),
        (("cross_fixture", "contract_count"), 71),
        (("verdict", "partial_count"), 1),
    ],
)
def test_p3e_closure_validator_rejects_mutations(
    path: tuple[str, str], replacement: object
) -> None:
    mutated = deepcopy(load_audit())
    mutated[path[0]][path[1]] = replacement
    with pytest.raises(ClosureError, match="content drifted"):
        validate_audit(mutated, ROOT, MANIFEST)


def test_p3e_closure_writer_rejects_repository_escape() -> None:
    outside = ROOT.parent / ".p3e-closure-escape-must-not-exist.json"
    assert not outside.exists()
    with pytest.raises(ClosureError, match="escapes repository"):
        write_document(ROOT, outside, {})
    assert not outside.exists()


def test_p3e_closure_hash_is_canonical() -> None:
    content = AUDIT.read_bytes()
    assert content.endswith(b"\n")
    assert hashlib.sha256(content).hexdigest() == hashlib.sha256(
        (json.dumps(load_audit(), ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode()
    ).hexdigest()
