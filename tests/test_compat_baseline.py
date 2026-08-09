from pathlib import Path

from tools.check_compat_baseline import verify, verify_frozen_files


REPO_ROOT = Path(__file__).resolve().parents[1]


def test_frozen_catalog_and_all_schemas_are_self_consistent():
    assert verify_frozen_files(REPO_ROOT) == []


def test_dependency_lock_matches_local_repositories_and_abi_headers():
    assert verify(REPO_ROOT) == []
