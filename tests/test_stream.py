# test_stream.py — stream actions (BSD-3-Clause)
from __future__ import annotations

import pytest

from conftest import STREAM_CONFIG, open_session
from runner import StdioLoopRunner


PACKET_STREAM_CONFIG = {
    "streams": [{"name": "packet_fifo",
        "signals": {"clk": "top.clk", "vld": "top.in_valid",
                    "rdy": "top.in_ready", "data": "top.in_data",
                    "sop": "top.in_valid", "eop": "top.in_valid"},
        "clock": "clk", "edge": "posedge", "sample_point": "after",
        "vld": "vld", "rdy": "rdy", "sop": "sop", "eop": "eop",
        "beat_fields": {"byte": "data", "low": "data[3:0]",
                        "is_aa": "data == 8'haa",
                        "joined": "{data[7:4], data[3:0]}"}}],
}

VLD_STREAM_CONFIG = {"streams": [{"name": "vld_fifo",
    "signals": {"clk": "top.clk", "vld": "top.in_valid",
                "data": "top.in_data"},
    "clock": "clk", "edge": "posedge", "sample_point": "after",
    "vld": "vld", "beat_fields": {"data": "data"}}]}

BP_STREAM_CONFIG = {"streams": [{"name": "bp_fifo",
    "signals": {"clk": "top.clk", "vld": "top.in_valid",
                "bp": "top.reset", "data": "top.in_data"},
    "clock": "clk", "edge": "posedge", "sample_point": "after",
    "vld": "vld", "bp": "bp", "beat_fields": {"data": "data"}}]}

WELLEN_XZ_STREAM_CONFIG = {"streams": [{
    "name": "wellen_xz_stream",
    "signals": {
        "clk": "top.masslav_if.clk",
        "vld": "top.masslav_if.Psel",
        "rdy": "top.masslav_if.Pready",
        "data": "top.masslav_if.Pwdata",
    },
    "clock": "clk", "edge": "posedge", "sample_point": "after",
    "vld": "vld", "rdy": "rdy", "beat_fields": {"data": "data"},
}]}


def test_stream_config_list(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    empty = loop_runner.request("stream.config.list")
    assert empty.get("ok"), empty
    assert empty["summary"]["count"] == 0
    assert empty["data"] == {"streams": []}
    second = dict(STREAM_CONFIG["streams"][0])
    second["name"] = "fifo_second"
    loaded = loop_runner.request("stream.config.load", args={
        "config": {"streams": [STREAM_CONFIG["streams"][0], second]}})
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("stream.config.list")
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["count"] == 2
    assert len(rsp["data"]["streams"]) == 2


def test_stream_config_get_default(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.config.get", args={"name": "fifo"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["stream"]["name"] == "fifo"


def test_stream_config_get_unknown(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.get", args={"name": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CONFIG_NOT_FOUND"


def test_stream_config_load(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.load",
                              args={"config": STREAM_CONFIG})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["streams"] == ["fifo"]


def test_stream_config_load_invalid(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.load",
                              args={"config": {"streams": []}})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "INVALID_REQUEST"


def test_stream_describe(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.describe", args={"stream": "fifo"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["handshake"] == "vld/rdy"
    assert rsp["data"]["validation"]["status"] == "ok"


def test_stream_query(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.query", args={
        "stream": "fifo", "query": "transfer_window", "cache_scope": "full",
        "time_range": {"begin": "0ps", "end": "200ps"},
        "line_limit": 32, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    rows = rsp["data"]["rows"]
    assert rsp["summary"]["transfer_count"] == 4
    assert [row["fields"]["data"]["value"] for row in rows] == [
        "8'haa", "8'hbb", "8'hcc", "8'hdd"]
    assert [row["time"] for row in rows] == ["40ps", "60ps", "80ps", "120ps"]


def test_stream_query_max_rows(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.query", args={
        "stream": "fifo", "query": "transfer_window", "cache_scope": "full",
        "line_limit": 2, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["rows"]) == 2
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["total_count"] == 4
    assert rsp["summary"]["returned_count"] == 2


def test_stream_query_and_export_empty(loop_runner: StdioLoopRunner,
                                       stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    empty_range = {"begin": "0ps", "end": "10ps"}

    queried = loop_runner.request("stream.query", args={
        "stream": "fifo", "query": "transfer_window",
        "cache_scope": "range", "time_range": empty_range,
        "line_limit": 16,
    })
    assert queried.get("ok"), queried
    assert queried["summary"]["total_count"] == 0
    assert queried["summary"]["returned_count"] == 0
    assert queried["data"]["rows"] == []

    exported = loop_runner.request("stream.export", args={
        "stream": "fifo", "kind": "transfer", "cache_scope": "range",
        "time_range": empty_range, "line_limit": 16,
    })
    assert exported.get("ok"), exported
    assert exported["summary"]["status"] == "preview"
    assert exported["summary"]["total_count"] == 0
    assert exported["summary"]["returned_count"] == 0
    assert exported["data"]["preview"] == []


def test_stream_query_summary(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.query", args={
        "stream": "fifo", "query": "summary", "cache_scope": "range",
        "time_range": {"begin": "0ps", "end": "200ps"},
        "line_limit": 16, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["transfer_count"] == 4
    assert rsp["summary"]["scan_complete"] is True
    assert rsp["data"] == {}


def test_stream_query_all_beat_field_expressions(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loaded = loop_runner.request("stream.config.load",
                                 args={"config": PACKET_STREAM_CONFIG})
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("stream.query", args={
        "stream": "packet_fifo", "query": "first_transfer",
        "cache_scope": "full", "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    fields = rsp["data"]["row"]["fields"]
    assert fields["byte"]["value"] == "8'haa"
    assert fields["low"]["value"] == "4'ha"
    assert fields["is_aa"]["value"] == "1'h1"
    assert fields["joined"]["value"] == "8'haa"


def test_stream_query_stall_window(loop_runner: StdioLoopRunner,
                                   stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.query", args={
        "stream": "fifo", "query": "stall_window", "cache_scope": "full",
        "line_limit": 16, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["stall_cycles"] > 0
    assert rsp["summary"]["stall_windows"] == len(rsp["data"]["stalls"])
    assert rsp["data"]["stalls"][0]["reason"] == "vld_without_rdy"


def test_stream_query_packet_window_and_filter(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": PACKET_STREAM_CONFIG})
    rsp = loop_runner.request("stream.query", args={
        "stream": "packet_fifo", "query": "packet_window",
        "cache_scope": "full", "line_limit": 16,
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["complete_packet_count"] == 4
    assert [packet["first_fields"]["byte"]["value"]
            for packet in rsp["data"]["packets"]] == [
                "8'haa", "8'hbb", "8'hcc", "8'hdd"]

    filtered = loop_runner.request("stream.query", args={
        "stream": "packet_fifo", "query": "packet_window",
        "cache_scope": "full", "line_limit": 16,
        "render_time_unit": "ps",
        "filter": {"position": "sop", "fields": {
            "byte": {"mode": "exact", "values": ["8'hbb"]}}}})
    assert filtered.get("ok"), filtered
    assert filtered["summary"]["matched_packet_count"] == 1
    assert filtered["data"]["packets"][0]["first_fields"]["byte"]["value"] == "8'hbb"


@pytest.mark.parametrize("query,extra", [
    ("summary", {}), ("first_transfer", {}), ("last_transfer", {}),
    ("transfer_window", {"line_limit": 16}),
    ("first_stall", {}), ("last_stall", {}),
    ("stall_window", {"line_limit": 16}),
    ("first_packet", {}), ("last_packet", {}),
    ("packet_at", {"packet_index": 2}),
    ("packet_window", {"line_limit": 16}),
])
def test_stream_all_query_kinds(loop_runner: StdioLoopRunner, stream_fst,
                                query: str, extra: dict) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": PACKET_STREAM_CONFIG})
    args = {"stream": "packet_fifo", "query": query,
            "cache_scope": "full", "render_time_unit": "ps", **extra}
    rsp = loop_runner.request("stream.query", args=args)
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["query"] == query


@pytest.mark.parametrize("rule,expected", [
    ({"mode": "range", "begin": "8'hbb", "end": "8'hcc"},
     ["8'hbb", "8'hcc"]),
    ({"mode": "mask", "value": "8'ha0", "mask": "8'hf0"},
     ["8'haa"]),
])
def test_stream_packet_filter_modes(loop_runner: StdioLoopRunner, stream_fst,
                                    rule: dict, expected: list[str]) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": PACKET_STREAM_CONFIG})
    rsp = loop_runner.request("stream.query", args={
        "stream": "packet_fifo", "query": "packet_window",
        "cache_scope": "full", "line_limit": 16,
        "render_time_unit": "ps", "filter": {
            "position": "eop", "fields": {"byte": rule}}})
    assert rsp.get("ok"), rsp
    assert [packet["last_fields"]["byte"]["value"]
            for packet in rsp["data"]["packets"]] == expected


@pytest.mark.parametrize("config,name,handshake", [
    (VLD_STREAM_CONFIG, "vld_fifo", "vld"),
    (BP_STREAM_CONFIG, "bp_fifo", "vld/bp"),
])
def test_stream_optional_flow_control(loop_runner: StdioLoopRunner, stream_fst,
                                      config: dict, name: str,
                                      handshake: str) -> None:
    open_session(loop_runner, stream_fst)
    loaded = loop_runner.request("stream.config.load", args={"config": config})
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("stream.query", args={
        "stream": name, "query": "transfer_window", "cache_scope": "full",
        "line_limit": 16, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["handshake"] == handshake
    assert rsp["summary"]["transfer_count"] == 5


def test_stream_export(loop_runner: StdioLoopRunner, stream_fst, tmp_path) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    output = tmp_path / "fifo.tsv"
    rsp = loop_runner.request("stream.export", args={
        "stream": "fifo", "kind": "transfer", "cache_scope": "range",
        "time_range": {"begin": "0ps", "end": "200ps"},
        "render_time_unit": "ps",
        "output": {"path": str(output), "file_format": "tsv"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["output_written"] is True
    assert rsp["summary"]["row_count"] == 4
    assert output.read_text().splitlines()[0] == "cycle\ttime\tdata"
    assert output.with_name(output.name + ".meta.json").is_file()


@pytest.mark.parametrize("kind,expected", [
    ("transfer", 4), ("packet", 4), ("packet_beats", 4),
])
def test_stream_export_preview_kinds(loop_runner: StdioLoopRunner, stream_fst,
                                     kind: str, expected: int) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": PACKET_STREAM_CONFIG})
    rsp = loop_runner.request("stream.export", args={
        "stream": "packet_fifo", "kind": kind, "cache_scope": "full",
        "line_limit": 16, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "preview"
    assert rsp["summary"]["output_written"] is False
    assert len(rsp["data"]["preview"]) == expected


def test_stream_export_preview_line_limit_marks_truncation(
        loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loaded = loop_runner.request("stream.config.load", args={
        "config": STREAM_CONFIG,
    })
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("stream.export", args={
        "stream": "fifo", "kind": "transfer", "cache_scope": "full",
        "line_limit": 1, "render_time_unit": "ps",
    })
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["total_count"] == 4
    assert rsp["summary"]["returned_count"] == 1
    assert rsp["summary"]["response_truncated"] is True
    assert rsp["summary"]["truncation_scopes"] == ["response_rows"]
    assert len(rsp["data"]["preview"]) == 1


def test_stream_export_packet_final_artifact(
        loop_runner: StdioLoopRunner, stream_fst, tmp_path) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": PACKET_STREAM_CONFIG})
    output = tmp_path / "packets.jsonl"
    rsp = loop_runner.request("stream.export", args={
        "stream": "packet_fifo", "kind": "packet", "cache_scope": "full",
        "render_time_unit": "ps",
        "output": {"path": str(output), "file_format": "xout"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["row_count"] == 4
    assert rsp["data"] == {}
    assert '"packet_index":0' in output.read_text()


def test_stream_validate(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": STREAM_CONFIG})
    rsp = loop_runner.request("stream.validate", args={
        "stream": "fifo", "dynamic": True, "cache_scope": "full",
        "time_range": {"begin": "0ps", "end": "200ps"},
        "line_limit": 32, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["ok"] is True
    assert rsp["summary"]["scan_complete"] is True
    assert rsp["data"]["issues"] == []
    assert rsp["data"]["dynamic"]["transfer_count"] == 4


def test_stream_validate_packet_dynamic(loop_runner: StdioLoopRunner,
                                        stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    loop_runner.request("stream.config.load", args={"config": PACKET_STREAM_CONFIG})
    rsp = loop_runner.request("stream.validate", args={
        "stream": "packet_fifo", "dynamic": True, "cache_scope": "full",
        "line_limit": 32, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["dynamic"]["complete_packet_count"] == 4
    assert rsp["data"]["dynamic"]["partial_packet_count"] == 0


def test_stream_validate_bad_config(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    bad = {"streams": [{"name": "bad",
            "signals": {"clk": "top.clk", "vld": "no.such",
                        "rdy": "top.in_ready"},
            "clock": "clk", "edge": "posedge", "sample_point": "after",
            "vld": "vld", "rdy": "rdy"}]}
    rsp = loop_runner.request("stream.config.load", args={"config": bad})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CONFIG_SIGNAL_NOT_FOUND"


def test_stream_validate_truncates_dynamic_issues_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    loaded = loop_runner.request("stream.config.load", args={
        "config": WELLEN_XZ_STREAM_CONFIG,
    })
    assert loaded.get("ok"), loaded

    validated = loop_runner.request("stream.validate", args={
        "stream": "wellen_xz_stream", "dynamic": True,
        "cache_scope": "full", "line_limit": 1,
    })
    assert validated.get("ok"), validated
    assert validated["summary"]["total_count"] >= 2
    assert validated["summary"]["returned_count"] == 1
    assert validated["summary"]["response_truncated"] is True
    assert validated["summary"]["scan_complete"] is False
    assert validated["summary"]["analysis_complete"] is False
    assert validated["summary"]["truncation_scopes"] == [
        "analysis_samples", "response_issues"
    ]
    assert len(validated["data"]["issues"]) == 1


def test_stream_query_and_export_report_xz_from_direct_raw_fst(
        loop_runner: StdioLoopRunner, wellen_apb_fst) -> None:
    open_session(loop_runner, wellen_apb_fst)
    loaded = loop_runner.request("stream.config.load", args={
        "config": WELLEN_XZ_STREAM_CONFIG,
    })
    assert loaded.get("ok"), loaded
    common = {
        "stream": "wellen_xz_stream", "cache_scope": "range",
        "time_range": {"begin": "0ps", "end": "20ns"},
        "line_limit": 20,
    }

    queried = loop_runner.request("stream.query", args={
        **common, "query": "transfer_window",
    })
    assert queried.get("ok"), queried
    assert queried["summary"]["control_xz_count"] == 2
    assert queried["summary"]["scan_complete"] is False

    exported = loop_runner.request("stream.export", args={
        **common, "kind": "transfer",
    })
    assert exported.get("ok"), exported
    assert exported["summary"]["status"] == "preview"
    assert exported["summary"]["control_xz_count"] == 2
    assert exported["summary"]["scan_complete"] is False
