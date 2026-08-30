# test_ai_complex_wave.py — locked original ai_complex public semantics
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from conftest import open_session
from runner import StdioLoopRunner


def _digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _load_event_config(
        loop_runner: StdioLoopRunner, tmp_path: Path) -> dict:
    config_path = tmp_path / "ai_complex_event.json"
    config_path.write_text(json.dumps({
        "clock": "ai_complex_top.clk",
        "reset": {
            "signal": "ai_complex_top.rst_n",
            "polarity": "active_low",
        },
        "edge": "posedge",
        "signals": {
            "vld": "ai_complex_top.event_vld",
            "rdy": "ai_complex_top.event_rdy",
            "payload": "ai_complex_top.event_payload",
            "xz": "ai_complex_top.xz_bus",
        },
        "fields": {
            "payload_lo": {
                "signal": "payload", "left": 3, "right": 0,
            },
        },
    }, sort_keys=True), encoding="utf-8")
    return loop_runner.request("event.config.load", args={
        "name": "ai_event", "config_path": str(config_path),
    })


def test_ai_complex_fixture_is_frozen_and_four_state() -> None:
    fixture = Path(__file__).resolve().parents[1] / \
        "testdata/fixtures/ai_complex"
    recorded = {}
    for line in (fixture / "fixture.sha256").read_text(
            encoding="utf-8").splitlines():
        digest, name = line.split(maxsplit=1)
        recorded[name] = digest
    assert set(recorded) == {
        "ai_complex_top.sv",
        "fstcpp-four-state-vector.patch",
        "generate_ai_complex_fst.cpp",
        "waves.fst",
    }
    assert all(_digest(fixture / name) == digest
               for name, digest in recorded.items())
    assert recorded["waves.fst"] == \
        "71218e2a72fb43567cc4b1fe6070caeb1d9319568412cfa2d05adffc44fcbea9"


def test_ai_complex_scope_value_and_four_state_contract(
        loop_runner: StdioLoopRunner, ai_complex_fst: Path) -> None:
    open_session(loop_runner, ai_complex_fst)
    scope = loop_runner.request("scope.list", args={
        "path": "ai_complex_top", "level": 0,
    }, limits={"max_rows": 100})
    assert scope.get("ok"), scope
    assert scope["summary"]["scan_complete"] is True
    assert scope["summary"]["analysis_complete"] is True
    assert scope["summary"]["response_truncated"] is False
    assert any(row["name"] == "sig_a" and row["width"] == 8
               for row in scope["data"]["signals"])

    value = loop_runner.request("value.at", args={
        "signal": "ai_complex_top.sig_a",
        "clock": "ai_complex_top.clk",
        "time": "75ns",
        "value_format": "hex",
    })
    assert value.get("ok"), value
    sample = value["data"]["samples"][0]
    assert sample["values"][0]["value"]["value"] == "8'h22"
    assert sample["clock_context"]["clock_edge_kind"] == "posedge"
    assert sample["clock_context"]["requested_sampling"]["edge"] == "negedge"
    assert sample["clock_context"]["requested_any_edge_hit"] is True
    assert sample["clock_context"]["requested_target_edge_hit"] is False

    xz = loop_runner.request("value.at", args={
        "signal": "ai_complex_top.xz_bus",
        "clock": "ai_complex_top.clk",
        "time": "95ns",
        "value_format": "bin",
    })
    assert xz.get("ok"), xz
    xz_value = xz["data"]["samples"][0]["values"][0]["value"]
    assert xz_value["known"] is False
    assert xz_value["bits"] == "zzzzzzzz"
    assert xz_value["has_z"] is True

    missing = loop_runner.request("value.at", args={
        "signal": "ai_complex_top.no_such",
        "time": "10ns",
    })
    assert not missing.get("ok"), missing
    assert missing["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_ai_complex_event_reset_and_sampling_contract(
        loop_runner: StdioLoopRunner, ai_complex_fst: Path,
        tmp_path: Path) -> None:
    open_session(loop_runner, ai_complex_fst)
    loaded = _load_event_config(loop_runner, tmp_path)
    assert loaded.get("ok"), loaded

    found = loop_runner.request("event.find", args={
        "name": "ai_event",
        "expr": "vld && !rdy && payload_lo != 0",
        "time_range": {"begin": "0ns", "end": "200ns"},
    })
    assert found.get("ok"), found
    assert found["summary"]["total_count"] == 1
    assert len(found["data"]["events"]) == 1
    assert found["data"]["sampling"]["effective"]["edge"] == "posedge"

    common = {
        "clock": "ai_complex_top.clk",
        "edge": "posedge",
        "reset": {
            "signal": "ai_complex_top.rst_n",
            "polarity": "active_low",
        },
        "signals": {
            "vld": "ai_complex_top.event_vld",
            "race": "ai_complex_top.event_race",
        },
        "time_range": {"begin": "100ns", "end": "110ns"},
        "mode": "first",
    }
    before = loop_runner.request("event.find", args={
        **common, "sample_point": "before", "expr": "!vld && !race",
    })
    after = loop_runner.request("event.find", args={
        **common, "sample_point": "after", "expr": "vld && race",
    })
    assert before.get("ok"), before
    assert after.get("ok"), after
    assert len(before["data"]["events"]) == 1
    assert len(after["data"]["events"]) == 1
    assert before["data"]["sampling"]["effective"]["sample_point"] == \
        "before"
    assert after["data"]["sampling"]["effective"]["sample_point"] == \
        "after"


def test_ai_complex_stream_before_after_contract(
        loop_runner: StdioLoopRunner, ai_complex_fst: Path) -> None:
    open_session(loop_runner, ai_complex_fst)
    streams = []
    for point in ("before", "after"):
        streams.append({
            "name": f"race_{point}",
            "signals": {
                "clk": "ai_complex_top.clk",
                "vld": "ai_complex_top.event_vld",
                "rdy": "ai_complex_top.event_race",
                "payload": "ai_complex_top.event_payload",
            },
            "clock": "clk",
            "edge": "posedge",
            "sample_point": point,
            "reset": {
                "signal": "ai_complex_top.rst_n",
                "polarity": "active_low",
            },
            "vld": "vld",
            "rdy": "rdy",
            "data": "payload",
        })
    loaded = loop_runner.request("stream.config.load", args={
        "config": {"streams": streams},
    })
    assert loaded.get("ok"), loaded

    results = {}
    for point in ("before", "after"):
        results[point] = loop_runner.request("stream.query", args={
            "stream": f"race_{point}",
            "query": "summary",
            "time_range": {"begin": "100ns", "end": "110ns"},
        })
        assert results[point].get("ok"), results[point]
        assert results[point]["summary"]["sample_point"] == point
    assert results["before"]["summary"]["transfer_count"] == 0
    assert results["after"]["summary"]["transfer_count"] == 1


def test_ai_complex_expression_window_and_xz_contract(
        loop_runner: StdioLoopRunner, ai_complex_fst: Path) -> None:
    open_session(loop_runner, ai_complex_fst)
    checks = loop_runner.request("verify.conditions", args={
        "clock": "ai_complex_top.clk",
        "time": "95ns",
        "signals": {
            "a": "ai_complex_top.sig_a",
            "b": "ai_complex_top.sig_b",
            "xz": "ai_complex_top.xz_bus",
        },
        "conditions": [
            {"expr": "a == 'h22"},
            {"expr": "b == 'h22"},
            {"expr": "xz == 0"},
        ],
    })
    assert checks.get("ok"), checks
    assert checks["summary"]["passed"] == 1
    assert checks["summary"]["failed"] == 1
    assert checks["summary"]["unknown"] == 1

    expr = loop_runner.request("expr.eval_at", args={
        "clock": "ai_complex_top.clk",
        "time": "95ns",
        "expr": "xz != 0",
        "signals": {"xz": "ai_complex_top.xz_bus"},
    })
    assert expr.get("ok"), expr
    assert expr["summary"]["known"] is False

    window = loop_runner.request("window.verify", args={
        "clock": "ai_complex_top.clk",
        "edge": "posedge",
        "sample_point": "after",
        "time_range": {"begin": "140ns", "end": "175ns"},
        "signals": {
            "valid": "ai_complex_top.hs_valid",
            "ready": "ai_complex_top.hs_ready",
        },
        "conditions": [{"expr": "valid && !ready", "mode": "always"}],
    })
    assert window.get("ok"), window
    assert window["summary"]["all_passed"] is True
    assert window["summary"]["scan_complete"] is True

    exact_x = loop_runner.request("signal.xz_verify", args={
        "signal": "ai_complex_top.xz_bus",
        "expected_state": "x",
        "time_range": {"begin": "86ns", "end": "94ns"},
    })
    assert exact_x.get("ok"), exact_x
    assert exact_x["summary"]["verdict"] == "pass"
    assert exact_x["summary"]["scan_complete"] is True
    exact_z = loop_runner.request("signal.xz_verify", args={
        "signal": "ai_complex_top.xz_bus",
        "expected_state": "z",
        "time_range": {"begin": "95ns", "end": "95ns"},
    })
    assert exact_z.get("ok"), exact_z
    assert exact_z["summary"]["verdict"] == "pass"


def test_ai_complex_counter_statistics_contract(
        loop_runner: StdioLoopRunner, ai_complex_fst: Path) -> None:
    open_session(loop_runner, ai_complex_fst)
    common = {
        "clock": "ai_complex_top.clk",
        "edge": "posedge",
        "time_range": {"begin": "55ns", "end": "95ns"},
        "vld": "ai_complex_top.rst_n",
        "cnt": "ai_complex_top.counter_inc",
    }
    direct = loop_runner.request("counter.statistics", args=common)
    assert direct.get("ok"), direct
    assert direct["summary"]["valid_count"] >= 4
    assert direct["summary"]["scan_complete"] is True
    assert direct["summary"]["analysis_complete"] is True
    assert direct["summary"]["response_truncated"] is False
    assert direct["summary"]["min_value"]["value"] == "8'h00"
    assert direct["summary"]["max_value"]["value"] == "8'h04"
    assert direct["data"]["min_count"] == 1
    assert direct["data"]["max_count"] == 1

    limited = loop_runner.request("counter.statistics", args={
        **common, "line_limit": 1,
    })
    assert limited.get("ok"), limited
    assert limited["summary"]["valid_count"] == \
        direct["summary"]["valid_count"]
    assert limited["summary"]["analysis_complete"] is True
    assert limited["summary"]["returned_count"] == 1
    assert limited["summary"]["response_truncated"] is True

    budgeted = loop_runner.request("counter.statistics", args={
        **common, "max_samples": 2,
    })
    assert budgeted.get("ok"), budgeted
    assert budgeted["summary"]["analysis_complete"] is False

    expression = loop_runner.request("counter.statistics", args={
        **common,
        "vld": {
            "expr": "rst",
            "signals": {"rst": "ai_complex_top.rst_n"},
        },
    })
    assert expression.get("ok"), expression
    assert expression["summary"]["valid_count"] == \
        direct["summary"]["valid_count"]
    assert expression["summary"]["average_value"] == "2"

    concat = loop_runner.request("counter.statistics", args={
        **common,
        "cnt": "{ai_complex_top.sig_a,ai_complex_top.counter_inc}",
    })
    assert concat.get("ok"), concat
    assert int(concat["summary"]["max_value"]["bits"], 2) > 255

    for name, time in (("cnt_begin", "55ns"), ("cnt_end", "95ns")):
        cursor = loop_runner.request("waveform.cursor.set", args={
            "name": name, "time": time,
        })
        assert cursor.get("ok"), cursor
    cursor = loop_runner.request("counter.statistics", args={
        **common,
        "time_range": {"begin": "@cnt_begin", "end": "@cnt_end"},
    })
    assert cursor.get("ok"), cursor
    assert cursor["summary"]["min_value"] == direct["summary"]["min_value"]
    assert cursor["summary"]["max_value"] == direct["summary"]["max_value"]


def test_ai_complex_changes_statistics_anomaly_and_handshake_contract(
        loop_runner: StdioLoopRunner, ai_complex_fst: Path) -> None:
    open_session(loop_runner, ai_complex_fst)
    changes = loop_runner.request("signal.changes", args={
        "signal": "ai_complex_top.sig_a",
        "time_range": {"begin": "0ns", "end": "120ns"},
        "line_limit": 2,
    })
    assert changes.get("ok"), changes
    assert changes["summary"]["analysis_complete"] is True
    assert changes["summary"]["response_truncated"] is True
    assert changes["summary"]["returned_count"] == 2
    assert changes["summary"]["total_count"] > 2

    stats = loop_runner.request("signal.statistics", args={
        "signal": "ai_complex_top.hs_valid",
        "clock": "ai_complex_top.clk",
        "time_range": {"begin": "120ns", "end": "210ns"},
        "line_limit": 1000,
    })
    assert stats.get("ok"), stats
    assert stats["summary"]["sample_count"] > 0
    assert stats["summary"]["known_count"] > 0
    assert "high_cycles" in stats["data"]
    assert "low_cycles" in stats["data"]

    anomaly = loop_runner.request("signal.anomaly.inspect", args={
        "signals": [
            "ai_complex_top.glitch_sig",
            "ai_complex_top.stuck_sig",
            "ai_complex_top.xz_bus",
        ],
        "time_range": {"begin": "0ns", "end": "200ns"},
        "checks": [
            {"type": "glitch", "min_pulse_width": "1ns"},
            {"type": "stuck", "min_duration": "100ns"},
            {"type": "unknown_xz"},
        ],
        "line_limit": 10,
    })
    assert anomaly.get("ok"), anomaly
    finding_types = {row["type"] for row in anomaly["data"]["findings"]}
    assert {"glitch", "stuck", "unknown_xz"} <= finding_types
    assert any(row.get("value", {}).get("value") == "8'hzz"
               for row in anomaly["data"]["findings"]
               if row["type"] == "unknown_xz")

    handshake = loop_runner.request("protocol.handshake.inspect", args={
        "clock": "ai_complex_top.clk",
        "valid": "ai_complex_top.hs_valid",
        "ready": "ai_complex_top.hs_ready",
        "data": ["ai_complex_top.hs_data"],
        "time_range": {"begin": "120ns", "end": "210ns"},
        "rules": {
            "max_wait_cycles": 2,
            "check_data_stable_when_stalled": True,
        },
    })
    assert handshake.get("ok"), handshake
    assert handshake["summary"]["max_stall_cycles"] >= 3
    assert handshake["summary"]["data_stability_violations"] >= 1
    assert handshake["summary"]["scan_complete"] is True
