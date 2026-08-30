# test_p3d_apb_differential.py — P3-D2 APB SVT/XAMBA 双侧公开语义门禁
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import re
from typing import Any

import pytest

from conftest import open_session
from runner import StdioLoopRunner


REPO_ROOT = Path(__file__).resolve().parents[1]
DATA_ROOT = REPO_ROOT / "tests/data/rtl_wave_differential"
RUNTIME_REVISION = "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
SCHEMA_REVISION = (
    "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
)
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
APB_ACTIONS = [
    "apb.config.list",
    "apb.config.load",
    "apb.query",
    "apb.statistics",
    "apb.transaction.cursor",
    "apb.transfer_window",
]
EXPECTED = {
    "xdebug.apb_vip": {
        "oracle": "p3d-apb-vip.public-oracle.json",
        "oracle_sha256": (
            "c99fd91efc9f9f2558b6e037ed677907f6f681fa1777c8015dbf1fd1fb16f96b"
        ),
        "producer_kind": "svt",
        "fixture_version": (
            "5b0d1be836520bd8421bb4193d12949c5ba4c3098cc94bd1dede3d5a81fb4709"
            "-prepare-ui7xh3j3"
        ),
        "manifest_sha256": (
            "ec844cb757f40a9518b1e5e747f70d52524421e2df9d34b42db9c383d3dee28b"
        ),
        "cache_manifest_sha256": (
            "ed6988672f58d343adf74d202228a5fbf24f5214973cdc50b198aec8c0922a62"
        ),
        "fsdb_sha256": (
            "fea2e60f2a575980db7fdddee4a22d1be05520769251144bcd04671a8da63e1f"
        ),
        "fsdb_size": 21054,
        "top": "apb_vip_fixture_top",
        "interface": "apb_vip_fixture_top.apb_if",
        "observation_count": 36,
        "transaction_count": 10,
        "write_count": 5,
        "read_count": 5,
        "error_count": 1,
        "first_time": "125ns",
        "last_time": "525ns",
        "source_hashes": {
            "xdebug/testdata/waveform/apb_vip_real/seq/apb_vip_sequence.sv": (
                "7ddc94661d4fb062d5ce6956de4368ed9a715a75dcbd907f69aff7d7e5c6d16a"
            ),
            "xdebug/testdata/waveform/apb_vip_real/tb/apb_slave_dut.sv": (
                "40de7c2d9e8508c98fbbf5c856bbba605d1d8e00cd337eaf72a98bb1da7965d6"
            ),
            "xdebug/testdata/waveform/apb_vip_real/tb/apb_vip_fixture_top.sv": (
                "3235afba27ce07b231b6096c33197648dfe04b7c47e6eb5ceb153dbc786f2763"
            ),
        },
        "xout_hashes": {
            "query.preview": (
                "d2351c4f24aa0f3ccf34a92bab7dca7430c5f5ad08dba94365e62225c5a02129"
            ),
            "statistics.all": (
                "43e0861890315acdb7b2f2ec079254aae44f0366334ff89f39ba1d88af419ee7"
            ),
            "window.preview": (
                "c53d4b89a22deb24643db478a73ad9b7cde27bd7847a9ef67c7aed26e964207e"
            ),
            "cursor.begin": (
                "b478c3f7915aac5f3ce45cdeac3b209479aae22f8b3591bd7cd77b4795b8830b"
            ),
        },
        "fixture_dir": "apb_vip",
        "rtl": "apb_vip_fixture_top.sv",
        "testbench": "tb_apb_vip.cpp",
    },
    "xdebug.apb_xamba_vip": {
        "oracle": "p3d-apb-xamba-vip.public-oracle.json",
        "oracle_sha256": (
            "574c9890ff9df89d70a9ecdbeb0c56183621d8b076975e3df26537ee93975b0c"
        ),
        "producer_kind": "xamba",
        "fixture_version": (
            "05c16a52c99491286afecda1bd88dbcb992f340c627cbca5d6304985cc8a262e"
            "-prepare-xgy6zv8g"
        ),
        "manifest_sha256": (
            "4f61a180bfbfa7468602ea43912e4d7babbb9803f438dc1f83975ce4d6a9fee0"
        ),
        "cache_manifest_sha256": (
            "1bc0ba5d86d4b361a4f6c4c949382a24cf158dd0eab0638d299612fafd510e96"
        ),
        "fsdb_sha256": (
            "8f103264f3cbd45bf0155583e161946009856513bdfe2fa6b9c71e9b37b1f016"
        ),
        "fsdb_size": 11087,
        "top": "xdebug_apb_xamba_fixture_top",
        "interface": "xdebug_apb_xamba_fixture_top.dut.apb_reply_if",
        "observation_count": 34,
        "transaction_count": 64,
        "write_count": 32,
        "read_count": 32,
        "error_count": 6,
        "first_time": "45ns",
        "last_time": "2895ns",
        "source_hashes": {
            (
                "xdebug/testdata/waveform/apb_xamba_vip_real/tb/"
                "xdebug_apb_xamba_fixture_pkg.sv"
            ): (
                "fc039da4a994ef49dd21600b021fe51b7da8d40627afa213b6263680523dc12f"
            ),
            (
                "xdebug/testdata/waveform/apb_xamba_vip_real/tb/"
                "xdebug_apb_xamba_fixture_top.sv"
            ): (
                "fa8475957bb42b248e0beb8f82048c70811e6a5304ee825972a666bbc0a91420"
            ),
        },
        "xout_hashes": {
            "query.preview": (
                "42f36a7b1dbe152a405a0fd83fe648b8cfd2782a00b3c9821ba9a74f6018971e"
            ),
            "statistics.all": (
                "ed4c58663e8436d43c60d81fe332a56d345da15773f3690b98a2549096b470c3"
            ),
            "window.preview": (
                "b7841f4c33a0dd58b2784509435eab4c9afc119f43b7eb104c076cf58b93ffbd"
            ),
            "cursor.begin": (
                "ea3b441ff4493a981200c708e1af111f5eb29247d6ff7a374aab8d969d073014"
            ),
        },
        "fixture_dir": "apb_xamba_vip",
        "rtl": "xdebug_apb_xamba_fixture_top.sv",
        "testbench": "tb_apb_xamba.cpp",
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def load_oracle(fixture_id: str) -> dict[str, Any]:
    path = DATA_ROOT / EXPECTED[fixture_id]["oracle"]
    return json.loads(path.read_text(encoding="utf-8"))


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    return selected


def observation_by_id(oracle: dict[str, Any], observation_id: str) -> dict[str, Any]:
    return next(
        row for row in oracle["observations"]
        if row["observation_id"] == observation_id
    )


@pytest.mark.parametrize("fixture_id", EXPECTED)
def test_apb_oracle_locks_runtime_fixture_surface_and_all_transactions(
    fixture_id: str,
) -> None:
    expected = EXPECTED[fixture_id]
    path = DATA_ROOT / expected["oracle"]
    oracle = load_oracle(fixture_id)
    assert sha256(path) == expected["oracle_sha256"]
    assert oracle["schema_version"] == "xdebug.p3d-apb-public-oracle.v1"
    assert oracle["goal_id"] == GOAL_ID
    assert oracle["fixture_id"] == fixture_id
    assert oracle["producer_kind"] == expected["producer_kind"]
    assert oracle["locked_runtime"] == {
        "runtime_revision": RUNTIME_REVISION,
        "schema_revision": SCHEMA_REVISION,
        "build_id": f"{RUNTIME_REVISION[:12]}-{SCHEMA_REVISION}",
        "action_count": 73,
        "wrapper_sha256": (
            "c9569332281ccad39099e06d547075d33b35f9b50699e4d148203ad5645978e7"
        ),
        "binary_sha256": (
            "0f54515fa1c7e80634cdba1e9455ed7cdc1acae26144c7c5a39200853becf46d"
        ),
        "npi_version": "X-2025.06-SP1",
        "fixture_version": expected["fixture_version"],
        "cache_reused": True,
        "fixture_rebuilt": False,
        "source_access": "read_only",
    }
    assert oracle["session"] == {
        "mode": "waveform",
        "transport": "uds",
        "session_count": 3,
        "all_sessions_closed_gracefully": True,
        "all_runtime_writes_repository_local": True,
        "source_access": "read_only",
        "fixture_rebuilt": False,
        "fallback_used": False,
    }

    original = oracle["original_fixture"]
    assert original["manifest_sha256"] == expected["manifest_sha256"]
    assert original["cache_manifest_sha256"] == expected[
        "cache_manifest_sha256"
    ]
    assert original["fsdb_sha256"] == expected["fsdb_sha256"]
    assert original["fsdb_size"] == expected["fsdb_size"]
    assert original["top"] == expected["top"]
    assert original["interface"] == expected["interface"]
    assert original["seed"] == 11
    source_hashes = {
        row["path"]: row["sha256"] for row in original["source_files"]
    }
    assert source_hashes | expected["source_hashes"] == source_hashes
    assert all(
        re.fullmatch(r"[0-9a-f]{64}", row["sha256"])
        and not row["path"].startswith("/")
        for row in original["source_files"] + original["resource_files"]
    )
    assert original["field_observability"] == {
        "direct_transaction_fields": [
            "time", "is_write", "addr", "data", "has_error"
        ],
        "indirect_source_fields": ["pstrb", "wait_cycles"],
        "xamba_source_only_fields": (
            ["pprot", "pnse"] if expected["producer_kind"] == "xamba" else []
        ),
        "rule": (
            "PSTRB/PPROT/PNSE/wait count are not direct fields of the six "
            "frozen APB Action responses; preserve them in the source "
            "stimulus contract and compare completion time/data/error."
        ),
    }

    assert oracle["observation_count"] == expected["observation_count"]
    ids = [row["observation_id"] for row in oracle["observations"]]
    assert len(ids) == len(set(ids)) == oracle["observation_count"]
    assert {
        "query.full",
        "query.preview",
        "query.address.exact",
        "query.address.range.write",
        "query.address.mask.read",
        "query.value.decimal",
        "query.value.binary",
        "statistics.all",
        "window.full",
        "window.invalid_range",
        "cursor.pre.at_begin",
        "query.missing_config",
    } <= set(ids)
    assert all(row["action"] != "apb.export" for row in oracle["observations"])

    full = observation_by_id(oracle, "query.full")["response"]
    assert full["summary"]["total_count"] == expected["transaction_count"]
    assert full["summary"]["returned_count"] == expected["transaction_count"]
    assert full["summary"]["scan_complete"] is True
    assert full["summary"]["analysis_complete"] is True
    assert full["summary"]["response_truncated"] is False
    assert full["summary"]["value_width_complete"] is True
    assert full["summary"]["width_diagnostics"] == []
    transactions = full["data"]["transactions"]
    assert len(transactions) == expected["transaction_count"]
    assert sum(row["is_write"] for row in transactions) == expected["write_count"]
    assert sum(not row["is_write"] for row in transactions) == expected["read_count"]
    assert sum(row["has_error"] for row in transactions) == expected["error_count"]
    assert transactions[0]["time"] == expected["first_time"]
    assert transactions[-1]["time"] == expected["last_time"]

    stimulus = original["stimulus_contract"]
    assert len(stimulus) == len(transactions)
    for index, (contract, transaction) in enumerate(zip(stimulus, transactions)):
        assert contract["index"] == index
        assert transaction == {
            "time": transaction["time"],
            "is_write": contract["is_write"],
            "addr": contract["address"],
            "data": contract["public_data"],
            "has_error": contract["has_error"],
        }
        if expected["producer_kind"] == "xamba":
            assert contract["address"] == f"32'h{0x1000 + index * 4:08x}"
            assert contract["pstrb"] == (
                f"4'h{(1 << (index % 4)) if index % 2 == 0 else 0:x}"
            )
            assert contract["pprot"] == index % 8
            assert contract["pnse"] == (index // 8) % 2
            assert contract["wait_cycles"] == index % 4
            assert contract["has_error"] is (index % 11 == 0)
            expected_time = 45 + 30 * index + 10 * sum(
                value % 4 for value in range(1, index + 1)
            )
            assert transaction["time"] == f"{expected_time}ns"
        elif index:
            previous = int(transactions[index - 1]["time"].removesuffix("ns"))
            current = int(transaction["time"].removesuffix("ns"))
            assert current - previous == 30 + 10 * contract["wait_cycles"]

    xout_rows = {
        row["observation_id"]: row
        for row in oracle["observations"] if "xout" in row
    }
    assert set(xout_rows) == set(expected["xout_hashes"])
    for observation_id, digest in expected["xout_hashes"].items():
        row = xout_rows[observation_id]
        assert hashlib.sha256(row["xout"].encode()).hexdigest() == digest
        assert row["xout_sha256"] == digest
        assert row["xout"].startswith(f"@xdebug.{row['action']}.v1\n")
        assert row["xout"].endswith("\n")
    assert "/home/" not in json.dumps(oracle, ensure_ascii=False)


@pytest.mark.parametrize("fixture_id", EXPECTED)
def test_apb_oracle_locks_six_action_authority_and_cache_boundaries(
    fixture_id: str,
) -> None:
    oracle = load_oracle(fixture_id)
    authority = oracle["action_authority"]
    assert authority["catalog_action_count"] == 73
    assert authority["apb_actions"] == APB_ACTIONS
    assert authority["private_probe_fields_in_public_request_schema"] == []
    assert authority["private_probe_fields_in_public_response_schema"] == [
        "key_summary"
    ]
    assert [row["action"] for row in authority["schemas"]] == APB_ACTIONS
    for row in authority["schemas"]:
        for kind in ("request", "response"):
            schema = row[kind]
            assert schema["schema_id"] == f"xdebug.{row['action']}.{kind}.v1"
            assert schema["schema_path"] == (
                f"schemas/v1/actions/{row['action']}.{kind}.schema.json"
            )
            assert re.fullmatch(r"[0-9a-f]{64}", schema["schema_sha256"])
            assert re.fullmatch(r"[0-9a-f]{64}", schema["canonical_sha256"])
    guide = authority["guide_request"]
    assert guide["ok"] is False
    assert guide["error"]["code"] == "INVALID_REQUEST"
    assert guide["error"]["invalid_arg"] == "args.output.view"

    excluded = authority["excluded_apb_export"]
    assert excluded["classification"] == "not-in-frozen-public-surface"
    assert excluded["dynamic_oracle_eligible"] is False
    assert excluded["fallback_to_live_runtime_allowed"] is False
    assert excluded["response"] == {
        "ok": False,
        "summary": {"error_code": "UNKNOWN_ACTION", "status": "error"},
        "data": None,
        "error": {
            "available_values": ["axi.export", "apb.query", "list.export"],
            "code": "UNKNOWN_ACTION",
            "error_layer": "handler",
            "invalid_arg": "action",
            "message": "unknown action: apb.export",
            "received": "apb.export",
            "recoverable": True,
        },
    }
    if fixture_id == "xdebug.apb_vip":
        assert excluded["fixture_consumer_reference_count"] == 7
        assert excluded["frozen_consumer_present"] is True
        assert excluded["frozen_consumer_reference_count"] == 0
    else:
        assert excluded["fixture_consumer_reference_count"] == 0
        assert excluded["frozen_consumer_present"] is False
        assert excluded["frozen_consumer_reference_count"] is None

    cache = oracle["cache_contract"]
    base = cache["base_hit_index"]
    assert base["private_probe_classification"] == "proven-unobservable"
    assert base["private_probe"]["event_counts"] == {
        "build": 1,
        "hit": 33,
        "index_build": 1,
        "invalidate": 1,
        "miss": 2,
        "scan": 1,
    }
    assert base["private_probe"]["last"]["scanner_invocations"] == 1

    soft = cache["soft_lru"]
    assert soft["soft_max_bytes"] == 1
    assert soft["hard_max_bytes"] == 2147483648
    assert soft["private_eviction_classification"] == "proven-unobservable"
    assert [row["observation_id"] for row in soft["public_observations"]] == [
        "config.before",
        "config.after",
        "cursor.before.begin",
        "cursor.before.next",
        "query.after.write",
        "cursor.before.resume",
    ]
    assert soft["private_probe"]["event_counts"] == {
        "build": 3,
        "evict": 2,
        "hit": 1,
        "invalidate": 1,
        "miss": 3,
        "oversize_admitted": 3,
        "scan": 3,
    }
    assert soft["private_probe"]["last"]["scanner_invocations"] == 3
    assert soft["private_probe"]["last"]["evictions"] == 2

    hard = cache["hard_limit"]
    assert hard["classification"] == "publicly-observable"
    assert hard["soft_max_bytes"] == hard["hard_max_bytes"] == 1
    assert hard["allowed_current_projection"] == {
        "error.key_summary": (
            "opaque 16-hex cache identity may differ because the current fixture "
            "is native FST rather than the original FSDB"
        )
    }
    assert [row["observation_id"] for row in hard["public_observations"]] == [
        "config.load", "query.write.rejected"
    ]
    error = hard["public_observations"][-1]["response"]["error"]
    assert error["code"] == "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
    assert error["protocol"] == "apb"
    assert error["hard_max_bytes"] == 1
    assert error["current_estimated_bytes"] == 0
    assert error["recoverable"] is True
    assert re.fullmatch(r"[0-9a-f]{16}", error["key_summary"])
    assert error["next_actions"] == [
        (
            "For stream analysis, explicitly retry with cache_scope=range or a "
            "smaller time_range."
        ),
        (
            "If range analysis still exceeds the limit, use x-npi for one-off "
            "offline analysis."
        ),
    ]
    assert hard["private_probe"]["event_counts"] == {"build_failed": 1}
    assert hard["private_probe"]["last"]["scanner_invocations"] == 0


@pytest.mark.parametrize("fixture_id", EXPECTED)
def test_current_apb_fixture_matches_every_locked_base_observation_and_xout(
    fixture_id: str,
    loop_runner: StdioLoopRunner,
) -> None:
    expected = EXPECTED[fixture_id]
    oracle = load_oracle(fixture_id)
    fixture = REPO_ROOT / "testdata/fixtures" / expected["fixture_dir"]
    required = (
        fixture / expected["rtl"],
        fixture / expected["testbench"],
        fixture / "waves.fst",
        fixture / "fixture.manifest.json",
        fixture / "fixture.sha256",
    )
    for path in required:
        assert path.is_file(), f"P3-D2 当前专属 APB fixture 资产缺失: {path}"
    assert (fixture / "waves.fst").stat().st_size > 1024

    open_session(loop_runner, fixture / "waves.fst")
    try:
        for locked in oracle["observations"]:
            args = copy.deepcopy(locked["request"])
            actual = loop_runner.request(locked["action"], args=args)
            assert public_response(actual) == locked["response"], (
                f"P3-D2 APB 公开响应差异: {fixture_id}: "
                f"{locked['observation_id']}"
            )
            if "xout" in locked:
                actual_xout = loop_runner.request_xout(
                    locked["action"], args=args
                )
                assert actual_xout == locked["xout"], (
                    f"P3-D2 APB XOUT 差异: {fixture_id}: "
                    f"{locked['observation_id']}"
                )
    finally:
        if loop_runner.has_current_session:
            closed = loop_runner.request("session.close", args={})
            assert closed.get("ok"), closed
