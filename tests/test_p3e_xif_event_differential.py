from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any

from conftest import _base_env, open_session
from runner import StdioLoopRunner


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "testdata/fixtures/xif_event"
ORACLE = (
    ROOT / "tests/data/rtl_wave_differential/"
    "p3e-xif-event.public-oracle.json"
)
ORACLE_SHA256 = "251255661924fe455eccd79ecc863f62304d0052b9bd242dca474d65fd52f35f"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def observation_by_id(oracle: dict[str, Any], observation_id: str) -> dict[str, Any]:
    return next(
        row for row in oracle["observations"]
        if row["observation_id"] == observation_id
    )


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    result = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data"),
    }
    if response.get("ok") is False:
        result["error"] = response.get("error", {})
    return result


def literal_bits(value: str, width_hint: int | None = None) -> str:
    packed = re.fullmatch(r"'b\{([01xXzZ,]+)\}", value)
    if packed:
        return packed.group(1).replace(",", "").lower()
    match = re.fullmatch(r"(?:(\d+))?'([hHbB])([0-9a-fA-FxXzZ]+)", value)
    assert match, f"unsupported public logic literal: {value}"
    width = int(match.group(1)) if match.group(1) else width_hint
    radix = match.group(2).lower()
    digits = match.group(3).lower()
    if radix == "b":
        bits = digits
    else:
        expanded: list[str] = []
        for digit in digits:
            if digit in "xz":
                expanded.append(digit * 4)
            else:
                expanded.append(f"{int(digit, 16):04b}")
        bits = "".join(expanded)
    if width is not None:
        fill = bits[0] if bits and bits[0] in "xz" else "0"
        bits = (fill * max(width - len(bits), 0) + bits)[-width:]
    return bits


def assert_value_semantically_equal(expected: dict[str, Any],
                                    current: dict[str, Any]) -> None:
    assert current["known"] is expected["known"]
    for key in ("has_x", "has_z"):
        if key in expected:
            assert current[key] is expected[key]
    current_bits = current.get("bits")
    if current_bits is None:
        current_bits = literal_bits(current["value"], current.get("width"))
    assert literal_bits(expected["value"], len(current_bits)) == current_bits.lower()
    assert current.get("width") == len(current_bits)


def assert_events_semantically_equal(expected: list[dict[str, Any]],
                                     current: list[dict[str, Any]]) -> None:
    assert len(current) == len(expected)
    for expected_event, current_event in zip(expected, current):
        assert current_event["time"] == expected_event["time"]
        for section in ("signals", "fields"):
            assert set(current_event[section]) == set(expected_event[section])
            for name, expected_value in expected_event[section].items():
                assert_value_semantically_equal(
                    expected_value, current_event[section][name]
                )


def assert_summary_semantically_equal(expected: dict[str, Any],
                                      current: dict[str, Any]) -> None:
    backend_width_fields = {"value_width_complete", "width_diagnostics"}
    for key, value in expected.items():
        if key not in backend_width_fields and key != "output":
            assert current.get(key) == value, key
    assert current.get("value_width_complete") is True
    assert current.get("width_diagnostics") == []


def start_repo_local_runner(xfst_bin: Path, tmp_path: Path) -> StdioLoopRunner:
    resolved = tmp_path.resolve()
    assert ROOT.resolve() in resolved.parents
    home = resolved / "home"
    temp = resolved / "tmp"
    cache = resolved / "cache"
    socket_id = hashlib.sha256(str(resolved).encode()).hexdigest()[:8]
    socket = ROOT / ".tmp/s" / socket_id
    for path in (home, temp, cache, socket):
        path.mkdir(parents=True, exist_ok=True)
    environment = _base_env(home, xfst_bin)
    environment.update({
        "TMPDIR": str(temp),
        "XDG_CACHE_HOME": str(cache),
        "XVERIF_TEST_TMPDIR": str(socket),
    })
    runner = StdioLoopRunner(xfst_bin, cwd=ROOT, env=environment)
    runner.start()
    return runner


def test_xif_fixture_exists_and_is_locked() -> None:
    assert FIXTURE.is_dir(), "E2 red：当前仓库缺少独立 XIF event FST fixture"
    expected = {
        line.split(maxsplit=1)[1]: line.split(maxsplit=1)[0]
        for line in (FIXTURE / "fixture.sha256").read_text(encoding="utf-8").splitlines()
        if line.strip()
    }
    assert expected
    for relative, digest in expected.items():
        path = FIXTURE / relative
        assert path.is_file()
        assert sha256(path) == digest

    manifest = json.loads((FIXTURE / "fixture.manifest.json").read_text())
    assert manifest["fixture_id"] == "current.xif_event"
    source = manifest["source_contract"]
    assert source["producer_equivalence"] == "pin-level-semantic-mirror"
    assert source["proprietary_vip_used"] is False
    assert source["fsdb_conversion_used"] is False
    assert source["action_export_feedback_used"] is False
    assert source["randomization"] is False
    oracle = json.loads(ORACLE.read_text(encoding="utf-8"))
    original_rtl = {
        row["path"]: row["sha256"]
        for row in oracle["original_fixture"]["sources"]
        if row["path"].startswith("tb/")
    }
    assert source["original_rtl_sha256"] == original_rtl
    assert manifest["output_contract"] == {
        "path": "waves.fst",
        "sha256": "244b042da3a9e6adbdcfe046f224cafcd56f9c7cb20e88bd1c5f62a5476bf4ff",
        "size": 1128,
        "timescale": "1ps",
        "deterministic_build_directories": 2,
        "external_cache_rebuilt": False,
        "proprietary_artifact_committed": False,
    }


def test_xif_original_oracle_locks_all_e2_observations() -> None:
    oracle = json.loads(ORACLE.read_text(encoding="utf-8"))
    assert sha256(ORACLE) == ORACLE_SHA256
    assert oracle["schema_version"] == "xdebug.p3e-xif-event-public-oracle.v1"
    assert len(oracle["observations"]) == 32
    assert {row["observation_id"] for row in oracle["observations"]} >= {
        "artifact.rdy.full",
        "find.rdy.limit2",
        "find.unknown_alias",
        "full.bp",
        "full.none",
        "full.pair_master",
        "full.pair_slave",
        "full.rdy",
        "full.xz",
        "relational.rdy",
        "xz.unknown",
    }
    assert oracle["original_fixture"]["cache_reused"] is True
    assert oracle["original_fixture"]["fixture_rebuilt"] is False
    assert oracle["session"]["fallback_used"] is False


def test_xif_all_32_original_observations_replay_on_raw_fst(
    xfst_bin: Path,
    tmp_path: Path,
) -> None:
    assert ROOT.resolve() in tmp_path.resolve().parents
    oracle = json.loads(ORACLE.read_text(encoding="utf-8"))
    runner = start_repo_local_runner(xfst_bin, tmp_path)
    try:
        open_session(runner, FIXTURE / "waves.fst")
        current_by_id: dict[str, dict[str, Any]] = {}

        for row in oracle["observations"]:
            args = json.loads(json.dumps(row["request"]))
            if row["action"] == "event.config.load":
                name = row["observation_id"].removeprefix("config.")
                args["config_path"] = str(FIXTURE / f"event_{name}.json")
            artifact_path: Path | None = None
            if row["observation_id"] == "artifact.rdy.full":
                artifact_path = tmp_path / "rdy-full.json"
                args["output"]["path"] = str(artifact_path)

            response = public_response(runner.request(row["action"], args=args))
            current_by_id[row["observation_id"]] = response
            expected = row["response"]
            assert response["ok"] is expected["ok"], row["observation_id"]

            if row["action"] == "event.config.load":
                assert response["summary"] == expected["summary"]
                assert response["data"] == expected["data"]
            elif row["action"] == "value.at":
                assert response == expected
            elif response["ok"] is False:
                assert response == expected
            else:
                assert_summary_semantically_equal(
                    expected["summary"], response["summary"]
                )
                assert response["data"]["sampling"] == expected["data"]["sampling"]
                if "events" in expected["data"]:
                    assert_events_semantically_equal(
                        expected["data"]["events"], response["data"]["events"]
                    )

            if artifact_path is not None:
                artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
                expected_artifact = row["artifact"]
                assert set(artifact) == {"events", "sampling", "summary"}
                assert_events_semantically_equal(
                    expected_artifact["events"], artifact["events"]
                )
                assert artifact["sampling"] == expected_artifact["sampling"]
                assert_summary_semantically_equal(
                    expected_artifact["summary"], artifact["summary"]
                )
    finally:
        runner.stop()

    assert len(current_by_id) == 32
    assert current_by_id["full.rdy"]["summary"]["total_count"] == 5
    assert current_by_id["full.bp"]["summary"]["total_count"] == 2
    assert current_by_id["full.none"]["summary"]["total_count"] == 3
    assert current_by_id["full.pair_master"]["summary"]["total_count"] == 4
    assert current_by_id["full.pair_slave"]["summary"]["total_count"] == 4
    assert current_by_id["full.xz"]["summary"]["total_count"] == 1
    assert current_by_id["xz.unknown"]["summary"]["total_count"] == 0


def test_xif_event_find_xout_preserves_complete_public_evidence(
    xfst_bin: Path,
    tmp_path: Path,
) -> None:
    runner = start_repo_local_runner(xfst_bin, tmp_path)
    try:
        open_session(runner, FIXTURE / "waves.fst")
        loaded = runner.request("event.config.load", args={
            "name": "rdy", "config_path": str(FIXTURE / "event_rdy.json")
        })
        assert loaded["ok"], loaded
        xout = runner.request_xout("event.find", args={
            "name": "rdy", "expr": "vld && rdy", "mode": "all", "line_limit": 2
        })
    finally:
        runner.stop()
    assert xout.startswith("@xdebug.event.find.v1\n")
    assert "sample_count         : 20" in xout
    assert "total_count          : 5" in xout
    assert "returned_count       : 2" in xout
    assert "response_events" in xout
    assert "85ns" in xout and "95ns" in xout and "8'h5a" in xout
    assert re.search(r"\bend\s*: max\b", xout)


def test_xif_field_shorthand_rejects_partial_integer_bounds(
    xfst_bin: Path,
    tmp_path: Path,
) -> None:
    config = json.loads((FIXTURE / "event_xz.json").read_text(encoding="utf-8"))
    config["fields"]["data"] = "raw[15junk:0]"
    config_path = tmp_path / "invalid-field.json"
    config_path.write_text(json.dumps(config), encoding="utf-8")
    runner = start_repo_local_runner(xfst_bin, tmp_path)
    try:
        open_session(runner, FIXTURE / "waves.fst")
        response = runner.request("event.config.load", args={
            "name": "invalid", "config_path": str(config_path)
        })
    finally:
        runner.stop()
    assert response["ok"] is False
    assert response["error"]["code"] == "CONFIG_INVALID"
    assert response["error"]["message"] == (
        "event field shorthand has invalid bounds: data"
    )
