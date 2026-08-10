from pathlib import Path

from tools.audit_action_coverage import (
    classify,
    has_completeness,
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


def test_completeness_classifier_uses_direct_frozen_contract_fields() -> None:
    assert has_completeness({"summary": {"scan_complete": True}})
    assert has_completeness({"summary": {"value_width_complete": False}})
    assert not has_completeness({
        "data": {"validation": {"analysis_complete": True}},
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

    zero_statistics = classify(event(
        {"api_version": "xdebug.v1", "action": "apb.statistics"},
        {"ok": True, "summary": {"matched_transaction_count": 0}},
    ))
    assert zero_statistics == {"empty_result", "success"}


def test_empty_classifier_accepts_primary_results_not_auxiliary_lists() -> None:
    empty_primary = classify(event(
        {"api_version": "xdebug.v1", "action": "axi.latency_outlier"},
        {
            "ok": True,
            "data": {"outliers": []},
        },
    ))
    assert empty_primary == {"empty_result", "success"}

    empty_diagnostics = classify(event(
        {"api_version": "xdebug.v1", "action": "stream.config.load"},
        {
            "ok": True,
            "data": {"issues": [], "recommended_actions": []},
        },
    ))
    assert empty_diagnostics == {"success"}

    empty_session_cleanup = classify(event(
        {"api_version": "xdebug.v1", "action": "session.gc"},
        {
            "ok": True,
            "data": {"removed": [], "kept_sessions": []},
        },
    ))
    assert empty_session_cleanup == {"empty_result", "success"}

    nested_validation = classify(event(
        {"api_version": "xdebug.v1", "action": "axi.config.load"},
        {
            "ok": True,
            "summary": {"status": "loaded"},
            "data": {
                "validation": {
                    "signals": [{"path": "a"}, {"path": "b"}],
                },
            },
        },
    ))
    assert nested_validation == {"success"}


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


def test_timeout_watchdog_does_not_credit_result_limit_dimension() -> None:
    dimensions = classify(event(
        {
            "api_version": "xdebug.v1",
            "action": "signal.resolve",
            "args": {"signal": "top.x"},
            "limits": {"timeout_ms": 1000},
        },
        {
            "ok": True,
            "summary": {"status": "found"},
            "data": {"matches": [{"signal": "top.x"}]},
        },
    ))
    assert dimensions == {"success"}


def test_address_zero_range_does_not_credit_time_boundary_dimension() -> None:
    address_only = classify(event(
        {
            "api_version": "xdebug.v1", "action": "axi.query",
            "args": {
                "name": "axi0", "direction": "read",
                "address": {"mode": "range", "begin": "0", "end": "10"},
            },
        },
        {"ok": True, "summary": {"total_count": 1}, "data": {}},
    ))
    assert address_only == {"success"}

    temporal = classify(event(
        {
            "api_version": "xdebug.v1", "action": "axi.query",
            "args": {
                "name": "axi0", "direction": "read",
                "time_range": {"begin": "0ns", "end": "10ns"},
            },
        },
        {"ok": True, "summary": {"total_count": 1}, "data": {}},
    ))
    assert temporal == {"boundary_time", "success"}


def test_action_applicability_manifest_is_explicit_and_valid() -> None:
    empty_actions = {
        "apb.config.load", "axi.config.load", "event.config.load",
        "expr.eval_at", "expr.normalize", "list.add", "list.delete",
        "schema", "session.doctor", "session.open",
        "signal.canonicalize", "stream.config.get",
        "stream.config.load", "stream.describe",
        "waveform.cursor.delete", "waveform.cursor.get",
        "waveform.cursor.set", "waveform.cursor.use",
        "batch", "list.load", "nwave.rc.generate", "session.close",
        "session.kill", "signal.resolve", "signal.xz_verify",
        "trace.active_driver_chain", "value.at", "verify.conditions",
    }
    resource_entries = {
        ("actions", "resource_missing"),
        ("batch", "resource_missing"),
        ("expr.normalize", "resource_missing"),
        ("schema", "resource_missing"),
        ("session.gc", "resource_missing"),
        ("session.list", "resource_missing"),
    }
    empty_entries = {
        (action, "empty_result") for action in empty_actions
    }
    truncation_actions = {
        "actions", "apb.config.list", "apb.config.load",
        "axi.config.list", "axi.config.load", "batch",
        "event.config.load", "expr.eval_at", "expr.normalize",
        "list.add", "list.create", "list.delete", "list.first_change",
        "list.load", "list.show", "list.validate", "nwave.rc.generate",
        "schema", "session.close", "session.doctor", "session.gc",
        "session.kill", "session.list", "session.open",
        "signal.canonicalize", "signal.resolve", "signal.xz_verify",
        "stream.config.get", "stream.config.list",
        "stream.config.load", "stream.describe", "value.at",
        "verify.conditions", "waveform.cursor.delete", "waveform.cursor.get",
        "waveform.cursor.list", "waveform.cursor.set", "waveform.cursor.use",
    }
    truncation_entries = {
        (action, "truncation") for action in truncation_actions
    }
    limit_not_applicable_actions = {
        "actions", "apb.config.list", "apb.config.load", "apb.statistics",
        "apb.transaction.cursor", "axi.config.list", "axi.config.load",
        "axi.export", "axi.statistics", "axi.transaction.cursor", "batch",
        "event.config.load", "expr.eval_at", "list.add", "list.create",
        "list.delete", "list.first_change", "list.load", "list.show",
        "list.validate", "nwave.rc.generate", "schema", "scope.roots",
        "session.close", "session.doctor", "session.gc", "session.kill",
        "session.list", "session.open", "signal.canonicalize",
        "signal.resolve", "signal.stability", "signal.xz_verify",
        "stream.config.get", "stream.config.list", "stream.config.load",
        "stream.describe", "value.at", "verify.conditions",
        "waveform.cursor.delete", "waveform.cursor.get",
        "waveform.cursor.list", "waveform.cursor.set", "waveform.cursor.use",
    }
    limit_entries = {
        (action, "limits") for action in limit_not_applicable_actions
    }
    boundary_not_applicable_actions = {
        "actions", "apb.config.list", "apb.config.load", "apb.query",
        "apb.statistics", "apb.transaction.cursor", "axi.analysis",
        "axi.config.list", "axi.config.load", "axi.statistics",
        "axi.transaction.cursor", "batch", "event.config.list",
        "event.config.load", "expr.normalize", "list.add", "list.create",
        "list.delete", "list.load", "list.show", "list.validate",
        "nwave.rc.generate", "schema", "scope.list", "scope.roots",
        "session.close", "session.doctor", "session.gc", "session.kill",
        "session.list", "session.open", "signal.canonicalize",
        "signal.resolve", "stream.config.get", "stream.config.list",
        "stream.config.load", "stream.describe", "trace.driver",
        "trace.load", "waveform.cursor.delete", "waveform.cursor.get",
        "waveform.cursor.list", "waveform.cursor.use",
    }
    boundary_entries = {
        (action, "boundary_time")
        for action in boundary_not_applicable_actions
    }
    multiple_not_applicable_actions = {
        "apb.config.load", "axi.config.load", "event.config.load",
        "expr.eval_at", "expr.normalize", "list.add", "list.delete",
        "list.load", "schema", "session.doctor", "session.open",
        "signal.canonicalize", "signal.resolve", "stream.config.get",
        "stream.config.load", "stream.describe", "waveform.cursor.delete",
        "waveform.cursor.get", "waveform.cursor.set", "waveform.cursor.use",
    }
    multiple_entries = {
        (action, "multiple_results")
        for action in multiple_not_applicable_actions
    }
    completeness_not_applicable_actions = {
        "actions", "apb.config.list", "apb.config.load", "axi.config.list",
        "axi.config.load", "batch", "event.config.load", "expr.normalize",
        "list.add", "list.create", "list.delete", "list.load", "list.show",
        "list.validate", "nwave.rc.generate", "schema", "session.close",
        "session.doctor", "session.gc", "session.kill", "session.list",
        "session.open", "signal.canonicalize", "stream.config.get",
        "stream.config.list", "stream.config.load", "stream.describe",
        "waveform.cursor.delete", "waveform.cursor.get", "waveform.cursor.list",
        "waveform.cursor.set", "waveform.cursor.use",
    }
    completeness_entries = {
        (action, "completeness")
        for action in completeness_not_applicable_actions
    }
    manifest_actions = {
        action
        for action, _dimension in (
            resource_entries | empty_entries | truncation_entries |
            limit_entries | boundary_entries | multiple_entries |
            completeness_entries
        )
    }
    applicability = load_not_applicable(
        REPO_ROOT / "tests/coverage/action_applicability.json",
        sorted(manifest_actions),
    )
    assert set(applicability) == (
        resource_entries | empty_entries | truncation_entries |
        limit_entries | boundary_entries | multiple_entries |
        completeness_entries
    )
