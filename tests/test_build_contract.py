import os
from pathlib import Path
import subprocess


def run_build(repo_root: Path, build_dir: Path, env: dict) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(repo_root / "tools/build.sh"), "--build-dir", str(build_dir)],
        cwd=repo_root,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )


def test_build_requires_wellen_home(repo_root, tmp_path):
    env = os.environ.copy()
    env.pop("WELLEN_HOME", None)
    env["VERILATOR_HOME"] = str(tmp_path / "verilator")
    result = run_build(repo_root, tmp_path / "build", env)
    assert result.returncode != 0
    assert "必须设置 WELLEN_HOME" in result.stderr


def test_build_rejects_relative_dependency_home(repo_root, tmp_path):
    env = os.environ.copy()
    env["WELLEN_HOME"] = "relative/wellen"
    env["VERILATOR_HOME"] = str(tmp_path / "verilator")
    result = run_build(repo_root, tmp_path / "build", env)
    assert result.returncode == 2
    assert "WELLEN_HOME 必须是绝对路径" in result.stderr


def test_build_rejects_system_compiler_cache(repo_root, tmp_path):
    build_dir = tmp_path / "build"
    build_dir.mkdir()
    (build_dir / "CMakeCache.txt").write_text(
        "CMAKE_C_COMPILER:FILEPATH=/bin/cc\n"
        "CMAKE_CXX_COMPILER:FILEPATH=/bin/c++\n"
    )
    env = os.environ.copy()
    env["WELLEN_HOME"] = str(tmp_path / "wellen")
    env["VERILATOR_HOME"] = str(tmp_path / "verilator")
    env["CC"] = "/bin/cc"
    env["CXX"] = "/bin/c++"
    result = run_build(repo_root, build_dir, env)
    assert result.returncode == 2
    assert "已缓存其他 C 编译器 /bin/cc" in result.stderr
