from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path

import pytest

from tools.build_rtl_wave_semantic_matrix import MatrixError
from tools.collect_p3e_boundary_audit import (
    DEFAULT_OUTPUT,
    build_audit,
    validate_audit,
    write_document,
)


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "compat/xdebug-v1/rtl-wave-assets.manifest.json"
AUDIT = ROOT / DEFAULT_OUTPUT
AUDIT_SHA256 = "242750e22f7cb2fe76b661d4040e10db771fb2f4388f25d1ddac9174c5d1a1c0"


def original_root() -> Path:
    value = os.environ.get("XDEBUG_ORIGINAL_ROOT")
    assert value, "P3-E 门禁要求显式 XDEBUG_ORIGINAL_ROOT，禁止 fallback"
    return Path(value).resolve()


def load_audit() -> dict:
    assert AUDIT.is_file()
    assert hashlib.sha256(AUDIT.read_bytes()).hexdigest() == AUDIT_SHA256
    return json.loads(AUDIT.read_text(encoding="utf-8"))


def test_p3e_boundary_audit_is_reproducible_and_repository_local() -> None:
    audit = load_audit()
    assert build_audit(ROOT, original_root(), MANIFEST) == audit
    assert audit["session"] == {
        "all_writes_repository_local": True,
        "eda_invoked": False,
        "external_sources_read_only": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
    }
    assert audit["authority"]["catalog"]["action_count"] == 73
    assert audit["authority"]["contract_count"] == 73
    assert not any(
        isinstance(value, str) and value.startswith("/")
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


def test_sva_npi_boundary_is_private_and_bounded_by_frozen_schemas() -> None:
    sva = load_audit()["sva_npi"]
    assert sva["fixture_id"] == "xdebug.npi_fsdb_sva"
    assert sva["classification_target"] == "proven-unobservable"
    assert sva["consumer_paths"] == [
        "xdebug/tests/synthetic/test_npi_fsdb_sva.py"
    ]
    assert sva["observed_public_actions"] == []
    assert sva["public_exposure_count"] == 0
    assert all(not paths for paths in sva["public_schema_token_hits"].values())
    assert set(sva["private_probe_schema_versions"]) == {
        "npi-daidir-fsdb-sva-probe.v1",
        "npi-fsdb-sva-probe.v1",
    }
    assert set(sva["private_field_groups"]) == {
        "assertion_event",
        "assertion_identity",
        "design_ast",
        "join_diagnostics",
    }
    assert sva["private_event_values"] == [
        "failure",
        "incomplete",
        "match",
        "success",
    ]
    assert set(sva["candidate_action_disposition"]) == {
        "event.find",
        "scope.list",
        "signal.changes",
        "value.at",
    }
    assert sva["remaining_distinct_public_observation_count"] == 0


def test_xif_red_boundary_enumerates_every_missing_public_observation() -> None:
    xif = load_audit()["xif_event"]
    assert xif["fixture_id"] == "xdebug.xif_event"
    assert xif["classification_target"] == "semantic-equivalent"
    assert set(xif["public_action_contracts"]) == {
        "event.export",
        "event.find",
        "value.at",
    }
    assert xif["current_pre_e2_evidence"] == [
        "tests/test_waveform.py::test_value_at_preserves_event_kind"
    ]
    assert xif["pre_e2_remaining_observation_count"] == 12
    assert set(xif["required_observations"]) == {
        "bp_flow_control",
        "direct_packed_struct_fields",
        "event_export_full_set",
        "event_find_flat_xout",
        "event_find_line_limit",
        "none_flow_control",
        "paired_master_slave",
        "rdy_flow_control",
        "relational_expression",
        "unknown_alias_error",
        "value_at_struct_member",
        "xz_unknown_expression",
    }


def test_cross_fixture_surface_is_a_complete_contract_index_only() -> None:
    cross = load_audit()["cross_fixture"]
    assert cross["fixture_id"] == "original.cross_fixture"
    assert cross["classification_target"] == "proven-unobservable"
    assert cross["consumer_count"] == 37
    assert len(cross["consumer_paths"]) == 37
    assert len(cross["consumer_sha256"]) == 37
    assert cross["observed_public_action_count"] == 72
    assert len(cross["contracts"]) == 72
    assert cross["catalog_actions_not_observed"] == ["session.kill"]
    assert cross["remaining_unmapped_consumer_action_count"] == 0
    assert "各波形 fixture" in cross["proof_scope"]


def test_p3e_validator_rejects_four_boundary_mutation_classes() -> None:
    audit = load_audit()

    missing_schema = deepcopy(audit)
    missing_schema["authority"]["contracts"].pop("event.find")
    with pytest.raises(MatrixError, match="content drifted"):
        validate_audit(missing_schema, ROOT, original_root(), MANIFEST)

    private_became_public = deepcopy(audit)
    private_became_public["sva_npi"]["public_exposure_count"] = 1
    with pytest.raises(MatrixError, match="content drifted"):
        validate_audit(private_became_public, ROOT, original_root(), MANIFEST)

    missing_cross_action = deepcopy(audit)
    missing_cross_action["cross_fixture"]["observed_public_actions"].pop()
    with pytest.raises(MatrixError, match="content drifted"):
        validate_audit(missing_cross_action, ROOT, original_root(), MANIFEST)

    missing_xif_observation = deepcopy(audit)
    missing_xif_observation["xif_event"]["required_observations"].pop()
    with pytest.raises(MatrixError, match="content drifted"):
        validate_audit(missing_xif_observation, ROOT, original_root(), MANIFEST)


def test_p3e_writer_rejects_repository_escape() -> None:
    outside = ROOT.parent / ".p3e-escape-must-not-exist.json"
    assert not outside.exists()
    with pytest.raises(MatrixError, match="escapes repository"):
        write_document(ROOT, outside, {})
    assert not outside.exists()
