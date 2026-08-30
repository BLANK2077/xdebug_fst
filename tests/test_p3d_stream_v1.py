# test_p3d_stream_v1.py — P3-D1 stream_v1 双侧公开语义门禁
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
from typing import Any

from conftest import open_session
from runner import StdioLoopRunner


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "testdata/fixtures/stream_v1"
ORACLE_PATH = (
    REPO_ROOT
    / "tests/data/rtl_wave_differential/p3d-stream-v1.public-oracle.json"
)
ORACLE = json.loads(ORACLE_PATH.read_text(encoding="utf-8"))
RUNTIME_REVISION = "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
SCHEMA_REVISION = (
    "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def load_hash_lock(path: Path) -> dict[str, str]:
    rows: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        assert name not in rows
        rows[name] = digest
    return rows


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    return selected


def test_stream_v1_oracle_locks_runtime_fixture_and_observation_set() -> None:
    assert ORACLE["schema_version"] == "xdebug.p3d-stream-v1-public-oracle.v1"
    assert ORACLE["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert ORACLE["fixture_id"] == "xdebug.stream_v1"
    assert ORACLE["locked_runtime"] == {
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
        "fixture_version": (
            "5eca27af24084f076f68c6a77c6fe0cb9e0a152332912dbf074cabc3b4600ede"
            "-prepare-c54cyr7t"
        ),
        "cache_reused": True,
        "fixture_rebuilt": False,
        "source_access": "read_only",
    }
    assert ORACLE["session"] == {
        "mode": "waveform",
        "transport": "uds",
        "opened": True,
        "closed_gracefully": True,
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
    }
    original = ORACLE["original_fixture"]
    assert original["rtl_sha256"] == (
        "4e9a24d992ceb1d58f5b300cbca46345c70f9da9d1ad13648475711342357438"
    )
    assert original["config_sha256"] == (
        "22269ed6fc9bedf16be4370f7cbc9374980a9e52414a49d2b436b3d62a3dd22a"
    )
    assert original["fsdb_sha256"] == (
        "0507c9c05c067f70d75037b3fdedd7bc8c4464d527df9b5eb0ffddfd811280d0"
    )
    assert original["expected"] == {
        "streams": {
            "valid_only": {"transfer_count": 20000},
            "ready_stream": {"transfer_count": 15059, "stall_cycles": 3764},
            "bp_stream": {"transfer_count": 17142, "stall_cycles": 2858},
            "ready_packet": {"transfer_count": 20000, "packet_count": 5000},
            "bp_packet": {"transfer_count": 20000, "packet_count": 5000},
            "ready_bp_packet_negedge": {
                "transfer_count": 20000,
                "packet_count": 5000,
                "ready_bp_conflict_count": 1,
            },
            "interleaved_packet": {
                "transfer_count": 20000,
                "packet_count": 5000,
            },
        },
    }
    assert ORACLE["observation_count"] == 58
    ids = [row["observation_id"] for row in ORACLE["observations"]]
    assert len(ids) == len(set(ids)) == ORACLE["observation_count"]
    assert {
        "config.load",
        "valid_only.summary",
        "ready_stream.beat_filter",
        "ready_packet.partial_summary",
        "ready_packet.packet_filter",
        "bp_packet.first_packet",
        "ready_bp_packet_negedge.packet_window",
        "interleaved_packet.packet_window",
        "config.invalid_interleaving",
    } <= set(ids)
    assert "/home/" not in json.dumps(ORACLE, ensure_ascii=False)


def test_stream_v1_current_fst_matches_all_locked_public_observations(
    loop_runner: StdioLoopRunner,
) -> None:
    rtl = FIXTURE / "stream_v1_top.sv"
    config = FIXTURE / "streams.json"
    expected_path = FIXTURE / "stream_expected.json"
    fst = FIXTURE / "waves.fst"
    hash_lock = FIXTURE / "fixture.sha256"
    for path in (rtl, config, expected_path, fst, hash_lock):
        assert path.is_file(), f"P3-D1 当前 stream_v1 资产缺失: {path}"
    original = ORACLE["original_fixture"]
    assert sha256(rtl) == original["rtl_sha256"]
    assert sha256(config) == original["config_sha256"]
    assert json.loads(expected_path.read_text(encoding="utf-8")) == original[
        "expected"
    ]
    assert fst.stat().st_size > 1024

    open_session(loop_runner, fst)
    try:
        for observation in ORACLE["observations"]:
            request_args = copy.deepcopy(observation["request"])
            if "config_path" in request_args:
                request_args["config_path"] = str(config)
            actual = loop_runner.request(
                observation["action"], args=request_args
            )
            assert public_response(actual) == observation["response"], (
                f"stream_v1 公开观察差异: {observation['observation_id']}"
            )
    finally:
        if loop_runner.has_current_session:
            closed = loop_runner.request("session.close", args={})
            assert closed.get("ok"), closed


def test_stream_v1_current_fixture_locks_tool_seed_and_deterministic_fst() -> None:
    lock = load_hash_lock(FIXTURE / "fixture.sha256")
    assert list(lock) == [
        "stream_v1_top.sv",
        "streams.json",
        "stream_expected.json",
        "verilator-no-fsdb.patch",
        "tb_stream_v1.cpp",
        "fixture.manifest.json",
        "waves.fst",
    ]
    for name, digest in lock.items():
        path = FIXTURE / name
        assert path.is_file() and not path.is_symlink()
        assert sha256(path) == digest

    manifest = json.loads(
        (FIXTURE / "fixture.manifest.json").read_text(encoding="utf-8")
    )
    assert manifest["schema_version"] == "xdebug-fst.fixture.stream-v1.v1"
    assert manifest["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert manifest["source_contract"] == {
        "original_fixture_id": "xdebug.stream_v1",
        "rtl_byte_identical": True,
        "rtl_sha256": lock["stream_v1_top.sv"],
        "config_byte_identical": True,
        "config_sha256": lock["streams.json"],
        "expected_sha256": lock["stream_expected.json"],
        "stimulus_count": 20000,
        "randomization": False,
        "seed": "not-applicable-deterministic-formulas",
        "timescale": "1ns/1ps",
        "clock_half_period": "5ns",
        "waveform_end": "200105ns",
    }
    dependency_lock = json.loads(
        (REPO_ROOT / "dependencies.lock.json").read_text(encoding="utf-8")
    )["verilator"]
    build_contract = manifest["build_contract"]
    for key in ("version", "revision", "tree", "patchset_version"):
        assert build_contract[key] == dependency_lock[key]
    assert build_contract["fingerprint"] == (
        "8b8b0886d5338b71500dd218e7af46bcd1c2a2dabb16492addeea82c85b14bb9"
    )
    assert build_contract["gcc_version"] == "13.3.1"
    assert build_contract["trace_format"] == "fst"
    assert build_contract["fsdb_task_overlay"] == {
        "compile_only": True,
        "rtl_committed_copy_modified": False,
        "sha256": lock["verilator-no-fsdb.patch"],
    }
    assert build_contract["harness_sha256"] == lock["tb_stream_v1.cpp"]
    output = manifest["output_contract"]
    assert output == {
        "path": "waves.fst",
        "size": (FIXTURE / "waves.fst").stat().st_size,
        "sha256": lock["waves.fst"],
        "expected_path": "stream_expected.json",
        "expected_checked_by_rtl": True,
        "fsdb_conversion_used": False,
        "external_cache_rebuilt": False,
    }
    assert "/home/" not in json.dumps(manifest, ensure_ascii=False)
