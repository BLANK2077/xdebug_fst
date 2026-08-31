# test_p3d_apb_matrix_closure.py — P3-D2 清单/矩阵 fail-closed 变异门禁
from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from tools.build_rtl_wave_semantic_matrix import (
    MatrixError,
    asset_lookup,
    validate_p3d_apb_oracle_document,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads(
    (REPO_ROOT / "compat/xdebug-v1/rtl-wave-assets.manifest.json")
    .read_text(encoding="utf-8")
)
ORIGINAL_ASSETS = asset_lookup(MANIFEST, "original")
ORACLES = {
    "xdebug.apb_vip": json.loads(
        (REPO_ROOT / "tests/data/rtl_wave_differential/"
         "p3d-apb-vip.public-oracle.json").read_text(encoding="utf-8")
    ),
    "xdebug.apb_xamba_vip": json.loads(
        (REPO_ROOT / "tests/data/rtl_wave_differential/"
         "p3d-apb-xamba-vip.public-oracle.json").read_text(encoding="utf-8")
    ),
}


@pytest.mark.parametrize("fixture_id", ORACLES)
def test_p3d_apb_matrix_validator_accepts_both_locked_oracles(
    fixture_id: str,
) -> None:
    result = validate_p3d_apb_oracle_document(
        ORACLES[fixture_id], fixture_id, ORIGINAL_ASSETS
    )
    assert result["difference_count"] == 0
    assert result["remaining_observable_gap_count"] == 0
    assert result["xout_check_count"] == 4


def mutate(document: dict, mutation: str) -> None:
    observations = {
        row["observation_id"]: row for row in document["observations"]
    }
    transactions = observations["query.full"]["response"]["data"][
        "transactions"
    ]
    if mutation == "fixture_swap":
        document["fixture_id"] = "xdebug.apb_vip"
    elif mutation == "missing_transaction":
        transactions.pop()
    elif mutation == "timing_drift":
        transactions[1]["time"] = "86ns"
    elif mutation == "dropped_error":
        transactions[0]["has_error"] = False
    elif mutation == "formula_drift":
        document["original_fixture"]["stimulus_contract"][7]["pprot"] = 0
    elif mutation == "fake_complete_truncation":
        transactions.pop()
        observations["query.full"]["response"]["summary"].update({
            "scan_complete": True,
            "analysis_complete": True,
            "response_truncated": False,
            "truncation_scopes": [],
        })
    elif mutation == "private_probe_leak":
        document["cache_contract"]["soft_lru"]["public_observations"][0][
            "response"
        ]["summary"]["hits"] = 1
    elif mutation == "hidden_hard_limit":
        document["cache_contract"]["hard_limit"]["classification"] = (
            "proven-unobservable"
        )
    else:  # pragma: no cover
        raise AssertionError(mutation)


@pytest.mark.parametrize(
    "mutation",
    [
        "fixture_swap",
        "missing_transaction",
        "timing_drift",
        "dropped_error",
        "formula_drift",
        "fake_complete_truncation",
        "private_probe_leak",
        "hidden_hard_limit",
    ],
)
def test_p3d_apb_matrix_validator_rejects_semantic_mutations(
    mutation: str,
) -> None:
    document = copy.deepcopy(ORACLES["xdebug.apb_xamba_vip"])
    mutate(document, mutation)
    with pytest.raises(MatrixError):
        validate_p3d_apb_oracle_document(
            document, "xdebug.apb_xamba_vip", ORIGINAL_ASSETS
        )
