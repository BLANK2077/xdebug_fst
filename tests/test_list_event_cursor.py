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
    rsp = loop_runner.request("list.add", args={
        "name": "siglist",
        "signals": ["top.counter_top.count", "top.clk"]})
    assert rsp.get("ok"), rsp
    assert rsp["summary"]["added_count"] == 2
    rsp = loop_runner.request("list.show", args={"name": "siglist"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["list"]["signal_count"] == 2
    assert rsp["data"]["list"]["signals"] == ["top.counter_top.count", "top.clk"]


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
        "name": "l1", "signals": ["no.such.signal"]})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "SIGNAL_NOT_FOUND"


def test_list_add_missing_list(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("list.add", args={
        "name": "ghost", "signals": ["top.clk"]})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "LIST_NOT_FOUND"


def test_list_delete(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={"name": "gone"})
    rsp = loop_runner.request("list.delete", args={"name": "gone"})
    assert rsp.get("ok"), rsp
    rsp = loop_runner.request("list.show", args={"name": "gone"})
    assert not rsp.get("ok")


def test_list_load_show(loop_runner: StdioLoopRunner, counter_fst,
                        tmp_path) -> None:
    open_session(loop_runner, counter_fst)
    lst = tmp_path / "sigs.txt"
    lst.write_text("# comment\ntop.clk\ntop.counter_top.count\n")
    rsp = loop_runner.request("list.load", args={"name": "filelist",
                                                 "file": str(lst)})
    assert rsp.get("ok"), rsp
    rsp = loop_runner.request("list.show", args={"name": "filelist"})
    assert rsp["data"]["list"]["signal_count"] == 2


def test_list_validate_ok_and_bad(loop_runner: StdioLoopRunner, counter_fst,
                                 tmp_path) -> None:
    open_session(loop_runner, counter_fst)
    # list.add validates signals eagerly, so build the bad list via file load
    lst = tmp_path / "badlist.txt"
    lst.write_text("top.clk\nbad.sig\n")
    rsp = loop_runner.request("list.load", args={"name": "vl", "file": str(lst)})
    assert rsp.get("ok"), rsp
    rsp = loop_runner.request("list.validate", args={"name": "vl"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["valid"] is False
    assert rsp["data"]["missing"] == ["bad.sig"]


def test_list_export(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={"name": "ex"})
    loop_runner.request("list.add", args={"name": "ex",
                                          "signals": ["top.clk"]})
    rsp = loop_runner.request("list.export", args={"name": "ex",
                                                   "begin": "0", "end": "100"})
    assert rsp.get("ok"), rsp
    exports = rsp["data"]["exports"]
    assert len(exports) == 1
    assert len(exports[0]["changes"]) >= 2


def test_list_first_change(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("list.create", args={"name": "fc"})
    loop_runner.request("list.add", args={
        "name": "fc", "signals": ["top.counter_top.count", "top.clk"]})
    rsp = loop_runner.request("list.first_change", args={
        "name": "fc", "begin": "0", "end": "200"})
    assert rsp.get("ok"), rsp
    first = rsp["data"]["first_changes"]
    assert len(first) == 2
    by_sig = {f["signal"]: f for f in first}
    # first change point in window (t=0 is the initial value row)
    assert by_sig["top.counter_top.count"]["time"] == 0
    assert by_sig["top.clk"]["time"] == 0


# ── event.* ──

def test_event_config_list(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.config.list")
    assert rsp.get("ok"), rsp
    names = [c["name"] for c in rsp["data"]["configs"]]
    assert {"rising_edge", "falling_edge", "any_change", "value_equals",
            "x_occurrence"} <= set(names)


def test_event_config_load(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.config.load", args={"name": "rising_edge"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["config"]["name"] == "rising_edge"


def test_event_config_load_unknown(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("event.config.load", args={"name": "nope"})
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
                                                           "time": "300"})
    assert rsp.get("ok"), rsp
    rsp = loop_runner.request("waveform.cursor.get", args={"name": "c1"})
    assert rsp.get("ok"), rsp
    assert rsp["data"]["cursor"]["time"] == 300


def test_cursor_get_unknown(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    rsp = loop_runner.request("waveform.cursor.get", args={"name": "ghost"})
    assert not rsp.get("ok")
    assert rsp["error"]["code"] == "CURSOR_NOT_FOUND"


def test_cursor_list_and_delete(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("waveform.cursor.set", args={"name": "a", "time": "10"})
    loop_runner.request("waveform.cursor.set", args={"name": "b", "time": "20"})
    rsp = loop_runner.request("waveform.cursor.list")
    assert rsp.get("ok")
    assert len(rsp["data"]["cursors"]) == 2
    rsp = loop_runner.request("waveform.cursor.delete", args={"name": "a"})
    assert rsp.get("ok")
    rsp = loop_runner.request("waveform.cursor.list")
    assert len(rsp["data"]["cursors"]) == 1


def test_cursor_use(loop_runner: StdioLoopRunner, counter_fst) -> None:
    open_session(loop_runner, counter_fst)
    loop_runner.request("waveform.cursor.set", args={"name": "cu", "time": "150"})
    rsp = loop_runner.request("waveform.cursor.use", args={"name": "cu"})
    assert rsp.get("ok")
    assert rsp["data"]["cursor"]["time"] == 150


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
