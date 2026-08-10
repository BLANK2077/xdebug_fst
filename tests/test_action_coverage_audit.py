from tools.audit_action_coverage import classify, has_xz


def event(request: dict, response: dict) -> dict:
    return {"request": request, "response": response}


def test_xz_classifier_distinguishes_unknown_digits_from_hex_prefix() -> None:
    assert not has_xz({"value": "0xdead_beef"})
    assert not has_xz({"value": "32'hdead_beef"})
    assert has_xz({"value": "4'b10xz"})
    assert has_xz({"bits": "10x0"})
    assert has_xz({"kind": "unknown"})


def test_classifier_records_canonical_invalid_request() -> None:
    dimensions = classify(event(
        {"api_version": "xdebug.v1", "action": "value.at"},
        {
            "ok": False,
            "error": {"code": "INVALID_REQUEST", "error_layer": "schema"},
        },
    ))
    assert dimensions == {"invalid_request"}


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
