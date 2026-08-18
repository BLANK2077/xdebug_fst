import hashlib
import os
import subprocess

import pytest

from tools.prepare_dependencies import (
    PrepareError,
    apply_patch,
    normalize_remote,
    sha256,
    validate_repository,
)


def git(repo, *args):
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()


def test_normalize_remote_accepts_https_and_ssh():
    expected = "https://github.com/ekiwi/wellen"
    assert normalize_remote("https://github.com/ekiwi/wellen.git") == expected
    assert normalize_remote("git@github.com:ekiwi/wellen.git") == expected


def test_sha256_reads_binary_file(tmp_path):
    sample = tmp_path / "sample.bin"
    sample.write_bytes(b"xdebug\x00fst\n")
    assert sha256(sample) == hashlib.sha256(b"xdebug\x00fst\n").hexdigest()


def test_apply_patch_is_rooted_at_shadow_tree(tmp_path):
    source = tmp_path / "shadow"
    source.mkdir()
    (source / "input.txt").write_text("before\n")
    patch = tmp_path / "change.patch"
    patch.write_text(
        "diff --git a/input.txt b/input.txt\n"
        "--- a/input.txt\n"
        "+++ b/input.txt\n"
        "@@ -1 +1 @@\n"
        "-before\n"
        "+after\n"
    )
    apply_patch(source, patch)
    assert (source / "input.txt").read_text() == "after\n"


def test_apply_patch_fails_closed_on_conflict(tmp_path):
    source = tmp_path / "shadow"
    source.mkdir()
    (source / "input.txt").write_text("different\n")
    patch = tmp_path / "change.patch"
    patch.write_text(
        "diff --git a/input.txt b/input.txt\n"
        "--- a/input.txt\n"
        "+++ b/input.txt\n"
        "@@ -1 +1 @@\n"
        "-before\n"
        "+after\n"
    )
    with pytest.raises(PrepareError):
        apply_patch(source, patch)


def test_prepare_cli_requires_home_variables(repo_root, tmp_path):
    env = os.environ.copy()
    env.pop("WELLEN_HOME", None)
    env.pop("VERILATOR_HOME", None)
    result = subprocess.run(
        [
            "python3",
            str(repo_root / "tools/prepare_dependencies.py"),
            "--repo-root",
            str(repo_root),
            "--build-dir",
            str(tmp_path / "build"),
        ],
        text=True,
        capture_output=True,
        env=env,
        check=False,
    )
    assert result.returncode == 2
    assert "缺少环境变量 WELLEN_HOME" in result.stderr


def test_repository_is_an_object_store_not_a_checkout(monkeypatch, tmp_path):
    repository = tmp_path / "wellen"
    repository.mkdir()
    git(repository, "init")
    git(repository, "config", "user.name", "xdebug test")
    git(repository, "config", "user.email", "xdebug@example.invalid")
    (repository / "locked.txt").write_text("locked\n")
    git(repository, "add", "locked.txt")
    git(repository, "commit", "-m", "locked")
    revision = git(repository, "rev-parse", "HEAD")
    tree = git(repository, "rev-parse", "HEAD^{tree}")
    git(repository, "remote", "add", "upstream", "https://github.com/ekiwi/wellen.git")
    git(repository, "checkout", "-b", "unrelated-checkout")
    (repository / "untracked.txt").write_text("dirty\n")
    monkeypatch.setenv("WELLEN_HOME", str(repository))

    resolved = validate_repository(
        "Wellen",
        {
            "repository_env": "WELLEN_HOME",
            "official_url": "https://github.com/ekiwi/wellen.git",
            "revision": revision,
            "tree": tree,
        },
    )

    assert resolved == repository.resolve()
    assert (repository / "untracked.txt").is_file()


def test_repository_missing_locked_object_does_not_fetch(monkeypatch, tmp_path):
    repository = tmp_path / "verilator"
    repository.mkdir()
    git(repository, "init")
    git(repository, "remote", "add", "origin", "https://github.com/verilator/verilator.git")
    monkeypatch.setenv("VERILATOR_HOME", str(repository))

    with pytest.raises(PrepareError, match="禁止自动 fetch"):
        validate_repository(
            "Verilator",
            {
                "repository_env": "VERILATOR_HOME",
                "official_url": "https://github.com/verilator/verilator.git",
                "revision": "1" * 40,
                "tree": "2" * 40,
            },
        )
