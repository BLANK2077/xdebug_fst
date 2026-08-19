from pathlib import Path

import pytest

from tools.check_compat_baseline import parse_args as parse_compat_args
from tools.check_p1_protocol_parity import parse_args as parse_protocol_args
from tools.check_resource_applicability import parse_args as parse_resource_args


def test_original_root_environment_is_shared_by_compat_tools(monkeypatch):
    original_root = "/workspace/original-xverif"
    monkeypatch.setenv("XDEBUG_ORIGINAL_ROOT", original_root)

    assert parse_compat_args([]).original_root == Path(original_root)
    assert parse_protocol_args([]).original_root == Path(original_root)
    resource = parse_resource_args(["--repo-root", "/workspace/xdebug-fst"])
    assert resource.original_root == Path(original_root)


def test_explicit_original_root_overrides_environment(monkeypatch):
    monkeypatch.setenv("XDEBUG_ORIGINAL_ROOT", "/workspace/from-environment")
    arguments = ["--original-root", "/workspace/from-command-line"]

    assert parse_compat_args(arguments).original_root == Path(arguments[-1])
    assert parse_protocol_args(arguments).original_root == Path(arguments[-1])
    resource = parse_resource_args(
        ["--repo-root", "/workspace/xdebug-fst", *arguments]
    )
    assert resource.original_root == Path(arguments[-1])


@pytest.mark.parametrize(
    "parser,arguments",
    [
        (parse_protocol_args, []),
        (parse_resource_args, ["--repo-root", "/workspace/xdebug-fst"]),
    ],
)
def test_required_original_root_tools_fail_closed_without_input(
    monkeypatch, parser, arguments
):
    monkeypatch.delenv("XDEBUG_ORIGINAL_ROOT", raising=False)

    with pytest.raises(SystemExit) as error:
        parser(arguments)

    assert error.value.code == 2
