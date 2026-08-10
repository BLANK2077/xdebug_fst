from pathlib import Path

from tools.audit_action_coverage import (
    classify,
    has_truncation,
    has_xz,
    load_not_applicable,
)


REPO_ROOT = Path(__file__).resolve().parents[1]


def event(request: dict, response: dict) -> dict:
    return {"request": request, "response": response}


def test_xz_classifier_distinguishes_unknown_digits_from_hex_prefix() -> None:
    assert not has_xz({"value": "0xdead_beef"})
    assert not has_xz({"value": "32'hdead_beef"})
    assert has_xz({"value": "4'b10xz"})
    assert has_xz({"bits": "10x0"})
    assert has_xz({"kind": "unknown"})


def test_truncation_classifier_uses_frozen_contract_fields() -> None:
    assert has_truncation({"summary": {"response_truncated": True}})
    assert has_truncation({
        "summary": {
            "response_truncated": False,
            "truncation_scopes": ["analysis_samples"],
        }
    })
    assert has_truncation({"data": {"termination": "limit"}})
    assert not has_truncation({
        "summary": {
            "response_truncated": False,
            "truncation_scopes": [],
        },
        "data": {
            "limitations": [{"kind": "design_unavailable"}],
        },
    })


def test_classifier_records_canonical_invalid_request() -> None:
    dimensions = classify(event(
        {"api_version": "xdebug.v1", "action": "value.at"},
        {
            "ok": False,
            "error": {"code": "INVALID_REQUEST", "error_layer": "schema"},
        },
    ))
    assert dimensions == {"invalid_request"}


def test_empty_classifier_accepts_zero_total_and_explicit_not_found() -> None:
    zero_total = classify(event(
        {"api_version": "xdebug.v1", "action": "event.find"},
        {
            "ok": True,
            "summary": {"total_count": 0, "returned_count": 0},
            "data": {},
        },
    ))
    assert zero_total == {"empty_result", "success"}

    not_found = classify(event(
        {"api_version": "xdebug.v1", "action": "apb.transaction.cursor"},
        {"ok": True, "summary": {"found": False}, "data": {}},
    ))
    assert not_found == {"empty_result", "success"}

    count_only = classify(event(
        {"api_version": "xdebug.v1", "action": "apb.query"},
        {
            "ok": True,
            "summary": {"total_count": 4, "returned_count": 0},
            "data": {},
        },
    ))
    assert count_only == {"multiple_results", "success"}


def test_classifier_keeps_observed_dimensions_independent() -> None:
    dimensions = classify(event(
        {
            "api_version": "xdebug.v1",
            "action": "trace.x_origin",
            "args": {"time": "0ps"},
            "limits": {"max_nodes": 1},
        },
        {
            "ok": True,
            "summary": {"origin_count": 0},
            "data": {
                "analysis_complete": False,
                "limitations": [{"kind": "limit"}],
                "current": {"value": "1'bx"},
            },
        },
    ))
    assert dimensions == {
        "boundary_time",
        "completeness",
        "empty_result",
        "limits",
        "success",
        "truncation",
        "xz",
    }


def test_pre_dispatch_resource_error_does_not_credit_request_dimensions() -> None:
    dimensions = classify(event(
        {
            "api_version": "xdebug.v1",
            "action": "value.at",
            "args": {"signal": "top.x", "time": "0ps"},
            "limits": {"max_results": 1},
        },
        {
            "ok": False,
            "error": {
                "code": "SESSION_NOT_FOUND",
                "error_layer": "session_manager",
            },
        },
    ))
    assert dimensions == {"resource_missing"}


def test_resource_applicability_manifest_is_explicit_and_valid() -> None:
    applicability = load_not_applicable(
        REPO_ROOT / "tests/coverage/action_applicability.json",
        [
            "actions",
            "batch",
            "expr.normalize",
            "schema",
            "session.gc",
            "session.list",
        ],
    )
    assert set(applicability) == {
        ("actions", "resource_missing"),
        ("batch", "resource_missing"),
        ("expr.normalize", "resource_missing"),
        ("schema", "resource_missing"),
        ("session.gc", "resource_missing"),
        ("session.list", "resource_missing"),
    }
