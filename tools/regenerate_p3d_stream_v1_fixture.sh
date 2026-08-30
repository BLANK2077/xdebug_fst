#!/usr/bin/env bash
# Regenerate only the P3-D1 byte-identical stream_v1 stimulus as raw FST.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/stream_v1"
readonly REQUESTED_WORK_DIR="${XDEBUG_STREAM_V1_WORK_DIR:-${REPO_DIR}/build/fixtures/stream_v1}"
mkdir -p -- "${REQUESTED_WORK_DIR}"
readonly WORK_DIR="$(cd -- "${REQUESTED_WORK_DIR}" && pwd -P)"
readonly OBJECT_DIR="${WORK_DIR}/obj_dir"
readonly GENERATED_FST="${WORK_DIR}/waves.fst"
readonly GENERATED_EXPECTED="${WORK_DIR}/stream_expected.json"
readonly COMPILE_RTL="${WORK_DIR}/stream_v1_top.sv"
readonly VERILATOR_OVERLAY="${FIXTURE_DIR}/verilator-no-fsdb.patch"
readonly FIXTURE_MANIFEST="${FIXTURE_DIR}/fixture.manifest.json"
readonly DEPENDENCY_LOCK="${REPO_DIR}/build/dependencies.resolved.json"

case "${FIXTURE_DIR}" in
    "${REPO_DIR}"/*) ;;
    *) echo "P3-D1 fixture 输出逃逸当前仓库" >&2; exit 2 ;;
esac
case "${WORK_DIR}" in
    "${REPO_DIR}"/*) ;;
    *) echo "P3-D1 fixture 构建目录逃逸当前仓库" >&2; exit 2 ;;
esac

[[ -f "${FIXTURE_MANIFEST}" && -f "${DEPENDENCY_LOCK}" ]] || {
    echo "P3-D1 缺少 fixture/tool 身份锁" >&2
    exit 2
}
jq -e --arg revision "$(jq -r '.verilator.revision' "${DEPENDENCY_LOCK}")" \
    --arg tree "$(jq -r '.verilator.tree' "${DEPENDENCY_LOCK}")" \
    --arg fingerprint "$(jq -r '.verilator.fingerprint' "${DEPENDENCY_LOCK}")" \
    --arg patchset "$(jq -r '.verilator.patchset_version' "${DEPENDENCY_LOCK}")" \
    '.build_contract.revision == $revision and
     .build_contract.tree == $tree and
     .build_contract.fingerprint == $fingerprint and
     .build_contract.patchset_version == $patchset and
     .source_contract.randomization == false and
     .source_contract.seed == "not-applicable-deterministic-formulas" and
     .output_contract.fsdb_conversion_used == false and
     .output_contract.external_cache_rebuilt == false' \
    "${FIXTURE_MANIFEST}" >/dev/null || {
    echo "P3-D1 fixture manifest 与锁定工具/刺激合同不一致" >&2
    exit 2
}

mkdir -p -- "${OBJECT_DIR}"
install -m 0644 "${FIXTURE_DIR}/stream_v1_top.sv" "${COMPILE_RTL}"
patch --batch --silent "${COMPILE_RTL}" < "${VERILATOR_OVERLAY}"
(
    cd -- "${REPO_DIR}"
    "${VERILATOR_BIN}" -Wno-fatal --cc --exe --build --timing \
        --bbox-sys --trace-fst --trace-depth 2 \
        --top-module stream_v1_top --Mdir "${OBJECT_DIR}" \
        -CFLAGS "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[*]}" \
        "${COMPILE_RTL}" \
        "testdata/fixtures/stream_v1/tb_stream_v1.cpp"
)
(
    cd -- "${WORK_DIR}"
    "./obj_dir/Vstream_v1_top" "${GENERATED_FST}"
)

[[ -s "${GENERATED_FST}" ]] || {
    echo "P3-D1 未生成非空 FST" >&2
    exit 1
}
cmp -- "${GENERATED_EXPECTED}" "${FIXTURE_DIR}/stream_expected.json" || {
    echo "P3-D1 RTL 运行计数与锁定 expected 不一致" >&2
    exit 1
}
readonly EXPECTED_FST_SHA256="$(jq -r '.output_contract.sha256' "${FIXTURE_MANIFEST}")"
readonly EXPECTED_FST_SIZE="$(jq -r '.output_contract.size' "${FIXTURE_MANIFEST}")"
readonly ACTUAL_FST_SHA256="$(sha256sum "${GENERATED_FST}" | cut -d' ' -f1)"
readonly ACTUAL_FST_SIZE="$(stat -c '%s' "${GENERATED_FST}")"
[[ "${ACTUAL_FST_SHA256}" == "${EXPECTED_FST_SHA256}" &&
   "${ACTUAL_FST_SIZE}" == "${EXPECTED_FST_SIZE}" ]] || {
    echo "P3-D1 FST 与确定性输出锁不一致: ${ACTUAL_FST_SHA256}/${ACTUAL_FST_SIZE}" >&2
    exit 1
}

install -m 0644 "${GENERATED_FST}" "${FIXTURE_DIR}/waves.fst"
(
    cd -- "${FIXTURE_DIR}"
    sha256sum stream_v1_top.sv streams.json stream_expected.json \
        verilator-no-fsdb.patch tb_stream_v1.cpp fixture.manifest.json \
        waves.fst > fixture.sha256
)

sha256sum "${FIXTURE_DIR}/waves.fst"
