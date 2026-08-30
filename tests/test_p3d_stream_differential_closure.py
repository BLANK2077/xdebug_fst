# test_p3d_stream_differential_closure.py — P3-D1 differential/cache 闭环门禁
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import re
from typing import Any

from conftest import _base_env, open_session
from runner import StdioLoopRunner


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "testdata/fixtures/stream_v1"
AUDIT_PATH = (
    REPO_ROOT
    / "tests/data/rtl_wave_differential/"
    "p3d-stream-differential-closure.audit.json"
)
AUDIT_SHA256 = (
    "c606046efa26998c5be060f8e33658e0c56dcb475fa440c3a7c92a3d17770607"
)
AUDIT = json.loads(AUDIT_PATH.read_text(encoding="utf-8"))
RANGE_A = {"begin": "0ns", "end": "5us"}
RANGE_B = {"begin": "5us", "end": "10us"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    return selected


def normalized_batch(response: dict[str, Any]) -> dict[str, Any]:
    return {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "results": [
            public_response(child)
            for child in response.get("data", {}).get("results", [])
        ],
        "error": response.get("error"),
    }


def normalize_hard_key(document: dict[str, Any]) -> dict[str, Any]:
    normalized = copy.deepcopy(document)
    for result in normalized["results"]:
        error = result.get("error")
        assert isinstance(error, dict), (
            "hard-limit 子请求必须返回公开 error，而不是成功结果"
        )
        key = error.get("key_summary")
        assert isinstance(key, str) and re.fullmatch(r"[0-9a-f]{16}", key)
        error["key_summary"] = "<opaque-cache-key>"
    return normalized


def load_ready_packet(runner: StdioLoopRunner) -> None:
    loaded = runner.request(
        "stream.config.load",
        args={
            "config": {
                "streams": [AUDIT["cache_contract"]["ready_packet_config"]]
            },
            "mode": "replace",
        },
    )
    assert loaded.get("ok"), loaded


def start_budget_runner(
    xfst_bin: Path,
    tmp_path: Path,
    *,
    soft_bytes: str,
    hard_bytes: str,
) -> StdioLoopRunner:
    resolved_tmp = tmp_path.resolve()
    assert REPO_ROOT.resolve() in resolved_tmp.parents
    home = resolved_tmp / "home"
    temp = resolved_tmp / "tmp"
    cache = resolved_tmp / "cache"
    socket_id = hashlib.sha256(str(resolved_tmp).encode()).hexdigest()[:8]
    socket = REPO_ROOT / ".tmp/s" / socket_id
    for path in (home, temp, cache, socket):
        path.mkdir(parents=True, exist_ok=True)
    environment = _base_env(home, xfst_bin)
    environment.update({
        "TMPDIR": str(temp),
        "XDG_CACHE_HOME": str(cache),
        "XVERIF_TEST_TMPDIR": str(socket),
        "XDEBUG_ANALYSIS_CACHE_MAX_BYTES": soft_bytes,
        "XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES": hard_bytes,
    })
    runner = StdioLoopRunner(xfst_bin, cwd=REPO_ROOT, env=environment)
    runner.start()
    return runner


def child_request(
    target_name: str, action: str, args: dict[str, Any]
) -> dict[str, Any]:
    return {
        "api_version": "xdebug.v1",
        "action": action,
        "target": {"session_id": target_name},
        "args": args,
    }


def batch_requests(target_name: str) -> list[dict[str, Any]]:
    return [
        child_request(target_name, "stream.query", {
            "stream": "ready_packet", "query": "summary",
            "cache_scope": "range", "time_range": RANGE_A,
        }),
        child_request(target_name, "stream.export", {
            "stream": "ready_packet", "kind": "packet",
            "cache_scope": "range", "time_range": RANGE_A,
            "line_limit": 2,
        }),
        child_request(target_name, "stream.validate", {
            "stream": "ready_packet", "dynamic": True,
            "cache_scope": "range", "time_range": RANGE_A,
        }),
        child_request(target_name, "stream.query", {
            "stream": "ready_packet", "query": "summary",
            "cache_scope": "range", "time_range": RANGE_B,
        }),
        child_request(target_name, "stream.query", {
            "stream": "ready_packet", "query": "summary",
            "cache_scope": "full", "time_range": RANGE_A,
        }),
        child_request(target_name, "stream.query", {
            "stream": "ready_packet", "query": "summary",
            "cache_scope": "range", "time_range": RANGE_B,
        }),
    ]


def test_differential_audit_locks_bounded_original_contract() -> None:
    assert sha256(AUDIT_PATH) == AUDIT_SHA256
    assert AUDIT["schema_version"] == \
        "xdebug.p3d-stream-differential-closure-audit.v1"
    assert AUDIT["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert AUDIT["fixture_id"] == "xdebug.stream_differential_tool"
    assert AUDIT["classification"] == "proven-unobservable"
    assert AUDIT["fixture_contract"] == {
        "source_dir": "xdebug",
        "builder_argv": [
            "make", "stream-differential-test-dist",
            "STREAM_DIFFERENTIAL_DIST={resources}/out",
        ],
        "outputs": [
            {"name": "frontend", "path": "out/xdebug", "kind": "file", "min_bytes": 1024},
            {"name": "engine", "path": "out/libexec/xdebug-engine", "kind": "file", "min_bytes": 1024},
        ],
        "rtl_input_count": 0,
        "waveform_output_count": 0,
        "reused_waveform_fixture_id": "xdebug.stream_v1",
    }
    runtime = AUDIT["locked_runtime"]
    assert runtime["build_id"] == (
        "846edd6800bd-"
        "6ace27b232adefe5a896cb4222f9ea539e6127e600baf1761e560ec103872574"
    )
    assert runtime["action_count"] == 73
    assert runtime["frontend_sha256"] == \
        "9a5467c8d1d15c20b30c5baf1065a1d79d902cf53784ed256fc7ef93009f0bba"
    assert runtime["engine_sha256"] == \
        "ac584ab512aac9122d2ebf656b3920da571edd224b7dc5692f25a6bd6e5683a0"
    assert runtime["legacy_object_sha256"] == \
        "d8e3621cc1e13e929ac1b341634c1bb3284f34e62ddd9c470d96184be138ca14"
    assert runtime["cache_reused"] is True
    assert runtime["fixture_rebuilt"] is False
    assert runtime["source_access"] == "read_only"

    comparator = AUDIT["comparator"]
    assert comparator["compile_guard"] == \
        "XDEBUG_STREAM_DIFFERENTIAL_TEST_BUILD"
    assert comparator["public_action"] is False
    assert comparator["interposed_actions"] == [
        "stream.query", "stream.export", "stream.validate"
    ]
    assert comparator["engine_failure_sentinels"] == [
        "legacy stream differential oracle failed: ",
        "stream columnar differential mismatch for ",
    ]
    replay = comparator["public_replay"]
    assert replay == {
        "query_config_observation_count": 58,
        "query_config_difference_count": 0,
        "query_config_oracle_sha256": (
            "8c40c3ecde23070d5879a0ed6e9160005ae6f6482f7b17ca9823a8d4f825c455"
        ),
        "export_observation_count": 6,
        "export_difference_count": 0,
        "export_oracle_sha256": (
            "66a9c58192c4f6ff95edc70ff67537425ddc1aaaa52ce32d667ec25b113b8c29"
        ),
        "artifact_check_count": 3,
        "xout_check_count": 3,
        "comparator_failure_count": 0,
    }
    assert comparator["remaining_public_difference_count"] == 0

    boundary = AUDIT["public_boundary"]
    assert boundary["private_action_count"] == 0
    assert boundary["private_probe_fields_in_public_schema"] == []
    assert boundary["public_cache_error_code"] == \
        "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
    assert [item["schema_id"] for item in boundary["request_schemas"]] == [
        "xdebug.stream.query.request.v1",
        "xdebug.stream.export.request.v1",
        "xdebug.stream.validate.request.v1",
    ]
    assert len(AUDIT["source_files"]) == 15
    assert all(
        re.fullmatch(r"[0-9a-f]{64}", item["sha256"])
        and not item["path"].startswith("/")
        for item in AUDIT["source_files"]
    )
    assert AUDIT["session"] == {
        "mode": "waveform",
        "transport": "uds",
        "all_runtime_writes_repository_local": True,
        "source_access": "read_only",
        "fixture_rebuilt": False,
        "fallback_used": False,
    }
    assert AUDIT["closure"] == {
        "stream_v1_classification_candidate": "semantic-equivalent",
        "differential_tool_classification_candidate": "proven-unobservable",
        "private_cache_metrics_classification": "proven-unobservable",
        "public_hard_limit_requires_current_gate": True,
        "remaining_unmapped_public_observation_count": 0,
    }
    assert "/home/" not in json.dumps(AUDIT, ensure_ascii=False)


def test_current_matches_all_public_base_cache_observations(
    loop_runner: StdioLoopRunner,
) -> None:
    open_session(loop_runner, FIXTURE / "waves.fst")
    try:
        load_ready_packet(loop_runner)
        observations = AUDIT["cache_contract"]["public_observations"]
        assert len(observations) == 13
        for observation in observations:
            actual = loop_runner.request(
                observation["action"],
                args=copy.deepcopy(observation["request"]),
            )
            assert public_response(actual) == observation["response"], (
                f"cache 公开观察差异: {observation['observation_id']}"
            )
    finally:
        if loop_runner.has_current_session:
            closed = loop_runner.request("session.close", args={})
            assert closed.get("ok"), closed


def test_current_matches_public_batch_and_soft_budget_results(
    loop_runner: StdioLoopRunner,
    xfst_bin: Path,
    tmp_path: Path,
) -> None:
    open_session(loop_runner, FIXTURE / "waves.fst")
    try:
        load_ready_packet(loop_runner)
        actual = loop_runner.request(
            "batch",
            args={
                "mode": "continue_on_error",
                "requests": batch_requests("test"),
            },
        )
        assert normalized_batch(actual) == AUDIT["cache_contract"]["batch"][
            "public"
        ]
    finally:
        if loop_runner.has_current_session:
            loop_runner.request("session.close", args={})

    soft = start_budget_runner(
        xfst_bin, tmp_path / "soft",
        soft_bytes="1", hard_bytes="2147483648",
    )
    try:
        open_session(soft, FIXTURE / "waves.fst")
        load_ready_packet(soft)
        for observation in AUDIT["cache_contract"]["soft_lru"][
            "public_observations"
        ]:
            actual = soft.request(
                observation["action"], args=observation["request"]
            )
            assert public_response(actual) == observation["response"]
    finally:
        if soft.has_current_session:
            soft.request("session.close", args={})
        soft.stop()


def test_current_exposes_locked_public_hard_limit_error(
    xfst_bin: Path,
    tmp_path: Path,
) -> None:
    hard_contract = AUDIT["cache_contract"]["hard_limit"]
    assert hard_contract["classification"] == "publicly-observable"
    assert hard_contract["allowed_current_projection"] == {
        "error.key_summary": (
            "opaque 16-hex cache identity may differ because the current fixture "
            "is native FST rather than the original FSDB"
        )
    }
    runner = start_budget_runner(
        xfst_bin, tmp_path / "hard", soft_bytes="1", hard_bytes="1"
    )
    session_name = "p3d_current_hard"
    try:
        opened = runner.request(
            "session.open",
            target={"fsdb": str(FIXTURE / "waves.fst")},
            args={"name": session_name},
        )
        assert opened.get("ok"), opened
        load_ready_packet(runner)
        hard_child = child_request(session_name, "stream.query", {
            "stream": "ready_packet", "query": "summary",
            "cache_scope": "full", "time_range": RANGE_A,
        })
        actual = runner.request(
            "batch",
            args={
                "mode": "continue_on_error",
                "requests": [hard_child, copy.deepcopy(hard_child)],
            },
        )
        assert normalize_hard_key(normalized_batch(actual)) == \
            normalize_hard_key(hard_contract["public"])
    finally:
        if runner.has_current_session:
            runner.request("session.close", args={})
        runner.stop()
