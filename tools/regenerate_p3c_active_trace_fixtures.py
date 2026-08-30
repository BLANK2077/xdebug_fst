#!/usr/bin/env python3
"""Rebuild only selected exact-original active-trace RTL fixtures."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROOT = ROOT / "testdata/fixtures/active_trace"
RTL_ROOT = FIXTURE_ROOT / "rtl"
WORK_ROOT = ROOT / "build/fixtures/p3c-active-trace"
PROBE = FIXTURE_ROOT / "dump_probe.sv"
PREFIX = "Vactive_trace"


def require_inside(path: Path, root: Path, label: str) -> Path:
    resolved = path.resolve()
    expected = root.resolve()
    if resolved != expected and expected not in resolved.parents:
        raise RuntimeError(f"{label} 逃逸当前仓库: {resolved}")
    return resolved


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sources(group: str, case: str) -> list[Path]:
    if group == "p0":
        result = [RTL_ROOT / group / case / "tb.sv"]
    elif group == "composite":
        result = [
            RTL_ROOT / group / case / "tb.sv",
            RTL_ROOT / group / "chain_dut.sv",
        ]
    elif group == "timing":
        result = [
            RTL_ROOT / group / case / "tb.sv",
            RTL_ROOT / group / "timing_boundary_dut.sv",
        ]
    elif group == "phase4":
        result = [
            RTL_ROOT / group / case / "tb.sv",
            RTL_ROOT / group / "phase4_dut.sv",
            RTL_ROOT / "composite/chain_dut.sv",
        ]
    elif group == "phase5":
        result = [RTL_ROOT / group / "dut.sv", RTL_ROOT / group / "tb.sv"]
    else:
        raise RuntimeError(f"unsupported group: {group}")
    missing = [path for path in result if not path.is_file()]
    if missing:
        raise RuntimeError("缺少精确 RTL 镜像: " + ", ".join(map(str, missing)))
    return result


def cases(group: str, selected: list[str]) -> list[str]:
    if selected:
        result = sorted(set(selected))
    elif group == "phase5":
        result = ["phase5"]
    else:
        result = sorted(
            path.name for path in (RTL_ROOT / group).iterdir()
            if path.is_dir() and (path / "tb.sv").is_file()
        )
    if not result:
        raise RuntimeError(f"组 {group} 没有可重建 case")
    for case in result:
        sources(group, case)
    return result


def producer_environment() -> dict[str, str]:
    toolchain = ROOT.parent / ".toolchains/gcc-13"
    gcc = toolchain / "bin/gcc"
    gxx = toolchain / "bin/g++"
    verilator = ROOT / "build/tools/verilator/bin/verilator"
    if not gcc.is_file() or not gxx.is_file() or not verilator.is_file():
        raise RuntimeError("缺少统一构建或私有 GCC 13.3.1")
    version = subprocess.check_output(
        [str(gxx), "-dumpfullversion"], text=True).strip()
    if version != "13.3.1":
        raise RuntimeError(f"G++ 版本漂移: {version}")
    environment = dict(os.environ)
    environment.update({
        "CC": str(gcc),
        "CXX": str(gxx),
        "PATH": f"{toolchain / 'bin'}:{environment.get('PATH', '')}",
        "HOME": str(require_inside(WORK_ROOT / "home", ROOT, "HOME")),
        "TMPDIR": str(require_inside(WORK_ROOT / "tmp", ROOT, "TMPDIR")),
        "XDG_CACHE_HOME": str(
            require_inside(WORK_ROOT / "cache", ROOT, "cache")
        ),
    })
    for name in ("HOME", "TMPDIR", "XDG_CACHE_HOME"):
        Path(environment[name]).mkdir(parents=True, exist_ok=True)
    return environment


def relative(path: Path) -> str:
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def build_once(group: str, case: str, environment: dict[str, str]) -> dict:
    work_group = require_inside(WORK_ROOT / group, ROOT, "构建目录")
    work_group.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f"{case}-", dir=work_group))
    obj = work / "obj"
    source_paths = sources(group, case)
    command = [
        str(ROOT / "build/tools/verilator/bin/verilator"),
        "-Wno-fatal", "--binary", "--timing", "--trace-fst",
        "--design-db-binary", "--top-module", "top",
        "--prefix", PREFIX, "--Mdir", str(obj),
        *(relative(path) for path in source_paths), relative(PROBE),
    ]
    built = subprocess.run(
        command, cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=180, check=False,
    )
    if built.returncode != 0:
        raise RuntimeError(
            f"{group}/{case} Verilator 失败\n{built.stdout}{built.stderr}"
        )
    simulated = subprocess.run(
        [str(obj / PREFIX)], cwd=work, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=60, check=False,
    )
    if simulated.returncode != 0:
        raise RuntimeError(
            f"{group}/{case} 仿真失败\n"
            f"{simulated.stdout}{simulated.stderr}"
        )
    waveform = work / "waves.fst"
    database = obj / f"{PREFIX}__DesignDb.xddb"
    manifest = obj / "xdebug-design-db.json"
    expected_manifest = {
        "schema_version": "xdebug.design-db-bundle.v2",
        "format": "binary-v1",
        "database": database.name,
    }
    if json.loads(manifest.read_text(encoding="utf-8")) != expected_manifest:
        raise RuntimeError(f"{group}/{case} DesignDB manifest 漂移")
    if not waveform.is_file() or not database.is_file():
        raise RuntimeError(f"{group}/{case} 缺少 FST 或 DesignDB")
    return {
        "work": work,
        "waveform": waveform,
        "database": database,
        "manifest": manifest,
        "waveform_sha256": sha256(waveform),
        "database_sha256": sha256(database),
        "sources": source_paths,
    }


def publish(group: str, case: str, built: dict) -> dict:
    destination = require_inside(FIXTURE_ROOT / group / case, ROOT, "fixture 输出")
    design_dir = destination / "design_db"
    design_dir.mkdir(parents=True, exist_ok=True)
    outputs = {
        destination / "waves.fst": built["waveform"],
        design_dir / built["database"].name: built["database"],
        design_dir / "xdebug-design-db.json": built["manifest"],
    }
    for target, source in outputs.items():
        temporary = target.with_suffix(target.suffix + ".tmp")
        shutil.copyfile(source, temporary)
        os.replace(temporary, target)

    recorded = [*built["sources"], PROBE, *outputs]
    checksum = destination / "fixture.sha256"
    checksum_text = "".join(
        f"{sha256(path)}  {relative(path)}\n" for path in recorded
    )
    temporary_checksum = checksum.with_suffix(".sha256.tmp")
    temporary_checksum.write_text(checksum_text, encoding="utf-8")
    os.replace(temporary_checksum, checksum)
    return {
        "group": group,
        "case": case,
        "fst_sha256": built["waveform_sha256"],
        "fst_size": (destination / "waves.fst").stat().st_size,
        "design_db_sha256": built["database_sha256"],
        "design_db_size": (design_dir / built["database"].name).stat().st_size,
    }


def build_case(group: str, case: str, repeat: int,
               environment: dict[str, str]) -> dict:
    runs = [build_once(group, case, environment) for _ in range(repeat)]
    identities = {
        (run["waveform_sha256"], run["database_sha256"]) for run in runs
    }
    if len(identities) != 1:
        raise RuntimeError(f"{group}/{case} 重复构建不确定: {sorted(identities)}")
    result = publish(group, case, runs[-1])
    result["repeat"] = repeat
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--group", required=True,
        choices=("p0", "composite", "timing", "phase4", "phase5"),
    )
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 8:
        raise SystemExit("--jobs 必须在 1..8")
    if args.repeat < 1 or args.repeat > 3:
        raise SystemExit("--repeat 必须在 1..3")
    require_inside(FIXTURE_ROOT, ROOT, "fixture 根")
    require_inside(WORK_ROOT, ROOT, "构建根")
    environment = producer_environment()
    selected = cases(args.group, args.case)
    results: list[dict] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        pending = {
            executor.submit(
                build_case, args.group, case, args.repeat, environment
            ): case for case in selected
        }
        for future in as_completed(pending):
            results.append(future.result())
    print(json.dumps({
        "schema_version": "xdebug.p3c-fixture-build.v1",
        "group": args.group,
        "case_count": len(results),
        "results": sorted(results, key=lambda row: row["case"]),
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
