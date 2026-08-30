# conftest.py — xdebug-fst test fixtures (BSD-3-Clause)
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any

import pytest

from runner import CliRunner, StdioLoopRunner

Json = Any

AXI_CONFIG = {
    "clock": "TOP.aclk",
    "reset": {"signal": "TOP.aresetn", "polarity": "active_low"},
    "edge": "posedge", "sample_point": "after",
    "awaddr": "TOP.awaddr", "awid": "TOP.awid", "awlen": "TOP.awlen",
    "awsize": "TOP.awsize", "awburst": "TOP.awburst",
    "awvalid": "TOP.awvalid", "awready": "TOP.awready",
    "wdata": "TOP.wdata", "wstrb": "TOP.wstrb", "wlast": "TOP.wlast",
    "wvalid": "TOP.wvalid", "wready": "TOP.wready",
    "bid": "TOP.bid", "bresp": "TOP.bresp",
    "bvalid": "TOP.bvalid", "bready": "TOP.bready",
    "araddr": "TOP.araddr", "arid": "TOP.arid", "arlen": "TOP.arlen",
    "arsize": "TOP.arsize", "arburst": "TOP.arburst",
    "arvalid": "TOP.arvalid", "arready": "TOP.arready",
    "rid": "TOP.rid", "rdata": "TOP.rdata", "rresp": "TOP.rresp",
    "rlast": "TOP.rlast", "rvalid": "TOP.rvalid", "rready": "TOP.rready",
}

STREAM_CONFIG = {
    "streams": [{"name": "fifo",
        "signals": {"clk": "top.clk", "vld": "top.in_valid",
                    "rdy": "top.in_ready", "data": "top.in_data"},
        "clock": "clk", "edge": "posedge", "sample_point": "after",
        "reset": {"signal": "top.reset", "polarity": "active_high"},
        "vld": "vld", "rdy": "rdy", "beat_fields": {"data": "data"}}],
}

TESTS_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TESTS_ROOT.parent
FIXTURES = REPO_ROOT / "testdata" / "fixtures"


if str(TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(TESTS_ROOT))

# Wellen FFI library dirs needed at runtime
_LD_EXTRA = [
    str(REPO_ROOT / "build" / "lib"),
    str(REPO_ROOT.parent / ".toolchains" / "gcc-13" / "lib64"),
]


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("xdebug-fst")
    group.addoption(
        "--xfst-bin",
        default=os.environ.get("XFST_BIN", str(REPO_ROOT / "build" / "xdebug-fst")),
        help="xdebug-fst binary path",
    )
    group.addoption(
        "--xfst-env",
        default=os.environ.get("XFST_CONDA_ENV", sys.executable),
        help="conda python used to run pytest (informational)",
    )


def pytest_sessionstart(session: pytest.Session) -> None:
    raw_trace_path = os.environ.get("XDEBUG_ACTION_COVERAGE_LOG")
    if not raw_trace_path:
        return
    trace_path = Path(raw_trace_path)
    if not trace_path.is_absolute():
        raise pytest.UsageError(
            "XDEBUG_ACTION_COVERAGE_LOG must be an absolute path"
        )
    if trace_path.exists():
        raise pytest.UsageError(
            f"XDEBUG_ACTION_COVERAGE_LOG must not already exist: {trace_path}"
        )
    if not trace_path.parent.is_dir():
        raise pytest.UsageError(
            f"XDEBUG_ACTION_COVERAGE_LOG parent is unavailable: {trace_path.parent}"
        )


@pytest.fixture(scope="session")
def repo_root() -> Path:
    return REPO_ROOT


@pytest.fixture(scope="session")
def xfst_bin(pytestconfig: pytest.Config) -> Path:
    return Path(pytestconfig.getoption("--xfst-bin")).expanduser().resolve()


def _base_env(test_home: Path | None = None, xfst_bin: Path | None = None) -> dict:
    env = dict(os.environ)
    xfst_bin = xfst_bin or (REPO_ROOT / "build" / "xdebug-fst")
    library_paths = [str(xfst_bin.parent / "lib"), *_LD_EXTRA]
    for path in env.get("LD_LIBRARY_PATH", "").split(":"):
        if path and path not in library_paths:
            library_paths.append(path)
    env["LD_LIBRARY_PATH"] = ":".join(library_paths)
    if test_home is not None:
        env["HOME"] = str(test_home)
    return env


@pytest.fixture(scope="session")
def test_home(tmp_path_factory: pytest.TempPathFactory) -> Path:
    return tmp_path_factory.mktemp("xdebug-home")


@pytest.fixture(scope="session")
def cli_runner(xfst_bin: Path, repo_root: Path, test_home: Path) -> CliRunner:
    return CliRunner(xfst_bin, cwd=repo_root, env=_base_env(test_home, xfst_bin))


@pytest.fixture(scope="session")
def loop_runner(xfst_bin: Path, repo_root: Path,
                test_home: Path) -> StdioLoopRunner:
    runner = StdioLoopRunner(xfst_bin, cwd=repo_root,
                             env=_base_env(test_home, xfst_bin))
    runner.start()
    try:
        yield runner
    finally:
        runner.stop()


# ── Fixture paths ──

def _fix(name: str) -> Path:
    return FIXTURES / name / "waves.fst"


@pytest.fixture(scope="session")
def counter_fst() -> Path:
    return _fix("counter")


@pytest.fixture(scope="session")
def ai_complex_fst() -> Path:
    return _fix("ai_complex")


@pytest.fixture(scope="session")
def active_driver_fst() -> Path:
    return _fix("active_driver")


@pytest.fixture(scope="session")
def active_driver_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata/fixtures/active_driver/obj_dir"


@pytest.fixture(scope="session")
def interface_port_root_fst() -> Path:
    return _fix("interface_port_root")


@pytest.fixture(scope="session")
def interface_port_root_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata/fixtures/interface_port_root/obj_dir"


@pytest.fixture(scope="session")
def active_zero_evidence_fst() -> Path:
    return _fix("active_zero_evidence")


@pytest.fixture(scope="session")
def active_zero_evidence_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata/fixtures/active_zero_evidence/obj_dir"


@pytest.fixture(scope="session")
def counter_design_db() -> Path:
    return FIXTURES / "counter" / "obj_dir"


@pytest.fixture(scope="session")
def xprop_fst() -> Path:
    return FIXTURES / "xprop" / "waves.fst"


@pytest.fixture(scope="session")
def xprop_design_db() -> Path:
    return FIXTURES / "xprop" / "obj_dir"


@pytest.fixture(scope="session")
def wellen_source(xfst_bin: Path) -> Path:
    source = xfst_bin.parent / "_deps" / "wellen-src"
    if not source.is_dir():
        raise RuntimeError(f"prepared Wellen shadow source is unavailable: {source}")
    return source


@pytest.fixture(scope="session")
def gcd_xorigin_fst(wellen_source: Path) -> Path:
    return wellen_source / "wellen" / "inputs" / "treadle" / "GCD.vcd.fst"


@pytest.fixture(scope="session")
def wellen_apb_fst(wellen_source: Path) -> Path:
    return (
        wellen_source / "wellen" / "inputs" / "vcs" /
        "Apb_slave_uvm_new.vcd.fst"
    )


@pytest.fixture(scope="session")
def gcd_xorigin_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "gcd_xorigin" / "obj_dir"


@pytest.fixture(scope="session")
def gcd_xpredicate_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "gcd_xpredicate" / "obj_dir"


@pytest.fixture(scope="session")
def gcd_unresolved_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "gcd_unresolved" / "obj_dir"


@pytest.fixture(scope="session")
def xorigin_time_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "xorigin_time" / "obj_dir"


@pytest.fixture(scope="session")
def xorigin_alias_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "xorigin_alias" / "obj_dir"


@pytest.fixture(scope="session")
def xorigin_modport_design_db(xfst_bin: Path) -> Path:
    return (xfst_bin.parent / "testdata" / "fixtures" /
            "xorigin_modport" / "obj_dir")


@pytest.fixture(scope="session")
def xorigin_ref_port_loop_design_db(xfst_bin: Path) -> Path:
    return (xfst_bin.parent / "testdata" / "fixtures" /
            "xorigin_ref_port_loop" / "obj_dir")


@pytest.fixture(scope="session")
def xorigin_loop_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "xorigin_loop" / "obj_dir"


@pytest.fixture(scope="session")
def xorigin_loop_branch_design_db(xfst_bin: Path) -> Path:
    return (xfst_bin.parent / "testdata" / "fixtures" /
            "xorigin_loop_branch" / "obj_dir")


@pytest.fixture(scope="session")
def xorigin_patternvar_design_db(xfst_bin: Path) -> Path:
    return (xfst_bin.parent / "testdata" / "fixtures" /
            "xorigin_patternvar" / "obj_dir")


@pytest.fixture(scope="session")
def xorigin_ref_driver_branch_design_db(xfst_bin: Path) -> Path:
    return (xfst_bin.parent / "testdata" / "fixtures" /
            "xorigin_ref_driver_branch" / "obj_dir")


@pytest.fixture(scope="session")
def primitive_output_design_db(xfst_bin: Path) -> Path:
    return (xfst_bin.parent / "testdata" / "fixtures" /
            "primitive_output" / "obj_dir")


@pytest.fixture(scope="session")
def interface_modport_fst() -> Path:
    return _fix("interface_modport")


@pytest.fixture(scope="session")
def interface_modport_design_db() -> Path:
    return FIXTURES / "interface_modport" / "obj_dir"


@pytest.fixture(scope="session")
def ref_port_fst() -> Path:
    return _fix("ref_port")


@pytest.fixture(scope="session")
def ref_port_design_db() -> Path:
    return FIXTURES / "ref_port" / "obj_dir"


@pytest.fixture(scope="session")
def apb_fst() -> Path:
    return _fix("apb")


@pytest.fixture(scope="session")
def apb_design_db() -> Path:
    return FIXTURES / "apb" / "obj_dir"


@pytest.fixture(scope="session")
def case_fst() -> Path:
    return _fix("case")


@pytest.fixture(scope="session")
def case_design_db() -> Path:
    return FIXTURES / "case" / "obj_dir"


@pytest.fixture(scope="session")
def output_mixed_fst() -> Path:
    return _fix("output_mixed")


@pytest.fixture(scope="session")
def output_mixed_design_db() -> Path:
    return FIXTURES / "output_mixed" / "obj_dir"


@pytest.fixture(scope="session")
def matches_fst() -> Path:
    return _fix("matches")


@pytest.fixture(scope="session")
def matches_design_db() -> Path:
    return FIXTURES / "matches" / "obj_dir"


@pytest.fixture(scope="session")
def axi_fst() -> Path:
    return _fix("axi")


@pytest.fixture(scope="session")
def axi_design_db() -> Path:
    return FIXTURES / "axi" / "obj_dir"


@pytest.fixture(scope="session")
def stream_fst() -> Path:
    return _fix("stream")


@pytest.fixture(scope="session")
def phase5_fst() -> Path:
    return _fix("phase5")


@pytest.fixture(scope="session")
def phase5_design_db() -> Path:
    return FIXTURES / "phase5" / "obj_dir"


@pytest.fixture(scope="session")
def wide_xz_fst(wellen_source: Path) -> Path:
    return wellen_source / "wellen" / "inputs" / \
        "xilinx_isim" / "test2x2_regex22_string1.vcd.fst"


@pytest.fixture(scope="session")
def wellen_processor_fst(wellen_source: Path) -> Path:
    return wellen_source / "wellen" / "inputs" / "vcs" / \
        "processor.vcd.fst"


@pytest.fixture(scope="session")
def string_delta_fst(wellen_source: Path) -> Path:
    return wellen_source / "wellen" / "inputs" / "nvc" / \
        "shortstring.fst"


@pytest.fixture(scope="session")
def real_fst(wellen_source: Path) -> Path:
    return wellen_source / "wellen" / "inputs" / "verilator" / \
        "many_sv_datatypes.fst"


@pytest.fixture(scope="session")
def event_fst(wellen_source: Path) -> Path:
    return wellen_source / "wellen" / "inputs" / "icarus" / \
        "pull_67_event_example.fst"


# ── Helpers ──

def open_session(loop: StdioLoopRunner, fsdb: Path, design_db: Path | None = None) -> Json:
    if loop.has_current_session:
        closed = loop.request("session.close", args={})
        assert closed.get("ok"), closed
    target: dict = {"fsdb": str(fsdb)}
    if design_db is not None:
        target["daidir"] = str(design_db)
    rsp = loop.request("session.open", target=target, args={"name": "test"})
    assert rsp.get("ok"), rsp
    assert rsp["session"]["session_id"] == "test"
    return rsp
