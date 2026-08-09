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
    str(REPO_ROOT / "build"),
    str(REPO_ROOT.parent / "wellen" / "target" / "release"),
    str(REPO_ROOT / "wellenx_capi" / "target" / "release"),
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
        default=os.environ.get("XFST_CONDA_ENV", "${XFST_CONDA_ENV}"),
        help="conda python used to run pytest (informational)",
    )


@pytest.fixture(scope="session")
def repo_root() -> Path:
    return REPO_ROOT


@pytest.fixture(scope="session")
def xfst_bin(pytestconfig: pytest.Config) -> Path:
    return Path(pytestconfig.getoption("--xfst-bin")).expanduser().resolve()


def _base_env(test_home: Path | None = None) -> dict:
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = ":".join(_LD_EXTRA)
    if test_home is not None:
        env["HOME"] = str(test_home)
    return env


@pytest.fixture(scope="session")
def test_home(tmp_path_factory: pytest.TempPathFactory) -> Path:
    return tmp_path_factory.mktemp("xdebug-home")


@pytest.fixture(scope="session")
def cli_runner(xfst_bin: Path, repo_root: Path, test_home: Path) -> CliRunner:
    return CliRunner(xfst_bin, cwd=repo_root, env=_base_env(test_home))


@pytest.fixture(scope="session")
def loop_runner(xfst_bin: Path, repo_root: Path,
                test_home: Path) -> StdioLoopRunner:
    runner = StdioLoopRunner(xfst_bin, cwd=repo_root,
                             env=_base_env(test_home))
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
def counter_design_db() -> Path:
    return FIXTURES / "counter" / "obj_dir"


@pytest.fixture(scope="session")
def xprop_fst() -> Path:
    return FIXTURES / "xprop" / "waves.fst"


@pytest.fixture(scope="session")
def xprop_design_db() -> Path:
    return FIXTURES / "xprop" / "obj_dir"


@pytest.fixture(scope="session")
def gcd_xorigin_fst(repo_root: Path) -> Path:
    return repo_root.parent / "wellen" / "wellen" / "inputs" / "treadle" / "GCD.vcd.fst"


@pytest.fixture(scope="session")
def gcd_xorigin_design_db(xfst_bin: Path) -> Path:
    return xfst_bin.parent / "testdata" / "fixtures" / "gcd_xorigin" / "obj_dir"


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
def axi_fst() -> Path:
    return _fix("axi")


@pytest.fixture(scope="session")
def axi_design_db() -> Path:
    return FIXTURES / "axi" / "obj_dir"


@pytest.fixture(scope="session")
def stream_fst() -> Path:
    return _fix("stream")


@pytest.fixture(scope="session")
def wide_xz_fst() -> Path:
    return REPO_ROOT.parent / "wellen" / "wellen" / "inputs" / \
        "xilinx_isim" / "test2x2_regex22_string1.vcd.fst"


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
