#!/usr/bin/env bash
# Regenerate only the P3-D3 XAMBA AXI semantic mirror as a raw FST.
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/axi_xamba_vip"
readonly REQUESTED_WORK_DIR="${XDEBUG_AXI_XAMBA_WORK_DIR:-${REPO_DIR}/build/fixtures/axi_xamba_vip}"
mkdir -p -- "${REQUESTED_WORK_DIR}"
readonly WORK_DIR="$(cd -- "${REQUESTED_WORK_DIR}" && pwd -P)"
readonly OBJECT_DIR="${WORK_DIR}/obj_dir"
readonly GENERATED_FST="${WORK_DIR}/waves.fst"
readonly FIXTURE_MANIFEST="${FIXTURE_DIR}/fixture.manifest.json"
readonly DEPENDENCY_LOCK="${REPO_DIR}/build/dependencies.resolved.json"

case "${FIXTURE_DIR}" in "${REPO_DIR}"/*) ;; *) echo "P3-D3 XAMBA fixture 输出逃逸当前仓库" >&2; exit 2;; esac
case "${WORK_DIR}" in "${REPO_DIR}"/*) ;; *) echo "P3-D3 XAMBA fixture 构建目录逃逸当前仓库" >&2; exit 2;; esac
[[ -f "${FIXTURE_MANIFEST}" && -f "${DEPENDENCY_LOCK}" ]] || {
  echo "P3-D3 XAMBA fixture 缺少 fixture/tool 身份锁" >&2
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
   .source_contract.original_fixture_id == "xdebug.axi_xamba_vip" and
   .source_contract.producer_equivalence == "pin-level-semantic-mirror" and
   .source_contract.proprietary_vip_used == false and
   .source_contract.fsdb_conversion_used == false and
   .source_contract.randomization == false and
   .source_contract.seed == 7 and
   .source_contract.transaction_count == 64 and
   .source_contract.id_formula == "index % 16" and
   .source_contract.beat_formula == "index % 4 + 1" and
   .source_contract.response_formula == "index%7==0 ? 3 : index%5==0 ? 2 : 0" and
   .output_contract.external_cache_rebuilt == false and
   .output_contract.proprietary_artifact_committed == false' \
  "${FIXTURE_MANIFEST}" >/dev/null || {
  echo "P3-D3 XAMBA fixture manifest 与锁定工具/刺激合同不一致" >&2
  exit 2
}
mkdir -p -- "${OBJECT_DIR}"
(
  cd -- "${REPO_DIR}"
  "${VERILATOR_BIN}" -Wno-fatal --cc --exe --build --timing \
    --trace-fst --trace-depth 4 --top-module xdebug_axi_xamba_fixture_top \
    --Mdir "${OBJECT_DIR}" -CFLAGS "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[*]}" \
    testdata/fixtures/axi_xamba_vip/xdebug_axi_xamba_fixture_top.sv \
    testdata/fixtures/axi_xamba_vip/tb_axi_xamba.cpp
)
"${OBJECT_DIR}/Vxdebug_axi_xamba_fixture_top" "${GENERATED_FST}"
[[ -s "${GENERATED_FST}" ]]
readonly EXPECTED_FST_SHA256="$(jq -r '.output_contract.sha256' "${FIXTURE_MANIFEST}")"
readonly EXPECTED_FST_SIZE="$(jq -r '.output_contract.size' "${FIXTURE_MANIFEST}")"
readonly ACTUAL_FST_SHA256="$(sha256sum "${GENERATED_FST}" | cut -d' ' -f1)"
readonly ACTUAL_FST_SIZE="$(stat -c '%s' "${GENERATED_FST}")"
[[ "${ACTUAL_FST_SHA256}" == "${EXPECTED_FST_SHA256}" &&
   "${ACTUAL_FST_SIZE}" == "${EXPECTED_FST_SIZE}" ]] || {
  echo "P3-D3 XAMBA FST 与确定性输出锁不一致: ${ACTUAL_FST_SHA256}/${ACTUAL_FST_SIZE}" >&2
  exit 1
}
install -m 0644 "${GENERATED_FST}" "${FIXTURE_DIR}/waves.fst"
(
  cd -- "${FIXTURE_DIR}"
  sha256sum xdebug_axi_xamba_fixture_top.sv tb_axi_xamba.cpp \
    fixture.manifest.json waves.fst > fixture.sha256
)
sha256sum "${FIXTURE_DIR}/waves.fst"
