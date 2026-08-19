#!/usr/bin/env python3
"""Build and measure deterministic large-RTL FST/DesignDB workloads locally."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import selectors
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, TextIO

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from large_rtl_trace_generator import generate


DEFAULT_SCALES = (1024, 2048, 4096, 8192, 16384, 32768, 65536)
TIME_FORMAT = "wall_seconds=%e\nuser_seconds=%U\nsys_seconds=%S\nmax_rss_kib=%M\nexit_code=%x"


class BenchmarkError(RuntimeError):
    pass


def parse_scales(text: str) -> tuple[int, ...]:
    values = tuple(int(item.strip()) for item in text.split(",") if item.strip())
    if not values or any(value < 1024 for value in values):
        raise ValueError("scales must contain integers of at least 1024 lines")
    if len(set(values)) != len(values):
        raise ValueError("scales must not contain duplicates")
    return values


def parse_time_metrics(text: str) -> dict[str, float | int]:
    result: dict[str, float | int] = {}
    integer_keys = {"max_rss_kib", "exit_code"}
    for raw_line in text.splitlines():
        key, separator, value = raw_line.partition("=")
        if not separator:
            continue
        result[key] = int(value) if key in integer_keys else float(value)
    required = {"wall_seconds", "user_seconds", "sys_seconds", "max_rss_kib", "exit_code"}
    if set(result) != required:
        raise BenchmarkError(f"incomplete GNU time metrics: {sorted(result)}")
    return result


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_revision(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int(round((len(ordered) - 1) * fraction))))
    return ordered[index]


def latency_summary(samples_ms: list[float]) -> dict[str, Any]:
    return {
        "samples": len(samples_ms),
        "first_ms": round(samples_ms[0], 3),
        "median_ms": round(statistics.median(samples_ms), 3),
        "p95_ms": round(percentile(samples_ms, 0.95), 3),
        "min_ms": round(min(samples_ms), 3),
        "max_ms": round(max(samples_ms), 3),
    }


def rss_kib(pid: int) -> int | None:
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError, ValueError):
        return None
    return None


def query_index_metrics(path: Path) -> dict[str, float | int]:
    match = re.search(
        r"design query index: build_ms=([0-9.]+) estimated_bytes=([0-9]+) "
        r"signals_scanned=([0-9]+) port_records_scanned=([0-9]+)",
        path.read_text(encoding="utf-8", errors="replace"),
    )
    if not match:
        raise BenchmarkError("engine log lacks design query index metrics")
    return {
        "build_ms": float(match.group(1)),
        "estimated_bytes": int(match.group(2)),
        "signals_scanned": int(match.group(3)),
        "port_records_scanned": int(match.group(4)),
    }


class TimedCommand:
    def __init__(self, time_bin: Path, timeout: int) -> None:
        self.time_bin = time_bin
        self.timeout = timeout

    def run(self, name: str, argv: list[str], cwd: Path, env: dict[str, str]) -> dict[str, Any]:
        metrics_path = cwd / f"{name}.time"
        stdout_path = cwd / f"{name}.stdout.log"
        stderr_path = cwd / f"{name}.stderr.log"
        with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open(
            "w", encoding="utf-8"
        ) as stderr:
            completed = subprocess.run(
                [str(self.time_bin), "-f", TIME_FORMAT, "-o", str(metrics_path), "--", *argv],
                cwd=cwd,
                env=env,
                stdout=stdout,
                stderr=stderr,
                timeout=self.timeout,
                check=False,
            )
        metrics = parse_time_metrics(metrics_path.read_text(encoding="utf-8"))
        if completed.returncode != 0 or metrics["exit_code"] != 0:
            tail = stderr_path.read_text(encoding="utf-8", errors="replace")[-4000:]
            raise BenchmarkError(f"stage {name} failed with {completed.returncode}:\n{tail}")
        return metrics


class StdioClient:
    def __init__(self, binary: Path, cwd: Path, env: dict[str, str], stderr_log: Path):
        self.binary = binary
        self.cwd = cwd
        self.env = env
        self.stderr_log = stderr_log
        self.proc: subprocess.Popen[str] | None = None
        self.sequence = 0
        self.session_id: str | None = None
        self._stderr_stream: TextIO | None = None

    def _read_json(self, timeout: float) -> dict[str, Any]:
        assert self.proc is not None and self.proc.stdout is not None
        selector = selectors.DefaultSelector()
        selector.register(self.proc.stdout, selectors.EVENT_READ)
        deadline = time.monotonic() + timeout
        try:
            while time.monotonic() < deadline:
                events = selector.select(deadline - time.monotonic())
                if not events:
                    break
                line = self.proc.stdout.readline()
                if line == "":
                    raise BenchmarkError("xdebug-fst stdio-loop exited unexpectedly")
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    continue
        finally:
            selector.close()
        raise BenchmarkError("timed out waiting for xdebug-fst response")

    def start(self) -> dict[str, Any]:
        self._stderr_stream = self.stderr_log.open("w", encoding="utf-8")
        self.proc = subprocess.Popen(
            [str(self.binary), "--stdio-loop", "--json"],
            cwd=self.cwd,
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self._stderr_stream,
            text=True,
            bufsize=1,
        )
        ready = self._read_json(60.0)
        if ready.get("type") != "ready":
            raise BenchmarkError(f"unexpected stdio ready envelope: {ready}")
        return ready

    def request(
        self,
        action: str,
        *,
        args: dict[str, Any] | None = None,
        target: dict[str, Any] | None = None,
        limits: dict[str, Any] | None = None,
        timeout: float = 300.0,
    ) -> tuple[dict[str, Any], float]:
        assert self.proc is not None and self.proc.stdin is not None
        self.sequence += 1
        request: dict[str, Any] = {
            "api_version": "xdebug.v1",
            "request_id": f"benchmark-{self.sequence}",
            "action": action,
            "payload_format": "json",
        }
        if args is not None:
            request["args"] = args
        if limits is not None:
            request["limits"] = limits
        if target is not None:
            request["target"] = target
        elif self.session_id and action not in {"session.open", "session.list", "session.gc"}:
            request["target"] = {"session_id": self.session_id}
        started = time.monotonic_ns()
        self.proc.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()
        envelope = self._read_json(timeout)
        elapsed_ms = (time.monotonic_ns() - started) / 1_000_000.0
        if envelope.get("id") != request["request_id"]:
            raise BenchmarkError(f"stdio response id mismatch for {action}")
        response = envelope.get("json")
        if not isinstance(response, dict):
            raise BenchmarkError(f"missing JSON response for {action}: {envelope}")
        if action == "session.open" and response.get("ok"):
            self.session_id = str(args["name"] if args else "")
        if action in {"session.close", "session.kill"} and response.get("ok"):
            self.session_id = None
        return response, elapsed_ms

    def stop(self) -> None:
        if self.proc is None:
            return
        try:
            if self.session_id:
                self.request("session.kill", args={}, timeout=30.0)
            assert self.proc.stdin is not None
            self.proc.stdin.write('{"api_version":"xdebug.v1","action":"stdio.quit"}\n')
            self.proc.stdin.flush()
            self.proc.wait(timeout=10)
        except (BrokenPipeError, OSError, subprocess.TimeoutExpired, BenchmarkError):
            self.proc.kill()
            self.proc.wait(timeout=10)
        finally:
            if self._stderr_stream is not None:
                self._stderr_stream.close()
            self.proc = None


def response_contract(action: str, response: dict[str, Any]) -> dict[str, Any]:
    if not response.get("ok"):
        error = response.get("error", {})
        raise BenchmarkError(f"{action} failed: {error.get('code')}: {error.get('message')}")
    summary = response.get("summary", {})
    selected = {
        key: summary[key]
        for key in (
            "analysis_complete", "scan_complete", "response_truncated",
            "total_count", "returned_count", "termination", "active_time",
        )
        if key in summary
    }
    if "limitations" in response:
        selected["limitations"] = response["limitations"]
    return selected


def benchmark_actions(
    scale_dir: Path,
    xfst: Path,
    env: dict[str, str],
    metadata: dict[str, Any],
    repetitions: int,
    design_bundle: Path | None = None,
    stderr_log: Path | None = None,
) -> dict[str, Any]:
    short_home_root = Path("/tmp") / "xfst-large-rtl-home"
    short_home_root.mkdir(exist_ok=True)
    home_dir = short_home_root / f"run-{os.getpid()}-{metadata['target_rtl_lines']}"
    home_dir.mkdir()
    action_env = dict(env)
    action_env["HOME"] = str(home_dir)
    client = StdioClient(
        xfst, scale_dir, action_env, stderr_log or (scale_dir / "xdebug.stderr.log"))
    try:
        ready = client.start()
        frontend_rss_before = rss_kib(int(ready["pid"]))
        opened, open_ms = client.request(
            "session.open",
            target={"fsdb": str(scale_dir / "waves.fst"),
                    "daidir": str(design_bundle or (scale_dir / "obj_dir"))},
            args={"name": f"large_rtl_{metadata['target_rtl_lines']}"},
            timeout=600.0,
        )
        open_contract = response_contract("session.open", opened)
        server_pid = int(opened["session"]["server_pid"])
        server_rss = rss_kib(server_pid)
        anchors = metadata["query_anchors"]
        requests = [
            ("signal.resolve", {"signal": anchors["resolve"]}, None),
            ("value.at", {
                "signal": anchors["value"], "time": anchors["query_time"],
                "render_time_unit": "ps",
            }, None),
            ("trace.driver", {"signal": anchors["driver"]}, {"max_results": 32}),
            ("trace.active_driver", {
                "signal": anchors["active_driver"], "time": anchors["query_time"],
                "render_time_unit": "ps",
            }, {"max_results": 32}),
            ("trace.active_driver_chain", {
                "signal": anchors["active_chain"], "time": anchors["query_time"],
                "render_time_unit": "ps",
            }, {"max_depth": 12, "max_nodes": 128, "max_trace_signals": 128}),
            ("signal.changes", {
                "signal": anchors["changes"],
                "time_range": {"begin": anchors["changes_begin"], "end": anchors["changes_end"]},
                "render_time_unit": "ps",
            }, None),
        ]
        actions: dict[str, Any] = {}
        for action, args, limits in requests:
            samples: list[float] = []
            contract: dict[str, Any] = {}
            for _ in range(repetitions):
                response, elapsed = client.request(
                    action, args=args, limits=limits, timeout=600.0
                )
                contract = response_contract(action, response)
                samples.append(elapsed)
            actions[action] = {"latency": latency_summary(samples), "contract": contract}
        closed, close_ms = client.request("session.close", args={}, timeout=60.0)
        response_contract("session.close", closed)
        index_logs = list((home_dir / ".xdebug" / "engine" / "sessions").glob(
            "*/debug.log"))
        if len(index_logs) != 1:
            raise BenchmarkError(
                f"expected one managed engine debug log, found {len(index_logs)}")
        return {
            "session_open_ms": round(open_ms, 3),
            "session_close_ms": round(close_ms, 3),
            "frontend_rss_before_open_kib": frontend_rss_before,
            "server_rss_after_open_kib": server_rss,
            "open_contract": open_contract,
            "query_index": query_index_metrics(index_logs[0]),
            "actions": actions,
        }
    finally:
        client.stop()


def tool_environment(repo: Path, build: Path) -> tuple[dict[str, str], dict[str, Any]]:
    toolchain = repo.parent / ".toolchains" / "gcc-13"
    cc = toolchain / "bin" / "gcc"
    cxx = toolchain / "bin" / "g++"
    verilator = build / "tools" / "verilator" / "bin" / "verilator"
    xfst = build / "xdebug-fst"
    time_bin = Path("/usr/bin/time")
    xdd_header = build / "_deps" / "verilator-src" / "include" / "xdd_api.h"
    required = (cc, cxx, verilator, xfst, time_bin, xdd_header, build / "lib")
    missing = [path.name for path in required if not path.exists()]
    if missing:
        raise BenchmarkError(f"required unified build artifacts are missing: {missing}")
    gcc_version = subprocess.check_output([str(cc), "-dumpfullversion"], text=True).strip()
    gxx_version = subprocess.check_output([str(cxx), "-dumpfullversion"], text=True).strip()
    if gcc_version != "13.3.1" or gxx_version != "13.3.1":
        raise BenchmarkError("private GCC/G++ must both be version 13.3.1")
    env = dict(os.environ)
    env["CC"] = str(cc)
    env["CXX"] = str(cxx)
    env["PATH"] = str(toolchain / "bin") + os.pathsep + env.get("PATH", "")
    libraries = [str(build / "lib"), str(toolchain / "lib64")]
    if env.get("LD_LIBRARY_PATH"):
        libraries.append(env["LD_LIBRARY_PATH"])
    env["LD_LIBRARY_PATH"] = os.pathsep.join(libraries)
    info = {
        "gcc_version": gcc_version,
        "gxx_version": gxx_version,
        "verilator_version": subprocess.check_output([str(verilator), "--version"], text=True).strip(),
        "xdebug_fst_revision": git_revision(repo),
        "cpu_count": os.cpu_count(),
        "machine": platform.machine(),
        "kernel": platform.release(),
    }
    return env, info


def build_one(
    scale: int,
    output: Path,
    repo: Path,
    build: Path,
    env: dict[str, str],
    jobs: int,
    repetitions: int,
    timeout: int,
    design_db_format: str,
) -> dict[str, Any]:
    scale_dir = output / f"rtl-{scale}"
    if scale_dir.exists() and any(scale_dir.iterdir()):
        raise BenchmarkError(f"scale output already exists and is nonempty: rtl-{scale}")
    scale_dir.mkdir(parents=True, exist_ok=True)
    started = time.monotonic_ns()
    metadata = generate(scale, scale_dir)
    generate_ms = (time.monotonic_ns() - started) / 1_000_000.0
    command = TimedCommand(Path("/usr/bin/time"), timeout)
    verilator = build / "tools" / "verilator" / "bin" / "verilator"
    cxx = repo.parent / ".toolchains" / "gcc-13" / "bin" / "g++"
    obj_dir = scale_dir / "obj_dir"
    stages: dict[str, Any] = {}
    design_db_flag = (
        "--design-db-binary" if design_db_format == "binary-v1"
        else "--design-db")
    stages["verilate"] = command.run(
        "verilate",
        [str(verilator), "--cc", "--exe", "--trace-fst", design_db_flag,
         "--top-module", "large_trace_top", "--Mdir", "obj_dir",
         "large_trace.sv", "tb_large_trace.cpp", "-CFLAGS", "-fPIC"],
        scale_dir,
        env,
    )
    stages["simulator_build"] = command.run(
        "simulator-build",
        ["make", "-C", "obj_dir", "-f", "Vlarge_trace_top.mk", f"-j{jobs}"],
        scale_dir,
        env,
    )
    db_cpp = obj_dir / "Vlarge_trace_top__DesignDb.cpp"
    db_so = obj_dir / "libVlarge_trace_top__DesignDb.so"
    db_binary = obj_dir / "Vlarge_trace_top__DesignDb.xddb"
    prefix_flags = [
        f"-ffile-prefix-map={scale_dir}=.",
        f"-fdebug-prefix-map={scale_dir}=.",
        f"-fmacro-prefix-map={scale_dir}=.",
    ]
    if design_db_format == "xdd-so":
        stages["design_db_build"] = command.run(
            "design-db-build",
            [str(cxx), "-std=c++17", "-O2", "-shared", "-fPIC", *prefix_flags,
             f"-I{build / '_deps' / 'verilator-src' / 'include'}",
             "-o", str(db_so), str(db_cpp)],
            scale_dir,
            env,
        )
        manifest = {
            "schema_version": "xdebug.design-db-bundle.v1",
            "library": db_so.name,
        }
    else:
        if not db_binary.is_file() or db_binary.stat().st_size == 0:
            raise BenchmarkError(
                f"scale {scale} did not receive direct binary DesignDB output")
        stages["design_db_build"] = {
            "wall_seconds": 0.0, "user_seconds": 0.0, "sys_seconds": 0.0,
            "max_rss_kib": 0, "exit_code": 0,
            "measurement": "integrated_into_verilate",
        }
        manifest = {
            "schema_version": "xdebug.design-db-bundle.v2",
            "format": "binary-v1",
            "database": db_binary.name,
        }
    (obj_dir / "xdebug-design-db.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    stages["simulate"] = command.run(
        "simulate", [str(obj_dir / "Vlarge_trace_top")], scale_dir, env
    )
    fst = scale_dir / "waves.fst"
    artifacts = {
        "rtl_bytes": (scale_dir / "large_trace.sv").stat().st_size,
        "design_db_cpp_bytes": db_cpp.stat().st_size if db_cpp.exists() else 0,
        "design_db_so_bytes": db_so.stat().st_size if db_so.exists() else 0,
        "design_db_binary_bytes": (
            db_binary.stat().st_size if db_binary.exists() else 0),
        "simulator_bytes": (obj_dir / "Vlarge_trace_top").stat().st_size,
        "fst_bytes": fst.stat().st_size,
        "rtl_sha256": sha256(scale_dir / "large_trace.sv"),
        "design_db_so_sha256": sha256(db_so) if db_so.exists() else None,
        "design_db_binary_sha256": (
            sha256(db_binary) if db_binary.exists() else None),
        "fst_sha256": sha256(fst),
    }
    selected_db_size = artifacts[
        "design_db_so_bytes" if design_db_format == "xdd-so"
        else "design_db_binary_bytes"]
    required_artifacts = ["rtl_bytes", "simulator_bytes", "fst_bytes"]
    if design_db_format == "xdd-so":
        required_artifacts.append("design_db_cpp_bytes")
    if any(artifacts[key] <= 0 for key in required_artifacts) or selected_db_size <= 0:
        raise BenchmarkError(f"scale {scale} produced an empty artifact")
    actions = benchmark_actions(
        scale_dir, build / "xdebug-fst", env, metadata, repetitions,
    )
    return {
        "scale": scale,
        "design_db_format": design_db_format,
        "generation_ms": round(generate_ms, 3),
        "metadata": metadata,
        "stages": stages,
        "artifacts": artifacts,
        "runtime": actions,
    }


def write_markdown(result: dict[str, Any], path: Path) -> None:
    rows = [
        "# 大规模 RTL Trace 本地性能结果",
        "",
        "| RTL 行数 | Verilate(s) | 仿真构建(s) | DesignDB(s) | 仿真(s) | FST bytes | Open(ms) | Resolve 首次(ms) | Active chain 中位(ms) |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for item in result["results"]:
        runtime = item["runtime"]
        rows.append(
            "| {scale} | {verilate:.3f} | {sim_build:.3f} | {db:.3f} | {sim:.3f} | {fst} | {opened:.3f} | {resolve:.3f} | {chain:.3f} |".format(
                scale=item["scale"],
                verilate=item["stages"]["verilate"]["wall_seconds"],
                sim_build=item["stages"]["simulator_build"]["wall_seconds"],
                db=item["stages"]["design_db_build"]["wall_seconds"],
                sim=item["stages"]["simulate"]["wall_seconds"],
                fst=item["artifacts"]["fst_bytes"],
                opened=runtime["session_open_ms"],
                resolve=runtime["actions"]["signal.resolve"]["latency"]["first_ms"],
                chain=runtime["actions"]["trace.active_driver_chain"]["latency"]["median_ms"],
            )
        )
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--scales", default=",".join(map(str, DEFAULT_SCALES)))
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--stage-timeout", type=int, default=7200)
    parser.add_argument(
        "--design-db-format", choices=("xdd-so", "binary-v1"),
        default="xdd-so",
    )
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    build = (args.build_dir or (repo / "build")).resolve()
    output = args.output_dir.resolve()
    if not args.output_dir.is_absolute():
        raise BenchmarkError("--output-dir must be an absolute path")
    if output.exists() and any(output.iterdir()):
        raise BenchmarkError("--output-dir must be absent or empty")
    if args.jobs < 1 or args.repetitions < 1:
        raise BenchmarkError("--jobs and --repetitions must be positive")
    output.mkdir(parents=True, exist_ok=True)
    env, environment = tool_environment(repo, build)
    result: dict[str, Any] = {
        "schema_version": "xdebug.large-rtl-trace-benchmark.v1",
        "measurement_mode": "clean_build_os_cache_uncontrolled",
        "environment": environment,
        "scales": list(parse_scales(args.scales)),
        "jobs": args.jobs,
        "action_repetitions": args.repetitions,
        "design_db_format": args.design_db_format,
        "results": [],
    }
    result_path = output / "benchmark-results.json"
    try:
        for scale in result["scales"]:
            print(f"[large-rtl] start {scale} lines", flush=True)
            result["results"].append(
                build_one(
                    scale, output, repo, build, env, args.jobs,
                    args.repetitions, args.stage_timeout, args.design_db_format,
                )
            )
            result_path.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            print(f"[large-rtl] complete {scale} lines", flush=True)
    except Exception as exception:
        result["failure"] = {"type": type(exception).__name__, "message": str(exception)}
        result_path.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        raise
    write_markdown(result, output / "benchmark-results.md")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BenchmarkError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
