# test_list_event_cursor.py — list/event/cursor/nwave actions (BSD-3-Clause)
from __future__ import annotations

import json

import pytest

from conftest import open_session
from runner import StdioLoopRunner


# ── list.* ──

def test_list_create_add_show(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("list.create", args={"name": "siglist"})
    assert rsp.get("ok"), rsp
    for signal in ["top.counter_top.count", "top.clk"]:
        rsp = loop_runner.request("list.add", args={
            "name": "siglist", "signal": signal})
        assert rsp.get("ok"), rsp
        assert rsp["summary"]["signal"] == signal
    rsp = loop_runner.request("list.show", args={"name": "siglist"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {"name": "siglist", "signal_count": 2}
    assert rsp["data"]["signals"] == [
        {"index": 1, "signal": "top.counter_top.count"},
        {"index": 2, "signal": "top.clk"}]


def test_list_create_duplicate(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={"name": "dup"})
    rsp = loop_runner.request("list.create", args={"name": "dup"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "LIST_EXISTS"


def test_list_add_unknown_signal(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={"name": "l1"})
    rsp = loop_runner.request("list.add", args={
        "name": "l1", "signal": "no.such.signal"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_list_add_missing_list(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("list.add", args={
        "name": "ghost", "signal": "top.clk"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "LIST_NOT_FOUND"


def test_list_delete(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={
        "name": "gone", "signals": ["top.clk", "top.counter_top.count"]})
    rsp = loop_runner.request("list.delete", args={"name": "gone", "index": 1})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "name": "gone", "deleted": True, "removed": "top.clk"}
    shown = loop_runner.request("list.show", args={"name": "gone"})
    assert shown["data"]["signals"] == [
        {"index": 1, "signal": "top.counter_top.count"}]


def test_list_load_show(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("list.load", args={
        "config": {"lists": [{"name": "filelist", "signals": [
            "top.clk", "top.counter_top.count"]}]}, "mode": "replace"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {"loaded": 1, "mode": "replace"}
    rsp = loop_runner.request("list.show", args={"name": "filelist"})
    assert rsp["summary"]["signal_count"] == 2


def test_list_validate_ok_and_bad(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("list.create", args={
        "name": "vl", "signals": ["top.clk"]})
    assert rsp.get("ok"), rsp
    rsp = loop_runner.request("list.validate", args={"name": "vl"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {"name": "vl", "all_found": True}
    assert rsp["data"]["signals"] == [
        {"signal": "top.clk", "status": "ok"}]
    bad = loop_runner.request("list.load", args={
        "config": {"lists": [{"name": "bad", "signals": ["bad.sig"]}]}})
    assert not bad.get("ok")
    assert bad["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_list_export(loop_runner: StdioLoopRunner, counter_fst, tmp_path) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={
        "name": "ex", "signals": ["top.clk"]})
    rsp = loop_runner.request("list.export", args={
        "name": "ex", "time_range": {"begin": "0ps", "end": "100ps"},
        "line_limit": 1, "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["status"] == "preview"
    assert rsp["summary"]["row_count"] == 0
    assert rsp["summary"]["begin"] == "0ps"
    assert rsp["data"]["signals"] == [{"index": 0, "signal": "top.clk"}]

    output = tmp_path / "list-export"
    written = loop_runner.request("list.export", args={
        "name": "ex", "time_range": {"begin": "0ps", "end": "100ps"},
        "output": {"path": str(output), "file_format": "u64bin"}})
    assert written.get("ok"), written
    assert written["summary"]["status"] == "written"
    manifest = json.loads((output / "manifest.json").read_text())
    assert manifest["format"] == "u64bin.v1"
    assert manifest["row_layout"] == \
        "uint64_le: time_tick, value_words, known_mask_words"
    assert manifest["signals"][0]["signal"] == "top.clk"
    data_file = output / manifest["signals"][0]["file"]
    assert data_file.stat().st_size == manifest["signals"][0]["row_count"] * 24


def test_list_first_change(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={
        "name": "fc", "signals": ["top.counter_top.count", "top.clk"]})
    rsp = loop_runner.request("list.first_change", args={
        "name": "fc", "time_range": {"begin": "0ps", "end": "200ps"},
        "render_time_unit": "ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "name": "fc", "diff_found": True, "diff_time": "10ps",
        "changed_signal_count": 1}
    assert rsp["data"]["changed_signals"][0]["signal"] == "top.clk"
    assert rsp["data"]["changed_signals"][0]["before_time"] == "0ps"
    assert rsp["data"]["changed_signals"][0]["change_time"] == "10ps"


def test_list_first_change_no_difference(loop_runner: StdioLoopRunner,
                                         counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={
        "name": "stable", "signals": ["top.counter_top.count"]})
    rsp = loop_runner.request("list.first_change", args={
        "name": "stable", "time_range": {"begin": "490ps", "end": "490ps"}})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "name": "stable", "diff_found": False, "diff_time": None,
        "changed_signal_count": 0}
    assert rsp["data"]["changed_signals"] == []


# ── event.* ──

def _load_event_config(loop_runner: StdioLoopRunner, tmp_path,
                       name: str = "counter_event"):
    config_path = tmp_path / f"{name}.json"
    config_path.write_text(json.dumps({
        "clock": "top.clk", "edge": "posedge",
        "signals": {"count": "top.counter_top.count", "clk": "top.clk"},
        "fields": {"low_nibble": {"signal": "count", "left": 3, "right": 0}},
    }))
    return loop_runner.request("event.config.load", args={
        "name": name, "config_path": str(config_path)})


def test_event_config_list(loop_runner: StdioLoopRunner, counter_fst,
                           tmp_path) -> None:
    open_session(loop_runner, counter_fst)
    loaded = _load_event_config(loop_runner, tmp_path)
    assert loaded.get("ok"), loaded
    rsp = loop_runner.request("event.config.list")
    assert rsp.get("ok"), rsp
    assert "counter_event" in rsp["data"]["events"]
    named = loop_runner.request("event.config.list", args={"name": "counter_event"})
    assert named.get("ok"), named
    assert named["summary"] == {"status": "found"}
    assert named["data"]["config"]["clock"] == "top.clk"


def test_event_config_load(loop_runner: StdioLoopRunner, counter_fst,
                           tmp_path) -> None:
    open_session(loop_runner, counter_fst)
    rsp = _load_event_config(loop_runner, tmp_path, "loaded_event")
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {"status": "loaded"}
    assert rsp["data"]["config"] == {
        "name": "loaded_event", "clock": "top.clk", "edge": "posedge",
        "signals": {"count": "top.counter_top.count", "clk": "top.clk"},
        "fields": {"low_nibble": {
            "signal": "count", "left": 3, "right": 0}}}


def test_event_config_load_unknown(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.config.list", args={"name": "nope"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CONFIG_NOT_FOUND"


def test_event_find_rising_edge(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.find", args={
        "signal": "top.clk", "event": "rising_edge",
        "begin": "0", "end": "100"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["event_count"] == 5
    # clk starts at 1 (initial value); first rising edge is at t=20
    assert rsp["data"]["events"][0]["time"] == 20
    assert [e["time"] for e in rsp["data"]["events"]] == [20, 40, 60, 80, 100]


def test_event_find_value_equals(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.find", args={
        "signal": "top.counter_top.count", "event": "value_equals",
        "value": "8'h05", "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["event_count"] == 1
    assert rsp["data"]["events"][0]["time"] == 180


def test_event_find_x_occurrence(loop_runner: StdioLoopRunner, xprop_fst,
                                 xprop_design_db) -> None:
    open_session(loop_runner, xprop_fst, xprop_design_db)
    rsp = loop_runner.request("event.find", args={
        "signal": "top.xprop_top.a", "event": "x_occurrence",
        "begin": "0", "end": "200"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["event_count"] >= 1


def test_event_export(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.export", args={
        "signal": "top.clk", "event": "rising_edge",
        "begin": "0", "end": "100"})
    assert rsp.get("ok"), rsp
    assert len(rsp["data"]["events"]) == 5


# ── waveform.cursor.* ──

def test_cursor_set_get(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("waveform.cursor.set", args={"name": "c1",
                                                           "time": "300ps"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "name": "c1", "time": "0.3ns", "status": "set", "active": False}
    assert rsp["data"]["resolved_time"] == {
        "source": "explicit", "time": "0.3ns"}
    rsp = loop_runner.request("waveform.cursor.get", args={"name": "c1"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"] == {
        "name": "c1", "time": "0.3ns", "status": "found"}
    assert rsp["data"]["metadata"] == {
        "note": "", "origin": "user", "clock": ""}


def test_cursor_get_unknown(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("waveform.cursor.get", args={"name": "ghost"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CURSOR_NOT_FOUND"


def test_cursor_list_and_delete(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("waveform.cursor.set", args={"name": "a", "time": "10ps"})
    loop_runner.request("waveform.cursor.set", args={"name": "b", "time": "20ps"})
    rsp = loop_runner.request("waveform.cursor.list")
    assert rsp.get("ok")
    assert len(rsp["data"]["cursors"]) == 2
    assert rsp["summary"] == {"cursor_count": 2, "active_cursor": None}
    rsp = loop_runner.request("waveform.cursor.delete", args={"name": "a"})
    assert rsp.get("ok")
    rsp = loop_runner.request("waveform.cursor.list")
    assert len(rsp["data"]["cursors"]) == 1


def test_cursor_use(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("waveform.cursor.set", args={"name": "cu", "time": "150ps"})
    rsp = loop_runner.request("waveform.cursor.use", args={
        "name": "cu", "render_time_unit": "ps"})
    assert rsp.get("ok")
    assert rsp["summary"] == {
        "status": "active", "active_cursor": "cu", "time": "150ps"}
    listed = loop_runner.request("waveform.cursor.list")
    assert listed["summary"]["active_cursor"] == "cu"


# ── nwave.rc.generate ──

def test_nwave_rc_generate(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("nwave.rc.generate", args={
        "signal": "top.counter_top.count", "clock": "top.clk",
        "begin": "0", "end": "500"})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["constraint_count"] >= 1
    c0 = rsp["data"]["constraints"][0]
    assert "kind" in c0 and c0["kind"] in ("recovery", "removal", "change")
