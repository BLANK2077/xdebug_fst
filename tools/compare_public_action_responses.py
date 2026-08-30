#!/usr/bin/env python3
"""Compare normalized public xdebug Action observations from FSDB and FST.

The comparator never reads waveform binaries.  It compares request/response
JSON bundles, preserves ordered arrays (including same-time deltas), and only
permits ignored or rewritten fields that are named with an exact JSON pointer
and a non-empty audit reason in the checked-in plan.
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any


PLAN_SCHEMA = "xdebug.public-action-differential-plan.v1"
BUNDLE_SCHEMA = "xdebug.public-action-observation-bundle.v1"
REPORT_SCHEMA = "xdebug.public-action-differential-report.v1"

TIME_RE = re.compile(
    r"^(?P<number>[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))\s*"
    r"(?P<unit>s|ms|us|ns|ps|fs|[munpf])$",
    re.IGNORECASE,
)
SV_LITERAL_RE = re.compile(
    r"^(?P<width>[0-9]+)'(?P<signed>s?)(?P<base>[bBoOdDhH])"
    r"(?P<digits>[0-9a-fA-F_xXzZ?]+)$"
)
TIME_FS = {
    "s": Decimal(10**15),
    "m": Decimal(10**12),
    "ms": Decimal(10**12),
    "u": Decimal(10**9),
    "us": Decimal(10**9),
    "n": Decimal(10**6),
    "ns": Decimal(10**6),
    "p": Decimal(10**3),
    "ps": Decimal(10**3),
    "f": Decimal(1),
    "fs": Decimal(1),
}
TIME_FIELD_NAMES = {
    "time",
    "times",
    "timescale",
    "begin",
    "end",
    "start",
    "stop",
    "duration",
    "latency",
    "period",
    "active_time",
    "before_time",
    "after_time",
    "continue_time",
    "query_time",
    "start_time",
    "end_time",
    "value_time",
    "x_onset_time",
}
LOGIC_FIELD_NAMES = {
    "value",
    "values",
    "id",
    "address",
    "response",
    "data",
    "payload",
    "before_value",
    "after_value",
    "current_value",
    "expected_value",
}


class DifferentialError(RuntimeError):
    """Raised when inputs or normalization policy are not auditable."""


@dataclass(frozen=True)
class Difference:
    pointer: str
    kind: str
    original: Any
    current: Any

    def as_json(self) -> dict:
        return {
            "pointer": self.pointer,
            "kind": self.kind,
            "original": self.original,
            "current": self.current,
        }


def canonical_json(value: object) -> str:
    return json.dumps(
        value, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False
    ) + "\n"


def reject_nonfinite(value: str):
    raise DifferentialError(f"non-finite JSON number is forbidden: {value}")


def load_json(path: Path) -> dict:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), parse_constant=reject_nonfinite
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DifferentialError(f"cannot load JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise DifferentialError(f"top-level JSON must be an object: {path}")
    return value


def ensure_output_within_repo(repo_root: Path, output: Path) -> Path:
    root = repo_root.resolve()
    resolved = output.resolve(strict=False)
    if resolved != root and root not in resolved.parents:
        raise DifferentialError(
            f"report escapes the only writable repository: {output}"
        )
    return resolved


def pointer_tokens(pointer: str) -> list[str]:
    if pointer == "":
        return []
    if not pointer.startswith("/"):
        raise DifferentialError(f"JSON pointer must start with '/': {pointer!r}")
    return [
        token.replace("~1", "/").replace("~0", "~")
        for token in pointer[1:].split("/")
    ]


def _index(container: Any, token: str, pointer: str):
    if isinstance(container, dict):
        if token not in container:
            raise DifferentialError(f"JSON pointer does not exist: {pointer}")
        return token
    if isinstance(container, list):
        if not re.fullmatch(r"0|[1-9][0-9]*", token):
            raise DifferentialError(f"invalid array index in JSON pointer: {pointer}")
        index = int(token)
        if index >= len(container):
            raise DifferentialError(f"JSON pointer does not exist: {pointer}")
        return index
    raise DifferentialError(f"JSON pointer crosses a scalar: {pointer}")


def pointer_get(document: Any, pointer: str) -> Any:
    value = document
    for token in pointer_tokens(pointer):
        value = value[_index(value, token, pointer)]
    return value


def pointer_parent(document: Any, pointer: str) -> tuple[Any, Any]:
    tokens = pointer_tokens(pointer)
    if not tokens:
        raise DifferentialError("normalization cannot replace or ignore the document root")
    parent = document
    for token in tokens[:-1]:
        parent = parent[_index(parent, token, pointer)]
    return parent, _index(parent, tokens[-1], pointer)


def pointer_set(document: Any, pointer: str, value: Any) -> None:
    parent, key = pointer_parent(document, pointer)
    parent[key] = value


def pointer_delete(document: Any, pointer: str) -> None:
    parent, key = pointer_parent(document, pointer)
    if isinstance(parent, list):
        parent.pop(key)
    else:
        del parent[key]


def escape_pointer_token(token: str) -> str:
    return token.replace("~", "~0").replace("/", "~1")


def child_pointer(pointer: str, token: str | int) -> str:
    encoded = escape_pointer_token(str(token))
    return f"{pointer}/{encoded}" if pointer else f"/{encoded}"


def canonical_time(value: str) -> dict | None:
    match = TIME_RE.fullmatch(value)
    if not match:
        return None
    try:
        fs = Decimal(match.group("number")) * TIME_FS[match.group("unit").lower()]
    except (InvalidOperation, KeyError) as error:
        raise DifferentialError(f"invalid time value: {value!r}") from error
    integral = fs.to_integral_value()
    if fs != integral:
        raise DifferentialError(f"time is not representable in integer femtoseconds: {value!r}")
    return {"$xdebug_time_fs": str(int(integral))}


def _expand_logic_digit(digit: str, width: int) -> str:
    upper = digit.upper()
    if upper in {"X", "Z", "?"}:
        return ("Z" if upper == "?" else upper) * width
    return format(int(upper, 16), f"0{width}b")


def canonical_sv_literal(value: str) -> dict | None:
    match = SV_LITERAL_RE.fullmatch(value)
    if not match:
        return None
    width = int(match.group("width"))
    if width <= 0:
        raise DifferentialError(f"zero-width SV literal is forbidden: {value!r}")
    base = match.group("base").lower()
    digits = match.group("digits").replace("_", "")
    signed = bool(match.group("signed"))
    if base == "d":
        if not re.fullmatch(r"[0-9]+|[xX]|[zZ?]", digits):
            raise DifferentialError(f"invalid decimal SV literal: {value!r}")
        if re.fullmatch(r"[xXzZ?]", digits):
            fill = digits[0].upper().replace("?", "Z")
            bits = fill * width
        else:
            try:
                number = int(digits, 10)
            except ValueError as error:
                raise DifferentialError(f"invalid decimal SV literal: {value!r}") from error
            if number >= 1 << width:
                number &= (1 << width) - 1
            bits = format(number, f"0{width}b")
    else:
        allowed = {
            "b": r"[01xXzZ?]+",
            "o": r"[0-7xXzZ?]+",
            "h": r"[0-9a-fA-FxXzZ?]+",
        }[base]
        if not re.fullmatch(allowed, digits):
            raise DifferentialError(f"invalid base-{base} SV literal: {value!r}")
        digit_width = {"b": 1, "o": 3, "h": 4}[base]
        bits = "".join(_expand_logic_digit(digit, digit_width) for digit in digits)
        fill = bits[0] if bits and bits[0] in {"X", "Z"} else "0"
        bits = (fill * max(0, width - len(bits)) + bits)[-width:]
    return {
        "$xdebug_logic": {
            "width": width,
            "bits": bits,
            "signed": signed,
        }
    }


def canonicalize(
    value: Any,
    stats: dict[str, int],
    parent_key: str | None = None,
    parent: dict | None = None,
) -> Any:
    if isinstance(value, dict):
        return {
            key: canonicalize(item, stats, key, value)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [canonicalize(item, stats, parent_key, parent) for item in value]
    if isinstance(value, str):
        typed_text = (
            parent_key == "value"
            and isinstance(parent, dict)
            and parent.get("kind", parent.get("type")) in {"string", "event"}
        )
        if not typed_text and (
            parent_key in TIME_FIELD_NAMES
            or bool(parent_key and parent_key.endswith("_time"))
        ):
            time_value = canonical_time(value)
            if time_value is not None:
                stats["canonical_time_count"] += 1
                return time_value
        if not typed_text and parent_key in LOGIC_FIELD_NAMES:
            logic_value = canonical_sv_literal(value)
            if logic_value is not None:
                stats["canonical_logic_count"] += 1
                return logic_value
        if parent_key in {"bits", "x_mask", "z_mask"} and re.fullmatch(
            r"[01xXzZ?_]+", value
        ):
            stats["canonical_raw_bits_count"] += 1
            return value.replace("_", "").upper()
    return value


def validate_bundle(bundle: dict, expected_side: str) -> dict[str, dict]:
    if bundle.get("schema_version") != BUNDLE_SCHEMA:
        raise DifferentialError(f"{expected_side} bundle has wrong schema_version")
    if bundle.get("side") != expected_side:
        raise DifferentialError(
            f"bundle side must be {expected_side!r}, got {bundle.get('side')!r}"
        )
    expected_format = "fsdb" if expected_side == "original" else "fst"
    if bundle.get("waveform_format") != expected_format:
        raise DifferentialError(
            f"{expected_side} waveform_format must be {expected_format!r}"
        )
    observations = bundle.get("observations")
    if not isinstance(observations, list) or not observations:
        raise DifferentialError(f"{expected_side} bundle observations must be non-empty")
    result = {}
    for observation in observations:
        if not isinstance(observation, dict):
            raise DifferentialError(f"{expected_side} observation must be an object")
        observation_id = observation.get("observation_id")
        action = observation.get("action")
        request = observation.get("request")
        response = observation.get("response")
        if not isinstance(observation_id, str) or not observation_id:
            raise DifferentialError(f"{expected_side} observation_id must be non-empty")
        if observation_id in result:
            raise DifferentialError(f"duplicate observation_id: {observation_id}")
        if not isinstance(action, str) or not action:
            raise DifferentialError(f"{observation_id}: action must be non-empty")
        if not isinstance(request, dict) or request.get("action") != action:
            raise DifferentialError(f"{observation_id}: request action mismatch")
        if request.get("api_version") != "xdebug.v1":
            raise DifferentialError(f"{observation_id}: request api_version mismatch")
        if not isinstance(response, dict) or response.get("action") != action:
            raise DifferentialError(f"{observation_id}: response action mismatch")
        if response.get("api_version") != "xdebug.v1":
            raise DifferentialError(f"{observation_id}: response api_version mismatch")
        if not isinstance(response.get("ok"), bool):
            raise DifferentialError(f"{observation_id}: response ok must be boolean")
        result[observation_id] = observation
    return result


def validate_plan(plan: dict) -> dict[str, dict]:
    if plan.get("schema_version") != PLAN_SCHEMA:
        raise DifferentialError("differential plan has wrong schema_version")
    if not isinstance(plan.get("plan_id"), str) or not plan["plan_id"]:
        raise DifferentialError("differential plan_id must be non-empty")
    observations = plan.get("observations")
    if not isinstance(observations, list) or not observations:
        raise DifferentialError("differential plan observations must be non-empty")
    result = {}
    for item in observations:
        if not isinstance(item, dict):
            raise DifferentialError("plan observation must be an object")
        observation_id = item.get("observation_id")
        if not isinstance(observation_id, str) or not observation_id:
            raise DifferentialError("plan observation_id must be non-empty")
        if observation_id in result:
            raise DifferentialError(f"duplicate plan observation_id: {observation_id}")
        if not isinstance(item.get("action"), str) or not item["action"]:
            raise DifferentialError(f"{observation_id}: plan action must be non-empty")
        for ignored in item.get("ignore", []):
            if not isinstance(ignored, dict) or not ignored.get("pointer") or not ignored.get("reason"):
                raise DifferentialError(
                    f"{observation_id}: every ignored field needs exact pointer and reason"
                )
        for rewrite in item.get("rewrites", []):
            if (
                not isinstance(rewrite, dict)
                or rewrite.get("side") not in {"original", "current"}
                or not rewrite.get("pointer")
                or "from" not in rewrite
                or "to" not in rewrite
                or not rewrite.get("reason")
            ):
                raise DifferentialError(
                    f"{observation_id}: every rewrite needs side/pointer/from/to/reason"
                )
        for requirement in item.get("requirements", []):
            if (
                not isinstance(requirement, dict)
                or not requirement.get("pointer")
                or "equals" not in requirement
                or not requirement.get("reason")
            ):
                raise DifferentialError(
                    f"{observation_id}: every requirement needs pointer/equals/reason"
                )
        result[observation_id] = item
    return result


def compare_json(original: Any, current: Any, pointer: str = "") -> list[Difference]:
    if type(original) is not type(current):
        return [Difference(pointer, "type_mismatch", original, current)]
    if isinstance(original, dict):
        differences = []
        for key in sorted(set(original) | set(current)):
            path = child_pointer(pointer, key)
            if key not in original:
                differences.append(Difference(path, "extra_in_current", None, current[key]))
            elif key not in current:
                differences.append(Difference(path, "missing_in_current", original[key], None))
            else:
                differences.extend(compare_json(original[key], current[key], path))
        return differences
    if isinstance(original, list):
        differences = []
        if len(original) != len(current):
            differences.append(
                Difference(pointer, "list_length_mismatch", len(original), len(current))
            )
        for index, (left, right) in enumerate(zip(original, current)):
            differences.extend(compare_json(left, right, child_pointer(pointer, index)))
        return differences
    if original != current:
        return [Difference(pointer, "value_mismatch", original, current)]
    return []


def _normalization_stats() -> dict[str, int]:
    return {
        "canonical_time_count": 0,
        "canonical_logic_count": 0,
        "canonical_raw_bits_count": 0,
        "ignored_field_count": 0,
        "explicit_rewrite_count": 0,
    }


def normalize_observation(
    observation: dict,
    plan_item: dict,
    side: str,
) -> tuple[dict, dict[str, int]]:
    document = copy.deepcopy(observation)
    stats = _normalization_stats()
    for rewrite in plan_item.get("rewrites", []):
        if rewrite["side"] != side:
            continue
        actual = pointer_get(document, rewrite["pointer"])
        if actual != rewrite["from"]:
            raise DifferentialError(
                f"{observation['observation_id']}: {side} rewrite precondition failed "
                f"at {rewrite['pointer']}: expected {rewrite['from']!r}, got {actual!r}"
            )
        pointer_set(document, rewrite["pointer"], copy.deepcopy(rewrite["to"]))
        stats["explicit_rewrite_count"] += 1
    for ignored in plan_item.get("ignore", []):
        pointer_get(document, ignored["pointer"])
        pointer_delete(document, ignored["pointer"])
        stats["ignored_field_count"] += 1
    document = canonicalize(document, stats)
    for requirement in plan_item.get("requirements", []):
        actual = pointer_get(document, requirement["pointer"])
        expected_stats = _normalization_stats()
        expected = canonicalize(copy.deepcopy(requirement["equals"]), expected_stats)
        if actual != expected:
            raise DifferentialError(
                f"{observation['observation_id']}: {side} completeness requirement failed "
                f"at {requirement['pointer']}: expected {expected!r}, got {actual!r}"
            )
    return document, stats


def compare_bundles(plan: dict, original: dict, current: dict) -> dict:
    plan_items = validate_plan(plan)
    original_items = validate_bundle(original, "original")
    current_items = validate_bundle(current, "current")
    expected_ids = set(plan_items)
    if set(original_items) != expected_ids:
        raise DifferentialError(
            "original observation IDs differ from plan: "
            f"expected {sorted(expected_ids)!r}, got {sorted(original_items)!r}"
        )
    if set(current_items) != expected_ids:
        raise DifferentialError(
            "current observation IDs differ from plan: "
            f"expected {sorted(expected_ids)!r}, got {sorted(current_items)!r}"
        )

    reports = []
    difference_count = 0
    for observation_id in [item["observation_id"] for item in plan["observations"]]:
        plan_item = plan_items[observation_id]
        left = original_items[observation_id]
        right = current_items[observation_id]
        if left["action"] != plan_item["action"] or right["action"] != plan_item["action"]:
            raise DifferentialError(f"{observation_id}: Action differs from plan")
        normalized_left, left_stats = normalize_observation(left, plan_item, "original")
        normalized_right, right_stats = normalize_observation(right, plan_item, "current")
        differences = compare_json(normalized_left, normalized_right)
        difference_count += len(differences)
        reports.append({
            "observation_id": observation_id,
            "action": plan_item["action"],
            "equivalent": not differences,
            "difference_count": len(differences),
            "differences": [difference.as_json() for difference in differences],
            "normalization": {
                "original": left_stats,
                "current": right_stats,
                "ignored_fields": plan_item.get("ignore", []),
                "explicit_rewrites": plan_item.get("rewrites", []),
            },
        })
    return {
        "schema_version": REPORT_SCHEMA,
        "plan_id": plan["plan_id"],
        "equivalent": difference_count == 0,
        "difference_count": difference_count,
        "observation_count": len(reports),
        "observations": reports,
    }


def write_atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent,
        prefix=f".{path.name}.", delete=False,
    ) as stream:
        stream.write(content)
        temporary = Path(stream.name)
    os.replace(temporary, path)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    repo_root = Path(__file__).resolve().parents[1]
    report = compare_bundles(
        load_json(args.plan), load_json(args.original), load_json(args.current)
    )
    content = canonical_json(report)
    if args.report is not None:
        output = args.report if args.report.is_absolute() else repo_root / args.report
        output = ensure_output_within_repo(repo_root, output)
        write_atomic(output, content)
    else:
        sys.stdout.write(content)
    return 0 if report["equivalent"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except DifferentialError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
