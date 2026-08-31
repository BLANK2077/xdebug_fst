# test_p3d_axi_matrix_closure.py — P3-D3 清单/矩阵 fail-closed 变异门禁
from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from tools.build_rtl_wave_semantic_matrix import (
    MatrixError,
    validate_p3d_axi_oracle_document,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
ORACLES = {
    "xdebug.axi_vip": json.loads(
        (REPO_ROOT / "tests/data/rtl_wave_differential/"
         "p3d-axi-vip.public-oracle.json").read_text(encoding="utf-8")
    ),
    "xdebug.axi_xamba_vip": json.loads(
        (REPO_ROOT / "tests/data/rtl_wave_differential/"
         "p3d-axi-xamba-vip.public-oracle.json").read_text(encoding="utf-8")
    ),
}


@pytest.mark.parametrize(
    ("fixture_id", "profiles", "transactions", "xouts", "artifacts"),
    [
        ("xdebug.axi_vip", 5, 8000, 35, 15),
        ("xdebug.axi_xamba_vip", 1, 64, 7, 3),
    ],
)
def test_p3d_axi_matrix_validator_accepts_all_locked_profiles(
    fixture_id: str,
    profiles: int,
    transactions: int,
    xouts: int,
    artifacts: int,
) -> None:
    result = validate_p3d_axi_oracle_document(ORACLES[fixture_id], fixture_id)
    assert result == {
        "profile_count": profiles,
        "observation_count": profiles * 17,
        "transaction_count": transactions,
        "difference_count": 0,
        "xout_check_count": xouts,
        "artifact_check_count": artifacts,
        "remaining_observable_gap_count": 0,
    }


def mutate(document: dict, mutation: str) -> None:
    run = document["runs"][0]
    observations = {row["observation_id"]: row for row in run["observations"]}
    if mutation == "fixture_swap":
        document["fixture_id"] = "xdebug.axi_xamba_vip"
    elif mutation == "missing_profile":
        document["runs"].pop()
    elif mutation == "seed_drift":
        run["seed"] = 99
    elif mutation == "channel_count_drift":
        run["handshake_channel_counts"]["W"] -= 1
    elif mutation == "source_identity_drift":
        run["handshake_oracle"]["sha256"] = "0" * 64
    elif mutation == "missing_observation":
        run["observations"].pop()
    elif mutation == "fake_complete_truncation":
        observations["pair.full"]["response"]["summary"][
            "response_truncated"
        ] = True
    elif mutation == "export_row_drift":
        observations["export.full"]["response"]["summary"]["row_count"] -= 1
    elif mutation == "artifact_identity_drift":
        observations["export.full"]["artifacts"][0]["sha256"] = "invalid"
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
        "missing_profile",
        "seed_drift",
        "channel_count_drift",
        "source_identity_drift",
        "missing_observation",
        "fake_complete_truncation",
        "export_row_drift",
        "artifact_identity_drift",
        "hidden_hard_limit",
    ],
)
def test_p3d_axi_matrix_validator_rejects_semantic_mutations(
    mutation: str,
) -> None:
    document = copy.deepcopy(ORACLES["xdebug.axi_vip"])
    mutate(document, mutation)
    with pytest.raises(MatrixError):
        validate_p3d_axi_oracle_document(document, "xdebug.axi_vip")
