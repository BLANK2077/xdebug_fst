from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

import pytest

from conftest import open_session
from runner import StdioLoopRunner
from tools.collect_p3c_original_oracles import inside
from tools.compare_public_action_responses import canonical_sv_literal, canonical_time


ROOT = Path(__file__).resolve().parents[1]
ACTIVE_ROOT = ROOT / "testdata/fixtures/active_trace"
ORACLE_PATH = (
    ROOT / "tests/data/rtl_wave_differential/"
    "p3c-timing.original-oracle.json"
)
ORACLE = json.loads(ORACLE_PATH.read_text(encoding="utf-8"))
ROWS = ORACLE["rows"]
CHAIN_SCHEMA = json.loads((
    ROOT / "compat/xdebug-v1/schemas/v1/actions/"
    "trace.active_driver_chain.response.schema.json"
).read_text(encoding="utf-8"))
CHAIN_REQUEST_SCHEMA = json.loads((
    ROOT / "compat/xdebug-v1/schemas/v1/actions/"
    "trace.active_driver_chain.request.schema.json"
).read_text(encoding="utf-8"))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def all_strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from all_strings(item)
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from all_strings(key)
            yield from all_strings(item)


def time_fs(value: str) -> str:
    if value.strip("0.") == "":
        return "0"
    canonical = canonical_time(value)
    assert canonical is not None, value
    return canonical["$xdebug_time_fs"]


def logic_bits(value: str) -> str:
    canonical = canonical_sv_literal(value)
    if canonical is not None:
        return canonical["$xdebug_logic"]["bits"]
    assert value and set(value.lower()) <= set("01xz?"), value
    return value.upper().replace("?", "Z")


def fixture_dir(row: dict[str, Any]) -> Path:
    return ACTIVE_ROOT / "timing" / row["case"]


def value_at_bits(loop_runner: StdioLoopRunner, signal: str,
                  time: str) -> str:
    response = loop_runner.request(
        "value.at", args={"signal": signal, "time": time}
    )
    assert response.get("ok"), response
    values = response["data"]["samples"][0]["values"]
    assert len(values) == 1 and values[0]["status"] == "ok", values
    return values[0]["value"]["bits"]


def test_p3c_timing_oracle_is_locked_complete_and_sanitized() -> None:
    assert ORACLE["schema_version"] == \
        "xdebug.p3c-original-active-trace-oracle.v1"
    assert ORACLE["goal_id"] == "01a050fa-b864-7ce2-af88-56083d84ea21"
    assert ORACLE["group"] == "timing"
    assert ORACLE["catalog"] == {
        "row_count": 12,
        "schema_version": "xdebug-active-trace-cases.v1",
        "sha256": (
            "f4e61f065e1c1c4412f3c823c336d8962da559274e8c7f71387919d38e6bfb3c"
        ),
    }
    assert ORACLE["locked_runtime"] == {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": (
            "1042c712bf8a59877d837e55ffd4e986a62eefc16aba01eaec2d6e459eca5fb5-"
            "prepare-d9oyxhx2"
        ),
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": (
            "f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237"
        ),
        "source_access": "read_only",
    }
    assert ORACLE["session"] == {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }
    assert [row["scenario_id"] for row in ROWS] == [
        f"active.timing.{index:02d}" for index in range(1, 13)
    ]
    assert not [
        value for value in all_strings(ORACLE)
        if value.startswith("/") or "/home/" in value
    ]
    assert all(
        row["request"]["stop_on_temporal"] is True
        and row["catalog_expectation"] == {}
        and row["native_result"]["active_trace_calls"] == 1
        and row["native_result"]["total_hops"] == 1
        and row["native_result"]["temporal_boundaries"] == 1
        and row["native_result"]["temporal_boundary_stops"] == 1
        and row["native_result"]["termination"] == "temporal_boundary"
        and row["native_result"]["truncated"] is False
        and row["native_result"]["limitations"] == []
        and row["native_result"]["branch_evidence"] == []
        for row in ROWS
    )
    assert all(
        len(row["native_result"]["chain"]) == 1
        and row["native_result"]["chain"][0]["driver_kind"] == "cont_assign"
        and row["native_result"]["chain"][0]["hop_type"] ==
        "temporal_boundary"
        and row["native_result"]["chain"][0]["line"] == 44
        and row["native_result"]["chain"][0]["value_known"] is True
        for row in ROWS
    )

    # 原版 runner 的 stop_on_temporal 是私有采集开关，不属于冻结的公开
    # action v1 请求。这里锁住不可表达边界：当前实现比较原版“停止前缀”的
    # 第一个 hop，同时仍要求公开接口把余下链路完整分析，不能伪造同名参数。
    args_schema = CHAIN_REQUEST_SCHEMA["properties"]["args"]
    assert args_schema["additionalProperties"] is False
    assert "stop_on_temporal" not in args_schema["properties"]
    assert CHAIN_SCHEMA["$defs"]["nonSamplingTraceHop"]["properties"][
        "file"
    ]["minLength"] == 1
    assert inside(ORACLE_PATH, ROOT, "oracle") == ORACLE_PATH
    with pytest.raises(RuntimeError, match="必须位于当前仓库"):
        inside(ROOT.parent / "timing-oracle.json", ROOT, "oracle")


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_timing_exact_rtl_and_generated_fixture_hashes(
        row: dict[str, Any]) -> None:
    mirrors = row["rtl_mirrors"]
    assert len(mirrors) == 2
    for mirror in mirrors:
        source = ROOT / mirror["current_path"]
        assert mirror["byte_identical"] is True
        assert source.stat().st_size == mirror["size"]
        assert digest(source) == mirror["sha256"]

    fixture = fixture_dir(row)
    recorded = {}
    for line in (fixture / "fixture.sha256").read_text(
            encoding="utf-8").splitlines():
        checksum, path = line.split(maxsplit=1)
        recorded[path] = checksum
    assert len(recorded) == 6
    assert all(digest(ROOT / path) == checksum
               for path, checksum in recorded.items())
    assert set(recorded) == {
        *(mirror["current_path"] for mirror in mirrors),
        "testdata/fixtures/active_trace/dump_probe.sv",
        f"testdata/fixtures/active_trace/timing/{row['case']}/waves.fst",
        (
            "testdata/fixtures/active_trace/timing/"
            f"{row['case']}/design_db/Vactive_trace__DesignDb.xddb"
        ),
        (
            "testdata/fixtures/active_trace/timing/"
            f"{row['case']}/design_db/xdebug-design-db.json"
        ),
    }
    design = fixture / "design_db"
    manifest = json.loads(
        (design / "xdebug-design-db.json").read_text(encoding="utf-8")
    )
    assert manifest == {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": "Vactive_trace__DesignDb.xddb",
    }
    assert {path.name for path in design.iterdir()} == {
        "Vactive_trace__DesignDb.xddb", "xdebug-design-db.json",
    }


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["scenario_id"])
def test_p3c_timing_matches_locked_native_temporal_prefix_semantics(
        loop_runner: StdioLoopRunner, row: dict[str, Any]) -> None:
    fixture = fixture_dir(row)
    opened = open_session(
        loop_runner, fixture / "waves.fst", fixture / "design_db"
    )
    assert opened["session"]["mode"] == "combined"
    request = row["request"]
    response = loop_runner.request("trace.active_driver_chain", args={
        "signal": request["signal"], "time": request["time"],
    }, limits={"max_depth": 64, "max_nodes": 64})
    assert response.get("ok"), response
    summary = response["summary"]
    hops = response["data"]["hops"]
    native_hop = row["native_result"]["chain"][0]

    # stop_on_temporal 无公开表示，因此只把 native 停止点映射到公开完整链的
    # 前缀；完整性、计数与非截断仍由当前公开响应独立承担。
    assert summary["scan_complete"] is True
    assert summary["analysis_complete"] is True
    assert summary["response_truncated"] is False
    assert summary["truncation_scopes"] == []
    assert summary["total_count"] == summary["returned_count"] == len(hops)
    assert hops
    current_hop = hops[0]
    assert current_hop["relation"] == "root"
    assert current_hop["signal"] == native_hop["signal"]
    assert current_hop["line"] == native_hop["line"] == 44
    assert current_hop["file"].endswith(
        "testdata/fixtures/active_trace/rtl/timing/timing_boundary_dut.sv"
    )
    assert any(
        context["active"] and context["line"] == 44
        for context in current_hop["source_context"]
    )
    assert time_fs(current_hop["time"]) == time_fs(request["time"])
    assert time_fs(current_hop["active_time"]) == time_fs(
        native_hop["active_time"]
    )
    current_bits = logic_bits(current_hop["value"])
    if native_hop["value"]:
        assert current_bits == logic_bits(native_hop["value"])
    else:
        # case_10 的原版 NPI 返回 known=true 但字符串为空；公开 schema 只允许
        # 字符串值，不把空串臆测成某个原版常量；但投影后的值必须与当前 FST
        # 在原版 active_time 上的已知采样一致，不能只改时间标签。
        assert native_hop["value_known"] is True
        assert current_bits == logic_bits(value_at_bits(
            loop_runner, request["signal"], native_hop["active_time"]
        ))


def test_p3c_timing_limits_are_explicit_analysis_boundaries(
        loop_runner: StdioLoopRunner) -> None:
    row = ROWS[0]
    fixture = fixture_dir(row)
    open_session(loop_runner, fixture / "waves.fst", fixture / "design_db")
    for limits, detail in (({"max_depth": 1}, "max_depth"),
                           ({"max_nodes": 1}, "max_nodes")):
        response = loop_runner.request(
            "trace.active_driver_chain",
            args={"signal": row["request"]["signal"],
                  "time": row["request"]["time"]},
            limits=limits,
        )
        assert response.get("ok"), response
        assert response["summary"]["termination"] == "limit"
        assert response["summary"]["termination_detail"] == detail
        assert response["summary"]["scan_complete"] is False
        assert response["summary"]["analysis_complete"] is False
        assert response["summary"]["response_truncated"] is False
        assert response["summary"]["truncation_scopes"] == ["analysis_trace"]
