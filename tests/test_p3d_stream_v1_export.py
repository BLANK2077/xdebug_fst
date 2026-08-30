# test_p3d_stream_v1_export.py — P3-D1 stream.export / XOUT 双侧门禁
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
from typing import Any

from conftest import open_session
from runner import StdioLoopRunner
from tools.audit_xout_semantics import audit_event


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "testdata/fixtures/stream_v1"
ORACLE_PATH = (
    REPO_ROOT
    / "tests/data/rtl_wave_differential/p3d-stream-v1.export-oracle.json"
)
ORACLE = json.loads(ORACLE_PATH.read_text(encoding="utf-8"))
RUNTIME_REVISION = "8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8"
SCHEMA_REVISION = (
    "c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c"
)


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def public_response(response: dict[str, Any]) -> dict[str, Any]:
    selected: dict[str, Any] = {
        "ok": response.get("ok"),
        "summary": response.get("summary", {}),
        "data": response.get("data", {}),
    }
    if response.get("ok") is False:
        selected["error"] = response.get("error", {})
    return selected


def normalize_output_paths(
    response: dict[str, Any], artifact_name: str
) -> dict[str, Any]:
    selected = public_response(response)
    output = selected["summary"]["output"]
    output["path"] = f"artifact/{artifact_name}"
    output["meta_path"] = f"artifact/{artifact_name}.meta.json"
    return selected


def test_stream_v1_export_oracle_locks_runtime_schema_and_xout() -> None:
    assert ORACLE["schema_version"] == "xdebug.p3d-stream-v1-export-oracle.v1"
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
    assert ORACLE["action_discovery"] == {
        "guide_request_supported": False,
        "guide_error_code": "INVALID_REQUEST",
        "guide_invalid_arg": "args.output.view",
        "catalog_action_count": 73,
        "request_schema_id": "xdebug.stream.export.request.v1",
        "request_schema_path": "schemas/v1/actions/stream.export.request.schema.json",
    }
    assert ORACLE["session"] == {
        "mode": "waveform",
        "transport": "uds",
        "opened": True,
        "closed_gracefully": True,
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
    }
    assert ORACLE["observation_count"] == 6
    assert [row["observation_id"] for row in ORACLE["observations"]] == [
        "transfer_preview",
        "packet_preview",
        "packet_beats_preview",
        "transfer_written",
        "packet_written",
        "packet_beats_written",
    ]
    expected_original_xout_gaps = {
        "transfer_preview": [
            "missing summary field requested_range.begin",
            "missing summary field requested_range.end",
            "missing summary field scanned_range.end",
        ],
        "packet_preview": [
            "missing logic value data.preview[0].beat_fields_preview.head[1].fields.data=32'h40000001",
            "missing logic value data.preview[0].beat_fields_preview.head[1].fields.seq=16'h1",
            "missing logic value data.preview[0].beat_fields_preview.head[2].fields.data=32'h40000002",
            "missing logic value data.preview[0].beat_fields_preview.head[2].fields.seq=16'h2",
            "missing summary field requested_range.begin",
            "missing summary field requested_range.end",
            "missing summary field scanned_range.end",
        ],
        "packet_beats_preview": [
            "missing summary field requested_range.begin",
            "missing summary field requested_range.end",
            "missing summary field scanned_range.end",
        ],
    }
    for row in ORACLE["observations"][:3]:
        xout = row["xout"]
        assert sha256_bytes(xout.encode()) == row["xout_sha256"]
        failures, _ = audit_event(
            "stream.export",
            {"response": row["response"], "xout": xout},
        )
        assert failures == expected_original_xout_gaps[row["observation_id"]]
    assert "/home/" not in json.dumps(ORACLE, ensure_ascii=False)


def test_stream_v1_current_export_matches_locked_json_artifacts_and_xout(
    loop_runner: StdioLoopRunner,
    tmp_path: Path,
) -> None:
    resolved_tmp = tmp_path.resolve()
    assert REPO_ROOT.resolve() in resolved_tmp.parents, (
        "P3-D1 stream.export 临时产物必须位于当前仓库"
    )
    config = FIXTURE / "streams.json"
    open_session(loop_runner, FIXTURE / "waves.fst")
    try:
        loaded = loop_runner.request(
            "stream.config.load",
            args={"config_path": str(config), "mode": "replace"},
        )
        assert loaded.get("ok"), loaded
        for observation in ORACLE["observations"]:
            request_args = copy.deepcopy(observation["request"])
            artifact = observation.get("artifact")
            artifact_path: Path | None = None
            if artifact is not None:
                artifact_name = Path(artifact["path"]).name
                artifact_path = resolved_tmp / artifact_name
                request_args["output"]["path"] = str(artifact_path)
            actual = loop_runner.request("stream.export", args=request_args)
            if artifact is None:
                assert public_response(actual) == observation["response"], (
                    f"stream.export 公开响应差异: {observation['observation_id']}"
                )
                xout = loop_runner.request_xout(
                    "stream.export", args=request_args
                )
                failures, _ = audit_event(
                    "stream.export", {"response": actual, "xout": xout}
                )
                assert failures == [], (
                    f"stream.export XOUT 语义缺失: "
                    f"{observation['observation_id']}: {failures}"
                )
                continue

            assert artifact_path is not None
            artifact_name = artifact_path.name
            assert normalize_output_paths(actual, artifact_name) == observation[
                "response"
            ], f"stream.export 写出响应差异: {observation['observation_id']}"
            payload = artifact_path.read_bytes()
            meta_path = Path(str(artifact_path) + ".meta.json")
            meta_payload = meta_path.read_bytes()
            assert len(payload) == artifact["size"]
            assert sha256_bytes(payload) == artifact["sha256"]
            assert payload.decode() == artifact["text"]
            assert len(meta_payload) == artifact["meta_size"]
            assert sha256_bytes(meta_payload) == artifact["meta_sha256"]
            assert json.loads(meta_payload) == artifact["meta"]
    finally:
        if loop_runner.has_current_session:
            closed = loop_runner.request("session.close", args={})
            assert closed.get("ok"), closed
