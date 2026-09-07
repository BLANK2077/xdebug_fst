# test_p3d_axi_differential.py — P3-D3 AXI 六波形差分红灯与冻结证据
from __future__ import annotations

import copy
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from typing import Any

import pytest

from conftest import _base_env, open_session
from runner import StdioLoopRunner


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
ROW_FIELDS = {
    "transactions", "findings", "outliers", "change_points",
    "pending_transactions",
}
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
SVT_CURRENT_RUNS = (
    "stress", "fixed_delay", "random_seed_7", "random_seed_19",
    "random_seed_73",
)


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_oracle(fixture_id: str) -> dict:
    return json.loads(
        (DATA_ROOT / EXPECTED[fixture_id]["oracle"]).read_text(encoding="utf-8")
    )


def scrub_repo_paths(value: Any) -> Any:
    if isinstance(value, str):
        try:
            path = Path(value)
            if path.is_absolute() and (
                path == REPO_ROOT.resolve() or REPO_ROOT.resolve() in path.parents
            ):
                return "<repo-local-output>/" + path.name
        except (OSError, ValueError):
            pass
        return value
    if isinstance(value, list):
        return [scrub_repo_paths(item) for item in value]
    if isinstance(value, dict):
        return {key: scrub_repo_paths(item) for key, item in value.items()}
    return value


def canonical_sha256(value: Any) -> str:
    payload = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    selected = scrub_repo_paths(selected)
    data = selected.get("data")
    if isinstance(data, dict):
        for field in sorted(ROW_FIELDS & set(data)):
            rows = data[field]
            if isinstance(rows, list) and len(rows) > 64:
                selected["data"][field] = {
                    "canonical_sha256": canonical_sha256(rows),
                    "count": len(rows),
                    "anchors": [rows[0], rows[len(rows) // 2], rows[-1]],
                }
    return selected


def artifact_records(prefix: Path) -> list[dict[str, Any]]:
    records = []
    for path in sorted(prefix.parent.glob(prefix.name + ".*")):
        payload = path.read_bytes()
        if path.suffix == ".json":
            normalized = scrub_repo_paths(json.loads(payload.decode("utf-8")))
            payload = (
                json.dumps(normalized, ensure_ascii=False, indent=2, sort_keys=True)
                + "\n"
            ).encode("utf-8")
        records.append({
            "name": path.name,
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        })
    return records


def load_hash_lock(path: Path) -> dict[str, str]:
    rows = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        digest, name = line.split(maxsplit=1)
        rows[name] = digest
    return rows


def start_budget_runner(
    xfst_bin: Path, tmp_path: Path, *, soft_bytes: str, hard_bytes: str
) -> StdioLoopRunner:
    resolved = tmp_path.resolve()
    assert REPO_ROOT.resolve() in resolved.parents
    home, temp, cache = resolved / "home", resolved / "tmp", resolved / "cache"
    socket = REPO_ROOT / ".tmp/s" / hashlib.sha256(
        str(resolved).encode()
    ).hexdigest()[:8]
    for path in (home, temp, cache, socket):
        path.mkdir(parents=True, exist_ok=True)
    environment = _base_env(home, xfst_bin)
    environment.update({
        "TMPDIR": str(temp), "XDG_CACHE_HOME": str(cache),
        "XVERIF_TEST_TMPDIR": str(socket),
        "XDEBUG_ANALYSIS_CACHE_MAX_BYTES": soft_bytes,
        "XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES": hard_bytes,
    })
    runner = StdioLoopRunner(xfst_bin, cwd=REPO_ROOT, env=environment)
    runner.start()
    return runner


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


def test_current_xamba_axi_fixture_matches_locked_public_semantics(
    loop_runner: StdioLoopRunner,
    tmp_path: Path,
) -> None:
    oracle = load_oracle("xdebug.axi_xamba_vip")
    run = oracle["runs"][0]
    fixture = REPO_ROOT / "testdata/fixtures/axi_xamba_vip/waves.fst"
    export_prefix = tmp_path / "xamba-export" / "axi0"
    export_prefix.parent.mkdir(parents=True)

    open_session(loop_runner, fixture)
    try:
        for locked in run["observations"]:
            args = copy.deepcopy(locked["request"])
            if locked["observation_id"] == "export.full":
                args["output"]["path"] = str(export_prefix)
            actual = loop_runner.request(locked["action"], args=args)
            assert public_response(actual) == locked["response"], (
                "P3-D3 XAMBA AXI 公开响应差异: "
                f"{locked['observation_id']}"
            )
            if "xout" in locked:
                actual_xout = loop_runner.request_xout(
                    locked["action"], args=args
                )
                assert actual_xout == locked["xout"], (
                    "P3-D3 XAMBA AXI XOUT 差异: "
                    f"{locked['observation_id']}"
                )
            if "artifacts" in locked:
                assert artifact_records(export_prefix) == locked["artifacts"]
    finally:
        if loop_runner.has_current_session:
            closed = loop_runner.request("session.close", args={})
            assert closed.get("ok"), closed


def test_current_xamba_axi_fixture_locks_formula_tool_and_deterministic_fst() -> None:
    fixture = REPO_ROOT / "testdata/fixtures/axi_xamba_vip"
    lock = load_hash_lock(fixture / "fixture.sha256")
    assert list(lock) == [
        "xdebug_axi_xamba_fixture_top.sv", "tb_axi_xamba.cpp",
        "fixture.manifest.json", "waves.fst",
    ]
    for name, digest in lock.items():
        path = fixture / name
        assert path.is_file() and not path.is_symlink()
        assert file_sha256(path) == digest
    manifest = json.loads(
        (fixture / "fixture.manifest.json").read_text(encoding="utf-8")
    )
    dependency = json.loads(
        (REPO_ROOT / "dependencies.lock.json").read_text(encoding="utf-8")
    )["verilator"]
    assert manifest["goal_id"] == GOAL_ID
    assert manifest["source_contract"]["original_fixture_id"] == (
        "xdebug.axi_xamba_vip"
    )
    assert manifest["source_contract"]["transaction_count"] == 64
    assert manifest["source_contract"]["randomization"] is False
    assert manifest["source_contract"]["external_cache_rebuilt"] is False
    for key in ("version", "revision", "tree", "patchset_version"):
        assert manifest["build_contract"][key] == dependency[key]
    assert manifest["build_contract"]["rtl_sha256"] == lock[
        "xdebug_axi_xamba_fixture_top.sv"
    ]
    assert manifest["build_contract"]["harness_sha256"] == lock[
        "tb_axi_xamba.cpp"
    ]
    assert manifest["output_contract"] == {
        "path": "waves.fst", "size": (fixture / "waves.fst").stat().st_size,
        "sha256": lock["waves.fst"], "deterministic_build_directories": 2,
        "external_cache_rebuilt": False,
        "proprietary_artifact_committed": False,
    }
    assert "/home/" not in json.dumps(manifest, ensure_ascii=False)


def test_current_xamba_axi_matches_locked_soft_budget_public_observations(
    xfst_bin: Path, tmp_path: Path
) -> None:
    oracle = load_oracle("xdebug.axi_xamba_vip")
    runner = start_budget_runner(
        xfst_bin, tmp_path / "soft", soft_bytes="1", hard_bytes="2147483648"
    )
    try:
        open_session(runner, REPO_ROOT / "testdata/fixtures/axi_xamba_vip/waves.fst")
        for observation in oracle["cache_contract"]["soft_lru"][
            "public_observations"
        ]:
            actual = runner.request(
                observation["action"], args=copy.deepcopy(observation["request"])
            )
            assert public_response(actual) == observation["response"], (
                "P3-D3 XAMBA AXI soft-budget 公开差异: "
                f"{observation['observation_id']}"
            )
    finally:
        if runner.has_current_session:
            runner.request("session.close", args={})
        runner.stop()


def test_current_xamba_axi_exposes_locked_public_hard_limit_error(
    xfst_bin: Path, tmp_path: Path
) -> None:
    oracle = load_oracle("xdebug.axi_xamba_vip")
    observations = oracle["cache_contract"]["hard_limit"]["public_observations"]
    runner = start_budget_runner(
        xfst_bin, tmp_path / "hard", soft_bytes="1", hard_bytes="1"
    )
    try:
        open_session(runner, REPO_ROOT / "testdata/fixtures/axi_xamba_vip/waves.fst")
        for index, observation in enumerate(observations):
            actual = public_response(runner.request(
                observation["action"], args=copy.deepcopy(observation["request"])
            ))
            expected = copy.deepcopy(observation["response"])
            if index == len(observations) - 1:
                for response in (actual, expected):
                    key = response["error"]["key_summary"]
                    assert re.fullmatch(r"[0-9a-f]{16}", key)
                    response["error"]["key_summary"] = "<opaque-cache-key>"
            assert actual == expected
    finally:
        if runner.has_current_session:
            runner.request("session.close", args={})
        runner.stop()


@pytest.mark.parametrize("run_name", SVT_CURRENT_RUNS)
def test_current_svt_axi_profile_matches_locked_public_semantics(
    loop_runner: StdioLoopRunner,
    tmp_path: Path,
    run_name: str,
) -> None:
    oracle = load_oracle("xdebug.axi_vip")
    run = next(item for item in oracle["runs"] if item["name"] == run_name)
    fixture = REPO_ROOT / f"testdata/fixtures/axi_vip/{run_name}/waves.fst"
    export_prefix = tmp_path / run_name / "axi0"
    export_prefix.parent.mkdir(parents=True)

    open_session(loop_runner, fixture)
    try:
        for locked in run["observations"]:
            args = copy.deepcopy(locked["request"])
            if locked["observation_id"] == "export.full":
                args["output"]["path"] = str(export_prefix)
            actual = loop_runner.request(locked["action"], args=args)
            assert public_response(actual) == locked["response"], (
                f"P3-D3 SVT AXI {run_name} 公开响应差异: "
                f"{locked['observation_id']}"
            )
            if "xout" in locked:
                assert loop_runner.request_xout(
                    locked["action"], args=args
                ) == locked["xout"]
            if "artifacts" in locked:
                assert artifact_records(export_prefix) == locked["artifacts"]
    finally:
        if loop_runner.has_current_session:
            closed = loop_runner.request("session.close", args={})
            assert closed.get("ok"), closed


def test_current_svt_axi_profiles_lock_events_tool_and_fst(xfst_bin: Path) -> None:
    fixture = REPO_ROOT / "testdata/fixtures/axi_vip"
    lock = load_hash_lock(fixture / "fixture.sha256")
    expected_names = [
        "tb_axi_svt.cpp",
        "axi_vip_stress_top.sv",
        "tb_axi_svt_stress.cpp",
        "stress.events.tsv",
        "fixed_delay.events.tsv",
        "random_seed_7.events.tsv",
        "random_seed_19.events.tsv",
        "random_seed_73.events.tsv",
        "fixture.manifest.json",
        "stress/waves.fst",
        "fixed_delay/waves.fst",
        "random_seed_7/waves.fst",
        "random_seed_19/waves.fst",
        "random_seed_73/waves.fst",
    ]
    assert list(lock) == expected_names
    for name, digest in lock.items():
        path = fixture / name
        assert path.is_file() and not path.is_symlink()
        assert file_sha256(path) == digest

    manifest = json.loads(
        (fixture / "fixture.manifest.json").read_text(encoding="utf-8")
    )
    dependency = json.loads(
        (xfst_bin.parent / "dependencies.resolved.json").read_text(
            encoding="utf-8"
        )
    )["verilator"]
    assert manifest["goal_id"] == GOAL_ID
    assert manifest["source_contract"] == {
        "original_fixture_id": "xdebug.axi_vip",
        "producer_equivalence": "frozen-pin-level-handshake-mirror",
        "proprietary_vip_used": False,
        "fsdb_conversion_used": False,
        "export_feedback_used": False,
        "external_cache_rebuilt": False,
        "event_source": "repository-local-normalized-tsv",
        "first_write_payload_source": "locked-public-oracle",
    }
    for key in ("version", "revision", "tree", "fingerprint", "patchset_version"):
        assert manifest["build_contract"][key] == dependency[key]
    assert file_sha256(REPO_ROOT / "tools/generate_p3d_axi_svt_mirror.py") == (
        manifest["build_contract"]["generator_sha256"]
    )
    assert lock["tb_axi_svt.cpp"] == manifest["build_contract"][
        "harness_sha256"
    ]
    assert lock["axi_vip_stress_top.sv"] == manifest["build_contract"][
        "stress_rtl_sha256"
    ]
    assert lock["tb_axi_svt_stress.cpp"] == manifest["build_contract"][
        "stress_harness_sha256"
    ]
    assert manifest["build_contract"]["stress_public_flat_rw"] is True
    assert manifest["output_contract"] == {
        "deterministic_build_directories": 2,
        "repository_local_only": True,
        "external_cache_rebuilt": False,
        "proprietary_artifact_committed": False,
    }

    oracle = load_oracle("xdebug.axi_vip")
    oracle_runs = {run["name"]: run for run in oracle["runs"]}
    for run_name in SVT_CURRENT_RUNS:
        run = manifest["runs"][run_name]
        events = fixture / f"{run_name}.events.tsv"
        wave = fixture / run_name / "waves.fst"
        assert lock[f"{run_name}.events.tsv"] == run["events_sha256"]
        assert lock[f"{run_name}/waves.fst"] == run["fst_sha256"]
        assert wave.stat().st_size == run["fst_size"]
        assert run["handshake_count"] == oracle_runs[run_name][
            "handshake_line_count"
        ]
        assert run["transaction_count"] == 2 * oracle_runs[run_name][
            "expected_direction_count"
        ]
        event_lines = events.read_text(encoding="utf-8").splitlines()
        assert len(event_lines) == run["handshake_count"] + 1
        assert dict(Counter(
            line.split("\t",1)[0] for line in event_lines[1:]
        )) == oracle_runs[run_name]["handshake_channel_counts"]
    assert "/home/" not in json.dumps(manifest, ensure_ascii=False)
