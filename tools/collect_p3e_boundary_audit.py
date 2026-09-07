#!/usr/bin/env python3
"""生成并校验 P3-E SVA/XIF/cross-fixture 裁定边界证据。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.build_rtl_wave_semantic_matrix import (
    MatrixError,
    action_contract,
    asset_lookup,
    consumer_catalog,
    fixture_actions,
    fixture_consumers,
    public_actions,
    validate_frozen_file,
)


GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
SCHEMA_VERSION = "xdebug.p3e-boundary-audit.v1"
DEFAULT_OUTPUT = Path(
    "tests/data/rtl_wave_differential/p3e-boundary.audit.json"
)
SVA_FIXTURE = "xdebug.npi_fsdb_sva"
XIF_FIXTURE = "xdebug.xif_event"
CROSS_FIXTURE = "original.cross_fixture"
SVA_CONSUMER = "xdebug/tests/synthetic/test_npi_fsdb_sva.py"
XIF_CONSUMERS = {
    "xdebug/tests/native_xout/test_native_xout_all.py",
    "xdebug/tests/synthetic/test_xif_event.py",
}
SVA_PRIVATE_FIELD_GROUPS = {
    "assertion_identity": [
        "assertion_type",
        "canonical_path",
        "derived_path",
        "full_name",
        "object_type",
    ],
    "assertion_event": [
        "begin_time_raw",
        "duration_ok",
        "end_time_raw",
        "native_value_format",
        "sequence_number",
        "sequence_number_ok",
        "time_ok",
        "time_raw",
        "value_format_ok",
        "value_ok",
    ],
    "design_ast": [
        "clocking_event",
        "decompile",
        "disable_condition",
        "expression_tree",
        "fail_statement",
        "file",
        "line",
        "property",
        "property_declaration",
        "property_expression",
        "references",
        "sequence_declarations",
    ],
    "join_diagnostics": [
        "ambiguous_join_count",
        "design_assertion_count",
        "design_indices",
        "exact_join_count",
        "join_policy",
        "local_name_candidates",
        "unmatched_join_count",
        "wave_assertion_count",
    ],
}
SVA_SCHEMA_TOKENS = [
    "assertion",
    "decompile",
    "design_assertions",
    "join_policy",
    "property_declaration",
    "sequence_declarations",
    "sva",
    "wave_assertions",
]
XIF_REQUIRED_OBSERVATIONS = [
    "direct_packed_struct_fields",
    "rdy_flow_control",
    "bp_flow_control",
    "none_flow_control",
    "paired_master_slave",
    "xz_unknown_expression",
    "relational_expression",
    "event_find_line_limit",
    "event_find_flat_xout",
    "event_export_full_set",
    "unknown_alias_error",
    "value_at_struct_member",
]


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _fixture_assets(
    original_assets: dict[str, dict[str, Any]], fixture_id: str
) -> list[dict[str, Any]]:
    return [
        {
            "path": asset["path"],
            "sha256": asset["sha256"],
            "kind": asset["kind"],
        }
        for asset in sorted(original_assets.values(), key=lambda item: item["path"])
        if fixture_id in asset.get("fixture_ids", [])
    ]


def _schema_token_hits(repo_root: Path, actions: list[str]) -> dict[str, list[str]]:
    hits = {token: [] for token in SVA_SCHEMA_TOKENS}
    for action in actions:
        path = repo_root / action_contract(repo_root, action)["response_schema"]
        text = path.read_text(encoding="utf-8")
        for token in SVA_SCHEMA_TOKENS:
            if re.search(rf"(?<![A-Za-z0-9_]){re.escape(token)}(?![A-Za-z0-9_])", text, re.I):
                hits[token].append(action)
    return hits


def _construct_audit(repo_root: Path, original_root: Path | None, manifest_path: Path,
                     *, frozen_consumers: dict | None = None) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("goal_id") != GOAL_ID:
        raise MatrixError("P3-E audit manifest belongs to a different Goal")
    original_assets = asset_lookup(manifest, "original")
    if frozen_consumers is None:
        if original_root is None:
            raise MatrixError('Live audit requires an explicit original source root')
        for asset in original_assets.values():
            validate_frozen_file(original_root, asset)

    actions = public_actions(repo_root)
    contracts = {action: action_contract(repo_root, action) for action in actions}
    consumers = frozen_consumers if frozen_consumers is not None else consumer_catalog(original_root, original_assets, set(actions))
    sva_paths = fixture_consumers(consumers, SVA_FIXTURE)
    xif_paths = fixture_consumers(consumers, XIF_FIXTURE)
    cross_paths = fixture_consumers(consumers, CROSS_FIXTURE)
    xif_actions = fixture_actions(consumers, xif_paths)
    cross_actions = fixture_actions(consumers, cross_paths)
    fixture_map = {item["id"]: item for item in manifest["original_fixtures"]}

    return {
        "schema_version": SCHEMA_VERSION,
        "goal_id": GOAL_ID,
        "session": {
            "all_writes_repository_local": True,
            "external_sources_read_only": True,
            "fallback_used": False,
            "fixture_rebuilt": False,
            "eda_invoked": False,
        },
        "authority": {
            "catalog": {
                "path": "compat/xdebug-v1/catalog.response.json",
                "sha256": _sha256(repo_root / "compat/xdebug-v1/catalog.response.json"),
                "action_count": len(actions),
                "actions": actions,
            },
            "contract_count": len(contracts),
            "contracts": contracts,
        },
        "sva_npi": {
            "fixture_id": SVA_FIXTURE,
            "classification_target": "proven-unobservable",
            "fixture": fixture_map[SVA_FIXTURE],
            "frozen_assets": _fixture_assets(original_assets, SVA_FIXTURE),
            "consumer_paths": sva_paths,
            "consumer_sha256": consumers[SVA_CONSUMER]["sha256"],
            "observed_public_actions": fixture_actions(consumers, sva_paths),
            "private_probe_schema_versions": [
                "npi-daidir-fsdb-sva-probe.v1",
                "npi-fsdb-sva-probe.v1",
            ],
            "private_field_groups": SVA_PRIVATE_FIELD_GROUPS,
            "private_event_values": [
                "failure",
                "incomplete",
                "match",
                "success",
            ],
            "public_schema_token_hits": _schema_token_hits(repo_root, actions),
            "public_exposure_count": 0,
            "candidate_action_disposition": {
                "event.find": "通用采样表达式事件，不暴露 assertion object/event kind",
                "scope.list": "通用层级枚举，不暴露 SVA AST 或 NPI handle",
                "signal.changes": "通用信号变化，不暴露 assertion attempt/result",
                "value.at": "通用信号取值，不暴露 assertion attempt/result",
            },
            "remaining_distinct_public_observation_count": 0,
            "proof_scope": (
                "仅裁定冻结 NPI probe 的 assertion handle/AST/join/专属事件字段；"
                "普通 RTL 信号与通用 waveform Action 不在该不可观察结论内。"
            ),
        },
        "xif_event": {
            "fixture_id": XIF_FIXTURE,
            "classification_target": "semantic-equivalent",
            "fixture": fixture_map[XIF_FIXTURE],
            "frozen_assets": _fixture_assets(original_assets, XIF_FIXTURE),
            "consumer_paths": xif_paths,
            "consumer_sha256": {
                path: consumers[path]["sha256"] for path in sorted(XIF_CONSUMERS)
            },
            "observed_public_actions": xif_actions,
            "public_action_contracts": {
                action: contracts[action]
                for action in ("event.export", "event.find", "value.at")
            },
            "required_observations": XIF_REQUIRED_OBSERVATIONS,
            "current_pre_e2_evidence": [
                "tests/test_waveform.py::test_value_at_preserves_event_kind"
            ],
            "pre_e2_remaining_observation_count": len(XIF_REQUIRED_OBSERVATIONS),
        },
        "cross_fixture": {
            "fixture_id": CROSS_FIXTURE,
            "classification_target": "proven-unobservable",
            "consumer_count": len(cross_paths),
            "consumer_paths": cross_paths,
            "consumer_sha256": {
                path: consumers[path]["sha256"] for path in cross_paths
            },
            "observed_public_action_count": len(cross_actions),
            "observed_public_actions": cross_actions,
            "catalog_actions_not_observed": sorted(set(actions) - set(cross_actions)),
            "contracts": {action: contracts[action] for action in cross_actions},
            "remaining_unmapped_consumer_action_count": 0,
            "proof_scope": (
                "只关闭跨 fixture consumer 的公开合同索引；各波形 fixture 的运行时语义"
                "仍由所属 scenario 差分关闭。"
            ),
        },
    }


def build_audit(repo_root: Path, original_root: Path, manifest_path: Path) -> dict:
    document = _construct_audit(repo_root, original_root, manifest_path)
    return validate_audit(document, repo_root, original_root, manifest_path)


def build_frozen_audit(repo_root: Path, manifest_path: Path) -> dict:
    """Recompile the public audit from sealed evidence, never from an ambient checkout.

    This is a distinct offline operation. build_audit remains a strict live collector.
    The original consumer catalog was already frozen by the completed parity task.
    """
    matrix_path = repo_root / 'tests/coverage/rtl_wave_semantic_matrix.json'
    matrix = json.loads(matrix_path.read_text())
    consumers = matrix['original_consumers']
    fingerprint = hashlib.sha256(json.dumps(consumers, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if fingerprint != '6f42866f413c188d512ccb16c38be808ca7633dc1385a9a554efab8f92c3701e':
        raise MatrixError('Frozen consumer index content drifted')
    assets = asset_lookup(json.loads(manifest_path.read_text()), 'original')
    required = {path for path, asset in assets.items() if 'test_consumer' in asset['roles']}
    if set(consumers) != required:
        raise MatrixError('Frozen consumer index coverage drifted')
    for path, consumer in consumers.items():
        if consumer['sha256'] != assets[path]['sha256'] or consumer['fixture_ids'] != assets[path]['fixture_ids']:
            raise MatrixError('Frozen consumer source identity drifted: ' + path)
    return _construct_audit(repo_root, None, manifest_path, frozen_consumers=consumers)


def validate_frozen_audit(document: dict, repo_root: Path, manifest_path: Path) -> dict:
    if document != build_frozen_audit(repo_root, manifest_path):
        raise MatrixError('P3-E frozen boundary audit content drifted')
    return document


def validate_audit(
    document: dict, repo_root: Path, original_root: Path, manifest_path: Path
) -> dict:
    if document.get("schema_version") != SCHEMA_VERSION or document.get("goal_id") != GOAL_ID:
        raise MatrixError("P3-E boundary audit identity drifted")
    if document.get("session") != {
        "all_writes_repository_local": True,
        "external_sources_read_only": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
        "eda_invoked": False,
    }:
        raise MatrixError("P3-E write/cache/fallback boundary drifted")
    expected = _construct_audit(repo_root, original_root, manifest_path)
    if document != expected:
        raise MatrixError("P3-E boundary audit content drifted")
    return document
def _inside(root: Path, path: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def write_document(repo_root: Path, output: Path, document: dict) -> None:
    destination = output if output.is_absolute() else repo_root / output
    if not _inside(repo_root, destination):
        raise MatrixError("P3-E audit output escapes repository")
    destination.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=destination.parent, delete=False
    ) as handle:
        handle.write(payload)
        temporary = Path(handle.name)
    temporary.replace(destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--original-root", type=Path)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("compat/xdebug-v1/rtl-wave-assets.manifest.json"),
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    if args.original_root is None and not os.environ.get("XDEBUG_ORIGINAL_ROOT"):
        raise MatrixError(
            "set XDEBUG_ORIGINAL_ROOT or pass --original-root; no fallback is allowed"
        )
    original_root = (
        args.original_root
        or Path(os.environ.get("XDEBUG_ORIGINAL_ROOT", ""))
    ).resolve()
    manifest_path = args.manifest
    if not manifest_path.is_absolute():
        manifest_path = repo_root / manifest_path
    document = build_audit(repo_root, original_root, manifest_path)
    output = args.output if args.output.is_absolute() else repo_root / args.output
    if args.check:
        if json.loads(output.read_text(encoding="utf-8")) != document:
            raise MatrixError("P3-E boundary audit is stale")
    else:
        write_document(repo_root, output, document)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
