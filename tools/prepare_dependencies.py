#!/usr/bin/env python3
"""从锁定 Git 对象准备 Wellen/Verilator 影子源码，不修改 HOME 仓库。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile


class PrepareError(RuntimeError):
    pass


def run(args: list[str], *, cwd: Path, stdout=None) -> str:
    proc = subprocess.run(
        args,
        cwd=cwd,
        check=False,
        text=stdout is None,
        stdout=stdout if stdout is not None else subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode:
        stderr = proc.stderr.decode() if isinstance(proc.stderr, bytes) else proc.stderr
        raise PrepareError(f"命令失败 ({' '.join(args)}): {stderr.strip()}")
    return "" if stdout is not None else proc.stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_remote(url: str) -> str:
    value = url.strip().removesuffix("/").removesuffix(".git")
    if value.startswith("git@github.com:"):
        value = "https://github.com/" + value.removeprefix("git@github.com:")
    return value.lower()


def validate_repository(name: str, config: dict) -> Path:
    env_name = config["repository_env"]
    raw = os.environ.get(env_name, "")
    if not raw:
        raise PrepareError(f"缺少环境变量 {env_name}")
    repo = Path(raw)
    if not repo.is_absolute():
        raise PrepareError(f"{env_name} 必须是绝对路径: {raw}")
    repo = repo.resolve()
    if run(["git", "rev-parse", "--is-inside-work-tree"], cwd=repo) != "true":
        raise PrepareError(f"{env_name} 不是 Git 工作树: {repo}")

    remotes = run(["git", "remote", "-v"], cwd=repo).splitlines()
    expected = normalize_remote(config["official_url"])
    if not any(expected in normalize_remote(line.split()[1]) for line in remotes if len(line.split()) >= 2):
        raise PrepareError(f"{name} 未配置官方远端 {config['official_url']}: {repo}")

    revision = config["revision"]
    try:
        actual_revision = run(["git", "rev-parse", f"{revision}^{{commit}}"], cwd=repo)
    except PrepareError as error:
        raise PrepareError(f"{name} 缺少锁定对象 {revision}；禁止自动 fetch") from error
    if actual_revision != revision:
        raise PrepareError(f"{name} revision 解析不一致: {actual_revision}")
    tree = run(["git", "rev-parse", f"{revision}^{{tree}}"], cwd=repo)
    if tree != config["tree"]:
        raise PrepareError(f"{name} tree 不一致: 期望 {config['tree']}，实际 {tree}")
    return repo


def archive(repo: Path, revision: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix="xdebug-archive-", suffix=".tar") as temp:
        run(["git", "archive", "--format=tar", revision], cwd=repo, stdout=temp)
        temp.flush()
        with tarfile.open(temp.name) as source:
            source.extractall(destination, filter="data")


def verify_file(repo_root: Path, relative: str, expected: str) -> Path:
    path = repo_root / relative
    actual = sha256(path)
    if actual != expected:
        raise PrepareError(f"文件哈希不一致 {relative}: 期望 {expected}，实际 {actual}")
    return path


def apply_patch(source: Path, patch: Path) -> None:
    # 影子目录位于本仓库 build/ 下，git apply 会错误地发现父目录的 .git 并
    # 把当前目录当成 prefix。POSIX patch 只以 cwd 为根，适合无 .git 的归档树。
    run(["patch", "--batch", "--forward", "--dry-run", "-p1", "-i", str(patch)], cwd=source)
    run(["patch", "--batch", "--forward", "-p1", "-i", str(patch)], cwd=source)


def fingerprint(lock: dict, repo_root: Path, wellen_home: Path, verilator_home: Path) -> str:
    payload = {
        "lock": lock,
        "wellen_capi": sha256(repo_root / "wellen_capi/src/lib.rs"),
        "wellenx_capi": sha256(repo_root / "wellenx_capi/src/lib.rs"),
        "wellenx_manifest": sha256(repo_root / "wellenx_capi/Cargo.toml"),
        "cargo_lock": sha256(repo_root / lock["rust_workspace"]["cargo_lock"]),
        "wellen_home": str(wellen_home),
        "verilator_home": str(verilator_home),
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def prepare(repo_root: Path, build_dir: Path) -> dict:
    lock = json.loads((repo_root / "dependencies.lock.json").read_text())
    if lock.get("lock_version") != 2:
        raise PrepareError("dependencies.lock.json 的 lock_version 必须为 2")

    wellen_cfg = lock["wellen"]
    verilator_cfg = lock["verilator"]
    wellen_home = validate_repository("Wellen", wellen_cfg)
    verilator_home = validate_repository("Verilator", verilator_cfg)
    wellen_patch = verify_file(
        repo_root, wellen_cfg["workspace_patch"], wellen_cfg["workspace_patch_sha256"]
    )
    verilator_patch = verify_file(
        repo_root, verilator_cfg["patch"], verilator_cfg["patch_sha256"]
    )
    cargo_lock = verify_file(
        repo_root,
        lock["rust_workspace"]["cargo_lock"],
        lock["rust_workspace"]["cargo_lock_sha256"],
    )

    deps_dir = build_dir / "_deps"
    stamp_path = deps_dir / ".xdebug-dependencies.json"
    wanted = fingerprint(lock, repo_root, wellen_home, verilator_home)
    if stamp_path.is_file():
        previous = json.loads(stamp_path.read_text())
        if previous.get("fingerprint") == wanted:
            return previous

    staging = deps_dir / ".staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    try:
        wellen_source = staging / "wellen-src"
        verilator_source = staging / "verilator-src"
        archive(wellen_home, wellen_cfg["revision"], wellen_source)
        archive(verilator_home, verilator_cfg["revision"], verilator_source)
        apply_patch(wellen_source, wellen_patch)
        shutil.copytree(repo_root / "wellen_capi", wellen_source / "wellen_capi")
        shutil.copytree(repo_root / "wellenx_capi", wellen_source / "wellenx_capi")
        shutil.copy2(cargo_lock, wellen_source / "Cargo.lock")
        apply_patch(verilator_source, verilator_patch)

        if sha256(wellen_source / "wellen_capi/include/wellen_capi.h") != wellen_cfg["capi_header_sha256"]:
            raise PrepareError("影子 Wellen C ABI header 哈希不一致")
        if sha256(verilator_source / "include/xdd_api.h") != verilator_cfg["xdd_header_sha256"]:
            raise PrepareError("影子 Verilator XDD header 哈希不一致")

        deps_dir.mkdir(parents=True, exist_ok=True)
        for name in ("wellen-src", "verilator-src"):
            target = deps_dir / name
            if target.exists():
                shutil.rmtree(target)
            (staging / name).replace(target)
    finally:
        if staging.exists():
            shutil.rmtree(staging)

    resolved = {
        "fingerprint": wanted,
        "bundle_version": lock["bundle_version"],
        "wellen": {
            "home": str(wellen_home),
            "revision": wellen_cfg["revision"],
            "tree": wellen_cfg["tree"],
            "version": wellen_cfg["version"],
            "source": str((deps_dir / "wellen-src").resolve()),
        },
        "verilator": {
            "home": str(verilator_home),
            "revision": verilator_cfg["revision"],
            "tree": verilator_cfg["tree"],
            "patch_sha256": verilator_cfg["patch_sha256"],
            "patchset_version": verilator_cfg["patchset_version"],
            "version": verilator_cfg["version"],
            "source": str((deps_dir / "verilator-src").resolve()),
        },
    }
    stamp_path.write_text(json.dumps(resolved, indent=2, sort_keys=True) + "\n")
    (build_dir / "dependencies.resolved.json").write_text(
        json.dumps(resolved, indent=2, sort_keys=True) + "\n"
    )
    return resolved


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        resolved = prepare(args.repo_root.resolve(), args.build_dir.resolve())
    except (PrepareError, OSError, json.JSONDecodeError) as error:
        print(f"依赖准备失败: {error}", file=sys.stderr)
        return 2
    print(json.dumps(resolved, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
