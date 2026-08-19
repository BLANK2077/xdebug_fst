"""Cold RTL-to-session gate for the default binary DesignDB production path."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

from conftest import open_session
from test_binary_design_db_producer import producer_environment


def test_single_verilator_invocation_builds_binary_trace_session(
        loop_runner, repo_root: Path, xfst_bin: Path,
        test_home: Path, tmp_path: Path) -> None:
    verilator = xfst_bin.parent / "tools" / "verilator" / "bin" / "verilator"
    assert verilator.is_file(), "统一构建未安装 patched Verilator"
    environment = producer_environment(repo_root)
    rtl = tmp_path / "production_trace_top.sv"
    rtl.write_text(
        "`timescale 1ps/1ps\n"
        "module production_trace_top;\n"
        "  logic source = 1'b0;\n"
        "  wire observed;\n"
        "  assign observed = source;\n"
        "  initial begin\n"
        "    $dumpfile(\"production.fst\");\n"
        "    $dumpvars(0, production_trace_top);\n"
        "    #5 source = 1'b1;\n"
        "    #5 source = 1'b0;\n"
        "    #5 $finish;\n"
        "  end\n"
        "endmodule\n",
        encoding="utf-8",
    )
    obj_dir = tmp_path / "obj_dir"
    prefix = "Vproduction_trace"
    command = [
        str(verilator), "--binary", "--timing", "--trace-fst",
        "--design-db-binary", "--top-module", "production_trace_top",
        "--prefix", prefix, "--Mdir", str(obj_dir), str(rtl),
    ]
    built = subprocess.run(
        command, cwd=tmp_path, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=60, check=False,
    )
    assert built.returncode == 0, built.stderr
    database = obj_dir / f"{prefix}__DesignDb.xddb"
    manifest_path = obj_dir / "xdebug-design-db.json"
    assert json.loads(manifest_path.read_text(encoding="utf-8")) == {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": database.name,
    }
    assert database.is_file()
    assert not list(obj_dir.glob("*__DesignDb.cpp"))
    assert not list(obj_dir.glob("*.so"))
    assert not list(obj_dir.glob("*.tmp"))

    simulated = subprocess.run(
        [str(obj_dir / prefix)], cwd=tmp_path, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30, check=False,
    )
    assert simulated.returncode == 0, simulated.stderr
    waveform = tmp_path / "production.fst"
    assert waveform.is_file() and waveform.stat().st_size > 0

    opened = open_session(loop_runner, waveform, obj_dir)
    server_pid = int(opened["session"]["server_pid"])
    maps = Path(f"/proc/{server_pid}/maps").read_text(encoding="utf-8")
    assert str(database) in maps

    state_path, state = next(
        (path, value)
        for path in test_home.rglob("state.json")
        for value in [json.loads(path.read_text(encoding="utf-8"))]
        if value.get("session_id") == "test"
    )
    assert state["design_db_format"] == "binary-v1"
    debug_log = (state_path.parent / "debug.log").read_text(encoding="utf-8")
    assert "opened design db: format=binary-v1" in debug_log
    assert "design query index: build_ms=" in debug_log

    signal = "top.production_trace_top.observed"
    cases = [
        ("signal.resolve", {"signal": signal}),
        ("value.at", {"signal": signal, "time": "5ps"}),
        ("trace.driver", {"signal": signal}),
        ("trace.active_driver", {"signal": signal, "time": "5ps"}),
        ("trace.active_driver_chain", {"signal": signal, "time": "5ps"}),
        ("signal.changes", {
            "signal": signal,
            "time_range": {"begin": "0ps", "end": "max"},
        }),
    ]
    for action, args in cases:
        response = loop_runner.request(action, args=args)
        assert response.get("ok"), {"action": action, "response": response}
