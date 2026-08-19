from pathlib import Path
import subprocess

from tools.check_no_local_paths import scan_repository


REPO_ROOT = Path(__file__).resolve().parents[1]


def git(repo: Path, *arguments: str) -> None:
    subprocess.run(["git", *arguments], cwd=repo, check=True)


def test_current_repository_has_no_local_paths_or_tracked_codex_config():
    assert scan_repository(REPO_ROOT) == []


def test_audit_checks_text_binary_and_codex_paths(tmp_path):
    git(tmp_path, "init", "-q")
    local_root = b"/" + b"home/developer/work/project"
    (tmp_path / "text.txt").write_bytes(local_root + b"\n")
    (tmp_path / "binary.bin").write_bytes(b"\0prefix:" + local_root + b"\0")
    (tmp_path / ".codex").mkdir()
    (tmp_path / ".codex/config.toml").write_text("[shell_environment_policy.set]\n")
    git(tmp_path, "add", "text.txt", "binary.bin", ".codex/config.toml")

    errors = scan_repository(tmp_path)

    assert any("text.txt: Linux home directory" in error for error in errors)
    assert any("binary.bin: Linux home directory" in error for error in errors)
    assert any("tracked Codex configuration" in error for error in errors)
