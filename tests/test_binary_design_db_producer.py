"""Formal patched-Verilator binary DesignDB producer contracts."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def producer_environment(repo_root: Path) -> dict[str, str]:
    toolchain = Path(os.environ.get("XDEBUG_TOOLCHAIN_ROOT", str(repo_root.parent / ".toolchains" / "gcc-13")))
    gcc = toolchain / "bin" / "gcc"
    gxx = toolchain / "bin" / "g++"
    assert gcc.is_file() and gxx.is_file()
    assert subprocess.check_output(
        [str(gxx), "-dumpfullversion"], text=True).strip() == "13.3.1"
    environment = dict(os.environ)
    environment["CC"] = str(gcc)
    environment["CXX"] = str(gxx)
    environment["PATH"] = f"{toolchain / 'bin'}:{environment['PATH']}"
    return environment


def run_verilator(verilator: Path, rtl: Path, output: Path,
                  flag: str, prefix: str, environment: dict[str, str]
                  ) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(verilator), "--cc", flag, "--prefix", prefix,
         "--Mdir", str(output), str(rtl)],
        cwd=rtl.parent, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30, check=False,
    )


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_patched_verilator_publishes_atomic_binary_bundle_and_keeps_legacy(
        repo_root: Path, xfst_bin: Path, tmp_path: Path) -> None:
    verilator = xfst_bin.parent / "tools" / "verilator" / "bin" / "verilator"
    assert verilator.is_file(), "统一构建未安装 patched Verilator"
    environment = producer_environment(repo_root)
    rtl = tmp_path / "producer_contract.sv"
    rtl.write_text(
        "module producer_contract(input logic a, output logic y);\n"
        "  assign y = a;\n"
        "endmodule\n",
        encoding="utf-8",
    )

    binary_dir = tmp_path / "binary"
    first = run_verilator(
        verilator, rtl, binary_dir, "--design-db-binary",
        "Vproduction", environment)
    assert first.returncode == 0, first.stderr
    database = binary_dir / "Vproduction__DesignDb.xddb"
    manifest = binary_dir / "xdebug-design-db.json"
    assert json.loads(manifest.read_text(encoding="utf-8")) == {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": database.name,
    }
    assert database.is_file()
    assert not list(binary_dir.glob("*__DesignDb.cpp"))
    assert not list(binary_dir.glob("*.tmp"))
    first_digest = sha256(database)

    repeated = run_verilator(
        verilator, rtl, binary_dir, "--design-db-binary",
        "Vproduction", environment)
    assert repeated.returncode == 0, repeated.stderr
    assert sha256(database) == first_digest
    assert not list(binary_dir.glob("*.tmp"))

    legacy_dir = tmp_path / "legacy"
    legacy = run_verilator(
        verilator, rtl, legacy_dir, "--design-db",
        "Vlegacy", environment)
    assert legacy.returncode == 0, legacy.stderr
    assert (legacy_dir / "Vlegacy__DesignDb.cpp").is_file()
    assert not list(legacy_dir.glob("*__DesignDb.xddb"))
    assert not (legacy_dir / "xdebug-design-db.json").exists()

    legacy_library = legacy_dir / "libVlegacy__DesignDb.so"
    compiled = subprocess.run(
        [environment["CXX"], "-std=c++17", "-O2", "-shared", "-fPIC",
         f"-I{xfst_bin.parent / '_deps' / 'verilator-src' / 'include'}",
         "-o", str(legacy_library),
         str(legacy_dir / "Vlegacy__DesignDb.cpp")],
        cwd=rtl.parent, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30, check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    parity = subprocess.run(
        [sys.executable, str(repo_root / "tools" / "check_design_db_parity.py"),
         str(legacy_library), str(database)],
        cwd=repo_root, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30, check=False,
    )
    assert parity.returncode == 0, parity.stdout + parity.stderr
    summary = json.loads(parity.stdout)
    assert summary["signals"] > 0
    assert summary["drivers"] > 0


def test_patched_verilator_does_not_publish_partial_binary_bundle(
        repo_root: Path, xfst_bin: Path, tmp_path: Path) -> None:
    verilator = xfst_bin.parent / "tools" / "verilator" / "bin" / "verilator"
    assert verilator.is_file(), "统一构建未安装 patched Verilator"
    environment = producer_environment(repo_root)
    rtl = tmp_path / "producer_failure.sv"
    rtl.write_text("module producer_failure; endmodule\n", encoding="utf-8")
    output = tmp_path / "blocked"
    blocked_manifest = output / "xdebug-design-db.json"
    blocked_manifest.mkdir(parents=True)
    (blocked_manifest / "preserved").write_text("evidence\n", encoding="utf-8")

    result = run_verilator(
        verilator, rtl, output, "--design-db-binary", "Vblocked", environment)
    assert result.returncode != 0
    assert "Cannot retire previous DesignDB bundle manifest" in result.stderr
    assert (blocked_manifest / "preserved").read_text() == "evidence\n"
    assert not (output / "Vblocked__DesignDb.xddb").exists()
    assert not (output / "Vblocked__DesignDb.xddb.tmp").exists()
    assert not (output / "xdebug-design-db.json.tmp").exists()
