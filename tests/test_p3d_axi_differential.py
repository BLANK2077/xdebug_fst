# test_p3d_axi_differential.py — P3-D3 AXI 六波形差分红灯与冻结证据
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

import pytest


REPO_ROOT = Path(__file__).resolve().parents[1]
DATA_ROOT = REPO_ROOT / "tests/data/rtl_wave_differential"
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
AXI_ACTIONS = [
    "axi.analysis",
    "axi.channel_stall",
    "axi.config.list",
    "axi.config.load",
    "axi.export",
    "axi.latency_outlier",
    "axi.outstanding_timeline",
    "axi.query",
    "axi.request_response_pair",
    "axi.statistics",
    "axi.transaction.cursor",
]
EXPECTED = {
    "xdebug.axi_vip": {
        "oracle": "p3d-axi-vip.public-oracle.json",
        "sha256": "433f6197245c0ab9eff27e5e04f9670a9e2e83b71a0bb36f15a3b469e88476ff",
        "producer_kind": "svt",
        "fixture_version": (
            "b7a0d81ad90d77fb97c0da6239e1e69a10671089527be0adf5e7a21e5507c1f0"
            "-prepare-56r6ooao"
        ),
        "current_fixture": "axi_vip",
        "runs": {
            "stress": (7, 3200, 51472, "3ef5a7a37c61aae748ffecb24aabe75c0de3b6546ec2ef681e033d9a56f7366c"),
            "fixed_delay": (7, 32, 528, "95ef8c798c69c07b0d99749579630b7d5085fc067d2df0f2859462580c2ea029"),
            "random_seed_7": (7, 256, 3971, "7f66e5b41a8c0aaf219411d74fb48661bfffc7bc54827c43ae0e0bebe5028cf4"),
            "random_seed_19": (19, 256, 4114, "cecb489860f4f9fe6ee70fa653bed184dfe75a4e4ee2cafff323fe497aafb94e"),
            "random_seed_73": (73, 256, 3948, "792cbbc58bd02e9a8d39acfd969e5e96ed91d470c9e0a93ceac0b070fe43e580"),
        },
    },
    "xdebug.axi_xamba_vip": {
        "oracle": "p3d-axi-xamba-vip.public-oracle.json",
        "sha256": "608222fc871d389f3237b2230ff23f28bb106a7b4fd650f8de52091255a38bb7",
        "producer_kind": "xamba",
        "fixture_version": (
            "fbca46529549b7ac93228fce67a20a7487e03645bf5b111877d3c9dd3deb0ed8"
            "-prepare-0papt1js"
        ),
        "current_fixture": "axi_xamba_vip",
        "runs": {
            "xamba": (7, 32, 256, "09d6c69b87dad45fe48a572dda4ddbdab9b506c3a53cc782f41a8014327da5b5"),
        },
    },
}


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_oracle(fixture_id: str) -> dict:
    return json.loads(
        (DATA_ROOT / EXPECTED[fixture_id]["oracle"]).read_text(encoding="utf-8")
    )


@pytest.mark.parametrize("fixture_id", EXPECTED)
def test_axi_oracle_locks_runtime_surface_sources_and_six_waveforms(
    fixture_id: str,
) -> None:
    expected = EXPECTED[fixture_id]
    path = DATA_ROOT / expected["oracle"]
    oracle = load_oracle(fixture_id)
    assert file_sha256(path) == expected["sha256"]
    assert oracle["schema_version"] == "xdebug.p3d-axi-public-oracle.v1"
    assert oracle["goal_id"] == GOAL_ID
    assert oracle["fixture_id"] == fixture_id
    assert oracle["producer_kind"] == expected["producer_kind"]
    assert oracle["locked_runtime"]["fixture_version"] == expected["fixture_version"]
    assert oracle["locked_runtime"]["action_count"] == 73
    assert oracle["locked_runtime"]["cache_reused"] is True
    assert oracle["locked_runtime"]["fixture_rebuilt"] is False
    assert oracle["locked_runtime"]["source_access"] == "read_only"
    assert oracle["action_authority"]["axi_actions"] == AXI_ACTIONS
    assert len(oracle["action_authority"]["schemas"]) == 11
    assert oracle["action_authority"][
        "private_probe_fields_in_public_request_schema"
    ] == []
    assert oracle["action_authority"][
        "private_probe_fields_in_public_response_schema"
    ] == ["key_summary"]
    assert oracle["session"] == {
        "mode": "waveform",
        "transport": "uds",
        "session_count": len(expected["runs"]) + 2,
        "all_sessions_closed_gracefully": True,
        "all_runtime_writes_repository_local": True,
        "source_access": "read_only",
        "fixture_rebuilt": False,
        "fallback_used": False,
    }
    assert len(oracle["original_fixture"]["source_files"]) >= 5
    assert all(
        not row["path"].startswith("/")
        and re.fullmatch(r"[0-9a-f]{64}", row["sha256"])
        for row in oracle["original_fixture"]["source_files"]
    )
    assert "/home/" not in json.dumps(oracle, ensure_ascii=False)
    cache = oracle["cache_contract"]
    assert cache["soft_lru"]["private_eviction_classification"] == (
        "proven-unobservable"
    )
    assert cache["soft_lru"]["private_probe"]["last"]["evictions"] >= 1
    assert cache["hard_limit"]["classification"] == "publicly-observable"
    assert cache["hard_limit"]["hard_max_bytes"] == 1
    hard_error = cache["hard_limit"]["public_observations"][-1]["response"][
        "error"
    ]
    assert hard_error["code"] == "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
    assert hard_error["protocol"] == "axi"


@pytest.mark.parametrize("fixture_id", EXPECTED)
def test_axi_oracle_locks_full_transactions_xout_export_and_cache(
    fixture_id: str,
) -> None:
    oracle = load_oracle(fixture_id)
    expected_runs = EXPECTED[fixture_id]["runs"]
    assert oracle["run_count"] == len(expected_runs)
    assert {run["name"] for run in oracle["runs"]} == set(expected_runs)
    for run in oracle["runs"]:
        seed, direction_count, handshake_lines, fsdb_sha = expected_runs[run["name"]]
        assert run["seed"] == seed
        assert run["expected_direction_count"] == direction_count
        assert run["fsdb_sha256"] == fsdb_sha
        assert run["handshake_line_count"] == handshake_lines
        assert run["handshake_channel_counts"]["AW"] == direction_count
        assert run["handshake_channel_counts"]["AR"] == direction_count
        assert run["handshake_channel_counts"]["B"] == direction_count
        assert run["observation_count"] == 17
        assert len({row["observation_id"] for row in run["observations"]}) == 17
        assert len([row for row in run["observations"] if "xout" in row]) == 7
        observations = {row["observation_id"]: row for row in run["observations"]}
        pair = observations["pair.full"]["response"]
        assert pair["summary"]["total_count"] == 2 * direction_count
        assert pair["summary"]["returned_count"] == 2 * direction_count
        assert pair["summary"]["scan_complete"] is True
        assert pair["summary"]["analysis_complete"] is True
        assert pair["summary"]["response_truncated"] is False
        assert pair["summary"]["truncation_scopes"] == []
        export = observations["export.full"]
        assert export["response"]["summary"]["row_count"] == 2 * direction_count
        assert export["response"]["summary"]["output_written"] is True
        assert {item["name"] for item in export["artifacts"]} == {
            "axi0.meta.json", "axi0.read.tsv", "axi0.write.tsv"
        }
        assert all(item["size"] > 0 for item in export["artifacts"])
        assert all(re.fullmatch(r"[0-9a-f]{64}", item["sha256"])
                   for item in export["artifacts"])
        assert run["cache_probe"]["last"]["scanner_invocations"] == 1
        assert run["cache_probe"]["classification"] == (
            "private-proven-unobservable"
        )


@pytest.mark.parametrize("fixture_id", EXPECTED)
def test_current_axi_dedicated_fixture_exists_before_replay(
    fixture_id: str,
) -> None:
    """D3-1 有效业务红灯：当前专属六波形 fixture 尚未实现。"""

    fixture = REPO_ROOT / "testdata/fixtures" / EXPECTED[fixture_id][
        "current_fixture"
    ]
    assert fixture.is_dir(), (
        f"P3-D3 RED: missing dedicated current fixture {fixture.relative_to(REPO_ROOT)}"
    )
    assert (fixture / "fixture.manifest.json").is_file()
    assert (fixture / "fixture.sha256").is_file()
