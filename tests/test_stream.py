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


def test_stream_config_list(loop_runner: StdioLoopRunner, stream_fst) -> None:
    open_session(loop_runner, stream_fst)
    rsp = loop_runner.request("stream.config.list")
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["count"] == 0


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
    assert fields["is_aa"]["value"] == "1'b1"
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
