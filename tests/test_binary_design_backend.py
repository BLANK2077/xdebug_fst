"""Compact binary DesignDB conversion, manifest, and semantic parity tests."""

from __future__ import annotations

import json
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from conftest import open_session


def convert(repo_root: Path, source_bundle: Path, output_bundle: Path) -> Path:
    output_bundle.mkdir()
    output = output_bundle / "design.xddb"
    source = source_bundle / "libVgcd_xorigin__DesignDb.so"
    completed = subprocess.run(
        [sys.executable, str(repo_root / "tools" / "convert_xdd_so_to_binary.py"),
         "--write-manifest", str(source), str(output)],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30, check=False,
    )
    assert completed.returncode == 0, completed.stderr
    return output


def test_binary_converter_is_deterministic_and_compact(
        repo_root: Path, gcd_xorigin_design_db: Path, tmp_path: Path) -> None:
    first = convert(repo_root, gcd_xorigin_design_db, tmp_path / "first")
    second = convert(repo_root, gcd_xorigin_design_db, tmp_path / "second")
    assert first.read_bytes() == second.read_bytes()
    assert first.stat().st_size < (
        gcd_xorigin_design_db / "libVgcd_xorigin__DesignDb.so").stat().st_size
    magic, major, minor, endian, header_size, file_size = struct.unpack(
        "<8sIIIIQ", first.read_bytes()[:32])
    assert (magic, major, minor, endian, header_size, file_size) == (
        b"XDDBIN1\0", 1, 0, 0x01020304, 176, first.stat().st_size)
    assert json.loads((first.parent / "xdebug-design-db.json").read_text()) == {
        "database": "design.xddb",
        "format": "binary-v1",
        "schema_version": "xdebug.design-db-bundle.v2",
    }


def test_binary_backend_matches_xdd_so_action_responses(
        loop_runner, repo_root: Path, gcd_xorigin_fst: Path,
        gcd_xorigin_design_db: Path, test_home: Path, tmp_path: Path) -> None:
    cases = [
        ("signal.resolve", {"signal": "GCD.io_z"}),
        ("value.at", {"signal": "GCD.io_z", "time": "0ps"}),
        ("trace.driver", {"signal": "GCD.io_z"}),
        ("trace.active_driver", {"signal": "GCD.io_z", "time": "0ps"}),
        ("trace.active_driver_chain",
         {"signal": "GCD.io_z", "time": "0ps"}),
        ("signal.changes", {
            "signal": "GCD.io_z",
            "time_range": {"begin": "0ps", "end": "max"},
        }),
    ]
    open_session(loop_runner, gcd_xorigin_fst, gcd_xorigin_design_db)
    baseline = {}
    for action, args in cases:
        response = loop_runner.request(action, args=args)
        assert response.get("ok"), response
        baseline[action] = response

    binary_bundle = tmp_path / "binary"
    convert(repo_root, gcd_xorigin_design_db, binary_bundle)
    open_session(loop_runner, gcd_xorigin_fst, binary_bundle)
    states = [
        (path, json.loads(path.read_text(encoding="utf-8")))
        for path in test_home.rglob("state.json")
    ]
    state_path, state = next(
        item for item in states if item[1].get("session_id") == "test")
    assert state["design_db_format"] == "binary-v1"
    assert "opened design db: format=binary-v1" in \
        (state_path.parent / "debug.log").read_text(encoding="utf-8")
    listed = loop_runner.request("session.list", args={})
    assert listed.get("ok"), listed
    assert any(item["session_id"] == "test"
               for item in listed["data"]["sessions"])
    for action, args in cases:
        candidate = loop_runner.request(action, args=args)
        assert candidate.get("ok"), candidate
        assert candidate["summary"] == baseline[action]["summary"]
        assert candidate["data"] == baseline[action]["data"]
        assert candidate.get("limitations") == \
            baseline[action].get("limitations")


def test_binary_manifest_does_not_fallback_to_shared_library(
        loop_runner, gcd_xorigin_fst: Path, tmp_path: Path) -> None:
    if loop_runner.has_current_session:
        closed = loop_runner.request("session.close", args={})
        assert closed.get("ok"), closed
    bundle = tmp_path / "invalid"
    bundle.mkdir()
    (bundle / "design.so").write_bytes(b"not a shared library")
    (bundle / "xdebug-design-db.json").write_text(json.dumps({
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": "design.so",
    }), encoding="utf-8")
    response = loop_runner.request("session.open", target={
        "fsdb": str(gcd_xorigin_fst), "daidir": str(bundle)},
        args={"name": "test"})
    assert not response.get("ok")
    assert response["error"]["code"] == "DESIGN_BUNDLE_INVALID"


def test_truncated_binary_fails_closed_during_session_start(
        loop_runner, repo_root: Path, gcd_xorigin_fst: Path,
        gcd_xorigin_design_db: Path, tmp_path: Path) -> None:
    bundle = tmp_path / "truncated"
    output = convert(repo_root, gcd_xorigin_design_db, bundle)
    output.write_bytes(output.read_bytes()[:175])
    response = loop_runner.request("session.open", target={
        "fsdb": str(gcd_xorigin_fst), "daidir": str(bundle)},
        args={"name": "test"})
    assert not response.get("ok")
    assert response["error"]["code"] == "SESSION_START_FAILED"


def corrupt_incompatible_major(data: bytearray) -> None:
    struct.pack_into("<I", data, 8, 2)


def corrupt_overlapping_name_index(data: bytearray) -> None:
    signal_offset = struct.unpack_from("<Q", data, 32)[0]
    struct.pack_into("<Q", data, 48, signal_offset)


def corrupt_signal_string_offset(data: bytearray) -> None:
    signal_offset = struct.unpack_from("<Q", data, 32)[0]
    string_count = struct.unpack_from("<Q", data, 168)[0]
    struct.pack_into("<I", data, signal_offset, string_count)


def corrupt_unterminated_signal_string(data: bytearray) -> None:
    signal_offset = struct.unpack_from("<Q", data, 32)[0]
    string_offset = struct.unpack_from("<Q", data, 160)[0]
    string_count = struct.unpack_from("<Q", data, 168)[0]
    struct.pack_into("<I", data, signal_offset, string_count - 1)
    data[string_offset + string_count - 1] = ord("X")


def corrupt_duplicate_name_index_target(data: bytearray) -> None:
    name_offset = struct.unpack_from("<Q", data, 48)[0]
    first_signal = struct.unpack_from("<i", data, name_offset + 4)[0]
    struct.pack_into("<i", data, name_offset + 12, first_signal)


def corrupt_driver_group_owner(data: bytearray) -> None:
    signal_count = struct.unpack_from("<Q", data, 40)[0]
    driver_offset = struct.unpack_from("<Q", data, 64)[0]
    target = struct.unpack_from("<i", data, driver_offset)[0]
    struct.pack_into("<i", data, driver_offset, (target + 1) % signal_count)


@pytest.mark.parametrize("corrupt", [
    corrupt_incompatible_major,
    corrupt_overlapping_name_index,
    corrupt_signal_string_offset,
    corrupt_unterminated_signal_string,
    corrupt_duplicate_name_index_target,
    corrupt_driver_group_owner,
], ids=[
    "major-version", "section-overlap", "string-offset", "unterminated-string",
    "duplicate-name-target", "driver-group-owner",
])
def test_corrupt_binary_fails_closed_during_session_start(
        loop_runner, repo_root: Path, gcd_xorigin_fst: Path,
        gcd_xorigin_design_db: Path, tmp_path: Path, corrupt) -> None:
    bundle = tmp_path / "corrupt"
    output = convert(repo_root, gcd_xorigin_design_db, bundle)
    data = bytearray(output.read_bytes())
    corrupt(data)
    output.write_bytes(data)
    response = loop_runner.request("session.open", target={
        "fsdb": str(gcd_xorigin_fst), "daidir": str(bundle)},
        args={"name": "test"})
    assert not response.get("ok")
    assert response["error"]["code"] == "SESSION_START_FAILED"
