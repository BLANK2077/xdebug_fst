#!/usr/bin/env python3
"""Build the auditable original-RTL/current-FST semantic mapping matrix.

The original xverif checkout is an immutable input.  This program validates
every original file it reads against the P0 asset manifest and can only write
the generated JSON below the current repository root.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = "xdebug.rtl-wave-semantic-matrix.v1"
GOAL_ID = "01a050fa-b864-7ce2-af88-56083d84ea21"
ASSET_MANIFEST = Path("compat/xdebug-v1/rtl-wave-assets.manifest.json")
DEFAULT_OUTPUT = Path("tests/coverage/rtl_wave_semantic_matrix.json")
ACTIVE_CATALOG = Path("xdebug/tests/active_trace_chain/cases.v1.yaml")
ACTIVE_README = Path("xdebug/tests/active_trace_chain/README.md")
PHASE5_REPORT = Path(
    "xdebug/tests/active_trace_chain/reports/phase5_lane_select_report.md"
)
PHASE5_RUNTIME_AUDIT = Path(
    "tests/data/rtl_wave_differential/phase5.runtime-audit.json"
)
AI_COMPLEX_RUNTIME_AUDIT = Path(
    "tests/data/rtl_wave_differential/ai_complex.runtime-audit.json"
)
P3B_RUNTIME_AUDIT = Path(
    "tests/data/rtl_wave_differential/p3b.runtime-audit.json"
)
P3C_P0_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-p0.original-oracle.json"
)
P3C_COMPOSITE_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-composite.original-oracle.json"
)
P3C_PHASE4_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-phase4.original-oracle.json"
)
P3C_TIMING_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-timing.original-oracle.json"
)
P3C_PHASE5_PUBLIC_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3c-phase5.public-oracle.json"
)
P3C_ACTIVE_TRACE_CLOSURE_AUDIT = Path(
    "tests/data/rtl_wave_differential/"
    "p3c-active-trace-closure.audit.json"
)
P3D_STREAM_PUBLIC_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3d-stream-v1.public-oracle.json"
)
P3D_STREAM_EXPORT_ORACLE = Path(
    "tests/data/rtl_wave_differential/p3d-stream-v1.export-oracle.json"
)
P3D_STREAM_DIFFERENTIAL_CLOSURE_AUDIT = Path(
    "tests/data/rtl_wave_differential/"
    "p3d-stream-differential-closure.audit.json"
)
P3E_CLOSURE_AUDIT = Path(
    "tests/data/rtl_wave_differential/p3e-closure.audit.json"
)
P3D_APB_ORACLES = {
    "xdebug.apb_vip": Path(
        "tests/data/rtl_wave_differential/p3d-apb-vip.public-oracle.json"
    ),
    "xdebug.apb_xamba_vip": Path(
        "tests/data/rtl_wave_differential/"
        "p3d-apb-xamba-vip.public-oracle.json"
    ),
}
P3D_APB_EXPECTED = {
    "xdebug.apb_vip": {
        "current_fixture_id": "current.apb_vip",
        "observation_count": 36,
        "transaction_count": 10,
        "write_count": 5,
        "read_count": 5,
        "error_count": 1,
        "first_time": "125ns",
        "last_time": "525ns",
        "producer_kind": "svt",
        "fsdb_sha256": "fea2e60f2a575980db7fdddee4a22d1be05520769251144bcd04671a8da63e1f",
        "cache_manifest_sha256": "ed6988672f58d343adf74d202228a5fbf24f5214973cdc50b198aec8c0922a62",
    },
    "xdebug.apb_xamba_vip": {
        "current_fixture_id": "current.apb_xamba_vip",
        "observation_count": 34,
        "transaction_count": 64,
        "write_count": 32,
        "read_count": 32,
        "error_count": 6,
        "first_time": "45ns",
        "last_time": "2895ns",
        "producer_kind": "xamba",
        "fsdb_sha256": "8f103264f3cbd45bf0155583e161946009856513bdfe2fa6b9c71e9b37b1f016",
        "cache_manifest_sha256": "1bc0ba5d86d4b361a4f6c4c949382a24cf158dd0eab0638d299612fafd510e96",
    },
}
P3D_AXI_ORACLES = {
    "xdebug.axi_vip": Path(
        "tests/data/rtl_wave_differential/p3d-axi-vip.public-oracle.json"
    ),
    "xdebug.axi_xamba_vip": Path(
        "tests/data/rtl_wave_differential/"
        "p3d-axi-xamba-vip.public-oracle.json"
    ),
}
P3D_AXI_EXPECTED = {
    "xdebug.axi_vip": {
        "current_fixture_id": "current.axi_vip",
        "producer_kind": "svt",
        "profiles": {
            "stress": (7, 3200, 51472, {"AR": 3200, "AW": 3200, "B": 3200, "R": 21091, "W": 20781}, "1b19c87041282730f221fde779975dfb60d01e745990ee1b177328101f736a39"),
            "fixed_delay": (7, 32, 528, {"AR": 32, "AW": 32, "B": 32, "R": 203, "W": 229}, "23be697da2934298b03586440aa94b773d497625814fd953236377a5ff97d16c"),
            "random_seed_7": (7, 256, 3971, {"AR": 256, "AW": 256, "B": 256, "R": 1576, "W": 1627}, "afc6001c8a4863626f8059973e7e0e37a4e4f2076f04e1115c629109cbe3c7a2"),
            "random_seed_19": (19, 256, 4114, {"AR": 256, "AW": 256, "B": 256, "R": 1655, "W": 1691}, "52ac70c3eee9c685f5bc34903102d92466906f60aa76484ba04a1cb5bbffdae3"),
            "random_seed_73": (73, 256, 3948, {"AR": 256, "AW": 256, "B": 256, "R": 1562, "W": 1618}, "02777d03cb05ab3641cb8ddac08184ea3df4b7deb649011787e8260f5a9b4655"),
        },
    },
    "xdebug.axi_xamba_vip": {
        "current_fixture_id": "current.axi_xamba_vip",
        "producer_kind": "xamba",
        "profiles": {
            "xamba": (7, 32, 256, {"AR": 32, "AW": 32, "B": 32, "R": 96, "W": 64}, "0654226e170bcbab5937b8a06db7a322b0c7c1f0ae05e68a134f91b2ce81b67a"),
        },
    },
}
AI_COMPLEX_RUNNER_SHA256 = (
    "2c8f34c48d675d2e82b9edfd470a084fd17f84bc373ba26a98f0ab7cef848724"
)
AI_COMPLEX_COUNTER_RUNNER_SHA256 = (
    "fe0bafa4ff50d36d1fc283da07f915981ce613aae618341324edfb683c29ead5"
)
P3C_P0_RUNNER_SHA256 = (
    "f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237"
)
P3C_P0_FIXTURE_VERSION = (
    "c2bff810935847ad3debd15d1facb671d4943bc209ba25f38cb4b92d3d1ca06c-"
    "prepare-98pbtd3b"
)
P3C_COMPOSITE_FIXTURE_VERSION = (
    "93b0b714ac405932d426e9f03c88daaacad2d7c3a1595777d4c080f9a0c1d676-"
    "prepare-fpjmfmeo"
)
P3C_PHASE4_FIXTURE_VERSION = (
    "6a9d0fea1e9c68057e2ef12906dfd23f73a8a14622fc09b3be9798ee859b09ae-"
    "prepare-m3d4_3z7"
)
P3C_TIMING_FIXTURE_VERSION = (
    "1042c712bf8a59877d837e55ffd4e986a62eefc16aba01eaec2d6e459eca5fb5-"
    "prepare-d9oyxhx2"
)
P3C_PHASE5_FIXTURE_VERSION = (
    "2ee4a76b564e9c59197f85abde73928afc903d82fcf77224ccf1b99b3d160442-"
    "prepare-tydl8ogq"
)
P3C_PHASE5_BINARY_SHA256 = (
    "0f54515fa1c7e80634cdba1e9455ed7cdc1acae26144c7c5a39200853becf46d"
)
P3C_PHASE5_WRAPPER_SHA256 = (
    "c9569332281ccad39099e06d547075d33b35f9b50699e4d148203ad5645978e7"
)

ALLOWED_STATUSES = {
    "exact",
    "semantic-equivalent",
    "partial",
    "missing",
    "proven-unobservable",
}

CONSTRUCT_PATTERNS = {
    "module": r"\bmodule\b",
    "interface": r"\binterface\b",
    "modport": r"\bmodport\b",
    "continuous_assign": r"\bassign\b",
    "always_ff": r"\balways_ff\b",
    "always_comb": r"\balways_comb\b",
    "always": r"\balways\s*@",
    "initial": r"\binitial\b",
    "generate": r"\b(?:generate|genvar)\b",
    "procedural_for": r"\bfor\s*\(",
    "if": r"\bif\s*\(",
    "case": r"\bcase\s*\(",
    "casez": r"\bcasez\s*\(",
    "casex": r"\bcasex\s*\(",
    "matches": r"\bmatches\b",
    "inside": r"\binside\b",
    "nonblocking_assignment": r"<=",
    "force": r"\bforce\b",
    "release": r"\brelease\b",
    "function": r"\bfunction\b",
    "task": r"\btask\b",
    "assertion": r"\b(?:assert|assume|cover)\s+(?:property|final)\b",
    "property": r"\bproperty\b",
    "sequence": r"\bsequence\b",
    "event": r"\bevent\b|->",
    "real": r"\breal\b",
    "string": r"\bstring\b",
    "four_state_literal": r"(?:'[bBoOhH][0-9a-fA-F_xXzZ?]+|\b[01]*[xXzZ?][01xXzZ?]*\b)",
    "delay": r"#[0-9(]",
    "clock": r"\b(?:clk|clock|aclk|pclk)\b",
    "reset": r"\b(?:rst|reset|aresetn|presetn)\b",
    "apb": r"\bP(?:ADDR|SEL|ENABLE|WRITE|WDATA|RDATA|READY|SLVERR)\b",
    "axi": r"\b(?:AW|AR|W|R|B)(?:VALID|READY|ADDR|DATA|ID|RESP|LAST)\b",
    "valid_ready": r"\b(?:valid|ready|tvalid|tready)\b",
}

# A candidate only proves that a related capability exists.  Until P2 runs an
# equal request against both waveforms, every entry below remains partial.
FIXTURE_CANDIDATES = {
    "xdebug.active_driver": {
        "fixtures": ["current.active_driver"],
        "tests": [
            "test_locked_active_driver_assignment_and_force",
            "test_locked_active_driver_recurses_and_preserves_limits",
        ],
        "actions": ["trace.active_driver"],
        "batch": "P3-B",
        "evidence_scope": (
            "原版 RTL 字节相同；原版 runner 先在锁定 FSDB/runtime 上通过，"
            "同一公开请求与完整断言移植到当前原生 FST/DesignDB 后通过"
        ),
    },
    "xdebug.active_semantics": {
        "fixtures": ["current.case", "current.matches", "current.output_mixed"],
        "tests": [
            "test_trace_active_driver_selects_case_item_and_default",
            "test_trace_active_driver_selects_pattern_variable_binding",
            "test_trace_active_driver_chain_reports_two_active_procedural_drivers",
            "test_signal_canonicalize_port_connection",
        ],
        "actions": [
            "signal.canonicalize",
            "trace.active_driver",
            "trace.active_driver_chain",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "锁定原版完整 oracle 通过；当前按公开 Action、分支、时间、完整性字段"
            "逐项归一映射，而非宣称复用同一原版 runner"
        ),
    },
    "xdebug.active_zero_evidence": {
        "fixtures": ["current.active_zero_evidence"],
        "tests": [
            "test_locked_active_zero_scope_roots_discovers_combined_top",
            "test_locked_active_zero_precise_active_time",
            "test_locked_active_zero_reduction_zero_evidence",
            "test_locked_active_zero_module_input_follows_parent",
            "test_locked_active_zero_expression_outputs_have_zero_evidence",
        ],
        "actions": [
            "scope.list", "scope.roots", "trace.active_driver",
            "trace.active_driver_chain", "trace.driver",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "原版 RTL 字节相同；完整移植 scope/driver/chain/零证据与时间尺度 oracle"
            "在当前原生 FST/DesignDB 上通过"
        ),
    },
    "xdebug.interface_port_root": {
        "fixtures": ["current.interface_port_root"],
        "tests": [
            "test_locked_interface_scope_classification",
            "test_locked_interface_active_driver_aliases",
        ],
        "actions": ["scope.list", "trace.active_driver"],
        "batch": "P3-B",
        "evidence_scope": (
            "原版 RTL 字节相同；interface 分类、modport alias 和活动驱动公开 oracle"
            "在当前原生 FST/DesignDB 上通过"
        ),
    },
    "xdebug.trace_x_xprop": {
        "fixtures": ["current.xprop"],
        "tests": [
            "test_batch_preserves_nested_x_value_from_direct_raw_fst",
            "test_trace_x_origin_x_propagation",
            "test_trace_x_origin_branch_chain_ids_are_consistent",
            "test_trace_x_origin_keeps_loop_and_normal_source_branches",
        ],
        "actions": [
            "list.load", "trace.active_driver_chain", "trace.x_origin", "value.at",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "锁定原版 X-prop oracle 通过；当前 FST 逐项覆盖四态点读、list、"
            "X origin、alias/loop/limits 和 active chain 公开结果"
        ),
    },
    "xdebug.ai_complex_wave": {
        "fixtures": ["current.ai_complex"],
        "tests": [
            "test_ai_complex_fixture_is_frozen_and_four_state",
            "test_ai_complex_scope_value_and_four_state_contract",
            "test_ai_complex_event_reset_and_sampling_contract",
            "test_ai_complex_stream_before_after_contract",
            "test_ai_complex_expression_window_and_xz_contract",
            "test_ai_complex_counter_statistics_contract",
            "test_ai_complex_changes_statistics_anomaly_and_handshake_contract",
        ],
        "actions": [
            "counter.statistics", "event.config.list", "event.config.load", "event.export",
            "event.find", "expr.eval_at", "list.add", "list.create",
            "list.delete", "list.export", "list.first_change", "list.load",
            "list.show", "list.validate", "protocol.handshake.inspect",
            "scope.list", "signal.anomaly.inspect", "signal.changes",
            "signal.sampled_pulse.inspect", "signal.stability",
            "signal.statistics", "signal.xz_verify", "stream.config.load",
            "stream.query", "value.at", "verify.conditions", "waveform.cursor.set",
            "window.verify",
        ],
        "batch": "P3-A",
        "status": "semantic-equivalent",
        "rationale": (
            "锁定 8eec runtime 的同一 nonaxi oracle 已分别在原版 FSDB 与当前四态 FST 上完整通过；"
            "当前仓库另有逐 Action 回归，格式差异不改变公开可观察语义。"
        ),
        "evidence_scope": (
            "same locked original oracle on original FSDB and current FST; "
            "repository-local regression covers the closed differences"
        ),
    },
    "xdebug.stream_v1": {
        "fixtures": ["current.stream_v1"],
        "tests": [
            "test_stream_v1_oracle_locks_runtime_fixture_and_observation_set",
            "test_stream_v1_current_fst_matches_all_locked_public_observations",
            "test_stream_v1_current_fixture_locks_tool_seed_and_deterministic_fst",
            "test_stream_v1_export_oracle_locks_runtime_schema_and_xout",
            "test_stream_v1_current_export_matches_locked_json_artifacts_and_xout",
            "test_current_matches_all_public_base_cache_observations",
            "test_current_matches_public_batch_and_soft_budget_results",
            "test_current_exposes_locked_public_hard_limit_error",
        ],
        "actions": [
            "batch", "stream.config.list", "stream.config.load",
            "stream.describe", "stream.export", "stream.query",
            "stream.validate",
        ],
        "batch": "P3-D",
        "status": "semantic-equivalent",
        "rationale": (
            "原版 RTL/config 与当前镜像逐字节相同；锁定原版 FSDB 的 58 个查询/配置、"
            "6 个 export、3 个 artifact 和 cache 公开边界已在确定性原生 FST 上逐项通过。"
        ),
        "evidence_scope": (
            "P3-D1 只关闭冻结 stream_v1 刺激、时间、公开响应、artifact/XOUT 与 cache "
            "边界；原版 FSDB 与当前 FST 的二进制格式差异不作为语义等价证据"
        ),
    },
    "xdebug.stream_differential_tool": {
        "fixtures": [],
        "tests": [
            "test_differential_audit_locks_bounded_original_contract",
            "test_current_matches_all_public_base_cache_observations",
            "test_current_matches_public_batch_and_soft_budget_results",
            "test_current_exposes_locked_public_hard_limit_error",
        ],
        "actions": ["stream.export", "stream.query", "stream.validate"],
        "batch": "P3-D",
        "status": "proven-unobservable",
        "rationale": (
            "原版 differential 产物是无 RTL/波形输出的私有 comparator build；冻结 73 Action/"
            "schema 不暴露 comparator 或 cache probe，全部公开回放已由 stream_v1 门禁关闭。"
        ),
        "evidence_scope": (
            "有限证明仅覆盖冻结 comparator build、stream.query/export/validate 拦截点、"
            "公开 cache 响应及私有 probe 字段；hard memory limit 仍按公开错误单独验收"
        ),
    },
    "xdebug.npi_fsdb_sva": {
        "fixtures": [],
        "tests": [
            "test_sva_npi_boundary_is_private_and_bounded_by_frozen_schemas",
            "test_p3e_sva_and_cross_fixture_proofs_are_strictly_bounded",
        ],
        "actions": [
            "scope.list", "value.at", "signal.changes", "event.find",
        ],
        "batch": "P3-E",
        "status": "proven-unobservable",
        "rationale": (
            "冻结原版 consumer 只调用私有 NPI probe，73 个公开 Action/schema 不暴露"
            " assertion identity/event、SVA AST 或 design-wave join；有限静态证明剩余"
            " 独立公开观察点为零，未来 schema 暴露将 fail closed。"
        ),
        "evidence_scope": (
            "只关闭冻结 npi_fsdb_sva 私有 probe schema 和冻结 73 Action 的边界；"
            "不把通用 event/value/scope 能力宣称为 SVA 行为替代"
        ),
    },
    "xdebug.apb_vip": {
        "fixtures": ["current.apb_vip"],
        "tests": [
            "test_apb_oracle_locks_runtime_fixture_surface_and_all_transactions",
            "test_current_apb_fixture_matches_every_locked_base_observation_and_xout",
            "test_current_apb_matches_locked_soft_budget_public_observations",
            "test_current_apb_exposes_locked_public_hard_limit_error",
        ],
        "actions": [
            "apb.config.load", "apb.query", "apb.statistics",
            "apb.transaction.cursor", "apb.transfer_window",
        ],
        "batch": "P3-D",
        "status": "semantic-equivalent",
    },
    "xdebug.apb_xamba_vip": {
        "fixtures": ["current.apb_xamba_vip"],
        "tests": [
            "test_apb_oracle_locks_runtime_fixture_surface_and_all_transactions",
            "test_current_apb_fixture_matches_every_locked_base_observation_and_xout",
            "test_current_apb_matches_locked_soft_budget_public_observations",
            "test_current_apb_exposes_locked_public_hard_limit_error",
        ],
        "actions": [
            "apb.config.load", "apb.query", "apb.statistics",
            "apb.transaction.cursor", "apb.transfer_window",
        ],
        "batch": "P3-D",
        "status": "semantic-equivalent",
    },
    "xdebug.axi_vip": {
        "fixtures": ["current.axi_vip"],
        "tests": [
            "test_current_svt_axi_profile_matches_locked_public_semantics",
            "test_current_svt_axi_profiles_lock_events_tool_and_fst",
        ],
        "actions": [
            "axi.analysis", "axi.channel_stall", "axi.config.list",
            "axi.config.load", "axi.export", "axi.latency_outlier",
            "axi.outstanding_timeline", "axi.query",
            "axi.request_response_pair", "axi.statistics",
            "axi.transaction.cursor",
        ],
        "batch": "P3-D",
        "status": "semantic-equivalent",
    },
    "xdebug.axi_xamba_vip": {
        "fixtures": ["current.axi_xamba_vip"],
        "tests": [
            "test_current_xamba_axi_fixture_matches_locked_public_semantics",
            "test_current_xamba_axi_fixture_locks_formula_tool_and_deterministic_fst",
            "test_current_xamba_axi_matches_locked_soft_budget_public_observations",
            "test_current_xamba_axi_exposes_locked_public_hard_limit_error",
        ],
        "actions": [
            "axi.analysis", "axi.channel_stall", "axi.config.list",
            "axi.config.load", "axi.export", "axi.latency_outlier",
            "axi.outstanding_timeline", "axi.query",
            "axi.request_response_pair", "axi.statistics",
            "axi.transaction.cursor",
        ],
        "batch": "P3-D",
        "status": "semantic-equivalent",
    },
    "xdebug.design_uart": {
        "fixtures": ["current.counter", "current.output_mixed"],
        "tests": [
            "test_batch_aggregates_responses",
            "test_expr_normalize_contract",
            "test_expr_normalize_parse_error",
            "test_signal_canonicalize_port_connection",
            "test_signal_resolve_contract",
            "test_trace_driver_contract_and_role_filter",
        ],
        "actions": [
            "batch", "expr.normalize", "signal.canonicalize",
            "signal.resolve", "trace.driver",
        ],
        "batch": "P3-B",
        "evidence_scope": (
            "锁定 UART DesignDB runner 通过；当前以同一公开设计 Action 和完整响应合同"
            "完成归一映射，不比较不同设计数据库二进制"
        ),
    },
    "xdebug.design_p3": {
        "fixtures": [],
        "tests": ["test_design_p3_unobservable_proof_is_hash_anchored"],
        "actions": ["session.open"],
        "batch": "P3-B",
        "evidence_scope": (
            "Goal-start 与锁定 run_semantics 只对 P3 DesignDB 执行 session.open，"
            "没有任何 P3 语义查询；由 runner 哈希静态门禁证明不可观察"
        ),
    },
    "xdebug.design_hierarchy": {
        "fixtures": [],
        "tests": [
            "test_design_hierarchy_unobservable_proof_matches_locked_schema",
        ],
        "actions": ["scope.list"],
        "batch": "P3-B",
        "evidence_scope": (
            "Goal-start hierarchy test 不存在于锁定 runtime；所需 kind/data groups 又被"
            "冻结 scope.list schema 排除，由静态合同门禁证明不可观察"
        ),
    },
    "xdebug.active_trace_runner": {
        "fixtures": [],
        "tests": [
            "test_runner_private_helper_has_no_uncovered_public_observation",
        ],
        "actions": ["trace.active_driver_chain"],
        "batch": "P3-C",
        "status": "proven-unobservable",
        "rationale": (
            "原版 runner 是无 RTL/波形输出的私有 native oracle helper；68 个 catalog "
            "公开观察点已逐项闭合，冻结 schema 不暴露独立 runner Action"
        ),
        "evidence_scope": (
            "受 runner 输入/输出、58 个 native oracle、10 个公开 runtime oracle 和"
            "trace.active_driver_chain request schema 联合约束的有限不可观察证明"
        ),
    },
    "xdebug.xif_event": {
        "fixtures": ["current.xif_event"],
        "tests": [
            "test_xif_fixture_exists_and_is_locked",
            "test_xif_original_oracle_locks_all_e2_observations",
            "test_xif_all_32_original_observations_replay_on_raw_fst",
            "test_xif_event_find_xout_preserves_complete_public_evidence",
            "test_xif_field_shorthand_rejects_partial_integer_bounds",
        ],
        "actions": ["value.at", "event.find", "event.export"],
        "batch": "P3-E",
        "status": "semantic-equivalent",
        "rationale": (
            "六份原版 event 配置直接复制；依赖 UVM/XIF/VCS/FSDB 的 RTL 仅作最小"
            " pin-level 开源镜像。冻结原版 FSDB 的 32 项公开观察已在当前原始 FST 上"
            "按时序、位值、X 态、流控、错误、XOUT 和 artifact 内容逐项通过。"
        ),
        "evidence_scope": (
            "验收测试内容与公开可观察语义等价，不要求双侧源码或波形哈希相同；"
            "哈希只冻结身份和当前确定性重建"
        ),
    },
}

ACTIVE_CANDIDATES = {
    "p0": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_p0_oracle_is_locked_complete_and_sanitized",
            "test_p3c_p0_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_p0_matches_locked_native_chain_semantics",
            "test_p3c_p0_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "六份原版 RTL 字节相同；锁定 native NPI oracle 与当前原始 FST/"
            "binary-v1 DesignDB 按 hop、时间、值、候选、终止和完整性逐项通过"
        ),
    },
    "composite": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_composite_oracle_is_locked_complete_and_sanitized",
            "test_p3c_composite_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_composite_matches_locked_native_chain_semantics",
            "test_p3c_composite_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "二十份原版 case RTL 与共享 DUT 字节相同；锁定 native NPI oracle 与"
            "当前原始 FST/binary-v1 DesignDB 按完整复合链逐项通过"
        ),
    },
    "timing": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_timing_oracle_is_locked_complete_and_sanitized",
            "test_p3c_timing_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_timing_matches_locked_native_temporal_prefix_semantics",
            "test_p3c_timing_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "十二份原版 case RTL 与共享 timing DUT 字节相同；锁定 native NPI "
            "stop-on-temporal oracle 与当前完整公开链按首个时序边界逐项通过"
        ),
    },
    "phase4": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_phase4_oracle_is_locked_complete_and_sanitized",
            "test_p3c_phase4_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_phase4_matches_locked_native_chain_semantics",
            "test_p3c_phase4_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "二十份原版 case RTL 及两份共享 DUT 字节相同；锁定 native NPI oracle 与"
            "当前原始 FST/binary-v1 DesignDB 按完整复合链逐项通过"
        ),
    },
    "phase5": {
        "fixtures": ["current.active_trace"],
        "tests": [
            "test_p3c_phase5_public_oracle_is_locked_complete_and_sanitized",
            "test_p3c_phase5_exact_rtl_and_generated_fixture_hashes",
            "test_p3c_phase5_matches_locked_full_public_response",
            "test_p3c_phase5_limits_are_explicit_analysis_boundaries",
        ],
        "evidence_scope": (
            "两份原版 Phase5 RTL 与当前镜像字节相同；锁定公开 runtime 完整响应与"
            "当前原始 FST/binary-v1 DesignDB 按 statement、RHS、时间、值、源码、"
            "宽度投影、termination 和完整性逐项通过"
        ),
    },
}

ACTIVE_FIXTURE = {
    "p0": "xdebug.active_trace_p0",
    "composite": "xdebug.active_trace_composite",
    "timing": "xdebug.active_trace_timing",
    "phase4": "xdebug.active_trace_phase4",
    "phase5": "xdebug.active_trace_phase5",
}


class MatrixError(RuntimeError):
    """Raised when the matrix cannot be built without losing evidence."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def canonical_json(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def ensure_within_repo(repo_root: Path, path: Path) -> Path:
    root = repo_root.resolve()
    resolved = path.resolve(strict=False)
    if resolved != root and root not in resolved.parents:
        raise MatrixError(f"output escapes the only writable repository: {path}")
    return resolved


def parse_scalar(value: str):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    if value == "true":
        return True
    if value == "false":
        return False
    if re.fullmatch(r"[0-9]+", value):
        return int(value)
    return value


def parse_inline_mapping(body: str) -> dict:
    fields = next(csv.reader([body], skipinitialspace=True))
    result = {}
    for field in fields:
        if ":" not in field:
            raise MatrixError(f"invalid inline catalog field: {field!r}")
        key, value = field.split(":", 1)
        result[key.strip()] = parse_scalar(value)
    return result


def parse_active_catalog(text: str) -> list[dict]:
    group = None
    rows = []
    for line_number, line in enumerate(text.splitlines(), 1):
        group_match = re.fullmatch(r"  ([a-z0-9_]+):", line)
        if group_match:
            group = group_match.group(1)
            continue
        item_match = re.fullmatch(r"    - \{(.*)\}", line)
        if item_match:
            if group is None:
                raise MatrixError(f"catalog item without group at line {line_number}")
            item = parse_inline_mapping(item_match.group(1))
            item["group"] = group
            item["catalog_line"] = line_number
            rows.append(item)
    expected = {"p0": 6, "composite": 20, "timing": 12, "phase4": 20, "phase5": 10}
    counts = Counter(row["group"] for row in rows)
    if dict(counts) != expected:
        raise MatrixError(f"active catalog count drift: {dict(counts)!r}")
    return rows


def parse_phase5_report_terminations(text: str) -> list[dict]:
    rows = []
    pattern = re.compile(
        r"^\| S(?P<scene>[0-9]+) \| (?P<target>[^|]+) \| "
        r"(?P<time>[^|]+) \| (?P<hops>[0-9]+) \| "
        r"(?P<termination>[a-z_]+) \|",
        re.MULTILINE,
    )
    lines = text.splitlines()
    for match in pattern.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        rows.append({
            "scene": int(match.group("scene")),
            "target": match.group("target").strip(),
            "time": match.group("time").strip(),
            "hops": int(match.group("hops")),
            "termination": match.group("termination"),
            "report_line": line,
        })
    if len(rows) != 10:
        raise MatrixError(f"phase5 report table drift: expected 10 rows, got {len(rows)}")
    return rows


def validate_phase5_runtime_audit(
    audit: dict,
    runtime_revision: str,
    schema_revision: str,
) -> dict[str, dict]:
    if audit.get("schema_version") != "xdebug.phase5-runtime-audit.v1":
        raise MatrixError("Phase5 runtime audit has the wrong schema_version")
    if audit.get("goal_id") != GOAL_ID:
        raise MatrixError("Phase5 runtime audit belongs to a different Goal")
    locked = audit.get("locked_original_runtime", {})
    if locked.get("git_revision") != runtime_revision:
        raise MatrixError("Phase5 runtime audit does not use the locked runtime revision")
    if locked.get("schema_revision") != schema_revision:
        raise MatrixError("Phase5 runtime audit does not use the locked schema revision")
    if locked.get("action_count") != 73:
        raise MatrixError("Phase5 runtime audit does not prove the 73-Action identity gate")
    if locked.get("fixture", {}).get("fixture_rebuilt") is not False:
        raise MatrixError("Phase5 runtime audit must reuse, not rebuild, the fixture cache")
    if locked.get("external_write_audit", {}).get("unchanged") is not True:
        raise MatrixError("Phase5 runtime audit does not prove zero external writes")

    rows = audit.get("scene_results")
    if not isinstance(rows, list) or len(rows) != 10:
        raise MatrixError("Phase5 runtime audit must contain exactly ten scenes")
    result = {}
    for index, row in enumerate(rows, 1):
        scenario_id = f"active.phase5.{index:02d}"
        if row.get("scenario_id") != scenario_id:
            raise MatrixError(f"Phase5 runtime audit scene order drift at {scenario_id}")
        if row.get("status") != "partial" or row.get("p3_batch") != "P3-C":
            raise MatrixError(f"Phase5 runtime audit closes {scenario_id} prematurely")
        locked_result = row.get("locked_runtime", {})
        if (
            locked_result.get("scan_complete") is not True
            or locked_result.get("analysis_complete") is not True
            or locked_result.get("response_truncated") is not False
        ):
            raise MatrixError(f"Phase5 locked response is incomplete: {scenario_id}")
        if scenario_id in result:
            raise MatrixError(f"duplicate Phase5 runtime audit scene: {scenario_id}")
        result[scenario_id] = row
    verdict = audit.get("verdict", {})
    if (
        verdict.get("status") != "partial"
        or verdict.get("termination_and_ambiguity_subset_equivalent_scene_count") != 10
        or verdict.get("full_response_equivalent_scene_count") != 0
    ):
        raise MatrixError("Phase5 runtime audit verdict drifted")
    return result


def validate_p3c_active_trace_closure_audit(
    audit: dict,
    repo_root: Path,
    manifest: dict,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict:
    if (
        audit.get("schema_version") !=
            "xdebug.p3c-active-trace-closure-audit.v1"
        or audit.get("goal_id") != GOAL_ID
    ):
        raise MatrixError(
            "P3-C active-trace closure audit belongs to another Goal/schema"
        )
    if audit.get("session") != {
        "all_writes_repository_local": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
        "source_access": "read_only",
    }:
        raise MatrixError(
            "P3-C active-trace closure write/cache/fallback boundary drifted"
        )

    def strings(value: object) -> Iterable[str]:
        if isinstance(value, str):
            yield value
        elif isinstance(value, list):
            for item in value:
                yield from strings(item)
        elif isinstance(value, dict):
            for key, item in value.items():
                yield from strings(key)
                yield from strings(item)

    if any(
            value.startswith("/") or "/home/" in value
            for value in strings(audit)):
        raise MatrixError(
            "P3-C active-trace closure audit contains an absolute path"
        )

    runner = audit.get("runner", {})
    fixture_map = {
        item["id"]: item for item in manifest.get("original_fixtures", [])
    }
    runner_fixture = fixture_map.get("xdebug.active_trace_runner")
    if (
        runner.get("fixture_id") != "xdebug.active_trace_runner"
        or runner.get("classification") != "proven-unobservable"
        or runner.get("fixture_contract") != runner_fixture
        or runner.get("private_helper") != {
            "output_kind": "native_test_executable",
            "output_path": "build/chain_test",
            "public_action": False,
            "rtl_input_count": 0,
            "waveform_output_count": 0,
        }
        or not runner.get("proof_scope")
    ):
        raise MatrixError("P3-C private runner boundary drifted")

    expected_runner_sources = []
    for path in (
        "xdebug/tests/active_trace_chain/Makefile",
        "xdebug/tests/active_trace_chain/chain_test.cpp",
        "xdebug/tests/active_trace_chain/chain_test.h",
    ):
        asset = original_assets.get(path)
        if asset is None:
            raise MatrixError(f"P3-C runner source is not frozen: {path}")
        expected_runner_sources.append({
            "path": path,
            "sha256": asset["sha256"],
            "size_bytes": asset["size_bytes"],
        })
    if runner.get("source_assets") != expected_runner_sources:
        raise MatrixError("P3-C private runner source identity drifted")

    request_schema = (
        "compat/xdebug-v1/schemas/v1/actions/"
        "trace.active_driver_chain.request.schema.json"
    )
    schema_path = repo_root / request_schema
    if not schema_path.is_file():
        raise MatrixError("trace.active_driver_chain request schema is missing")
    schema_hash = hashlib.sha256(schema_path.read_bytes()).hexdigest()
    private_fields = [
        "active_trace_calls",
        "edgecheck_direct_count",
        "fallback_0_5ns_count",
        "stop_on_temporal",
        "temporal_boundary_stops",
    ]
    if runner.get("public_contract") != {
        "private_fields_absent": private_fields,
        "request_schema": request_schema,
        "request_schema_sha256": schema_hash,
        "required_args": ["signal", "time"],
    }:
        raise MatrixError("P3-C private/public runner schema boundary drifted")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    if catalog_asset is None:
        raise MatrixError("P3-C runner catalog is not frozen")
    expected_group_counts = {
        "composite": 20,
        "p0": 6,
        "phase4": 20,
        "phase5": 10,
        "timing": 12,
    }
    runner_sha = (
        "f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237"
    )
    native_specs = [
        (
            "p0",
            P3C_P0_ORACLE.as_posix(),
            6,
        ),
        (
            "composite",
            P3C_COMPOSITE_ORACLE.as_posix(),
            20,
        ),
        (
            "timing",
            P3C_TIMING_ORACLE.as_posix(),
            12,
        ),
        (
            "phase4",
            P3C_PHASE4_ORACLE.as_posix(),
            20,
        ),
    ]
    native_oracles = []
    covered_scenarios = []
    for group, path, row_count in native_specs:
        asset = current_assets.get(path)
        if asset is None:
            raise MatrixError(f"P3-C native runner oracle is not frozen: {path}")
        native_oracles.append({
            "group": group,
            "path": path,
            "row_count": row_count,
            "runner_sha256": runner_sha,
            "sha256": asset["sha256"],
        })
        covered_scenarios.extend(
            f"active.{group}.{index:02d}"
            for index in range(1, row_count + 1)
        )
    phase5_asset = current_assets.get(P3C_PHASE5_PUBLIC_ORACLE.as_posix())
    if phase5_asset is None:
        raise MatrixError("P3-C Phase5 public oracle is not frozen")
    covered_scenarios.extend(
        f"active.phase5.{index:02d}" for index in range(1, 11)
    )
    readme_asset = original_assets.get(ACTIVE_README.as_posix())
    phase5_test_path = "xdebug/tests/active_trace_chain/test_phase5.py"
    phase5_test_asset = original_assets.get(phase5_test_path)
    if readme_asset is None or phase5_test_asset is None:
        raise MatrixError("P3-C runner public consumer evidence is not frozen")
    native_paths = [path for _, path, _ in native_specs]
    consumer_action_disposition = {
        "session.close": {
            "covered_by": P3C_PHASE5_PUBLIC_ORACLE.as_posix(),
            "executable_request_count": 1,
            "source": phase5_test_path,
            "source_sha256": phase5_test_asset["sha256"],
        },
        "session.open": {
            "covered_by": P3C_PHASE5_PUBLIC_ORACLE.as_posix(),
            "executable_request_count": 1,
            "source": phase5_test_path,
            "source_sha256": phase5_test_asset["sha256"],
        },
        "trace.active_driver": {
            "documentation_only_lines": [9, 10, 27],
            "executable_request_count": 0,
            "source": ACTIVE_README.as_posix(),
            "source_sha256": readme_asset["sha256"],
        },
        "trace.active_driver_chain": {
            "catalog_observation_count": 68,
            "covered_by": [
                *native_paths, P3C_PHASE5_PUBLIC_ORACLE.as_posix(),
            ],
            "executable_request_count": 1,
            "source": phase5_test_path,
            "source_sha256": phase5_test_asset["sha256"],
        },
    }
    coverage = runner.get("coverage", {})
    if coverage != {
        "catalog": {
            "group_counts": expected_group_counts,
            "path": ACTIVE_CATALOG.as_posix(),
            "sha256": catalog_asset["sha256"],
        },
        "catalog_case_count": 68,
        "consumer_public_action_count": 4,
        "consumer_public_action_disposition": consumer_action_disposition,
        "covered_scenario_ids": covered_scenarios,
        "native_oracles": native_oracles,
        "native_runner_case_count": 58,
        "public_runtime_case_count": 10,
        "public_runtime_oracle": {
            "group": "phase5",
            "path": P3C_PHASE5_PUBLIC_ORACLE.as_posix(),
            "row_count": 10,
            "sha256": phase5_asset["sha256"],
        },
        "remaining_unmapped_consumer_action_count": 0,
        "remaining_distinct_public_observation_count": 0,
    }:
        raise MatrixError("P3-C private runner catalog coverage drifted")

    orphan = audit.get("declared_only_orphan", {})
    orphan_dir = (
        "xdebug/tests/active_trace_chain/p0_composability/"
        "p0_4_interface_modport"
    )
    orphan_entry = f"{orphan_dir}/.gitignore"
    orphan_asset = original_assets.get(orphan_entry)
    frozen_entries = sorted(
        path.removeprefix(orphan_dir + "/")
        for path in original_assets
        if path.startswith(orphan_dir + "/")
    )
    if orphan_asset is None or readme_asset is None:
        raise MatrixError("P3-C declared-only orphan evidence is not frozen")
    if frozen_entries != [".gitignore"]:
        raise MatrixError("P3-C declared-only orphan inventory drifted")
    if (
        orphan.get("scenario_id") != "active.p0.declared_only_p0_4"
        or orphan.get("case") != "p0_4_interface_modport"
        or orphan.get("classification") != "proven-unobservable"
        or orphan.get("declaration") != {
            "line": 22,
            "path": ACTIVE_README.as_posix(),
            "sha256": readme_asset["sha256"],
        }
        or orphan.get("frozen_directory") != {
            "concrete_waveform_count": 0,
            "entries": [".gitignore"],
            "entry_sha256": orphan_asset["sha256"],
            "path": orphan_dir,
            "rtl_count": 0,
            "stimulus_count": 0,
        }
        or orphan.get("catalog") != {
            "path": ACTIVE_CATALOG.as_posix(),
            "sha256": catalog_asset["sha256"],
        }
        or orphan.get("catalog_row_count") != 0
        or orphan.get("authoritative_public_request_count") != 0
        or orphan.get("remaining_distinct_public_observation_count") != 0
        or orphan.get("current_capability_is_not_equivalence") is not True
        or not orphan.get("proof_scope")
    ):
        raise MatrixError("P3-C declared-only orphan proof drifted")
    return audit


def validate_p3d_apb_oracle_document(
    document: dict,
    fixture_id: str,
    original_assets: dict[str, dict],
) -> dict:
    """Validate APB identity, full transaction semantics and cache boundary."""

    expected = P3D_APB_EXPECTED[fixture_id]
    observations = document.get("observations")
    if (
        document.get("schema_version") != "xdebug.p3d-apb-public-oracle.v1"
        or document.get("goal_id") != GOAL_ID
        or document.get("fixture_id") != fixture_id
        or document.get("producer_kind") != expected["producer_kind"]
        or document.get("observation_count") != expected["observation_count"]
        or not isinstance(observations, list)
        or len(observations) != expected["observation_count"]
        or len({row.get("observation_id") for row in observations}) !=
            expected["observation_count"]
    ):
        raise MatrixError(f"P3-D2 APB oracle identity drifted: {fixture_id}")

    session = document.get("session", {})
    runtime = document.get("locked_runtime", {})
    if (
        runtime.get("action_count") != 73
        or runtime.get("cache_reused") is not True
        or runtime.get("fixture_rebuilt") is not False
        or runtime.get("source_access") != "read_only"
        or session.get("all_runtime_writes_repository_local") is not True
        or session.get("fallback_used") is not False
        or session.get("fixture_rebuilt") is not False
        or session.get("source_access") != "read_only"
    ):
        raise MatrixError(f"P3-D2 APB runtime boundary drifted: {fixture_id}")

    fixture = document.get("original_fixture", {})
    stimulus = fixture.get("stimulus_contract")
    authority = document.get("action_authority", {})
    excluded_export = authority.get("excluded_apb_export", {})
    if (
        authority.get("apb_actions") != [
            "apb.config.list", "apb.config.load", "apb.query",
            "apb.statistics", "apb.transaction.cursor", "apb.transfer_window",
        ]
        or excluded_export.get("classification") !=
            "not-in-frozen-public-surface"
        or excluded_export.get("fallback_to_live_runtime_allowed") is not False
        or excluded_export.get("response", {}).get("error", {}).get("code") !=
            "UNKNOWN_ACTION"
        or fixture.get("fsdb_sha256") != expected["fsdb_sha256"]
        or fixture.get("cache_manifest_sha256") !=
            expected["cache_manifest_sha256"]
        or not isinstance(stimulus, list)
        or len(stimulus) != expected["transaction_count"]
    ):
        raise MatrixError(f"P3-D2 APB stimulus count drifted: {fixture_id}")
    for source in fixture.get("source_files", []):
        asset = original_assets.get(source.get("path"))
        if asset is None or asset.get("sha256") != source.get("sha256"):
            raise MatrixError(f"P3-D2 APB original source identity drifted: {fixture_id}")

    by_id = {row["observation_id"]: row for row in observations}
    full = by_id.get("query.full", {}).get("response", {})
    transactions = full.get("data", {}).get("transactions")
    summary = full.get("summary", {})
    if (
        not isinstance(transactions, list)
        or len(transactions) != expected["transaction_count"]
        or summary.get("total_count") != expected["transaction_count"]
        or summary.get("returned_count") != expected["transaction_count"]
        or summary.get("scan_complete") is not True
        or summary.get("analysis_complete") is not True
        or summary.get("response_truncated") is not False
        or summary.get("truncation_scopes") != []
    ):
        raise MatrixError(f"P3-D2 APB full replay is incomplete: {fixture_id}")

    projected = []
    completion_ns = None
    for index, row in enumerate(stimulus):
        if row.get("index") != index:
            raise MatrixError(f"P3-D2 APB stimulus index drifted: {fixture_id}")
        if fixture_id == "xdebug.apb_xamba_vip":
            formulas = {
                "address": f"32'h{0x1000 + index * 4:08x}",
                "is_write": index % 2 == 0,
                "write_data": f"32'h{0xa5000000 | index:08x}",
                "read_data": f"32'h{0x5a000000 | index:08x}",
                "pstrb": f"4'h{(1 << (index % 4)) if index % 2 == 0 else 0:x}",
                "pprot": index % 8,
                "pnse": (index // 8) % 2,
                "wait_cycles": index % 4,
                "has_error": index % 11 == 0,
            }
            if any(row.get(key) != value for key, value in formulas.items()):
                raise MatrixError("P3-D2 XAMBA formula/stimulus drifted")
        completion_ns = (
            int(expected["first_time"].removesuffix("ns"))
            if completion_ns is None else
            completion_ns + 30 + 10 * row["wait_cycles"]
        )
        projected.append({
            "time": f"{completion_ns}ns",
            "is_write": row["is_write"],
            "addr": row["address"],
            "data": row["public_data"],
            "has_error": row["has_error"],
        })
    if transactions != projected or projected[-1]["time"] != expected["last_time"]:
        raise MatrixError(f"P3-D2 APB transaction/timing replay drifted: {fixture_id}")
    if (
        sum(row["is_write"] for row in transactions) != expected["write_count"]
        or sum(not row["is_write"] for row in transactions) != expected["read_count"]
        or sum(row["has_error"] for row in transactions) != expected["error_count"]
    ):
        raise MatrixError(f"P3-D2 APB direction/error count drifted: {fixture_id}")

    xouts = [row for row in observations if "xout" in row]
    if len(xouts) != 4 or any(not row.get("xout", "").startswith("@xdebug.apb.") for row in xouts):
        raise MatrixError(f"P3-D2 APB XOUT coverage drifted: {fixture_id}")
    cache = document.get("cache_contract", {})
    if (
        cache.get("base_hit_index", {}).get("private_probe_classification") !=
            "proven-unobservable"
        or cache.get("soft_lru", {}).get("private_eviction_classification") !=
            "proven-unobservable"
        or cache.get("hard_limit", {}).get("classification") !=
            "publicly-observable"
        or cache.get("hard_limit", {}).get("hard_max_bytes") != 1
    ):
        raise MatrixError(f"P3-D2 APB cache boundary drifted: {fixture_id}")
    hard_public = cache.get("hard_limit", {}).get("public_observations", [])
    if not hard_public or any(
        row.get("response", {}).get("error", {}).get("code") !=
            "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
        for row in hard_public if row.get("action") != "apb.config.load"
    ):
        raise MatrixError(f"P3-D2 APB hard limit is hidden: {fixture_id}")
    forbidden = {"hits", "misses", "evictions", "entry_count", "index_count",
                 "scanner_invocations", "resident_bytes", "access_sequence"}
    for group in (cache.get("soft_lru", {}), cache.get("hard_limit", {})):
        public_text = canonical_json(group.get("public_observations", []))
        if any(f'"{key}":' in public_text for key in forbidden):
            raise MatrixError(f"P3-D2 APB private cache probe leaked: {fixture_id}")
    return {
        "observation_count": len(observations),
        "transaction_count": len(transactions),
        "difference_count": 0,
        "xout_check_count": len(xouts),
        "cache_public_observation_count": sum(
            len(cache.get(name, {}).get("public_observations", []))
            for name in ("soft_lru", "hard_limit")
        ),
        "remaining_observable_gap_count": 0,
    }


def validate_p3d_apb_assets(
    repo_root: Path,
    manifest: dict,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    """Fail closed on both APB oracles and deterministic current fixtures."""

    rows = {}
    for fixture_id, oracle_path in P3D_APB_ORACLES.items():
        oracle_asset = current_assets.get(oracle_path.as_posix())
        if oracle_asset is None:
            raise MatrixError(f"P3-D2 APB oracle is not frozen: {fixture_id}")
        oracle = json.loads(validate_frozen_file(repo_root, oracle_asset).decode("utf-8"))
        row = validate_p3d_apb_oracle_document(oracle, fixture_id, original_assets)
        current_id = P3D_APB_EXPECTED[fixture_id]["current_fixture_id"]
        fixture_assets = sorted(
            [
                asset for asset in current_assets.values()
                if current_id in asset.get("fixture_ids", [])
            ],
            key=lambda asset: asset["path"],
        )
        expected_names = {
            "current.apb_vip": {
                "apb_vip_fixture_top.sv", "fixture.manifest.json",
                "fixture.sha256", "tb_apb_vip.cpp", "waves.fst",
            },
            "current.apb_xamba_vip": {
                "xdebug_apb_xamba_fixture_top.sv", "fixture.manifest.json",
                "fixture.sha256", "tb_apb_xamba.cpp", "waves.fst",
            },
        }[current_id]
        if {Path(asset["path"]).name for asset in fixture_assets} != expected_names:
            raise MatrixError(f"P3-D2 APB fixture inventory drifted: {current_id}")
        for asset in fixture_assets:
            validate_frozen_file(repo_root, asset)
        fixture_dir = repo_root / "testdata/fixtures" / current_id.removeprefix("current.")
        fixture_manifest = json.loads((fixture_dir / "fixture.manifest.json").read_text())
        source = fixture_manifest.get("source_contract", {})
        output = fixture_manifest.get("output_contract", {})
        expected = P3D_APB_EXPECTED[fixture_id]
        if (
            fixture_manifest.get("goal_id") != GOAL_ID
            or fixture_manifest.get("fixture_id") != current_id
            or source.get("original_fixture_id") != fixture_id
            or source.get("transaction_count") != expected["transaction_count"]
            or source.get("write_count") != expected["write_count"]
            or source.get("read_count") != expected["read_count"]
            or source.get("error_count") != expected["error_count"]
            or source.get("first_completion") != expected["first_time"]
            or source.get("last_completion") != expected["last_time"]
            or output.get("external_cache_rebuilt") is not False
            or output.get("sha256") != sha256_file(fixture_dir / "waves.fst")
        ):
            raise MatrixError(f"P3-D2 APB fixture contract drifted: {current_id}")
        lock_rows = {}
        for line in (fixture_dir / "fixture.sha256").read_text().splitlines():
            digest, name = line.split(maxsplit=1)
            lock_rows[name] = digest
        if set(lock_rows) != expected_names - {"fixture.sha256"} or any(
            sha256_file(fixture_dir / name) != digest
            for name, digest in lock_rows.items()
        ):
            raise MatrixError(f"P3-D2 APB fixture lock drifted: {current_id}")
        row.update({
            "path": oracle_path.as_posix(),
            "sha256": oracle_asset["sha256"],
            "current_fixture_id": current_id,
        })
        rows[fixture_id] = row
    return rows


def validate_p3d_axi_oracle_document(document: dict, fixture_id: str) -> dict:
    """Validate all AXI profiles, full-result contracts and cache boundary."""

    expected = P3D_AXI_EXPECTED[fixture_id]
    runs = document.get("runs")
    runtime = document.get("locked_runtime", {})
    session = document.get("session", {})
    if (
        document.get("schema_version") != "xdebug.p3d-axi-public-oracle.v1"
        or document.get("goal_id") != GOAL_ID
        or document.get("fixture_id") != fixture_id
        or document.get("producer_kind") != expected["producer_kind"]
        or document.get("run_count") != len(expected["profiles"])
        or not isinstance(runs, list)
        or len(runs) != len(expected["profiles"])
        or runtime.get("action_count") != 73
        or runtime.get("cache_reused") is not True
        or runtime.get("fixture_rebuilt") is not False
        or runtime.get("source_access") != "read_only"
        or session.get("all_runtime_writes_repository_local") is not True
        or session.get("fallback_used") is not False
        or session.get("fixture_rebuilt") is not False
        or session.get("source_access") != "read_only"
    ):
        raise MatrixError(f"P3-D3 AXI identity/runtime drifted: {fixture_id}")
    actions = document.get("action_authority", {}).get("axi_actions")
    if actions != [
        "axi.analysis", "axi.channel_stall", "axi.config.list",
        "axi.config.load", "axi.export", "axi.latency_outlier",
        "axi.outstanding_timeline", "axi.query",
        "axi.request_response_pair", "axi.statistics",
        "axi.transaction.cursor",
    ]:
        raise MatrixError(f"P3-D3 AXI public surface drifted: {fixture_id}")

    transaction_count = 0
    xout_count = 0
    artifact_count = 0
    seen_profiles = set()
    for run in runs:
        name = run.get("name")
        if name not in expected["profiles"] or name in seen_profiles:
            raise MatrixError(f"P3-D3 AXI profile identity drifted: {fixture_id}")
        seen_profiles.add(name)
        seed, direction_count, handshake_count, channels, source_sha = (
            expected["profiles"][name]
        )
        observations = run.get("observations")
        if (
            run.get("seed") != seed
            or run.get("expected_direction_count") != direction_count
            or run.get("handshake_line_count") != handshake_count
            or run.get("handshake_channel_counts") != channels
            or run.get("handshake_oracle", {}).get("sha256") != source_sha
            or run.get("observation_count") != 17
            or not isinstance(observations, list)
            or len(observations) != 17
            or len({row.get("observation_id") for row in observations}) != 17
        ):
            raise MatrixError(f"P3-D3 AXI profile contract drifted: {name}")
        by_id = {row["observation_id"]: row for row in observations}
        pair = by_id.get("pair.full", {}).get("response", {})
        pair_summary = pair.get("summary", {})
        export_row = by_id.get("export.full", {})
        export_summary = export_row.get("response", {}).get("summary", {})
        total = direction_count * 2
        if any(
            summary.get("total_count") != total
            or summary.get("returned_count") != total
            or summary.get("scan_complete") is not True
            or summary.get("analysis_complete") is not True
            or summary.get("response_truncated") is not False
            or summary.get("truncation_scopes") != []
            for summary in (pair_summary, export_summary)
        ):
            raise MatrixError(f"P3-D3 AXI full result is incomplete: {name}")
        if (
            export_summary.get("row_count") != total
            or export_summary.get("write_count") != direction_count
            or export_summary.get("read_count") != direction_count
        ):
            raise MatrixError(f"P3-D3 AXI export count drifted: {name}")
        artifacts = export_row.get("artifacts")
        if (
            not isinstance(artifacts, list)
            or len(artifacts) != 3
            or {row.get("name") for row in artifacts} != {
                "axi0.meta.json", "axi0.read.tsv", "axi0.write.tsv"
            }
            or any(
                not re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", ""))
                or row.get("size", 0) <= 0 for row in artifacts
            )
        ):
            raise MatrixError(f"P3-D3 AXI export artifact drifted: {name}")
        run_xouts = [row for row in observations if "xout" in row]
        if len(run_xouts) != 7 or any(
            not row.get("xout", "").startswith("@xdebug.axi.")
            for row in run_xouts
        ):
            raise MatrixError(f"P3-D3 AXI XOUT coverage drifted: {name}")
        transaction_count += total
        xout_count += len(run_xouts)
        artifact_count += len(artifacts)
    if seen_profiles != set(expected["profiles"]):
        raise MatrixError(f"P3-D3 AXI profile set drifted: {fixture_id}")

    cache = document.get("cache_contract", {})
    if (
        cache.get("soft_lru", {}).get("private_eviction_classification") !=
            "proven-unobservable"
        or cache.get("hard_limit", {}).get("classification") !=
            "publicly-observable"
        or cache.get("hard_limit", {}).get("hard_max_bytes") != 1
    ):
        raise MatrixError(f"P3-D3 AXI cache boundary drifted: {fixture_id}")
    return {
        "profile_count": len(runs),
        "observation_count": len(runs) * 17,
        "transaction_count": transaction_count,
        "difference_count": 0,
        "xout_check_count": xout_count,
        "artifact_check_count": artifact_count,
        "remaining_observable_gap_count": 0,
    }


def validate_p3d_axi_assets(
    repo_root: Path,
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    """Fail closed on both AXI oracles and their dedicated current fixtures."""

    rows = {}
    for fixture_id, oracle_path in P3D_AXI_ORACLES.items():
        oracle_asset = current_assets.get(oracle_path.as_posix())
        if oracle_asset is None:
            raise MatrixError(f"P3-D3 AXI oracle is not frozen: {fixture_id}")
        oracle = json.loads(
            validate_frozen_file(repo_root, oracle_asset).decode("utf-8")
        )
        row = validate_p3d_axi_oracle_document(oracle, fixture_id)
        current_id = P3D_AXI_EXPECTED[fixture_id]["current_fixture_id"]
        fixture_assets = sorted(
            (
                asset for asset in current_assets.values()
                if current_id in asset.get("fixture_ids", [])
            ),
            key=lambda asset: asset["path"],
        )
        fixture_dir = repo_root / "testdata/fixtures" / current_id.removeprefix(
            "current."
        )
        for asset in fixture_assets:
            validate_frozen_file(repo_root, asset)
        lock_rows = {}
        for line in (fixture_dir / "fixture.sha256").read_text().splitlines():
            digest, name = line.split(maxsplit=1)
            lock_rows[name] = digest
        expected_paths = {
            (fixture_dir / name).relative_to(repo_root).as_posix()
            for name in lock_rows
        } | {(fixture_dir / "fixture.sha256").relative_to(repo_root).as_posix()}
        if (
            {asset["path"] for asset in fixture_assets} != expected_paths
            or any(
                sha256_file(fixture_dir / name) != digest
                for name, digest in lock_rows.items()
            )
        ):
            raise MatrixError(f"P3-D3 AXI fixture inventory/lock drifted: {current_id}")
        fixture_manifest = json.loads(
            (fixture_dir / "fixture.manifest.json").read_text()
        )
        source = fixture_manifest.get("source_contract", {})
        output = fixture_manifest.get("output_contract", {})
        if (
            fixture_manifest.get("goal_id") != GOAL_ID
            or fixture_manifest.get("fixture_id") != current_id
            or source.get("original_fixture_id") != fixture_id
            or source.get("external_cache_rebuilt") is not False
            or output.get("external_cache_rebuilt") is not False
            or output.get("deterministic_build_directories") != 2
        ):
            raise MatrixError(f"P3-D3 AXI fixture contract drifted: {current_id}")
        if fixture_id == "xdebug.axi_vip":
            manifest_runs = fixture_manifest.get("runs", {})
            if set(manifest_runs) != set(P3D_AXI_EXPECTED[fixture_id]["profiles"]):
                raise MatrixError("P3-D3 SVT AXI manifest profile set drifted")
            for name, expected_run in P3D_AXI_EXPECTED[fixture_id]["profiles"].items():
                _, direction_count, handshake_count, _, source_sha = expected_run
                actual = manifest_runs[name]
                if (
                    actual.get("transaction_count") != direction_count * 2
                    or actual.get("handshake_count") != handshake_count
                    or actual.get("handshake_source_sha256") != source_sha
                    or actual.get("fst_sha256") != lock_rows[f"{name}/waves.fst"]
                ):
                    raise MatrixError(f"P3-D3 SVT AXI run drifted: {name}")
        elif source.get("transaction_count") != 64:
            raise MatrixError("P3-D3 XAMBA AXI transaction count drifted")
        row.update({
            "path": oracle_path.as_posix(),
            "sha256": oracle_asset["sha256"],
            "current_fixture_id": current_id,
        })
        rows[fixture_id] = row
    return rows


def validate_p3d_stream_differential_closure_audit(
    audit: dict,
    repo_root: Path,
    manifest: dict,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict:
    """Fail closed on the bounded P3-D1 stream/comparator/cache proof."""

    audit_asset = current_assets.get(
        P3D_STREAM_DIFFERENTIAL_CLOSURE_AUDIT.as_posix()
    )
    canonical_audit = canonical_json(audit).encode("utf-8")
    if (
        audit_asset is None
        or sha256_bytes(canonical_audit) != audit_asset.get("sha256")
        or len(canonical_audit) != audit_asset.get("size_bytes")
    ):
        raise MatrixError(
            "P3-D1 stream differential audit is not manifest-anchored"
        )
    if (
        audit.get("schema_version") !=
            "xdebug.p3d-stream-differential-closure-audit.v1"
        or audit.get("goal_id") != GOAL_ID
        or audit.get("fixture_id") != "xdebug.stream_differential_tool"
        or audit.get("classification") != "proven-unobservable"
    ):
        raise MatrixError(
            "P3-D1 stream differential audit belongs to another Goal/schema"
        )
    if audit.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
        "mode": "waveform",
        "source_access": "read_only",
        "transport": "uds",
    }:
        raise MatrixError(
            "P3-D1 stream differential write/cache/fallback boundary drifted"
        )

    def strings(value: object) -> Iterable[str]:
        if isinstance(value, str):
            yield value
        elif isinstance(value, list):
            for item in value:
                yield from strings(item)
        elif isinstance(value, dict):
            for key, item in value.items():
                yield from strings(key)
                yield from strings(item)

    if any(
            value.startswith("/") or "/home/" in value
            for value in strings(audit)):
        raise MatrixError(
            "P3-D1 stream differential audit contains an absolute path"
        )

    fixture_map = {
        item["id"]: item for item in manifest.get("original_fixtures", [])
    }
    differential_fixture = fixture_map.get(
        "xdebug.stream_differential_tool"
    )
    stream_fixture = fixture_map.get("xdebug.stream_v1")
    if differential_fixture is None or stream_fixture is None:
        raise MatrixError("P3-D1 original stream fixture contract is missing")
    expected_fixture_contract = {
        "builder_argv": differential_fixture["builder_argv"],
        "outputs": differential_fixture["outputs"],
        "reused_waveform_fixture_id": "xdebug.stream_v1",
        "rtl_input_count": 0,
        "source_dir": differential_fixture["source_dir"],
        "waveform_output_count": 0,
    }
    if audit.get("fixture_contract") != expected_fixture_contract:
        raise MatrixError(
            "P3-D1 differential fixture/output/reuse contract drifted"
        )
    if any(
        item["fixture_id"] == "xdebug.stream_differential_tool"
        for item in manifest["original_declared_waveform_outputs"]
    ):
        raise MatrixError(
            "P3-D1 differential tool unexpectedly declares a waveform output"
        )
    if not any(
        item["fixture_id"] == "xdebug.stream_v1"
        and item["path"] == "out/waves.fsdb"
        for item in manifest["original_declared_waveform_outputs"]
    ):
        raise MatrixError("P3-D1 reused stream_v1 FSDB contract is missing")

    current_fixture_paths = sorted(
        asset["path"] for asset in current_assets.values()
        if "current.stream_v1" in asset.get("fixture_ids", [])
    )
    expected_current_fixture_paths = sorted([
        "testdata/fixtures/stream_v1/fixture.manifest.json",
        "testdata/fixtures/stream_v1/fixture.sha256",
        "testdata/fixtures/stream_v1/stream_expected.json",
        "testdata/fixtures/stream_v1/stream_v1_top.sv",
        "testdata/fixtures/stream_v1/streams.json",
        "testdata/fixtures/stream_v1/tb_stream_v1.cpp",
        "testdata/fixtures/stream_v1/verilator-no-fsdb.patch",
        "testdata/fixtures/stream_v1/waves.fst",
    ])
    if current_fixture_paths != expected_current_fixture_paths:
        raise MatrixError("P3-D1 current stream_v1 fixture inventory drifted")
    for path in current_fixture_paths:
        validate_frozen_file(repo_root, current_assets[path])

    lock_path = "testdata/fixtures/stream_v1/fixture.sha256"
    lock_text = validate_frozen_file(
        repo_root, current_assets[lock_path]
    ).decode("utf-8")
    locked_names = [
        "stream_v1_top.sv",
        "streams.json",
        "stream_expected.json",
        "verilator-no-fsdb.patch",
        "tb_stream_v1.cpp",
        "fixture.manifest.json",
        "waves.fst",
    ]
    locked_hashes: dict[str, str] = {}
    for line in lock_text.splitlines():
        try:
            digest, name = line.split("  ", 1)
        except ValueError as error:
            raise MatrixError(
                "P3-D1 current stream_v1 fixture lock is malformed"
            ) from error
        if name in locked_hashes or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise MatrixError(
                "P3-D1 current stream_v1 fixture lock identity drifted"
            )
        locked_hashes[name] = digest
    if list(locked_hashes) != locked_names:
        raise MatrixError("P3-D1 current stream_v1 fixture lock order drifted")
    for name, digest in locked_hashes.items():
        path = f"testdata/fixtures/stream_v1/{name}"
        if current_assets[path]["sha256"] != digest:
            raise MatrixError(f"P3-D1 current fixture asset drifted: {path}")

    original_rtl_path = (
        "xdebug/testdata/waveform/stream_v1/tb/stream_v1_top.sv"
    )
    original_config_path = (
        "xdebug/testdata/waveform/stream_v1/config/streams.json"
    )
    original_rtl = original_assets.get(original_rtl_path)
    original_config = original_assets.get(original_config_path)
    if (
        original_rtl is None
        or original_config is None
        or locked_hashes["stream_v1_top.sv"] != original_rtl["sha256"]
        or locked_hashes["streams.json"] != original_config["sha256"]
    ):
        raise MatrixError(
            "P3-D1 current RTL/config is not byte-identical to the original"
        )

    oracle_documents: dict[str, tuple[dict, dict]] = {}
    for path in (P3D_STREAM_PUBLIC_ORACLE, P3D_STREAM_EXPORT_ORACLE):
        asset = current_assets.get(path.as_posix())
        if asset is None:
            raise MatrixError(f"P3-D1 stream oracle is not frozen: {path}")
        document = json.loads(
            validate_frozen_file(repo_root, asset).decode("utf-8")
        )
        oracle_documents[path.as_posix()] = (document, asset)
    public_oracle, public_oracle_asset = oracle_documents[
        P3D_STREAM_PUBLIC_ORACLE.as_posix()
    ]
    export_oracle, export_oracle_asset = oracle_documents[
        P3D_STREAM_EXPORT_ORACLE.as_posix()
    ]
    runtime_baseline = manifest["baselines"]["original_runtime"]
    stream_fixture_version = (
        "5eca27af24084f076f68c6a77c6fe0cb9e0a152332912dbf074cabc3b4600ede-"
        "prepare-c54cyr7t"
    )
    expected_original_runtime = {
        "action_count": 73,
        "binary_sha256": P3C_PHASE5_BINARY_SHA256,
        "build_id": (
            f"{runtime_baseline['runtime_revision'][:12]}-"
            f"{runtime_baseline['schema_revision']}"
        ),
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": stream_fixture_version,
        "npi_version": "X-2025.06-SP1",
        "runtime_revision": runtime_baseline["runtime_revision"],
        "schema_revision": runtime_baseline["schema_revision"],
        "source_access": "read_only",
        "wrapper_sha256": P3C_PHASE5_WRAPPER_SHA256,
    }
    expected_oracle_session = {
        "all_runtime_writes_repository_local": True,
        "closed_gracefully": True,
        "fallback_used": False,
        "mode": "waveform",
        "opened": True,
        "transport": "uds",
    }
    for name, document, schema, count in (
        (
            "public", public_oracle,
            "xdebug.p3d-stream-v1-public-oracle.v1", 58,
        ),
        (
            "export", export_oracle,
            "xdebug.p3d-stream-v1-export-oracle.v1", 6,
        ),
    ):
        observations = document.get("observations")
        if (
            document.get("schema_version") != schema
            or document.get("goal_id") != GOAL_ID
            or document.get("fixture_id") != "xdebug.stream_v1"
            or document.get("locked_runtime") != expected_original_runtime
            or document.get("session") != expected_oracle_session
            or document.get("observation_count") != count
            or not isinstance(observations, list)
            or len(observations) != count
            or len({row.get("observation_id") for row in observations}) != count
        ):
            raise MatrixError(f"P3-D1 {name} stream oracle identity drifted")
    public_fixture = public_oracle.get("original_fixture", {})
    if (
        public_fixture.get("rtl_path") != original_rtl_path
        or public_fixture.get("rtl_sha256") != original_rtl["sha256"]
        or public_fixture.get("config_path") != original_config_path
        or public_fixture.get("config_sha256") != original_config["sha256"]
        or public_fixture.get("expected_sha256") !=
            locked_hashes["stream_expected.json"]
        or public_fixture.get("fsdb_sha256") !=
            "0507c9c05c067f70d75037b3fdedd7bc8c4464d527df9b5eb0ffddfd811280d0"
        or public_fixture.get("fsdb_size") != 60524
    ):
        raise MatrixError("P3-D1 original stream_v1 fixture identity drifted")
    if {
        row.get("action") for row in public_oracle["observations"]
    } != {
        "stream.config.list", "stream.config.load", "stream.describe",
        "stream.query", "stream.validate",
    }:
        raise MatrixError("P3-D1 public stream Action coverage drifted")
    if export_oracle.get("action_discovery") != {
        "catalog_action_count": 73,
        "guide_error_code": "INVALID_REQUEST",
        "guide_invalid_arg": "args.output.view",
        "guide_request_supported": False,
        "request_schema_id": "xdebug.stream.export.request.v1",
        "request_schema_path": (
            "schemas/v1/actions/stream.export.request.schema.json"
        ),
    }:
        raise MatrixError("P3-D1 stream.export discovery boundary drifted")
    if [
        row.get("observation_id") for row in export_oracle["observations"]
    ] != [
        "transfer_preview", "packet_preview", "packet_beats_preview",
        "transfer_written", "packet_written", "packet_beats_written",
    ]:
        raise MatrixError("P3-D1 stream.export observation coverage drifted")

    expected_runtime = {
        "action_count": 73,
        "build_id": (
            "846edd6800bd-"
            "6ace27b232adefe5a896cb4222f9ea539e6127e600baf1761e560ec103872574"
        ),
        "cache_fingerprint": (
            "d165237d0fd3e6e89fcd9eb15cb8e146bd09fe46895952e86729f79c026dddf1"
        ),
        "cache_manifest_sha256": (
            "dc5271fac8753dfcc2a82dcb135dc134fa7f4136da4c07588efef51b81ac576d"
        ),
        "cache_reused": True,
        "engine_sha256": (
            "ac584ab512aac9122d2ebf656b3920da571edd224b7dc5692f25a6bd6e5683a0"
        ),
        "fixture_rebuilt": False,
        "fixture_version": (
            "d165237d0fd3e6e89fcd9eb15cb8e146bd09fe46895952e86729f79c026dddf1-"
            "prepare-jat8dddw"
        ),
        "frontend_sha256": (
            "9a5467c8d1d15c20b30c5baf1065a1d79d902cf53784ed256fc7ef93009f0bba"
        ),
        "legacy_object_sha256": (
            "d8e3621cc1e13e929ac1b341634c1bb3284f34e62ddd9c470d96184be138ca14"
        ),
        "npi_version": "X-2025.06-SP1",
        "runtime_revision": "846edd6800bd",
        "schema_revision": (
            "6ace27b232adefe5a896cb4222f9ea539e6127e600baf1761e560ec103872574"
        ),
        "source_access": "read_only",
        "stream_fixture_version": stream_fixture_version,
    }
    if audit.get("locked_runtime") != expected_runtime:
        raise MatrixError("P3-D1 differential runtime/cache identity drifted")

    expected_replay = {
        "artifact_check_count": 3,
        "comparator_failure_count": 0,
        "export_difference_count": 0,
        "export_observation_count": 6,
        "export_oracle_sha256": export_oracle_asset["sha256"],
        "query_config_difference_count": 0,
        "query_config_observation_count": 58,
        "query_config_oracle_sha256": public_oracle_asset["sha256"],
        "xout_check_count": 3,
    }
    if audit.get("comparator") != {
        "compile_guard": "XDEBUG_STREAM_DIFFERENTIAL_TEST_BUILD",
        "engine_failure_sentinels": [
            "legacy stream differential oracle failed: ",
            "stream columnar differential mismatch for ",
        ],
        "interposed_actions": [
            "stream.query", "stream.export", "stream.validate",
        ],
        "linked_object": "obj/tests/stream_differential/legacy_stream_oracle.o",
        "public_action": False,
        "public_replay": expected_replay,
        "remaining_public_difference_count": 0,
    }:
        raise MatrixError("P3-D1 differential comparator/replay proof drifted")

    source_paths = [
        "testinfra/fixtures.v1.yaml",
        "testinfra/catalog.v1.yaml",
        "xdebug/Makefile",
        "xdebug/tests/stream_differential/test_stream_differential.py",
        "xdebug/tests/stream_differential/legacy_stream_oracle.h",
        "xdebug/tests/stream_differential/legacy_stream_oracle.cpp",
        "xdebug/tests/synthetic/test_stream_v1_real_waveform.py",
        "xdebug/src/engine/service/actions/stream/stream_query.cpp",
        "xdebug/src/engine/service/actions/stream/stream_export.cpp",
        "xdebug/src/engine/service/actions/stream/stream_validate.cpp",
        "xdebug/src/waveform/cache/analysis_probe.h",
        "xdebug/src/waveform/cache/analysis_probe.cpp",
        "xdebug/src/waveform/cache/analysis_repository.cpp",
        "xdebug/src/waveform/stream/stream_analyzer.cpp",
        "xdebug/src/waveform/stream/stream_analyzer.h",
    ]
    expected_sources = []
    for path in source_paths:
        asset = original_assets.get(path)
        if asset is None:
            raise MatrixError(f"P3-D1 original source is not frozen: {path}")
        expected_sources.append({
            "path": path,
            "sha256": asset["sha256"],
            "size": asset["size_bytes"],
        })
    if audit.get("source_files") != expected_sources:
        raise MatrixError("P3-D1 differential source identity drifted")

    private_fields = [
        "access_sequence", "build_bytes", "entry_count", "evictions",
        "hits", "index_count", "key_summary", "misses",
        "resident_bytes", "scanner_invocations",
    ]
    schema_records = []
    for action in ("stream.query", "stream.export", "stream.validate"):
        relative = f"schemas/v1/actions/{action}.request.schema.json"
        path = repo_root / "compat/xdebug-v1" / relative
        if not path.is_file():
            raise MatrixError(f"P3-D1 request schema is missing: {relative}")
        schema = json.loads(path.read_text(encoding="utf-8"))
        serialized = json.dumps(
            schema, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        )
        if any(field in serialized for field in private_fields):
            raise MatrixError(
                f"P3-D1 private cache field leaked into {action} schema"
            )
        schema_records.append({
            "action": action,
            "schema_id": schema.get("$id"),
            "schema_path": relative,
            "schema_sha256": sha256_bytes(serialized.encode("utf-8")),
        })
    action_names = public_actions(repo_root)
    if any(
        "differential" in action or "probe" in action
        for action in action_names
    ):
        raise MatrixError("P3-D1 private comparator/probe leaked as an Action")
    if audit.get("public_boundary") != {
        "action_count": 73,
        "private_action_count": 0,
        "private_probe_fields": private_fields,
        "private_probe_fields_in_public_schema": [],
        "public_cache_error_code": "ANALYSIS_MEMORY_LIMIT_EXCEEDED",
        "request_schemas": schema_records,
    }:
        raise MatrixError("P3-D1 public Action/schema boundary drifted")

    cache = audit.get("cache_contract", {})
    stream_config = json.loads(
        (repo_root / "testdata/fixtures/stream_v1/streams.json").read_text(
            encoding="utf-8"
        )
    )
    ready_packet = next(
        (
            item for item in stream_config.get("streams", [])
            if item.get("name") == "ready_packet"
        ),
        None,
    )
    if ready_packet is None or cache.get("ready_packet_config") != ready_packet:
        raise MatrixError("P3-D1 cache stream configuration drifted")
    base_observations = cache.get("public_observations")
    expected_base_ids = [
        "static_validate", "range_a_query", "range_a_export",
        "range_a_validate", "range_b_query", "full_from_range",
        "derived_range", "description_load", "description_query",
        "semantic_load", "semantic_query", "invalid_range",
        "invalid_static",
    ]
    if (
        cache.get("public_observation_count") != 13
        or not isinstance(base_observations, list)
        or [row.get("observation_id") for row in base_observations] !=
            expected_base_ids
        or any(
            row.get("action") not in {
                "stream.config.load", "stream.export", "stream.query",
                "stream.validate",
            }
            for row in base_observations
        )
    ):
        raise MatrixError("P3-D1 public cache observation coverage drifted")
    static_summary = base_observations[0].get("response", {}).get(
        "summary", {}
    )
    if (
        static_summary.get("scan_complete") is not True
        or static_summary.get("analysis_complete") is not True
        or static_summary.get("response_truncated") is not False
    ):
        raise MatrixError("P3-D1 static stream.validate completion drifted")

    def validate_probe(
        name: str,
        probe: object,
        *,
        row_count: int,
        scanners: int,
        evictions: int,
        events: dict[str, int],
    ) -> None:
        if not isinstance(probe, dict) or set(probe) != {
            "event_counts", "last", "row_count",
        }:
            raise MatrixError(f"P3-D1 {name} private probe shape drifted")
        last = probe.get("last")
        event_counts = probe.get("event_counts")
        if (
            probe.get("row_count") != row_count
            or not isinstance(last, dict)
            or set(last) != set(private_fields)
            or not isinstance(event_counts, dict)
            or any(event_counts.get(key) != value for key, value in events.items())
            or last.get("scanner_invocations") != scanners
            or last.get("evictions") != evictions
            or not re.fullmatch(r"[0-9a-f]{16}", last.get("key_summary", ""))
            or any(
                not isinstance(value, int) or value < 0
                for key, value in last.items() if key != "key_summary"
            )
        ):
            raise MatrixError(f"P3-D1 {name} private probe metrics drifted")

    base_probe = cache.get("base_private_probe", {})
    if (
        base_probe.get("classification") != "proven-unobservable"
        or set(base_probe.get("checkpoints", {})) != {
            "two_ranges", "full_build", "final",
        }
    ):
        raise MatrixError("P3-D1 base cache private classification drifted")
    checkpoints = base_probe["checkpoints"]
    validate_probe(
        "two-range cache", checkpoints["two_ranges"],
        row_count=8, scanners=2, evictions=0,
        events={"build": 2, "hit": 2, "miss": 2, "scan": 2},
    )
    validate_probe(
        "full cache", checkpoints["full_build"],
        row_count=13, scanners=3, evictions=0,
        events={
            "build": 3, "hit": 2, "invalidate": 2, "miss": 3,
            "scan": 3,
        },
    )
    validate_probe(
        "final cache", checkpoints["final"],
        row_count=22, scanners=5, evictions=0,
        events={
            "build": 5, "hit": 3, "invalidate": 4, "miss": 5,
            "scan": 5,
        },
    )

    batch = cache.get("batch", {})
    batch_public = batch.get("public", {})
    if (
        batch_public.get("ok") is not True
        or batch_public.get("error") is not None
        or batch_public.get("summary") != {
            "all_ok": True,
            "count": 6,
            "failed_codes": [],
            "failed_count": 0,
            "failed_indexes": [],
            "failed_layers": [],
        }
        or len(batch_public.get("results", [])) != 6
        or any(
            result.get("ok") is not True
            for result in batch_public.get("results", [])
        )
    ):
        raise MatrixError("P3-D1 public batch cache contract drifted")
    validate_probe(
        "batch cache", batch.get("private_probe"),
        row_count=16, scanners=4, evictions=0,
        events={
            "build": 4, "hit": 2, "invalidate": 2, "miss": 4,
            "scan": 4,
        },
    )

    soft = cache.get("soft_lru", {})
    soft_observations = soft.get("public_observations")
    if (
        soft.get("private_eviction_classification") !=
            "proven-unobservable"
        or not isinstance(soft_observations, list)
        or [row.get("observation_id") for row in soft_observations] != [
            "soft_range_0", "soft_range_1", "soft_range_2",
        ]
        or any(
            row.get("response", {}).get("ok") is not True
            for row in soft_observations
        )
    ):
        raise MatrixError("P3-D1 soft-LRU public/private boundary drifted")
    validate_probe(
        "soft-LRU cache", soft.get("private_probe"),
        row_count=14, scanners=3, evictions=2,
        events={
            "build": 3, "evict": 2, "miss": 3,
            "oversize_admitted": 3, "scan": 3,
        },
    )

    hard = cache.get("hard_limit", {})
    allowed_projection = {
        "error.key_summary": (
            "opaque 16-hex cache identity may differ because the current fixture "
            "is native FST rather than the original FSDB"
        )
    }
    hard_public = hard.get("public", {})
    expected_hard_summary = {
        "all_ok": False,
        "count": 2,
        "failed_codes": [
            "ANALYSIS_MEMORY_LIMIT_EXCEEDED",
            "ANALYSIS_MEMORY_LIMIT_EXCEEDED",
        ],
        "failed_count": 2,
        "failed_indexes": [0, 1],
        "failed_layers": ["handler", "handler"],
    }
    if (
        hard.get("classification") != "publicly-observable"
        or hard.get("allowed_current_projection") != allowed_projection
        or hard.get("soft_max_bytes") != 1
        or hard.get("hard_max_bytes") != 1
        or hard_public.get("ok") is not True
        or hard_public.get("error") is not None
        or hard_public.get("summary") != expected_hard_summary
        or len(hard_public.get("results", [])) != 2
    ):
        raise MatrixError("P3-D1 public hard-limit boundary drifted")
    hard_keys = set()
    for result in hard_public["results"]:
        error = result.get("error", {})
        key_summary = error.get("key_summary", "")
        hard_keys.add(key_summary)
        if (
            result.get("ok") is not False
            or result.get("data") is not None
            or result.get("summary") != {
                "error_code": "ANALYSIS_MEMORY_LIMIT_EXCEEDED",
                "status": "error",
            }
            or error.get("code") != "ANALYSIS_MEMORY_LIMIT_EXCEEDED"
            or error.get("current_estimated_bytes") != 0
            or error.get("error_layer") != "handler"
            or error.get("hard_max_bytes") != 1
            or error.get("protocol") != "stream"
            or error.get("recoverable") is not True
            or error.get("message") != (
                "analysis cache build exceeds the configured hard memory limit"
            )
            or error.get("next_actions") != [
                (
                    "For stream analysis, explicitly retry with "
                    "cache_scope=range or a smaller time_range."
                ),
                (
                    "If range analysis still exceeds the limit, use x-npi "
                    "for one-off offline analysis."
                ),
            ]
            or not re.fullmatch(r"[0-9a-f]{16}", key_summary)
        ):
            raise MatrixError("P3-D1 hard-limit public error contract drifted")
    if len(hard_keys) != 1:
        raise MatrixError("P3-D1 hard-limit cache identity is inconsistent")
    validate_probe(
        "hard-limit cache", hard.get("private_probe"),
        row_count=2, scanners=0, evictions=0,
        events={"build_failed": 2},
    )

    if audit.get("closure") != {
        "differential_tool_classification_candidate": "proven-unobservable",
        "private_cache_metrics_classification": "proven-unobservable",
        "public_hard_limit_requires_current_gate": True,
        "remaining_unmapped_public_observation_count": 0,
        "stream_v1_classification_candidate": "semantic-equivalent",
    }:
        raise MatrixError("P3-D1 closure classification/gap count drifted")
    return audit


def validate_p3c_phase5_public_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
    runtime_revision: str,
    schema_revision: str,
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-phase5-public-oracle.v1":
        raise MatrixError("P3-C Phase5 public oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "phase5":
        raise MatrixError(
            "P3-C Phase5 public oracle belongs to a different Goal/group"
        )
    expected_runtime = {
        "action_count": 73,
        "binary_sha256": P3C_PHASE5_BINARY_SHA256,
        "build_id": f"{runtime_revision[:12]}-{schema_revision}",
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_PHASE5_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runtime_revision": runtime_revision,
        "schema_revision": schema_revision,
        "source_access": "read_only",
        "wrapper_sha256": P3C_PHASE5_WRAPPER_SHA256,
    }
    if oracle.get("locked_runtime") != expected_runtime:
        raise MatrixError("P3-C Phase5 locked public runtime identity drifted")
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "closed_gracefully": True,
        "fallback_used": False,
        "mode": "combined",
        "opened": True,
        "transport": "uds",
    }:
        raise MatrixError("P3-C Phase5 write/session/fallback boundary is not proven")

    def strings(value: object) -> Iterable[str]:
        if isinstance(value, str):
            yield value
        elif isinstance(value, list):
            for item in value:
                yield from strings(item)
        elif isinstance(value, dict):
            for key, item in value.items():
                yield from strings(key)
                yield from strings(item)

    if any(
            value.startswith("/") or "/home/" in value
            for value in strings(oracle)):
        raise MatrixError("P3-C Phase5 public oracle contains an absolute path")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 10
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C Phase5 catalog identity drifted")

    rows = oracle.get("rows")
    if not isinstance(rows, list) or len(rows) != 10:
        raise MatrixError("P3-C Phase5 public oracle must contain exactly ten rows")
    expected_signals = [
        "top.u_dut.dout[2]", "top.u_dut.dout[2]",
        "top.u_dut.dout[2]", "top.u_dut.dout[2]",
        "top.u_dut.dout[1]", "top.u_dut.dout[1]",
        "top.u_dut.flag[2]", "top.u_dut.flag[2]",
        "top.u_dut.dout[2]", "top.u_dut.flag[2]",
    ]
    expected_times = [
        "10ns", "20ns", "30ns", "41ns", "50ns",
        "60ns", "71ns", "81ns", "90ns", "100ns",
    ]
    expected_active_times = [
        "10ns", "20ns", "30ns", "41ns", "50ns",
        "50ns", "71ns", "71ns", "90ns", "71ns",
    ]
    expected_catalog_terminations = [
        "ambiguous", "primary_input", "primary_input", "primary_input",
        "control_only", "ambiguous", "control_only", "primary_input",
        "primary_input", "control_only",
    ]
    normal_rhs = [
        "top.u_dut.ctrl_mode", "top.u_dut.ctrl_sel", "top.u_dut.en1",
        "top.u_dut.src_a", "top.u_dut.src_b", "top.u_dut.src_c",
    ]
    special_flag_rhs = [
        "top.u_dut.en0", "top.u_dut.mask_a[ln]", "top.u_dut.mask_b[ln]",
    ]
    normal_flag_rhs = [
        "top.u_dut.ctrl_mode", "top.u_dut.ctrl_sel", "top.u_dut.en1",
        "top.u_dut.en2", "top.u_dut.mask_a[ln]",
        "top.u_dut.mask_b[ln]",
    ]
    digest_pattern = re.compile(r"[0-9a-f]{64}")
    mirror_paths = [
        (
            "xdebug/tests/active_trace_chain/phase5/dut.sv",
            "testdata/fixtures/active_trace/rtl/phase5/dut.sv",
        ),
        (
            "xdebug/tests/active_trace_chain/phase5/tb.sv",
            "testdata/fixtures/active_trace/rtl/phase5/tb.sv",
        ),
    ]
    fixture_root = "testdata/fixtures/active_trace/phase5/phase5"
    result = {}
    for ordinal, row in enumerate(rows, 1):
        scenario_id = f"active.phase5.{ordinal:02d}"
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or row.get("case") != "phase5"
            or row.get("catalog_expectation") != {
                "termination": expected_catalog_terminations[ordinal - 1]
            }
        ):
            raise MatrixError(f"P3-C Phase5 row identity drifted: {scenario_id}")

        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != 2:
            raise MatrixError(
                f"P3-C Phase5 RTL mirror inventory drifted: {scenario_id}"
            )
        by_paths = {
            (mirror.get("original_path"), mirror.get("current_path")): mirror
            for mirror in mirrors
        }
        for original_path, current_path in mirror_paths:
            mirror = by_paths.get((original_path, current_path), {})
            original_asset = original_assets.get(original_path)
            current_asset = current_assets.get(current_path)
            if (
                mirror.get("byte_identical") is not True
                or original_asset is None
                or current_asset is None
                or mirror.get("sha256") != original_asset["sha256"]
                or current_asset["sha256"] != original_asset["sha256"]
                or mirror.get("size") != original_asset["size_bytes"]
                or current_asset["size_bytes"] != original_asset["size_bytes"]
            ):
                raise MatrixError(
                    f"P3-C Phase5 RTL mirror drifted: {scenario_id}"
                )

        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(
                f"P3-C Phase5 fixture lock is missing: {scenario_id}"
            )
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            *(current_path for _, current_path in mirror_paths),
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(
                f"P3-C Phase5 fixture lock inventory drifted: {scenario_id}"
            )
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C Phase5 fixture evidence drifted: {scenario_id}: {path}"
                )

        signal = expected_signals[ordinal - 1]
        request_time = expected_times[ordinal - 1]
        active_time = expected_active_times[ordinal - 1]
        if row.get("request") != {
            "render_time_unit": "ns", "signal": signal, "time": request_time,
        } or row.get("limits") != {"max_depth": 64, "max_nodes": 64}:
            raise MatrixError(f"P3-C Phase5 request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            original_fixture.get("fsdb_sha256") !=
                "9fdc31f039e65a24a25cb2808f0d62c232e3c4d91ea90e0cf6252c9e1d2df7ba"
            or original_fixture.get("fsdb_size") != 10799
        ):
            raise MatrixError(
                f"P3-C Phase5 original fixture proof drifted: {scenario_id}"
            )

        response = row.get("response", {})
        summary = response.get("summary", {})
        evidence = response.get("data", {}).get("ambiguity_evidence", {})
        is_flag = "flag" in signal
        expected_detail = (
            "multiple_active_candidates" if is_flag else "multiple_rhs_sources"
        )
        expected_hops = 0 if is_flag else 1
        if (
            summary.get("signal") != signal
            or summary.get("time") != request_time
            or summary.get("termination") != "ambiguous"
            or summary.get("termination_detail") != expected_detail
            or summary.get("scan_complete") is not True
            or summary.get("analysis_complete") is not True
            or summary.get("response_truncated") is not False
            or summary.get("total_count") != expected_hops
            or summary.get("returned_count") != expected_hops
            or summary.get("truncation_scopes") != []
            or summary.get("value_width_complete") is not is_flag
            or (is_flag and summary.get("width_diagnostics") != [])
            or (not is_flag and summary.get("width_diagnostics") != [{
                "reason": "npi_range_size_unavailable",
                "role": "hops[0].value",
                "signal": signal,
            }])
        ):
            raise MatrixError(
                f"P3-C Phase5 locked full response is incomplete: {scenario_id}"
            )

        expected_rhs = (
            [special_flag_rhs, normal_flag_rhs] if is_flag else [normal_rhs]
        )
        expected_lines = [34, 39] if is_flag else [37]
        statements = evidence.get("statements")
        if (
            evidence.get("kind") != expected_detail
            or evidence.get("signal") != signal
            or evidence.get("active_time") != active_time
            or evidence.get("hop_index") != 0
            or evidence.get("statement_count") != len(expected_rhs)
            or evidence.get("rhs_signal_count") != sum(map(len, expected_rhs))
            or evidence.get("returned_rhs_signal_count") !=
                sum(map(len, expected_rhs))
            or evidence.get("omitted_rhs_signal_count") != 0
            or evidence.get("analysis_complete") is not True
            or evidence.get("truncation_scopes") != []
            or not isinstance(statements, list)
            or len(statements) != len(expected_rhs)
        ):
            raise MatrixError(
                f"P3-C Phase5 ambiguity evidence drifted: {scenario_id}"
            )
        for statement, line, rhs_names in zip(
                statements, expected_lines, expected_rhs):
            samples = statement.get("rhs_samples")
            if (
                statement.get("kind") != "assignment"
                or not statement.get("driver")
                or statement.get("file") != mirror_paths[0][0]
                or statement.get("line") != line
                or statement.get("rhs_signal_count") != len(rhs_names)
                or statement.get("returned_rhs_signal_count") != len(rhs_names)
                or statement.get("complete") is not True
                or not isinstance(samples, list)
                or [sample.get("signal") for sample in samples] != rhs_names
            ):
                raise MatrixError(
                    f"P3-C Phase5 statement/RHS evidence drifted: {scenario_id}"
                )
            for sample in samples:
                before = sample.get("before", {})
                after = sample.get("after", {})
                if sample["signal"].endswith("[ln]"):
                    missing = {
                        "known": None, "status": "signal_not_found",
                        "value": None, "value_time": None,
                    }
                    if before != missing or after != missing or \
                            sample.get("changed") is not None:
                        raise MatrixError(
                            f"P3-C Phase5 dynamic selector evidence drifted: "
                            f"{scenario_id}"
                        )
                elif (
                    before.get("status") != "ok"
                    or after.get("status") != "ok"
                    or after.get("value_time") != active_time
                    or not isinstance(sample.get("changed"), bool)
                ):
                    raise MatrixError(
                        f"P3-C Phase5 sampled value evidence drifted: {scenario_id}"
                    )

        hops = response.get("data", {}).get("hops")
        if not isinstance(hops, list) or len(hops) != expected_hops:
            raise MatrixError(f"P3-C Phase5 hop inventory drifted: {scenario_id}")
        if hops:
            hop = hops[0]
            if (
                hop.get("index") != 0
                or hop.get("chain_id") != "c0"
                or hop.get("signal") != signal
                or hop.get("relation") != "root"
                or hop.get("file") != mirror_paths[0][0]
                or hop.get("line") != 37
                or hop.get("time") != request_time
                or hop.get("active_time") != active_time
                or not isinstance(hop.get("value"), str)
                or not hop["value"].startswith("'h")
            ):
                raise MatrixError(
                    f"P3-C Phase5 hop evidence drifted: {scenario_id}"
                )
        result[scenario_id] = row
    return result


def validate_p3c_p0_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C P0 oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "p0":
        raise MatrixError("P3-C P0 oracle belongs to a different Goal/group")
    locked = oracle.get("locked_runtime", {})
    if locked != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_P0_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError("P3-C P0 locked native runtime/cache identity drifted")
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C P0 write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 6
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C P0 catalog identity drifted")

    rows = oracle.get("rows")
    expected_results = [
        ("ambiguous", 3),
        ("primary_input", 4),
        ("control_only", 1),
        ("control_only", 1),
        ("ambiguous", 1),
        ("primary_input", 1),
    ]
    if not isinstance(rows, list) or len(rows) != len(expected_results):
        raise MatrixError("P3-C P0 oracle must contain exactly six rows")

    digest_pattern = re.compile(r"[0-9a-f]{64}")
    result = {}
    has_source_less_control = False
    has_control_only_candidates = False
    for ordinal, (row, expected_result) in enumerate(
            zip(rows, expected_results), 1):
        scenario_id = f"active.p0.{ordinal:02d}"
        case = row.get("case")
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or not isinstance(case, str)
            or not case
        ):
            raise MatrixError(f"P3-C P0 row identity drifted: {scenario_id}")

        original_path = (
            "xdebug/tests/active_trace_chain/p0_composability/"
            f"{case}/tb.sv"
        )
        current_path = f"testdata/fixtures/active_trace/rtl/p0/{case}/tb.sv"
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != 1:
            raise MatrixError(f"P3-C P0 RTL mirror inventory drifted: {scenario_id}")
        mirror = mirrors[0]
        original_asset = original_assets.get(original_path)
        current_asset = current_assets.get(current_path)
        if (
            mirror.get("byte_identical") is not True
            or mirror.get("original_path") != original_path
            or mirror.get("current_path") != current_path
            or original_asset is None
            or current_asset is None
            or mirror.get("sha256") != original_asset["sha256"]
            or current_asset["sha256"] != original_asset["sha256"]
            or mirror.get("size") != original_asset["size_bytes"]
            or current_asset["size_bytes"] != original_asset["size_bytes"]
        ):
            raise MatrixError(f"P3-C P0 RTL mirror drifted: {scenario_id}")

        fixture_root = f"testdata/fixtures/active_trace/p0/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(f"P3-C P0 fixture lock is missing: {scenario_id}")
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            current_path,
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(f"P3-C P0 fixture lock inventory drifted: {scenario_id}")
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C P0 fixture evidence drifted: {scenario_id}: {path}"
                )

        request = row.get("request", {})
        if (
            not isinstance(request.get("signal"), str)
            or not isinstance(request.get("time"), str)
            or request.get("stop_on_temporal") is not False
        ):
            raise MatrixError(f"P3-C P0 request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(f"P3-C P0 original fixture proof drifted: {scenario_id}")

        native = row.get("native_result", {})
        termination, hop_count = expected_result
        chain = native.get("chain")
        if (
            native.get("termination") != termination
            or native.get("total_hops") != hop_count
            or not isinstance(chain, list)
            or len(chain) != hop_count
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or native.get("temporal_boundaries") != sum(
                hop.get("hop_type") == "temporal_boundary" for hop in chain
            )
        ):
            raise MatrixError(f"P3-C P0 native result is incomplete: {scenario_id}")
        has_source_less_control |= any(
            hop.get("file") == "" and hop.get("line") == 0 for hop in chain
        )
        branches = native.get("branch_evidence")
        if not isinstance(branches, list):
            raise MatrixError(f"P3-C P0 branch evidence drifted: {scenario_id}")
        has_control_only_candidates |= termination == "control_only" and bool(branches)
        result[scenario_id] = row

    if not has_source_less_control or not has_control_only_candidates:
        raise MatrixError("P3-C P0 frozen schema representability boundaries disappeared")
    return result


def validate_p3c_composite_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C composite oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "composite":
        raise MatrixError(
            "P3-C composite oracle belongs to a different Goal/group"
        )
    if oracle.get("locked_runtime") != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_COMPOSITE_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError(
            "P3-C composite locked native runtime/cache identity drifted"
        )
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C composite write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 20
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C composite catalog identity drifted")

    expected_results = [
        ("primary_input", 11), ("primary_input", 11),
        ("primary_input", 11), ("primary_input", 12),
        ("primary_input", 12), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 6),
        ("ambiguous", 6), ("ambiguous", 7),
        ("primary_input", 6), ("primary_input", 6),
        ("ambiguous", 7), ("primary_input", 7),
        ("primary_input", 7), ("primary_input", 6),
    ]
    rows = oracle.get("rows")
    if not isinstance(rows, list) or len(rows) != len(expected_results):
        raise MatrixError("P3-C composite oracle must contain exactly twenty rows")

    digest_pattern = re.compile(r"[0-9a-f]{64}")
    expected_before = ["1", "0", "1", "0", "0", "1", "0", "1"]
    expected_after = ["0", "1", "0", "1", "1", "0", "1", "0"]
    result = {}
    source_less_hops = 0
    for ordinal, (row, expected_result) in enumerate(
            zip(rows, expected_results), 1):
        scenario_id = f"active.composite.{ordinal:02d}"
        case = f"case_{ordinal:02d}"
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or row.get("case") != case
        ):
            raise MatrixError(
                f"P3-C composite row identity drifted: {scenario_id}"
            )

        mirror_paths = [
            (
                f"xdebug/tests/active_trace_chain/composite/{case}/tb.sv",
                f"testdata/fixtures/active_trace/rtl/composite/{case}/tb.sv",
            ),
            (
                "xdebug/tests/active_trace_chain/composite/chain_dut.sv",
                "testdata/fixtures/active_trace/rtl/composite/chain_dut.sv",
            ),
        ]
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != len(mirror_paths):
            raise MatrixError(
                f"P3-C composite RTL mirror inventory drifted: {scenario_id}"
            )
        by_paths = {
            (mirror.get("original_path"), mirror.get("current_path")): mirror
            for mirror in mirrors
        }
        for original_path, current_path in mirror_paths:
            mirror = by_paths.get((original_path, current_path), {})
            original_asset = original_assets.get(original_path)
            current_asset = current_assets.get(current_path)
            if (
                mirror.get("byte_identical") is not True
                or original_asset is None
                or current_asset is None
                or mirror.get("sha256") != original_asset["sha256"]
                or current_asset["sha256"] != original_asset["sha256"]
                or mirror.get("size") != original_asset["size_bytes"]
                or current_asset["size_bytes"] != original_asset["size_bytes"]
            ):
                raise MatrixError(
                    f"P3-C composite RTL mirror drifted: {scenario_id}"
                )

        fixture_root = f"testdata/fixtures/active_trace/composite/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(
                f"P3-C composite fixture lock is missing: {scenario_id}"
            )
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            *(current_path for _, current_path in mirror_paths),
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(
                f"P3-C composite fixture lock inventory drifted: {scenario_id}"
            )
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C composite fixture evidence drifted: "
                    f"{scenario_id}: {path}"
                )

        request = row.get("request", {})
        if (
            request != {
                "signal": "top.data_out",
                "stop_on_temporal": False,
                "time": "50ns",
            }
        ):
            raise MatrixError(f"P3-C composite request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(
                f"P3-C composite original fixture proof drifted: {scenario_id}"
            )

        native = row.get("native_result", {})
        termination, hop_count = expected_result
        chain = native.get("chain")
        if (
            native.get("termination") != termination
            or native.get("total_hops") != hop_count
            or native.get("active_trace_calls") != hop_count
            or not isinstance(chain, list)
            or len(chain) != hop_count
            or native.get("temporal_boundaries") != 1
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or not all(hop.get("value_known") is True for hop in chain)
            or row.get("catalog_expectation") != {
                "hops": hop_count,
                "temporal_boundaries": 1,
                "termination": termination,
            }
        ):
            raise MatrixError(
                f"P3-C composite native result is incomplete: {scenario_id}"
            )
        source_less_hops += sum(
            hop.get("file") == "" and hop.get("line") == 0 for hop in chain
        )

        branches = native.get("branch_evidence")
        if not isinstance(branches, list):
            raise MatrixError(
                f"P3-C composite branch evidence drifted: {scenario_id}"
            )
        if termination == "ambiguous":
            branch = branches[0] if len(branches) == 1 else {}
            candidates = branch.get("candidates", [])
            expected_names = [
                "top.u_dut.g_g.u_gen.in[" + str(index) + "]"
                for index in range(8)
            ]
            if (
                branch.get("signal") != "top.u_dut.gen_out"
                or branch.get("time") != "35.0n"
                or branch.get("reason") != "8 signals toggled simultaneously"
                or [item.get("name") for item in candidates] != expected_names
                or [item.get("role") for item in candidates] != ["data"] * 8
                or [item.get("toggled") for item in candidates] != [True] * 8
                or [item.get("before") for item in candidates] != expected_before
                or [item.get("after") for item in candidates] != expected_after
            ):
                raise MatrixError(
                    f"P3-C composite branch evidence drifted: {scenario_id}"
                )
        elif branches:
            raise MatrixError(
                f"P3-C composite primary-input row has branch evidence: "
                f"{scenario_id}"
            )
        result[scenario_id] = row

    if source_less_hops != 5:
        raise MatrixError("P3-C composite source-less endpoint count drifted")
    return result


def validate_p3c_timing_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C timing oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "timing":
        raise MatrixError("P3-C timing oracle belongs to a different Goal/group")
    if oracle.get("locked_runtime") != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_TIMING_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError("P3-C timing locked native runtime/cache identity drifted")
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C timing write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 12
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C timing catalog identity drifted")

    rows = oracle.get("rows")
    if not isinstance(rows, list) or len(rows) != 12:
        raise MatrixError("P3-C timing oracle must contain exactly twelve rows")

    expected_request_times = [
        "75ns", "35ns", "75ns", "75ns", "75ns", "75ns",
        "75ns", "75ns", "75ns", "15ns", "75ns", "75ns",
    ]
    expected_active_times = [
        "55.0n", "25.0n", "55.0n", "55.0n", "55.0n", "55.0n",
        "55.0n", "55.0n", "45.0n", "0.00", "55.0n", "55.0n",
    ]
    expected_values = [
        "01011010", "10100101", "01011010", "01011010",
        "01011010", "01011010", "01011010", "01011010",
        "01011010", "", "01011010", "01011010",
    ]
    digest_pattern = re.compile(r"[0-9a-f]{64}")
    shared_original = (
        "xdebug/tests/active_trace_chain/timing/timing_boundary_dut.sv"
    )
    shared_current = (
        "testdata/fixtures/active_trace/rtl/timing/timing_boundary_dut.sv"
    )
    result = {}
    for ordinal, (row, request_time, active_time, expected_value) in enumerate(
            zip(rows, expected_request_times, expected_active_times,
                expected_values), 1):
        scenario_id = f"active.timing.{ordinal:02d}"
        case = f"case_{ordinal:02d}"
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or row.get("case") != case
        ):
            raise MatrixError(f"P3-C timing row identity drifted: {scenario_id}")

        case_original = (
            f"xdebug/tests/active_trace_chain/timing/{case}/tb.sv"
        )
        case_current = (
            f"testdata/fixtures/active_trace/rtl/timing/{case}/tb.sv"
        )
        mirror_paths = [
            (case_original, case_current),
            (shared_original, shared_current),
        ]
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != 2:
            raise MatrixError(
                f"P3-C timing RTL mirror inventory drifted: {scenario_id}"
            )
        by_paths = {
            (mirror.get("original_path"), mirror.get("current_path")): mirror
            for mirror in mirrors
        }
        for original_path, current_path in mirror_paths:
            mirror = by_paths.get((original_path, current_path), {})
            original_asset = original_assets.get(original_path)
            current_asset = current_assets.get(current_path)
            if (
                mirror.get("byte_identical") is not True
                or original_asset is None
                or current_asset is None
                or mirror.get("sha256") != original_asset["sha256"]
                or current_asset["sha256"] != original_asset["sha256"]
                or mirror.get("size") != original_asset["size_bytes"]
                or current_asset["size_bytes"] != original_asset["size_bytes"]
            ):
                raise MatrixError(
                    f"P3-C timing RTL mirror drifted: {scenario_id}"
                )

        fixture_root = f"testdata/fixtures/active_trace/timing/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(
                f"P3-C timing fixture lock is missing: {scenario_id}"
            )
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            case_current,
            shared_current,
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(
                f"P3-C timing fixture lock inventory drifted: {scenario_id}"
            )
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C timing fixture evidence drifted: {scenario_id}: {path}"
                )

        request = row.get("request", {})
        if request != {
            "signal": "top.data_out",
            "stop_on_temporal": True,
            "time": request_time,
        }:
            raise MatrixError(f"P3-C timing request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(
                f"P3-C timing original fixture proof drifted: {scenario_id}"
            )

        native = row.get("native_result", {})
        chain = native.get("chain")
        if (
            native.get("termination") != "temporal_boundary"
            or native.get("total_hops") != 1
            or native.get("active_trace_calls") != 1
            or native.get("temporal_boundaries") != 1
            or native.get("temporal_boundary_stops") != 1
            or native.get("edgecheck_direct_count") != 1
            or native.get("fallback_0_5ns_count") != 0
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or native.get("branch_evidence") != []
            or not isinstance(chain, list)
            or len(chain) != 1
            or row.get("catalog_expectation") != {}
        ):
            raise MatrixError(
                f"P3-C timing native result is incomplete: {scenario_id}"
            )
        hop = chain[0]
        if (
            hop.get("hop") != 0
            or hop.get("hop_type") != "temporal_boundary"
            or hop.get("driver_kind") != "cont_assign"
            or hop.get("signal") != "top.data_out"
            or hop.get("requested_time") != request_time
            or hop.get("file") != shared_original
            or hop.get("line") != 44
            or hop.get("value_known") is not True
            or hop.get("value") != expected_value
        ):
            raise MatrixError(
                f"P3-C timing native hop drifted: {scenario_id}"
            )
        if (
            hop.get("active_time") != active_time
            or hop.get("next_time") != active_time
        ):
            raise MatrixError(
                f"P3-C timing temporal boundary drifted: {scenario_id}"
            )
        result[scenario_id] = row
    return result


def validate_p3c_phase4_oracle(
    oracle: dict,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if oracle.get("schema_version") != \
            "xdebug.p3c-original-active-trace-oracle.v1":
        raise MatrixError("P3-C Phase4 oracle has the wrong schema_version")
    if oracle.get("goal_id") != GOAL_ID or oracle.get("group") != "phase4":
        raise MatrixError("P3-C Phase4 oracle belongs to a different Goal/group")
    if oracle.get("locked_runtime") != {
        "cache_reused": True,
        "fixture_rebuilt": False,
        "fixture_version": P3C_PHASE4_FIXTURE_VERSION,
        "npi_version": "X-2025.06-SP1",
        "runner_sha256": P3C_P0_RUNNER_SHA256,
        "source_access": "read_only",
    }:
        raise MatrixError("P3-C Phase4 locked native runtime/cache identity drifted")
    if oracle.get("session") != {
        "all_runtime_writes_repository_local": True,
        "fallback_used": False,
        "mode": "native_chain_test",
    }:
        raise MatrixError("P3-C Phase4 write/fallback boundary is not proven")

    catalog_asset = original_assets.get(ACTIVE_CATALOG.as_posix())
    mirrored_catalog = current_assets.get(
        "testdata/fixtures/active_trace/original-cases.v1.yaml"
    )
    catalog = oracle.get("catalog", {})
    if (
        catalog_asset is None
        or mirrored_catalog is None
        or catalog.get("schema_version") != "xdebug-active-trace-cases.v1"
        or catalog.get("row_count") != 20
        or catalog.get("sha256") != catalog_asset["sha256"]
        or mirrored_catalog["sha256"] != catalog_asset["sha256"]
    ):
        raise MatrixError("P3-C Phase4 catalog identity drifted")

    expected_results = [
        ("primary_input", 16), ("primary_input", 16),
        ("primary_input", 16), ("primary_input", 17),
        ("primary_input", 16), ("ambiguous", 11),
        ("ambiguous", 11), ("ambiguous", 11),
        ("ambiguous", 10), ("ambiguous", 11),
        ("ambiguous", 11), ("ambiguous", 11),
        ("ambiguous", 11), ("ambiguous", 12),
        ("primary_input", 11), ("primary_input", 11),
        ("ambiguous", 11), ("primary_input", 12),
        ("primary_input", 12), ("primary_input", 11),
    ]
    rows = oracle.get("rows")
    if not isinstance(rows, list) or len(rows) != len(expected_results):
        raise MatrixError("P3-C Phase4 oracle must contain exactly twenty rows")

    digest_pattern = re.compile(r"[0-9a-f]{64}")
    expected_toggles = [True, False, True, False, False, True, False, True]
    result = {}
    source_less_hops = 0
    for ordinal, (row, expected_result) in enumerate(
            zip(rows, expected_results), 1):
        scenario_id = f"active.phase4.{ordinal:02d}"
        case = f"case_{ordinal:02d}"
        if (
            row.get("scenario_id") != scenario_id
            or row.get("catalog_index") != ordinal
            or row.get("case") != case
        ):
            raise MatrixError(f"P3-C Phase4 row identity drifted: {scenario_id}")

        mirror_paths = [
            (
                f"xdebug/tests/active_trace_chain/phase4/{case}/tb.sv",
                f"testdata/fixtures/active_trace/rtl/phase4/{case}/tb.sv",
            ),
            (
                "xdebug/tests/active_trace_chain/phase4/phase4_dut.sv",
                "testdata/fixtures/active_trace/rtl/phase4/phase4_dut.sv",
            ),
            (
                "xdebug/tests/active_trace_chain/composite/chain_dut.sv",
                "testdata/fixtures/active_trace/rtl/composite/chain_dut.sv",
            ),
        ]
        mirrors = row.get("rtl_mirrors")
        if not isinstance(mirrors, list) or len(mirrors) != len(mirror_paths):
            raise MatrixError(
                f"P3-C Phase4 RTL mirror inventory drifted: {scenario_id}"
            )
        by_paths = {
            (mirror.get("original_path"), mirror.get("current_path")): mirror
            for mirror in mirrors
        }
        for original_path, current_path in mirror_paths:
            mirror = by_paths.get((original_path, current_path), {})
            original_asset = original_assets.get(original_path)
            current_asset = current_assets.get(current_path)
            if (
                mirror.get("byte_identical") is not True
                or original_asset is None
                or current_asset is None
                or mirror.get("sha256") != original_asset["sha256"]
                or current_asset["sha256"] != original_asset["sha256"]
                or mirror.get("size") != original_asset["size_bytes"]
                or current_asset["size_bytes"] != original_asset["size_bytes"]
            ):
                raise MatrixError(
                    f"P3-C Phase4 RTL mirror drifted: {scenario_id}"
                )

        fixture_root = f"testdata/fixtures/active_trace/phase4/{case}"
        lock_path = f"{fixture_root}/fixture.sha256"
        lock_asset = current_assets.get(lock_path)
        if lock_asset is None:
            raise MatrixError(
                f"P3-C Phase4 fixture lock is missing: {scenario_id}"
            )
        recorded = {}
        lock_text = validate_frozen_file(repo_root, lock_asset).decode("utf-8")
        for line in lock_text.splitlines():
            checksum, path = line.split(maxsplit=1)
            recorded[path] = checksum
        expected_paths = {
            *(current_path for _, current_path in mirror_paths),
            "testdata/fixtures/active_trace/dump_probe.sv",
            f"{fixture_root}/waves.fst",
            f"{fixture_root}/design_db/Vactive_trace__DesignDb.xddb",
            f"{fixture_root}/design_db/xdebug-design-db.json",
        }
        if set(recorded) != expected_paths:
            raise MatrixError(
                f"P3-C Phase4 fixture lock inventory drifted: {scenario_id}"
            )
        for path, checksum in recorded.items():
            asset = current_assets.get(path)
            if (
                asset is None
                or not digest_pattern.fullmatch(checksum)
                or asset["sha256"] != checksum
            ):
                raise MatrixError(
                    f"P3-C Phase4 fixture evidence drifted: {scenario_id}: {path}"
                )

        request = row.get("request", {})
        if (
            not isinstance(request.get("signal"), str)
            or not isinstance(request.get("time"), str)
            or request.get("stop_on_temporal") is not False
        ):
            raise MatrixError(f"P3-C Phase4 request drifted: {scenario_id}")
        original_fixture = row.get("fixture", {})
        if (
            not digest_pattern.fullmatch(original_fixture.get("fsdb_sha256", ""))
            or original_fixture.get("fsdb_size", 0) <= 0
        ):
            raise MatrixError(
                f"P3-C Phase4 original fixture proof drifted: {scenario_id}"
            )

        native = row.get("native_result", {})
        termination, hop_count = expected_result
        chain = native.get("chain")
        if (
            native.get("termination") != termination
            or native.get("total_hops") != hop_count
            or native.get("active_trace_calls") != hop_count
            or not isinstance(chain, list)
            or len(chain) != hop_count
            or native.get("temporal_boundaries") != 2
            or native.get("truncated") is not False
            or native.get("limitations") != []
            or not all(hop.get("value_known") is True for hop in chain)
            or row.get("catalog_expectation") != {
                "hops": hop_count,
                "temporal_boundaries": 2,
                "termination": termination,
            }
        ):
            raise MatrixError(
                f"P3-C Phase4 native result is incomplete: {scenario_id}"
            )
        source_less_hops += sum(
            hop.get("file") == "" and hop.get("line") == 0 for hop in chain
        )

        branches = native.get("branch_evidence")
        if not isinstance(branches, list):
            raise MatrixError(
                f"P3-C Phase4 branch evidence drifted: {scenario_id}"
        )
        if termination == "ambiguous":
            branch = branches[0] if len(branches) == 1 else {}
            candidates = branch.get("candidates", [])
            expected_names = [
                "top.u_dut.u_pre.g_g.u_gen.in[" + str(index) + "]"
                for index in range(8)
            ]
            if (
                branch.get("signal") != "top.u_dut.u_pre.gen_out"
                or branch.get("time") != "15.0n"
                or branch.get("reason") != "4 signals toggled simultaneously"
                or [item.get("name") for item in candidates] != expected_names
                or [item.get("role") for item in candidates] != ["data"] * 8
                or [item.get("toggled") for item in candidates] != expected_toggles
                or [item.get("before") for item in candidates] != ["0"] * 8
                or [item.get("after") for item in candidates] != [
                    "1" if toggled else "0" for toggled in expected_toggles
                ]
            ):
                raise MatrixError(
                    f"P3-C Phase4 branch evidence drifted: {scenario_id}"
                )
        elif branches:
            raise MatrixError(
                f"P3-C Phase4 primary-input row has branch evidence: {scenario_id}"
            )
        result[scenario_id] = row

    if source_less_hops != 5:
        raise MatrixError("P3-C Phase4 source-less endpoint count drifted")
    return result


def validate_ai_complex_runtime_audit(
    audit: dict,
    runtime_revision: str,
    schema_revision: str,
) -> None:
    if audit.get("schema_version") != "xdebug.ai-complex-runtime-audit.v1":
        raise MatrixError("ai_complex runtime audit has the wrong schema_version")
    if audit.get("goal_id") != GOAL_ID:
        raise MatrixError("ai_complex runtime audit belongs to a different Goal")
    locked = audit.get("locked_original_runtime", {})
    if locked.get("git_revision") != runtime_revision:
        raise MatrixError("ai_complex audit does not use the locked runtime revision")
    if locked.get("schema_revision") != schema_revision:
        raise MatrixError("ai_complex audit does not use the locked schema revision")
    if locked.get("action_count") != 73:
        raise MatrixError("ai_complex audit does not prove the 73-Action identity gate")
    runner = locked.get("runner", {})
    if (
        runner.get("sha256") != AI_COMPLEX_RUNNER_SHA256
        or runner.get("mode") != "nonaxi"
    ):
        raise MatrixError("ai_complex audit does not use the locked nonaxi oracle")
    counter_runner = locked.get("counter_runner", {})
    if counter_runner.get("sha256") != AI_COMPLEX_COUNTER_RUNNER_SHA256:
        raise MatrixError("ai_complex audit does not use the locked counter oracle")
    original_fixture = locked.get("fixture", {})
    if (
        original_fixture.get("cache_reused") is not True
        or original_fixture.get("fixture_rebuilt") is not False
        or original_fixture.get("source_access") != "read_only"
    ):
        raise MatrixError("ai_complex audit did not reuse the original fixture read-only")
    original_gate = locked.get("suite_gate", {})
    original_counter_gate = locked.get("counter_suite_gate", {})
    current = audit.get("current_runtime", {})
    current_gate = current.get("locked_suite_gate", {})
    current_counter_gate = current.get("locked_counter_suite_gate", {})
    if (
        original_gate.get("result") != "passed"
        or original_gate.get("session_closed_gracefully") is not True
        or original_counter_gate.get("result") != "passed"
        or original_counter_gate.get("session_closed_gracefully") is not True
        or current_gate.get("result") != "passed"
        or current_gate.get("same_runner_sha256") != runner.get("sha256")
        or current_gate.get("same_mode") != runner.get("mode")
        or current_gate.get("session_closed_gracefully") is not True
        or current_counter_gate.get("result") != "passed"
        or current_counter_gate.get("same_runner_sha256") !=
            counter_runner.get("sha256")
        or current_counter_gate.get("session_closed_gracefully") is not True
    ):
        raise MatrixError("ai_complex two-sided locked suite gate is incomplete")
    current_fixture = current.get("fixture", {})
    if (
        current_fixture.get("deterministic_second_build") is not True
        or current_fixture.get("vcd_or_json_conversion_used") is not False
    ):
        raise MatrixError("ai_complex FST regeneration contract is incomplete")
    observable = audit.get("observable_contract", {})
    if (
        observable.get("same_locked_oracle_passed_both_sides") is not True
        or observable.get("four_state_x_and_z_preserved") is not True
        or observable.get("binary_waveform_comparison_used") is not False
    ):
        raise MatrixError("ai_complex public observable comparison drifted")
    boundary = audit.get("write_boundary_audit", {})
    if (
        boundary.get("only_writable_repository") != "xdebug_fst"
        or boundary.get("external_inputs_read_only") is not True
        or boundary.get("fallback_used") is not False
    ):
        raise MatrixError("ai_complex write/fallback boundary is not proven")
    for repository in ("original_xverif", "wellen", "verilator"):
        if boundary.get(f"{repository}_snapshot_sha256_before") != \
                boundary.get(f"{repository}_snapshot_sha256_after"):
            raise MatrixError(f"ai_complex audit changed external {repository}")
    verdict = audit.get("verdict", {})
    if (
        verdict.get("scenario_id") != "fixture.ai_complex_wave"
        or verdict.get("status") != "semantic-equivalent"
        or verdict.get("p3_batch") != "P3-A"
        or verdict.get("remaining_observable_gap_count") != 0
    ):
        raise MatrixError("ai_complex runtime audit verdict drifted")


def validate_p3b_runtime_audit(
    audit: dict,
    runtime_revision: str,
    schema_revision: str,
    repo_root: Path,
    original_assets: dict[str, dict],
    current_assets: dict[str, dict],
) -> dict[str, dict]:
    if audit.get("schema_version") != "xdebug.p3b-runtime-audit.v1":
        raise MatrixError("P3-B runtime audit has the wrong schema_version")
    if audit.get("goal_id") != GOAL_ID:
        raise MatrixError("P3-B runtime audit belongs to a different Goal")
    policy = audit.get("authority_policy", {})
    if (
        policy.get("behavior_authority") != "locked_original_runtime"
        or policy.get("asset_authority") != "goal_start_original_assets"
        or policy.get("authority_conflicts_preserved") is not True
        or policy.get("binary_waveform_comparison_used") is not False
    ):
        raise MatrixError("P3-B audit collapsed or changed the two original authorities")

    locked = audit.get("locked_original_runtime", {})
    if locked.get("git_revision") != runtime_revision:
        raise MatrixError("P3-B audit does not use the locked runtime revision")
    if locked.get("schema_revision") != schema_revision:
        raise MatrixError("P3-B audit does not use the locked schema revision")
    if locked.get("action_count") != 73:
        raise MatrixError("P3-B audit does not prove the 73-Action identity gate")
    if (
        locked.get("source_access") != "read_only"
        or locked.get("fixture_cache_reused") is not True
        or locked.get("fixture_rebuilt") is not False
    ):
        raise MatrixError("P3-B original fixture/cache policy drifted")

    gates = {
        item.get("gate_id"): item
        for item in locked.get("runner_gates", [])
    }
    expected_gates = {
        "active_driver_and_interface": 10,
        "active_semantics": 1,
        "active_zero_evidence": 16,
        "trace_x_xprop": 1,
        "design_semantics": 1,
    }
    if set(gates) != set(expected_gates):
        raise MatrixError("P3-B original runner gate inventory drifted")
    for gate_id, passed in expected_gates.items():
        gate = gates[gate_id]
        if (
            gate.get("result") != "passed"
            or gate.get("passed") != passed
            or gate.get("failed") != 0
            or gate.get("skipped") != 0
            or gate.get("session_closed_gracefully") is not True
        ):
            raise MatrixError(f"P3-B original runner gate is incomplete: {gate_id}")
        path = gate.get("relative_path")
        asset = original_assets.get(path)
        if asset is None or asset["sha256"] != gate.get("goal_start_asset_sha256"):
            raise MatrixError(f"P3-B Goal-start runner evidence drifted: {path}")
        if "transitive_runner_relative_path" in gate:
            transitive = gate["transitive_runner_relative_path"]
            transitive_asset = original_assets.get(transitive)
            if (
                transitive_asset is None
                or transitive_asset["sha256"] !=
                    gate.get("goal_start_transitive_runner_sha256")
            ):
                raise MatrixError(
                    f"P3-B Goal-start transitive runner drifted: {transitive}"
                )

    caches = locked.get("fixture_caches", [])
    expected_caches = {
        "xdebug.active_driver", "xdebug.interface_port_root",
        "xdebug.active_semantics", "xdebug.active_zero_evidence",
        "xdebug.trace_x_xprop", "xdebug.design_uart", "xdebug.design_p3",
    }
    if {item.get("fixture_id") for item in caches} != expected_caches:
        raise MatrixError("P3-B locked fixture cache inventory drifted")
    digest_pattern = re.compile(r"[0-9a-f]{64}")
    for fixture in caches:
        fingerprint = fixture.get("cache_fingerprint", "")
        if (
            not digest_pattern.fullmatch(fingerprint)
            or not fixture.get("cache_version", "").startswith(
                fingerprint + "-prepare-"
            )
            or not digest_pattern.fullmatch(fixture.get("manifest_sha256", ""))
            or fixture.get("tool_identity") != "X-2025.06"
        ):
            raise MatrixError(
                f"P3-B cache evidence is incomplete: {fixture.get('fixture_id')}"
            )

    current = audit.get("current_runtime", {})
    generation = current.get("fixture_generation", {})
    if (
        current.get("implementation_commit") != "b4b810e"
        or generation.get("deterministic_second_build") is not True
        or generation.get("vcd_or_json_conversion_used") is not False
        or generation.get("fallback_used") is not False
    ):
        raise MatrixError("P3-B current fixture generation contract drifted")
    for path_key, hash_key in (
        ("script_path", "script_sha256"),
        ("verilator_patch_path", "verilator_patch_sha256"),
    ):
        path = generation.get(path_key)
        expected_hash = generation.get(hash_key)
        if (
            not isinstance(path, str)
            or not isinstance(expected_hash, str)
            or sha256_bytes((repo_root / path).read_bytes()) != expected_hash
        ):
            raise MatrixError(f"P3-B fixture generator evidence drifted: {path}")

    fixture_ids = set()
    for fixture in current.get("fixtures", []):
        fixture_id = fixture.get("fixture_id")
        if not isinstance(fixture_id, str) or fixture_id in fixture_ids:
            raise MatrixError("P3-B current fixture identity is missing or duplicated")
        fixture_ids.add(fixture_id)
        for path_key, hash_key in (
            ("rtl_path", "rtl_sha256"),
            ("harness_path", "harness_sha256"),
            ("design_db_path", "design_db_sha256"),
            ("fst_path", "fst_sha256"),
            ("hash_record_path", "hash_record_sha256"),
        ):
            if path_key not in fixture:
                continue
            path = fixture[path_key]
            asset = current_assets.get(path)
            if asset is None or asset["sha256"] != fixture.get(hash_key):
                raise MatrixError(f"P3-B current fixture evidence drifted: {path}")
            validate_frozen_file(repo_root, asset)

    repository_gates = {
        item.get("gate_id"): item
        for item in current.get("repository_gates", [])
    }
    if set(repository_gates) != {
        "ported_original_active_oracles", "design_contracts",
        "adjacent_regression", "static_differential_contracts", "ctest",
    }:
        raise MatrixError("P3-B current repository gate inventory drifted")
    expected_repository_passes = {
        "ported_original_active_oracles": 18,
        "design_contracts": 13,
        "adjacent_regression": 146,
        "static_differential_contracts": 27,
        "ctest": 7,
    }
    for gate_id, passed in expected_repository_passes.items():
        gate = repository_gates[gate_id]
        if gate.get("passed") != passed or gate.get("failed") != 0:
            raise MatrixError(f"P3-B current repository gate failed: {gate_id}")
        if "path" in gate:
            asset = current_assets.get(gate["path"])
            if asset is None or asset["sha256"] != gate.get("sha256"):
                raise MatrixError(f"P3-B current test evidence drifted: {gate['path']}")

    rows = audit.get("comparisons")
    if not isinstance(rows, list) or len(rows) != 8:
        raise MatrixError("P3-B audit must contain exactly eight fixture comparisons")
    row_map = {}
    for row in rows:
        scenario_id = row.get("scenario_id")
        if scenario_id in row_map:
            raise MatrixError(f"duplicate P3-B scenario: {scenario_id}")
        if (
            row.get("p3_batch") != "P3-B"
            or row.get("status") not in {
                "semantic-equivalent", "proven-unobservable",
            }
            or row.get("remaining_observable_gap_count") != 0
        ):
            raise MatrixError(f"P3-B scenario is not closed: {scenario_id}")
        if row.get("status") == "semantic-equivalent":
            if row.get("same_locked_oracle_executable_used_on_current") is not False:
                raise MatrixError(f"P3-B overclaims original runner reuse: {scenario_id}")
            if not (
                row.get("ported_public_oracle_complete") is True
                or row.get("normalized_contract_mapping_complete") is True
            ):
                raise MatrixError(f"P3-B observable mapping is incomplete: {scenario_id}")
        row_map[scenario_id] = row

    expected_rows = {
        "fixture.active_driver", "fixture.active_semantics",
        "fixture.active_zero_evidence", "fixture.interface_port_root",
        "fixture.trace_x_xprop", "fixture.design_uart",
        "fixture.design_p3", "fixture.design_hierarchy",
    }
    if set(row_map) != expected_rows:
        raise MatrixError("P3-B scenario inventory drifted")
    counts = Counter(row["status"] for row in rows)
    if counts != {"semantic-equivalent": 6, "proven-unobservable": 2}:
        raise MatrixError("P3-B status count drifted")

    for scenario_id in (
        "fixture.active_driver", "fixture.interface_port_root",
        "fixture.active_zero_evidence",
    ):
        row = row_map[scenario_id]
        original_asset = original_assets.get(row.get("original_rtl_path"))
        current_asset = current_assets.get(row.get("current_rtl_path"))
        if (
            row.get("comparison_method") != "exact_rtl_and_ported_public_oracle"
            or row.get("exact_rtl_match") is not True
            or original_asset is None
            or current_asset is None
            or original_asset["sha256"] != row.get("original_rtl_sha256")
            or current_asset["sha256"] != row.get("current_rtl_sha256")
            or row.get("original_rtl_sha256") != row.get("current_rtl_sha256")
        ):
            raise MatrixError(f"P3-B exact RTL proof drifted: {scenario_id}")

    hierarchy = row_map["fixture.design_hierarchy"]
    request_schema = json.loads((
        repo_root / "compat/xdebug-v1/schemas/v1/actions/"
        "scope.list.request.schema.json"
    ).read_text(encoding="utf-8"))
    response_schema = json.loads((
        repo_root / "compat/xdebug-v1/schemas/v1/actions/"
        "scope.list.response.schema.json"
    ).read_text(encoding="utf-8"))
    locked_kinds = request_schema["properties"]["args"]["properties"]["kind"][
        "enum"
    ]
    locked_groups = sorted(response_schema["$defs"]["successData"]["properties"])
    hierarchy_asset = original_assets.get(hierarchy.get("goal_start_test_path"))
    if (
        hierarchy.get("comparison_method") !=
            "locked_runtime_and_schema_negative_proof"
        or hierarchy.get("test_present_at_locked_runtime") is not False
        or hierarchy.get("locked_scope_list_kind_enum") != locked_kinds
        or hierarchy.get("locked_scope_list_data_groups") != locked_groups
        or hierarchy_asset is None
        or hierarchy_asset["sha256"] != hierarchy.get("goal_start_test_sha256")
        or not set(
            hierarchy.get("goal_start_requested_unsupported_kinds", [])
        ).isdisjoint(locked_kinds)
        or not set(
            hierarchy.get("goal_start_requested_unsupported_groups", [])
        ).isdisjoint(locked_groups)
    ):
        raise MatrixError("P3-B design hierarchy negative schema proof drifted")

    design_p3 = row_map["fixture.design_p3"]
    p3_runner = original_assets.get(design_p3.get("goal_start_runner_path"))
    if (
        design_p3.get("comparison_method") !=
            "goal_start_and_locked_runner_request_inventory"
        or design_p3.get("public_actions") != ["session.open"]
        or design_p3.get("p3_session_opened") is not True
        or design_p3.get("p3_semantic_query_count") != 0
        or p3_runner is None
        or p3_runner["sha256"] != design_p3.get("goal_start_runner_sha256")
    ):
        raise MatrixError("P3-B design P3 unobservable proof drifted")

    boundary = audit.get("write_boundary_audit", {})
    if (
        boundary.get("only_writable_repository") != "xdebug_fst"
        or boundary.get("external_inputs_read_only") is not True
        or boundary.get("fallback_used") is not False
        or boundary.get(
            "all_runtime_home_tmp_socket_and_artifacts_repository_local"
        ) is not True
    ):
        raise MatrixError("P3-B write/fallback boundary is not proven")
    for repository in ("original_xverif", "wellen", "verilator"):
        if boundary.get(f"{repository}_snapshot_sha256_before") != \
                boundary.get(f"{repository}_snapshot_sha256_after"):
            raise MatrixError(f"P3-B audit changed external {repository}")

    def strings(value: object) -> Iterable[str]:
        if isinstance(value, str):
            yield value
        elif isinstance(value, dict):
            for key, item in value.items():
                yield from strings(key)
                yield from strings(item)
        elif isinstance(value, list):
            for item in value:
                yield from strings(item)

    if any(value.startswith("/") for value in strings(audit)):
        raise MatrixError("P3-B audit contains an absolute path")

    verdict = audit.get("verdict", {})
    if verdict != {
        "p3_batch": "P3-B",
        "scenario_count": 8,
        "semantic_equivalent_count": 6,
        "proven_unobservable_count": 2,
        "partial_count": 0,
        "missing_count": 0,
        "remaining_observable_gap_count": 0,
    }:
        raise MatrixError("P3-B runtime audit verdict drifted")
    return row_map


def scan_constructs(text: str) -> dict[str, list[int]]:
    found: dict[str, list[int]] = {}
    in_block_comment = False
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw
        if in_block_comment:
            if "*/" not in line:
                continue
            line = line.split("*/", 1)[1]
            in_block_comment = False
        while "/*" in line:
            before, after = line.split("/*", 1)
            if "*/" in after:
                line = before + " " + after.split("*/", 1)[1]
            else:
                line = before
                in_block_comment = True
                break
        line = line.split("//", 1)[0]
        if not line.strip():
            continue
        for name, pattern in CONSTRUCT_PATTERNS.items():
            if re.search(pattern, line, re.IGNORECASE):
                found.setdefault(name, []).append(line_number)
    return found


def first_content_line(text: str) -> int:
    for line_number, line in enumerate(text.splitlines(), 1):
        if line.strip() and not line.lstrip().startswith(("//", "/*", "*")):
            return line_number
    return 1


def file_line(path: Path, pattern: str) -> int:
    regex = re.compile(pattern)
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if regex.search(line):
            return line_number
    raise MatrixError(f"evidence pattern {pattern!r} not found in {path}")


def public_actions(repo_root: Path) -> list[str]:
    catalog = json.loads(
        (repo_root / "compat/xdebug-v1/catalog.response.json").read_text(encoding="utf-8")
    )
    actions = [
        item["name"] if isinstance(item, dict) else item
        for item in catalog["data"]["actions"]
    ]
    if len(actions) != 73 or len(set(actions)) != 73:
        raise MatrixError("frozen public Action catalog is not exactly 73 unique entries")
    return actions


def completeness_fields(schema: object, prefix: str = "") -> list[str]:
    names = {
        "analysis_complete", "scan_complete", "response_truncated",
        "total_count", "returned_count", "termination", "termination_detail",
        "omitted_count", "truncated", "complete",
    }
    result = set()
    if isinstance(schema, dict):
        properties = schema.get("properties")
        if isinstance(properties, dict):
            for key, value in properties.items():
                field = f"{prefix}.{key}" if prefix else key
                if key in names:
                    result.add(field)
                result.update(completeness_fields(value, field))
        for key in ("$defs", "definitions"):
            values = schema.get(key)
            if isinstance(values, dict):
                for value in values.values():
                    result.update(completeness_fields(value, prefix))
        for key in ("allOf", "anyOf", "oneOf"):
            values = schema.get(key)
            if isinstance(values, list):
                for value in values:
                    result.update(completeness_fields(value, prefix))
        if "items" in schema:
            result.update(completeness_fields(schema["items"], prefix))
    elif isinstance(schema, list):
        for item in schema:
            result.update(completeness_fields(item, prefix))
    return sorted(result)


def action_contract(repo_root: Path, action: str) -> dict:
    request_dir = repo_root / "compat/xdebug-v1/examples/requests"
    candidates = sorted(request_dir.glob(f"{action}.*.json"))
    if not candidates:
        raise MatrixError(f"no frozen request example for Action {action}")
    basic = request_dir / f"{action}.basic.json"
    request = basic if basic in candidates else candidates[0]
    response = repo_root / f"compat/xdebug-v1/schemas/v1/actions/{action}.response.schema.json"
    if not response.is_file():
        raise MatrixError(f"no frozen response schema for Action {action}")
    schema = json.loads(response.read_text(encoding="utf-8"))
    note_fields = set()
    notes = schema.get("x-output_notes", "")
    for path in re.findall(r"\b(?:summary|data)\.([A-Za-z0-9_]+)", notes):
        if path in {
            "analysis_complete", "scan_complete", "response_truncated",
            "total_count", "returned_count", "termination", "termination_detail",
            "omitted_count", "truncated", "complete", "truncation_scopes",
            "value_width_complete",
        }:
            note_fields.add(f"summary.{path}")
    fields = note_fields or set(completeness_fields(schema))
    return {
        "request_example": request.relative_to(repo_root).as_posix(),
        "response_schema": response.relative_to(repo_root).as_posix(),
        "completeness_fields": sorted(fields),
    }


def asset_lookup(manifest: dict, side: str) -> dict[str, dict]:
    return {
        asset["path"]: asset
        for asset in manifest["assets"]
        if asset["side"] == side
    }


def validate_frozen_file(root: Path, asset: dict) -> bytes:
    path = root / asset["path"]
    data = path.read_bytes()
    digest = sha256_bytes(data)
    if digest != asset["sha256"]:
        raise MatrixError(
            f"frozen asset content drift: {asset['path']} "
            f"expected {asset['sha256']}, got {digest}"
        )
    return data


def source_record(root: Path, asset: dict, *, scan: bool) -> dict:
    data = validate_frozen_file(root, asset)
    text = data.decode("utf-8", "replace")
    record = {
        "path": asset["path"],
        "sha256": asset["sha256"],
        "line_anchor": first_content_line(text),
        "line_count": len(text.splitlines()),
    }
    if scan:
        record["construct_lines"] = scan_constructs(text)
    return record


def index_current_tests(repo_root: Path) -> dict[str, dict]:
    result = {}
    pattern = re.compile(r"^def (test_[A-Za-z0-9_]+)\(", re.MULTILINE)
    for path in sorted((repo_root / "tests").rglob("*.py")):
        text = path.read_text(encoding="utf-8")
        digest = sha256_bytes(path.read_bytes())
        for match in pattern.finditer(text):
            name = match.group(1)
            if name in result:
                raise MatrixError(f"duplicate current test evidence name: {name}")
            result[name] = {
                "test": name,
                "path": path.relative_to(repo_root).as_posix(),
                "line": text.count("\n", 0, match.start()) + 1,
                "sha256": digest,
            }
    return result


def current_evidence(
    repo_root: Path,
    current_assets: dict[str, dict],
    tests: dict[str, dict],
    candidate: dict,
) -> dict:
    fixture_ids = candidate.get("fixtures", [])
    source_assets = [
        asset
        for asset in current_assets.values()
        if set(asset["fixture_ids"]) & set(fixture_ids)
        and asset["kind"] in {"rtl", "testbench", "waveform_fst", "waveform_vcd"}
    ]
    sources = [
        source_record(repo_root, asset, scan=asset["kind"] == "rtl")
        | {"kind": asset["kind"], "fixture_ids": asset["fixture_ids"]}
        for asset in sorted(source_assets, key=lambda item: item["path"])
    ]
    test_evidence = []
    for name in candidate.get("tests", []):
        if name not in tests:
            raise MatrixError(f"configured current test evidence does not exist: {name}")
        test_evidence.append(tests[name])
    return {
        "candidate_fixture_ids": fixture_ids,
        "candidate_sources": sources,
        "test_evidence": test_evidence,
        "evidence_scope": (
            candidate.get(
                "evidence_scope",
                "related capability only; exact stimulus/time/result remains gated by P2",
            )
        ),
    }


def validate_p3e_closure_audit(
    audit: dict,
    repo_root: Path,
    current_assets: dict[str, dict],
) -> dict:
    if (
        audit.get("schema_version") != "xdebug.p3e-closure-audit.v1"
        or audit.get("goal_id") != GOAL_ID
    ):
        raise MatrixError("P3-E closure audit identity drifted")
    if audit.get("session") != {
        "all_writes_repository_local": True,
        "external_sources_read_only": True,
        "fallback_used": False,
        "fixture_rebuilt": False,
    }:
        raise MatrixError("P3-E closure write/fallback boundary drifted")
    if audit.get("verdict") != {
        "missing_count": 0,
        "p3_batch": "P3-E",
        "partial_count": 0,
        "proven_unobservable_count": 2,
        "remaining_observable_gap_count": 0,
        "scenario_count": 3,
        "semantic_equivalent_count": 1,
    }:
        raise MatrixError("P3-E closure verdict drifted")

    def locked(path: str, digest: str) -> None:
        asset = current_assets.get(path)
        if asset is None or asset["sha256"] != digest:
            raise MatrixError(f"P3-E closure asset drifted: {path}")

    boundary = audit.get("boundary", {})
    locked(boundary.get("path", ""), boundary.get("sha256", ""))
    xif = audit.get("xif_event", {})
    oracle = xif.get("oracle", {})
    locked(oracle.get("path", ""), oracle.get("sha256", ""))
    fixture = xif.get("fixture", {})
    locked(fixture.get("manifest_path", ""), fixture.get("manifest_sha256", ""))
    locked("testdata/fixtures/xif_event/fixture.sha256", fixture.get("lock_sha256", ""))
    locked("testdata/fixtures/xif_event/waves.fst", fixture.get("fst_sha256", ""))
    test_gate = xif.get("test_gate", {})
    locked(test_gate.get("path", ""), test_gate.get("sha256", ""))
    policy = xif.get("asset_reuse_policy", {})
    if (
        xif.get("classification") != "semantic-equivalent"
        or oracle.get("observation_count") != 32
        or oracle.get("cache_reused") is not True
        or oracle.get("fixture_rebuilt") is not False
        or xif.get("remaining_observable_gap_count") != 0
        or len(xif.get("requirements", {})) != 12
        or not all(xif.get("requirements", {}).values())
        or policy.get("direct_copy_preferred") is not True
        or len(policy.get("directly_reused_configs", [])) != 6
        or policy.get("cross_side_hash_equality_required") is not False
        or policy.get("public_content_equivalence_required") is not True
        or policy.get("original_rtl_directly_executable_open_source") is not False
        or fixture.get("proprietary_vip_used") is not False
        or fixture.get("fsdb_conversion_used") is not False
        or fixture.get("action_export_feedback_used") is not False
    ):
        raise MatrixError("P3-E XIF content-equivalence closure drifted")
    for row in policy["directly_reused_configs"]:
        locked(
            f"testdata/fixtures/xif_event/{row.get('name', '')}",
            row.get("sha256", ""),
        )
        if row.get("reuse") != "byte-identical-copy":
            raise MatrixError("P3-E XIF direct-reuse disposition drifted")

    sva = audit.get("sva_npi", {})
    if (
        sva.get("classification") != "proven-unobservable"
        or sva.get("observed_public_action_count") != 0
        or sva.get("public_exposure_count") != 0
        or sva.get("remaining_distinct_public_observation_count") != 0
    ):
        raise MatrixError("P3-E SVA bounded proof drifted")
    cross = audit.get("cross_fixture", {})
    if (
        cross.get("classification") != "proven-unobservable"
        or cross.get("consumer_count") != 37
        or cross.get("observed_public_action_count") != 72
        or cross.get("contract_count") != 72
        or cross.get("catalog_actions_not_observed") != ["session.kill"]
        or cross.get("remaining_unmapped_consumer_action_count") != 0
        or cross.get("remaining_distinct_public_observation_count") != 0
    ):
        raise MatrixError("P3-E cross-fixture bounded proof drifted")
    return audit


def active_source_paths(group: str, case: str, original_assets: dict[str, dict]) -> list[str]:
    base = "xdebug/tests/active_trace_chain"
    candidates: list[str]
    if group == "p0":
        candidates = [f"{base}/p0_composability/{case}/tb.sv"]
    elif group == "composite":
        candidates = [
            f"{base}/composite/{case}/tb.sv",
            f"{base}/composite/chain_dut.sv",
        ]
    elif group == "timing":
        candidates = [
            f"{base}/timing/{case}/tb.sv",
            f"{base}/timing/timing_boundary_dut.sv",
        ]
    elif group == "phase4":
        candidates = [
            f"{base}/phase4/{case}/tb.sv",
            f"{base}/phase4/phase4_dut.sv",
            f"{base}/composite/chain_dut.sv",
        ]
    elif group == "phase5":
        candidates = [f"{base}/phase5/dut.sv", f"{base}/phase5/tb.sv"]
    else:
        raise MatrixError(f"unknown active trace group: {group}")
    missing = [path for path in candidates if path not in original_assets]
    if missing:
        raise MatrixError(f"active scenario source not frozen: {missing!r}")
    return candidates


def consumer_catalog(
    original_root: Path,
    original_assets: dict[str, dict],
    actions: set[str],
) -> dict[str, dict]:
    result = {}
    action_pattern = re.compile(
        r"(?<![A-Za-z0-9_.])(" + "|".join(
            re.escape(action) for action in sorted(actions, key=len, reverse=True)
        ) + r")(?![A-Za-z0-9_.])"
    )
    for asset in original_assets.values():
        if "test_consumer" not in asset["roles"]:
            continue
        data = validate_frozen_file(original_root, asset)
        text = data.decode("utf-8", "replace")
        action_lines: dict[str, list[int]] = defaultdict(list)
        for line_number, line in enumerate(text.splitlines(), 1):
            for match in action_pattern.finditer(line):
                action_lines[match.group(1)].append(line_number)
        result[asset["path"]] = {
            "sha256": asset["sha256"],
            "fixture_ids": asset["fixture_ids"],
            "action_lines": {key: sorted(set(value)) for key, value in sorted(action_lines.items())},
        }
    return result


def fixture_consumers(catalog: dict[str, dict], fixture_id: str) -> list[str]:
    return sorted(path for path, value in catalog.items() if fixture_id in value["fixture_ids"])


def fixture_actions(catalog: dict[str, dict], paths: Iterable[str]) -> list[str]:
    return sorted({action for path in paths for action in catalog[path]["action_lines"]})


def build_matrix(repo_root: Path, original_root: Path, manifest_path: Path) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["goal_id"] != GOAL_ID:
        raise MatrixError("P0 manifest belongs to a different Goal")
    original_assets = asset_lookup(manifest, "original")
    current_assets = asset_lookup(manifest, "current")

    runtime_baseline = manifest["baselines"]["original_runtime"]
    audit_asset = current_assets.get(PHASE5_RUNTIME_AUDIT.as_posix())
    if audit_asset is None:
        raise MatrixError("P0 manifest does not freeze the Phase5 runtime audit")
    phase5_runtime_audit = json.loads(
        validate_frozen_file(repo_root, audit_asset).decode("utf-8")
    )
    phase5_runtime_rows = validate_phase5_runtime_audit(
        phase5_runtime_audit,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
    )
    ai_audit_asset = current_assets.get(AI_COMPLEX_RUNTIME_AUDIT.as_posix())
    if ai_audit_asset is None:
        raise MatrixError("P0 manifest does not freeze the ai_complex runtime audit")
    ai_complex_runtime_audit = json.loads(
        validate_frozen_file(repo_root, ai_audit_asset).decode("utf-8")
    )
    validate_ai_complex_runtime_audit(
        ai_complex_runtime_audit,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
    )
    ai_current = ai_complex_runtime_audit["current_runtime"]
    ai_fixture = ai_current["fixture"]
    ai_current_hashes = {
        "testdata/fixtures/ai_complex/ai_complex_top.sv":
            ai_fixture["rtl_sha256"],
        "testdata/fixtures/ai_complex/generate_ai_complex_fst.cpp":
            ai_fixture["generator_sha256"],
        "testdata/fixtures/ai_complex/fstcpp-four-state-vector.patch":
            ai_fixture["writer_patch_sha256"],
        "testdata/fixtures/ai_complex/waves.fst": ai_fixture["fst_sha256"],
        ai_current["repository_gate"]["path"]:
            ai_current["repository_gate"]["sha256"],
        ai_current["focused_regression"]["path"]:
            ai_current["focused_regression"]["sha256"],
    }
    for path, expected_hash in ai_current_hashes.items():
        asset = current_assets.get(path)
        if asset is None or asset["sha256"] != expected_hash:
            raise MatrixError(f"ai_complex current evidence hash drifted: {path}")
    ai_original_rtl = ai_complex_runtime_audit["observable_contract"][
        "original_rtl"
    ]
    original_rtl_asset = original_assets.get(ai_original_rtl["path"])
    if (
        original_rtl_asset is None
        or original_rtl_asset["sha256"] != ai_original_rtl["sha256"]
    ):
        raise MatrixError("ai_complex frozen original RTL evidence drifted")

    p3b_audit_asset = current_assets.get(P3B_RUNTIME_AUDIT.as_posix())
    if p3b_audit_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-B runtime audit")
    p3b_runtime_audit = json.loads(
        validate_frozen_file(repo_root, p3b_audit_asset).decode("utf-8")
    )
    p3b_runtime_rows = validate_p3b_runtime_audit(
        p3b_runtime_audit,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_p0_oracle_asset = current_assets.get(P3C_P0_ORACLE.as_posix())
    if p3c_p0_oracle_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-C P0 oracle")
    p3c_p0_oracle = json.loads(
        validate_frozen_file(repo_root, p3c_p0_oracle_asset).decode("utf-8")
    )
    p3c_p0_rows = validate_p3c_p0_oracle(
        p3c_p0_oracle,
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_composite_oracle_asset = current_assets.get(
        P3C_COMPOSITE_ORACLE.as_posix()
    )
    if p3c_composite_oracle_asset is None:
        raise MatrixError(
            "P0 manifest does not freeze the P3-C composite oracle"
        )
    p3c_composite_oracle = json.loads(
        validate_frozen_file(
            repo_root, p3c_composite_oracle_asset
        ).decode("utf-8")
    )
    p3c_composite_rows = validate_p3c_composite_oracle(
        p3c_composite_oracle,
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_timing_oracle_asset = current_assets.get(
        P3C_TIMING_ORACLE.as_posix()
    )
    if p3c_timing_oracle_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-C timing oracle")
    p3c_timing_oracle = json.loads(
        validate_frozen_file(
            repo_root, p3c_timing_oracle_asset
        ).decode("utf-8")
    )
    p3c_timing_rows = validate_p3c_timing_oracle(
        p3c_timing_oracle,
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_phase4_oracle_asset = current_assets.get(
        P3C_PHASE4_ORACLE.as_posix()
    )
    if p3c_phase4_oracle_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-C Phase4 oracle")
    p3c_phase4_oracle = json.loads(
        validate_frozen_file(
            repo_root, p3c_phase4_oracle_asset
        ).decode("utf-8")
    )
    p3c_phase4_rows = validate_p3c_phase4_oracle(
        p3c_phase4_oracle,
        repo_root,
        original_assets,
        current_assets,
    )
    p3c_phase5_oracle_asset = current_assets.get(
        P3C_PHASE5_PUBLIC_ORACLE.as_posix()
    )
    if p3c_phase5_oracle_asset is None:
        raise MatrixError(
            "P0 manifest does not freeze the P3-C Phase5 public oracle"
        )
    p3c_phase5_oracle = json.loads(
        validate_frozen_file(
            repo_root, p3c_phase5_oracle_asset
        ).decode("utf-8")
    )
    p3c_phase5_rows = validate_p3c_phase5_public_oracle(
        p3c_phase5_oracle,
        repo_root,
        original_assets,
        current_assets,
        runtime_baseline["runtime_revision"],
        runtime_baseline["schema_revision"],
    )
    p3c_closure_audit_asset = current_assets.get(
        P3C_ACTIVE_TRACE_CLOSURE_AUDIT.as_posix()
    )
    if p3c_closure_audit_asset is None:
        raise MatrixError(
            "P0 manifest does not freeze the P3-C active-trace closure audit"
        )
    p3c_closure_audit = json.loads(
        validate_frozen_file(
            repo_root, p3c_closure_audit_asset
        ).decode("utf-8")
    )
    validate_p3c_active_trace_closure_audit(
        p3c_closure_audit,
        repo_root,
        manifest,
        original_assets,
        current_assets,
    )
    p3d_closure_audit_asset = current_assets.get(
        P3D_STREAM_DIFFERENTIAL_CLOSURE_AUDIT.as_posix()
    )
    if p3d_closure_audit_asset is None:
        raise MatrixError(
            "P0 manifest does not freeze the P3-D1 stream closure audit"
        )
    p3d_closure_audit = json.loads(
        validate_frozen_file(
            repo_root, p3d_closure_audit_asset
        ).decode("utf-8")
    )
    validate_p3d_stream_differential_closure_audit(
        p3d_closure_audit,
        repo_root,
        manifest,
        original_assets,
        current_assets,
    )
    p3d_apb_rows = validate_p3d_apb_assets(
        repo_root,
        manifest,
        original_assets,
        current_assets,
    )
    p3d_axi_rows = validate_p3d_axi_assets(repo_root, current_assets)
    p3e_closure_asset = current_assets.get(P3E_CLOSURE_AUDIT.as_posix())
    if p3e_closure_asset is None:
        raise MatrixError("P0 manifest does not freeze the P3-E closure audit")
    p3e_closure = json.loads(
        validate_frozen_file(
            repo_root, p3e_closure_asset
        ).decode("utf-8")
    )
    validate_p3e_closure_audit(p3e_closure, repo_root, current_assets)

    # Validate all frozen original assets, including consumers that do not end
    # up as HDL sources.  P1 must fail closed on any P0 evidence drift.
    for asset in original_assets.values():
        validate_frozen_file(original_root, asset)

    action_names = public_actions(repo_root)
    action_set = set(action_names)
    contracts = {action: action_contract(repo_root, action) for action in action_names}
    consumers = consumer_catalog(original_root, original_assets, action_set)
    current_tests = index_current_tests(repo_root)
    fixture_map = {item["id"]: item for item in manifest["original_fixtures"]}

    catalog_asset = original_assets[ACTIVE_CATALOG.as_posix()]
    catalog_text = validate_frozen_file(original_root, catalog_asset).decode("utf-8")
    active_rows = parse_active_catalog(catalog_text)
    report_asset = original_assets[PHASE5_REPORT.as_posix()]
    phase5_report = parse_phase5_report_terminations(
        validate_frozen_file(original_root, report_asset).decode("utf-8")
    )

    scenarios = []
    active_fixture_ids = set(ACTIVE_FIXTURE.values())
    special_fixtures = active_fixture_ids | {"xdebug.active_trace_runner"}

    for fixture_id, fixture in sorted(fixture_map.items()):
        if fixture_id in active_fixture_ids:
            continue
        candidate = FIXTURE_CANDIDATES[fixture_id]
        status = candidate.get("status", "partial")
        paths = fixture_consumers(consumers, fixture_id)
        hdl_assets = sorted(
            (
                asset for asset in original_assets.values()
                if fixture_id in asset["fixture_ids"] and asset["kind"] == "rtl"
            ),
            key=lambda item: item["path"],
        )
        scenario_id = "fixture." + fixture_id.removeprefix("xdebug.")
        scenario = {
            "scenario_id": scenario_id,
            "kind": "fixture_semantic_surface",
            "p3_batch": candidate["batch"],
            "status": status,
            "rationale": candidate.get(
                "rationale",
                (
                    "当前仅有相关能力/fixture 证据，尚未用 P2 等价公开请求逐观察点比较。"
                    if status == "partial" else
                    "当前没有同类 RTL/波形 fixture；相关公开语义必须在指定批次补齐或证明不可观察。"
                ),
            ),
            "original": {
                "fixture_id": fixture_id,
                "fixture_source_dir": fixture["source_dir"],
                "catalog_reference_lines": fixture["catalog_reference_lines"],
                "sources": [source_record(original_root, asset, scan=True) for asset in hdl_assets],
                "declared_waveform_outputs": [
                    item for item in manifest["original_declared_waveform_outputs"]
                    if item["fixture_id"] == fixture_id
                ],
                "consumer_paths": paths,
                "observed_public_actions": fixture_actions(consumers, paths),
            },
            "current": current_evidence(repo_root, current_assets, current_tests, candidate),
            "public_action_contracts": {
                action: contracts[action] for action in candidate["actions"]
            },
        }
        if fixture_id == "xdebug.ai_complex_wave":
            verdict = ai_complex_runtime_audit["verdict"]
            observable = ai_complex_runtime_audit["observable_contract"]
            if sorted(candidate["actions"]) != observable["public_actions"]:
                raise MatrixError("ai_complex audited public Action set drifted")
            scenario["runtime_audit"] = {
                "path": AI_COMPLEX_RUNTIME_AUDIT.as_posix(),
                "sha256": ai_audit_asset["sha256"],
                "status": verdict["status"],
                "p3_batch": verdict["p3_batch"],
                "same_locked_oracle_passed_both_sides":
                    observable["same_locked_oracle_passed_both_sides"],
                "remaining_observable_gap_count":
                    verdict["remaining_observable_gap_count"],
            }
        if scenario_id in p3b_runtime_rows:
            comparison = p3b_runtime_rows[scenario_id]
            if sorted(candidate["actions"]) != comparison["public_actions"]:
                raise MatrixError(
                    f"P3-B audited public Action set drifted: {scenario_id}"
                )
            status = comparison["status"]
            scenario["status"] = status
            if status == "semantic-equivalent":
                if comparison["comparison_method"] == \
                        "exact_rtl_and_ported_public_oracle":
                    scenario["rationale"] = (
                        "锁定原版 runtime/FSDB oracle 已通过；当前使用字节相同 RTL、"
                        "确定性原生 FST/DesignDB 和完整移植的公开请求断言通过。"
                        "原版 runner 未直接用于当前侧，审计明确保留该差别。"
                    )
                else:
                    scenario["rationale"] = (
                        "锁定原版完整 oracle 已通过；当前按相同公开 Action、刺激/时间、"
                        "响应字段与完整性语义逐项归一映射通过，未宣称复用同一 runner。"
                    )
            else:
                scenario["rationale"] = (
                    "冻结 73 Action/runtime/schema 与 Goal-start runner 哈希共同证明"
                    "该资产差异没有可执行公开语义观察点；静态合同门禁禁止将其泛化为免测。"
                )
            scenario["runtime_audit"] = {
                "path": P3B_RUNTIME_AUDIT.as_posix(),
                "sha256": p3b_audit_asset["sha256"],
                "status": status,
                "p3_batch": comparison["p3_batch"],
                "comparison_method": comparison["comparison_method"],
                "remaining_observable_gap_count":
                    comparison["remaining_observable_gap_count"],
            }
        if fixture_id == "xdebug.stream_v1":
            replay = p3d_closure_audit["comparator"]["public_replay"]
            hard_limit = p3d_closure_audit["cache_contract"]["hard_limit"]
            scenario["original"]["locked_public_oracles"] = [
                {
                    "path": P3D_STREAM_PUBLIC_ORACLE.as_posix(),
                    "sha256": replay["query_config_oracle_sha256"],
                    "observation_count": replay[
                        "query_config_observation_count"
                    ],
                },
                {
                    "path": P3D_STREAM_EXPORT_ORACLE.as_posix(),
                    "sha256": replay["export_oracle_sha256"],
                    "observation_count": replay[
                        "export_observation_count"
                    ],
                },
            ]
            scenario["runtime_audit"] = {
                "path": P3D_STREAM_DIFFERENTIAL_CLOSURE_AUDIT.as_posix(),
                "sha256": p3d_closure_audit_asset["sha256"],
                "status": "semantic-equivalent",
                "p3_batch": "P3-D1",
                "query_config_observation_count": replay[
                    "query_config_observation_count"
                ],
                "query_config_difference_count": replay[
                    "query_config_difference_count"
                ],
                "export_observation_count": replay[
                    "export_observation_count"
                ],
                "export_difference_count": replay[
                    "export_difference_count"
                ],
                "artifact_check_count": replay["artifact_check_count"],
                "xout_check_count": replay["xout_check_count"],
                "public_cache_observation_count": p3d_closure_audit[
                    "cache_contract"
                ]["public_observation_count"],
                "hard_limit_classification": hard_limit["classification"],
                "hard_limit_allowed_projection": hard_limit[
                    "allowed_current_projection"
                ],
                "remaining_observable_gap_count": 0,
            }
        if fixture_id == "xdebug.stream_differential_tool":
            comparator = p3d_closure_audit["comparator"]
            boundary = p3d_closure_audit["public_boundary"]
            closure = p3d_closure_audit["closure"]
            scenario["original"]["private_helper"] = p3d_closure_audit[
                "fixture_contract"
            ]
            scenario["original"]["source_assets"] = p3d_closure_audit[
                "source_files"
            ]
            scenario["original"]["direct_helper_public_actions"] = []
            scenario["unobservable_proof"] = {
                "path": P3D_STREAM_DIFFERENTIAL_CLOSURE_AUDIT.as_posix(),
                "sha256": p3d_closure_audit_asset["sha256"],
                "classification": p3d_closure_audit["classification"],
                "compile_guard": comparator["compile_guard"],
                "linked_object": comparator["linked_object"],
                "interposed_actions": comparator["interposed_actions"],
                "public_action": comparator["public_action"],
                "public_replay": comparator["public_replay"],
                "remaining_public_difference_count": comparator[
                    "remaining_public_difference_count"
                ],
                "public_action_count": boundary["action_count"],
                "private_action_count": boundary["private_action_count"],
                "private_probe_fields": boundary["private_probe_fields"],
                "private_probe_fields_in_public_schema": boundary[
                    "private_probe_fields_in_public_schema"
                ],
                "reused_waveform_fixture_id": p3d_closure_audit[
                    "fixture_contract"
                ]["reused_waveform_fixture_id"],
                "private_cache_metrics_classification": closure[
                    "private_cache_metrics_classification"
                ],
                "public_hard_limit_requires_current_gate": closure[
                    "public_hard_limit_requires_current_gate"
                ],
                "remaining_unmapped_public_observation_count": closure[
                    "remaining_unmapped_public_observation_count"
                ],
                "proof_scope": candidate["evidence_scope"],
            }
        if fixture_id in p3d_apb_rows:
            comparison = p3d_apb_rows[fixture_id]
            scenario["original"]["locked_public_oracle"] = {
                "path": comparison["path"],
                "sha256": comparison["sha256"],
                "observation_count": comparison["observation_count"],
            }
            scenario["runtime_audit"] = {
                "status": "semantic-equivalent",
                "p3_batch": "P3-D2",
                **comparison,
            }
            scenario["rationale"] = (
                "锁定原版 APB runtime/FSDB 公开 oracle 已通过；当前确定性 FST "
                "fixture 按完整事务、完成时间、方向、数据、错误、XOUT 与公开 cache "
                "边界逐项回放，差异数和剩余公开缺口均为零。"
            )
        if fixture_id in p3d_axi_rows:
            comparison = p3d_axi_rows[fixture_id]
            scenario["original"]["locked_public_oracle"] = {
                "path": comparison["path"],
                "sha256": comparison["sha256"],
                "observation_count": comparison["observation_count"],
            }
            scenario["runtime_audit"] = {
                "status": "semantic-equivalent",
                "p3_batch": "P3-D3",
                **comparison,
            }
            scenario["rationale"] = (
                "锁定原版 AXI 六运行公开 oracle 已通过；当前两套专属确定性 "
                "FST fixture 按 profile、完整事务计数、通道 handshake、XOUT、"
                "export artifact 与公开 cache 边界逐项回放，剩余公开缺口为零。"
            )
        if fixture_id == "xdebug.xif_event":
            closure = p3e_closure["xif_event"]
            scenario["original"]["locked_public_oracle"] = closure["oracle"]
            scenario["runtime_audit"] = {
                "path": P3E_CLOSURE_AUDIT.as_posix(),
                "sha256": p3e_closure_asset["sha256"],
                "status": closure["classification"],
                "p3_batch": "P3-E",
                "observation_count": closure["oracle"]["observation_count"],
                "required_observation_count": len(closure["requirements"]),
                "directly_reused_config_count": len(
                    closure["asset_reuse_policy"]["directly_reused_configs"]
                ),
                "cross_side_hash_equality_required": closure[
                    "asset_reuse_policy"
                ]["cross_side_hash_equality_required"],
                "public_content_equivalence_required": closure[
                    "asset_reuse_policy"
                ]["public_content_equivalence_required"],
                "remaining_observable_gap_count": closure[
                    "remaining_observable_gap_count"
                ],
            }
        if fixture_id == "xdebug.npi_fsdb_sva":
            proof = p3e_closure["sva_npi"]
            scenario["unobservable_proof"] = {
                "path": P3E_CLOSURE_AUDIT.as_posix(),
                "sha256": p3e_closure_asset["sha256"],
                **proof,
            }
        if fixture_id == "xdebug.active_trace_runner":
            runner_proof = p3c_closure_audit["runner"]
            coverage = runner_proof["coverage"]
            scenario["original"]["private_helper"] = runner_proof[
                "private_helper"
            ]
            scenario["original"]["runner_source_assets"] = runner_proof[
                "source_assets"
            ]
            scenario["original"]["direct_helper_public_actions"] = []
            scenario["unobservable_proof"] = {
                "path": P3C_ACTIVE_TRACE_CLOSURE_AUDIT.as_posix(),
                "sha256": p3c_closure_audit_asset["sha256"],
                "classification": runner_proof["classification"],
                "catalog_case_count": coverage["catalog_case_count"],
                "native_runner_case_count": coverage[
                    "native_runner_case_count"
                ],
                "public_runtime_case_count": coverage[
                    "public_runtime_case_count"
                ],
                "consumer_public_action_disposition": coverage[
                    "consumer_public_action_disposition"
                ],
                "covered_scenario_ids": coverage["covered_scenario_ids"],
                "remaining_unmapped_consumer_action_count": coverage[
                    "remaining_unmapped_consumer_action_count"
                ],
                "remaining_distinct_public_observation_count": coverage[
                    "remaining_distinct_public_observation_count"
                ],
                "proof_scope": runner_proof["proof_scope"],
            }
        scenarios.append(scenario)

    phase5_by_scene = {row["scene"]: row for row in phase5_report}
    group_ordinals = Counter()
    for row in active_rows:
        group = row["group"]
        group_ordinals[group] += 1
        ordinal = group_ordinals[group]
        scenario_id = f"active.{group}.{ordinal:02d}"
        fixture_id = ACTIVE_FIXTURE[group]
        candidate = ACTIVE_CANDIDATES[group] | {
            "actions": ["trace.active_driver_chain"], "batch": "P3-C"
        }
        source_paths = active_source_paths(group, str(row["case"]), original_assets)
        paths = fixture_consumers(consumers, fixture_id)
        oracle = {key: value for key, value in row.items() if key not in {"group", "case", "catalog_line"}}
        original = {
            "fixture_id": fixture_id,
            "catalog": {"path": ACTIVE_CATALOG.as_posix(), "line": row["catalog_line"]},
            "case": row["case"],
            "query": {"signal": row["signal"], "time": row["time"]},
            "oracle": oracle,
            "sources": [source_record(original_root, original_assets[path], scan=True) for path in source_paths],
            "declared_waveform_outputs": [
                item for item in manifest["original_declared_waveform_outputs"]
                if item["fixture_id"] == fixture_id
            ],
            "consumer_paths": paths,
            "observed_public_actions": fixture_actions(consumers, paths),
        }
        status = "partial"
        rationale = "存在相关能力代表测试，但未覆盖该原版 case 的同一 RTL 组合、刺激时间和完整响应。"
        p0_runtime_row = None
        composite_runtime_row = None
        timing_runtime_row = None
        phase4_runtime_row = None
        phase5_public_row = None
        if group == "p0":
            p0_runtime_row = p3c_p0_rows[scenario_id]
            if (
                p0_runtime_row["case"] != row["case"]
                or p0_runtime_row["request"]["signal"] != original["query"]["signal"]
                or p0_runtime_row["request"]["time"] != original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C P0 oracle differs from frozen catalog: {scenario_id}"
                )
            native = p0_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_P0_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256": p0_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 RTL 与当前镜像逐字节相同；锁定 native NPI oracle 和当前原始 FST/"
                "binary-v1 DesignDB 已按请求、hop、源码行、时间、值、候选、termination 与"
                "完整性逐项通过。冻结 v1 schema 的两项表达限制由显式哨兵/value.at 门禁裁决。"
            )
        if group == "composite":
            composite_runtime_row = p3c_composite_rows[scenario_id]
            if (
                composite_runtime_row["case"] != row["case"]
                or composite_runtime_row["request"]["signal"] !=
                    original["query"]["signal"]
                or composite_runtime_row["request"]["time"] !=
                    original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C composite oracle differs from frozen catalog: "
                    f"{scenario_id}"
                )
            native = composite_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_COMPOSITE_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256":
                    composite_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 case RTL 与共享 DUT 均和当前镜像逐字节相同；锁定 native NPI "
                "oracle 和当前原始 FST/binary-v1 DesignDB 已按完整复合链、时间边界、"
                "源码行、值、generate 逐位候选、termination 与完整性逐项通过。"
            )
        if group == "timing":
            timing_runtime_row = p3c_timing_rows[scenario_id]
            if (
                timing_runtime_row["case"] != row["case"]
                or timing_runtime_row["request"]["signal"] !=
                    original["query"]["signal"]
                or timing_runtime_row["request"]["time"] !=
                    original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C timing oracle differs from frozen catalog: "
                    f"{scenario_id}"
                )
            native = timing_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_TIMING_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256":
                    timing_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 case RTL 与共享 timing DUT 均和当前镜像逐字节相同；锁定 "
                "native stop-on-temporal 单跳 oracle 是当前完整公开链的精确首跳前缀。"
                "同槽 NBA 调度差异仅在结构门控条件成立时投影到原版活动边界，并由十二"
                "场景逐项门禁约束，未宣称原始 FSDB/FST 转换时刻逐点相同。"
            )
        if group == "phase4":
            phase4_runtime_row = p3c_phase4_rows[scenario_id]
            if (
                phase4_runtime_row["case"] != row["case"]
                or phase4_runtime_row["request"]["signal"] !=
                    original["query"]["signal"]
                or phase4_runtime_row["request"]["time"] !=
                    original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C Phase4 oracle differs from frozen catalog: {scenario_id}"
                )
            native = phase4_runtime_row["native_result"]
            original["locked_native_oracle"] = {
                "path": P3C_PHASE4_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": native["termination"],
                "total_hops": native["total_hops"],
                "temporal_boundaries": native["temporal_boundaries"],
                "truncated": native["truncated"],
                "original_fsdb_sha256":
                    phase4_runtime_row["fixture"]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "原版 case RTL 与两份共享 DUT 均和当前镜像逐字节相同；锁定 native NPI "
                "oracle 和当前原始 FST/binary-v1 DesignDB 已按完整复合链、两次时序边界、"
                "源码行、值、generate 逐位候选、termination 与完整性逐项通过。"
            )
        if group == "phase5":
            historical_runtime_row = phase5_runtime_rows[scenario_id]
            if historical_runtime_row["locked_request"] != original["query"]:
                raise MatrixError(
                    f"Phase5 runtime request differs from frozen catalog: {scenario_id}"
                )
            phase5_public_row = p3c_phase5_rows[scenario_id]
            phase5_request = phase5_public_row["request"]
            if (
                phase5_public_row["case"] != row["case"]
                or phase5_request["signal"] != original["query"]["signal"]
                or phase5_request["time"] != original["query"]["time"]
            ):
                raise MatrixError(
                    f"P3-C Phase5 oracle differs from frozen catalog: {scenario_id}"
                )
            report = phase5_by_scene[ordinal]
            original["report_oracle"] = {
                "path": PHASE5_REPORT.as_posix(),
                "line": report["report_line"],
                "termination": report["termination"],
                "hops": report["hops"],
            }
            if report["termination"] != row["termination"]:
                original["authority_conflict"] = {
                    "field": "termination",
                    "catalog_value": row["termination"],
                    "report_value": report["termination"],
                    "resolution": (
                        "P2 已保留双值；冻结 runtime 实测为 ambiguous。catalog/report 作为"
                        "历史 oracle 漂移继续保留，禁止静默改写。"
                    ),
                }
            original["historical_subset_audit"] = {
                "path": PHASE5_RUNTIME_AUDIT.as_posix(),
                "scenario_id": scenario_id,
                "status": historical_runtime_row["status"],
                "termination": historical_runtime_row[
                    "locked_runtime"
                ]["termination"],
            }
            response = phase5_public_row["response"]
            summary = response["summary"]
            ambiguity = response["data"]["ambiguity_evidence"]
            original["locked_runtime_oracle"] = {
                "path": P3C_PHASE5_PUBLIC_ORACLE.as_posix(),
                "scenario_id": scenario_id,
                "termination": summary["termination"],
                "termination_detail": summary["termination_detail"],
                "scan_complete": summary["scan_complete"],
                "analysis_complete": summary["analysis_complete"],
                "response_truncated": summary["response_truncated"],
                "total_hops": summary["total_count"],
                "statement_count": ambiguity["statement_count"],
                "rhs_signal_count": ambiguity["rhs_signal_count"],
                "value_width_complete": summary["value_width_complete"],
                "width_diagnostics": summary["width_diagnostics"],
                "original_fsdb_sha256": phase5_public_row[
                    "fixture"
                ]["fsdb_sha256"],
            }
            status = "semantic-equivalent"
            rationale = (
                "两份原版 Phase5 RTL 与当前镜像逐字节相同；锁定公开 runtime 完整响应"
                "和当前原始 FST/binary-v1 DesignDB 已按 statement、RHS、活动时间、值、"
                "源码、宽度投影、termination 与完整性逐项通过。P2 的 catalog/report "
                "历史漂移继续留证，但不再作为完整响应等价性的裁决权威。"
            )
        current = current_evidence(repo_root, current_assets, current_tests, candidate)
        runtime_evidence = None
        public_args = {"signal": row["signal"], "time": row["time"]}
        public_limits = None
        if group == "p0":
            native = p0_runtime_row["native_result"]
            exact_current_paths = {
                p0_runtime_row["rtl_mirrors"][0]["current_path"],
                (
                    "testdata/fixtures/active_trace/p0/"
                    f"{p0_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C P0 exact current fixture evidence is missing: {scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C P0 六场景逐项差分已通过；不是以共享 DUT 或代表 case 替代"
            )
            schema_projections = []
            if any(
                    hop.get("file") == "" and hop.get("line") == 0
                    for hop in native["chain"]):
                schema_projections.append({
                    "kind": "source_location_sentinel",
                    "native_shape": "source-less file='' and line=0",
                    "public_shape": "file='<unknown>', line=1, source_context=[]",
                })
            if native["termination"] == "control_only" and \
                    native["branch_evidence"]:
                schema_projections.append({
                    "kind": "control_only_candidate_sampling",
                    "native_shape": "control_only with branch candidates",
                    "public_shape": (
                        "control_only without schema-invalid ambiguity_evidence; "
                        "candidate before/after values independently gated by value.at"
                    ),
                })
            runtime_evidence = {
                "path": P3C_P0_ORACLE.as_posix(),
                "sha256": p3c_p0_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "remaining_observable_gap_count": 0,
                "schema_projections": schema_projections,
            }
            public_args = dict(p0_runtime_row["request"])
        elif group == "composite":
            native = composite_runtime_row["native_result"]
            exact_current_paths = {
                *(mirror["current_path"]
                  for mirror in composite_runtime_row["rtl_mirrors"]),
                (
                    "testdata/fixtures/active_trace/composite/"
                    f"{composite_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C composite exact current fixture evidence is missing: "
                    f"{scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C composite 二十场景逐项完整差分已通过；"
                "没有用共享 DUT 代替 case 级参数与刺激"
            )
            schema_projections = []
            if any(
                    hop.get("file") == "" and hop.get("line") == 0
                    for hop in native["chain"]):
                schema_projections.append({
                    "kind": "source_location_sentinel",
                    "native_shape": "source-less file='' and line=0",
                    "public_shape": (
                        "file='<unknown>', line=1, source_context=[]"
                    ),
                })
            runtime_evidence = {
                "path": P3C_COMPOSITE_ORACLE.as_posix(),
                "sha256": p3c_composite_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "locked_temporal_boundaries": native["temporal_boundaries"],
                "remaining_observable_gap_count": 0,
                "schema_projections": schema_projections,
            }
            public_args = {
                "signal": composite_runtime_row["request"]["signal"],
                "time": composite_runtime_row["request"]["time"],
            }
            public_limits = {"max_depth": 11}
        elif group == "timing":
            native = timing_runtime_row["native_result"]
            exact_current_paths = {
                *(mirror["current_path"]
                  for mirror in timing_runtime_row["rtl_mirrors"]),
                (
                    "testdata/fixtures/active_trace/timing/"
                    f"{timing_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C timing exact current fixture evidence is missing: "
                    f"{scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C timing 十二场景逐项差分已通过；native 私有停止点只作为"
                "当前完整公开链的首跳前缀，不扩展冻结公开 schema"
            )
            runtime_evidence = {
                "path": P3C_TIMING_ORACLE.as_posix(),
                "sha256": p3c_timing_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "locked_temporal_boundaries": native["temporal_boundaries"],
                "remaining_observable_gap_count": 0,
                "schema_projections": [{
                    "kind": "native_stop_on_temporal_prefix",
                    "native_shape": (
                        "private stop_on_temporal=true returns one "
                        "temporal_boundary hop"
                    ),
                    "public_shape": (
                        "frozen v1 has no stop_on_temporal; exact native hop is "
                        "gated as the prefix of a complete untruncated public chain"
                    ),
                }],
                "scheduler_projection": {
                    "kind": "same_slot_nba_active_time_projection",
                    "raw_waveforms_declared_exact": False,
                    "native_observation": (
                        "VCS FSDB exposes staged array propagation at the next "
                        "matching NBA sensitivity edge"
                    ),
                    "current_observation": (
                        "Verilator FST can collapse the output propagation into "
                        "the source NBA slot"
                    ),
                    "public_resolution": (
                        "project only when the DesignDB continuous-driver graph, "
                        "same-array propagation, unique NBA event and next matching "
                        "edge gates all succeed; otherwise fail closed"
                    ),
                },
            }
            public_args = {
                "signal": timing_runtime_row["request"]["signal"],
                "time": timing_runtime_row["request"]["time"],
            }
            public_limits = {"max_depth": 64, "max_nodes": 64}
        elif group == "phase4":
            native = phase4_runtime_row["native_result"]
            exact_current_paths = {
                *(mirror["current_path"]
                  for mirror in phase4_runtime_row["rtl_mirrors"]),
                (
                    "testdata/fixtures/active_trace/phase4/"
                    f"{phase4_runtime_row['case']}/waves.fst"
                ),
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C Phase4 exact current fixture evidence is missing: {scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C Phase4 二十场景逐项完整差分已通过；没有用共享 DUT 代替 case 级刺激"
            )
            schema_projections = []
            if any(
                    hop.get("file") == "" and hop.get("line") == 0
                    for hop in native["chain"]):
                schema_projections.append({
                    "kind": "source_location_sentinel",
                    "native_shape": "source-less file='' and line=0",
                    "public_shape": (
                        "file='<unknown>', line=1, source_context=[]"
                    ),
                })
            runtime_evidence = {
                "path": P3C_PHASE4_ORACLE.as_posix(),
                "sha256": p3c_phase4_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_native_termination": native["termination"],
                "locked_native_hop_count": native["total_hops"],
                "locked_temporal_boundaries": native["temporal_boundaries"],
                "remaining_observable_gap_count": 0,
                "schema_projections": schema_projections,
            }
            public_args = {
                "signal": phase4_runtime_row["request"]["signal"],
                "time": phase4_runtime_row["request"]["time"],
            }
            public_limits = {"max_depth": 16}
        elif group == "phase5":
            summary = phase5_public_row["response"]["summary"]
            ambiguity = phase5_public_row["response"]["data"][
                "ambiguity_evidence"
            ]
            exact_current_paths = {
                *(mirror["current_path"]
                  for mirror in phase5_public_row["rtl_mirrors"]),
                "testdata/fixtures/active_trace/phase5/phase5/waves.fst",
            }
            current["candidate_sources"] = [
                source for source in current["candidate_sources"]
                if source["path"] in exact_current_paths
            ]
            if {
                source["path"] for source in current["candidate_sources"]
            } != exact_current_paths:
                raise MatrixError(
                    f"P3-C Phase5 exact current fixture evidence is missing: "
                    f"{scenario_id}"
                )
            current["evidence_scope"] = (
                "P3-C Phase5 十场景逐项完整响应差分已通过；P2 子集审计仅保留为历史证据"
            )
            schema_projections = [{
                "kind": "statement_kind_projection",
                "native_shape": "NPI assignment with textual driver",
                "public_shape": (
                    "DesignDB proc_assign with the same source line and "
                    "ordered RHS evidence"
                ),
            }]
            if "dout" in phase5_public_row["request"]["signal"]:
                schema_projections.append({
                    "kind": "exact_width_strengthening",
                    "native_shape": (
                        "value_width_complete=false with an explicit "
                        "npi_range_size_unavailable diagnostic and unsized hop"
                    ),
                    "public_shape": (
                        "exact 8-bit FST/DesignDB value with "
                        "value_width_complete=true and width_diagnostics=[]"
                    ),
                })
            runtime_evidence = {
                "path": P3C_PHASE5_PUBLIC_ORACLE.as_posix(),
                "sha256": p3c_phase5_oracle_asset["sha256"],
                "scenario_id": scenario_id,
                "status": "semantic-equivalent",
                "p3_batch": "P3-C",
                "locked_termination": summary["termination"],
                "locked_termination_detail": summary["termination_detail"],
                "locked_hop_count": summary["total_count"],
                "locked_statement_count": ambiguity["statement_count"],
                "locked_rhs_signal_count": ambiguity["rhs_signal_count"],
                "remaining_observable_gap_count": 0,
                "full_response_equivalent": True,
                "historical_subset_audit": {
                    "path": PHASE5_RUNTIME_AUDIT.as_posix(),
                    "sha256": audit_asset["sha256"],
                    "status": "partial",
                },
                "schema_projections": schema_projections,
            }
            public_args = dict(phase5_public_row["request"])
            public_limits = dict(phase5_public_row["limits"])
        scenarios.append({
            "scenario_id": scenario_id,
            "kind": "active_trace_catalog_case",
            "p3_batch": "P3-C",
            "status": status,
            "rationale": rationale,
            "original": original,
            "current": current,
            **({"runtime_audit": runtime_evidence} if runtime_evidence else {}),
            "public_request": {
                "api_version": "xdebug.v1",
                "action": "trace.active_driver_chain",
                "args": public_args,
                **({"limits": public_limits} if public_limits else {}),
            },
            "public_action_contracts": {
                "trace.active_driver_chain": contracts["trace.active_driver_chain"]
            },
        })

    readme_asset = original_assets[ACTIVE_README.as_posix()]
    readme_text = validate_frozen_file(original_root, readme_asset).decode("utf-8")
    orphan_line = file_line(original_root / ACTIVE_README, r"p0_4_interface_modport")
    orphan_candidate = {
        "fixtures": [],
        "tests": [
            "test_declared_only_p0_4_has_a_bounded_frozen_absence_proof",
        ],
        "actions": ["trace.active_driver_chain"],
        "batch": "P3-C",
        "evidence_scope": (
            "冻结原版 p0_4 仅有声明和 .gitignore；零 RTL/stimulus/catalog request/"
            "具体波形的有限不可观察证明，不使用当前 interface/modport 候选替代"
        ),
    }
    p0_paths = fixture_consumers(consumers, "xdebug.active_trace_p0")
    scenarios.append({
        "scenario_id": "active.p0.declared_only_p0_4",
        "kind": "declared_only_orphan",
        "p3_batch": "P3-C",
        "status": "proven-unobservable",
        "rationale": (
            "冻结 README 只声明 case 名，目录仅有 .gitignore，catalog 没有 signal/time 请求，"
            "也没有 RTL、刺激或具体波形；公开 schema 无法形成可执行观察点。该证明只关闭"
            "此冻结空骨架，不把当前 interface/modport 能力当作原版等价证据。"
        ),
        "original": {
            "fixture_id": "xdebug.active_trace_p0",
            "declaration": {"path": ACTIVE_README.as_posix(), "line": orphan_line},
            "case": "p0_4_interface_modport",
            "catalog_presence": False,
            "sources": [],
            "declared_waveform_outputs": [
                item for item in manifest["original_declared_waveform_outputs"]
                if item["fixture_id"] == "xdebug.active_trace_p0"
            ],
            "consumer_paths": p0_paths,
            "observed_public_actions": fixture_actions(consumers, p0_paths),
        },
        "current": current_evidence(repo_root, current_assets, current_tests, orphan_candidate),
        "unobservable_proof": {
            "path": P3C_ACTIVE_TRACE_CLOSURE_AUDIT.as_posix(),
            "sha256": p3c_closure_audit_asset["sha256"],
            "classification": p3c_closure_audit[
                "declared_only_orphan"
            ]["classification"],
            "catalog_row_count": 0,
            "authoritative_public_request_count": 0,
            "rtl_count": 0,
            "stimulus_count": 0,
            "concrete_waveform_count": 0,
            "remaining_distinct_public_observation_count": 0,
            "proof_scope": p3c_closure_audit[
                "declared_only_orphan"
            ]["proof_scope"],
        },
        "public_action_contracts": {
            "trace.active_driver_chain": contracts["trace.active_driver_chain"]
        },
    })

    scenarios_by_id = {item["scenario_id"]: item for item in scenarios}
    covered_runner_scenarios = p3c_closure_audit["runner"]["coverage"][
        "covered_scenario_ids"
    ]
    if any(
        scenarios_by_id.get(scenario_id, {}).get("status") !=
            "semantic-equivalent"
        for scenario_id in covered_runner_scenarios
    ):
        raise MatrixError(
            "P3-C runner cannot be proven unobservable before all 68 catalog "
            "observations are semantically closed"
        )

    cross_paths = fixture_consumers(consumers, "original.cross_fixture")
    cross_proof = p3e_closure["cross_fixture"]
    cross_actions = fixture_actions(consumers, cross_paths)
    if cross_actions != cross_proof["observed_public_actions"]:
        raise MatrixError("P3-E cross-fixture consumer Action index drifted")
    cross_tests = [
        current_tests[name]
        for name in (
            "test_cross_fixture_surface_is_a_complete_contract_index_only",
            "test_p3e_sva_and_cross_fixture_proofs_are_strictly_bounded",
        )
    ]
    scenarios.append({
        "scenario_id": "cross_fixture.public_contract_consumers",
        "kind": "cross_fixture_contract_surface",
        "p3_batch": "P3-E",
        "status": "proven-unobservable",
        "rationale": (
            "37 个 synthetic cross-fixture consumer 的 72 个公开 Action 已逐项绑定冻结合同；"
            "该分组没有独立 RTL、刺激或波形观察点，故只关闭额外独立观察点。各波形 fixture "
            "仍由自身 runtime differential 裁决，session.kill 继续由 73 Action/P4 门禁覆盖。"
        ),
        "original": {
            "fixture_id": None,
            "sources": [],
            "declared_waveform_outputs": [],
            "consumer_paths": cross_paths,
            "observed_public_actions": fixture_actions(consumers, cross_paths),
        },
        "current": {
            "candidate_fixture_ids": [],
            "candidate_sources": [],
            "test_evidence": cross_tests,
            "evidence_scope": (
                "P3-E 只关闭 synthetic consumer index 的额外独立观察点；"
                "不替代任何 fixture runtime differential"
            ),
        },
        "unobservable_proof": {
            "path": P3E_CLOSURE_AUDIT.as_posix(),
            "sha256": p3e_closure_asset["sha256"],
            **cross_proof,
        },
        "public_action_contracts": {
            action: contracts[action] for action in cross_actions
        },
    })

    scenarios.sort(key=lambda item: item["scenario_id"])
    scenario_ids = {item["scenario_id"] for item in scenarios}
    if len(scenario_ids) != len(scenarios):
        raise MatrixError("duplicate scenario_id")
    if any(item["status"] not in ALLOWED_STATUSES for item in scenarios):
        raise MatrixError("matrix contains an invalid status")

    fixture_index = {}
    for fixture_id in sorted(fixture_map):
        matches = [
            item["scenario_id"] for item in scenarios
            if item["original"].get("fixture_id") == fixture_id
        ]
        if not matches:
            raise MatrixError(f"original fixture has no scenario: {fixture_id}")
        fixture_index[fixture_id] = matches

    hdl_index = {}
    for path, asset in sorted(original_assets.items()):
        if asset["kind"] != "rtl":
            continue
        matches = [
            item["scenario_id"] for item in scenarios
            if path in {source["path"] for source in item["original"]["sources"]}
        ]
        if not matches:
            raise MatrixError(f"original HDL has no scenario: {path}")
        hdl_index[path] = matches

    output_index = {}
    for output in manifest["original_declared_waveform_outputs"]:
        key = f"{output['fixture_id']}::{output['path']}"
        matches = fixture_index.get(output["fixture_id"], [])
        if not matches:
            raise MatrixError(f"declared waveform output has no scenario: {key}")
        output_index[key] = matches

    consumer_index = {}
    for path in sorted(consumers):
        matches = [
            item["scenario_id"] for item in scenarios
            if path in item["original"]["consumer_paths"]
        ]
        if not matches:
            raise MatrixError(f"original consumer has no scenario: {path}")
        consumer_index[path] = matches

    status_counts = Counter(item["status"] for item in scenarios)
    p3_queue = defaultdict(list)
    for item in scenarios:
        if item["status"] in {"partial", "missing"}:
            p3_queue[item["p3_batch"]].append(item["scenario_id"])

    return {
        "schema_version": SCHEMA_VERSION,
        "goal_id": GOAL_ID,
        "policy": {
            "comparison_unit": (
                "fixture + RTL construct + stimulus/time + observable path + public Action + complete response"
            ),
            "candidate_evidence_is_not_equivalence": True,
            "unclassified_status_forbidden": True,
            "only_writable_repository": "xdebug_fst",
            "external_inputs_read_only": True,
        },
        "authority": {
            "runtime_revision": manifest["baselines"]["original_runtime"]["runtime_revision"],
            "schema_revision": manifest["baselines"]["original_runtime"]["schema_revision"],
            "asset_goal_start_head": manifest["baselines"]["original_assets"]["head"],
            "active_catalog": ACTIVE_CATALOG.as_posix(),
        },
        "action_contracts": contracts,
        "original_consumers": consumers,
        "scenarios": scenarios,
        "coverage": {
            "original_fixtures": fixture_index,
            "original_hdl": hdl_index,
            "declared_waveform_outputs": output_index,
            "original_consumers": consumer_index,
        },
        "p3_queue": {key: sorted(value) for key, value in sorted(p3_queue.items())},
        "summary": {
            "scenario_count": len(scenarios),
            "active_catalog_case_count": len(active_rows),
            "declared_only_orphan_count": 1,
            "original_fixture_count": len(fixture_index),
            "original_hdl_count": len(hdl_index),
            "declared_waveform_output_count": len(output_index),
            "original_consumer_count": len(consumer_index),
            "status_counts": dict(sorted(status_counts.items())),
            "unclassified_count": 0,
            "unqueued_gap_count": 0,
        },
    }


def write_atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.",
        delete=False,
    ) as stream:
        stream.write(content)
        temporary = Path(stream.name)
    os.replace(temporary, path)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ASSET_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--original-root", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    repo_root = Path(__file__).resolve().parents[1]
    manifest = args.manifest if args.manifest.is_absolute() else repo_root / args.manifest
    output = args.output if args.output.is_absolute() else repo_root / args.output
    output = ensure_within_repo(repo_root, output)
    original_value = args.original_root or (
        Path(os.environ["XDEBUG_ORIGINAL_ROOT"])
        if os.environ.get("XDEBUG_ORIGINAL_ROOT") else None
    )
    if original_value is None:
        raise MatrixError("set XDEBUG_ORIGINAL_ROOT or pass --original-root; no fallback is allowed")
    original_root = original_value.resolve()
    matrix = build_matrix(repo_root, original_root, manifest)
    content = canonical_json(matrix)
    if args.check:
        if not output.is_file() or output.read_text(encoding="utf-8") != content:
            raise MatrixError(f"semantic matrix is stale: {output.relative_to(repo_root)}")
        print(f"OK: {output.relative_to(repo_root)} is reproducible")
        return 0
    write_atomic(output, content)
    print(f"wrote {output.relative_to(repo_root)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except MatrixError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
