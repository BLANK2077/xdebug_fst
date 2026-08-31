#!/usr/bin/env bash
# Regenerate only the open-source P3-E XIF event semantic mirror.
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/xif_event"
readonly REQUESTED_WORK_DIR="${XDEBUG_XIF_EVENT_WORK_DIR:-${REPO_DIR}/build/fixtures/xif_event}"
mkdir -p -- "${REQUESTED_WORK_DIR}"
readonly WORK_DIR="$(cd -- "${REQUESTED_WORK_DIR}" && pwd -P)"
readonly FSTCPP_INCLUDE="${XDEBUG_FIXTURE_BUILD_DIR}/_deps/verilator-src/include"
readonly PATCHED_INCLUDE="${WORK_DIR}/include"
readonly GENERATOR="${WORK_DIR}/generate-xif-event-fst"
readonly GENERATED_FST="${WORK_DIR}/waves.fst"
readonly MANIFEST="${FIXTURE_DIR}/fixture.manifest.json"

case "${FIXTURE_DIR}" in "${REPO_DIR}"/*) ;; *) echo "XIF fixture 输出逃逸当前仓库" >&2; exit 2;; esac
case "${WORK_DIR}" in "${REPO_DIR}"/*) ;; *) echo "XIF fixture 构建目录逃逸当前仓库" >&2; exit 2;; esac
[[ -f "${MANIFEST}" && -f "${REPO_DIR}/build/dependencies.resolved.json" ]] || {
  echo "XIF fixture 缺少 manifest/dependency lock" >&2
  exit 2
}

jq -e --arg revision "$(jq -r '.verilator.revision' "${REPO_DIR}/build/dependencies.resolved.json")" \
  --arg tree "$(jq -r '.verilator.tree' "${REPO_DIR}/build/dependencies.resolved.json")" \
  --arg fingerprint "$(jq -r '.verilator.fingerprint' "${REPO_DIR}/build/dependencies.resolved.json")" \
  --arg patchset "$(jq -r '.verilator.patchset_version' "${REPO_DIR}/build/dependencies.resolved.json")" \
  '.fixture_id == "current.xif_event" and
   .source_contract.original_fixture_id == "xdebug.xif_event" and
   .source_contract.producer_equivalence == "pin-level-semantic-mirror" and
   .source_contract.proprietary_vip_used == false and
   .source_contract.fsdb_conversion_used == false and
   .source_contract.action_export_feedback_used == false and
   .source_contract.randomization == false and
   .source_contract.posedge_sample_count == 20 and
   .source_contract.interface_count == 5 and
   .build_contract.revision == $revision and
   .build_contract.tree == $tree and
   .build_contract.fingerprint == $fingerprint and
   .build_contract.patchset_version == $patchset and
   .output_contract.external_cache_rebuilt == false and
   .output_contract.proprietary_artifact_committed == false' "${MANIFEST}" >/dev/null || {
  echo "XIF fixture manifest 与冻结源/工具边界不一致" >&2
  exit 2
}

for source in fstcpp_variable_info.h fstcpp_writer.cpp fstcpp_variable_info.cpp; do
  expected="$(jq -r --arg source "${source}" '.build_contract.fstcpp_source_sha256[$source]' "${MANIFEST}")"
  actual="$(sha256sum "${FSTCPP_INCLUDE}/fstcpp/${source}" | awk '{print $1}')"
  [[ "${actual}" == "${expected}" ]] || {
    echo "XIF fstcpp source 漂移: ${source}" >&2
    exit 2
  }
done

for source in xif_event_top.sv generate_xif_event_fst.cpp fstcpp-four-state-vector.patch; do
  key="current_rtl_sha256"
  [[ "${source}" == generate_xif_event_fst.cpp ]] && key="generator_sha256"
  [[ "${source}" == fstcpp-four-state-vector.patch ]] && key="four_state_patch_sha256"
  expected="$(jq -r --arg key "${key}" '.source_contract[$key]' "${MANIFEST}")"
  actual="$(sha256sum "${FIXTURE_DIR}/${source}" | awk '{print $1}')"
  [[ "${actual}" == "${expected}" ]] || {
    echo "XIF fixture source 漂移: ${source}" >&2
    exit 2
  }
done
for config in "${FIXTURE_DIR}"/event_*.json; do
  name="${config##*/}"
  expected="$(jq -r --arg name "${name}" '.source_contract.config_sha256[$name]' "${MANIFEST}")"
  actual="$(sha256sum "${config}" | awk '{print $1}')"
  [[ "${actual}" == "${expected}" ]] || {
    echo "XIF config 漂移: ${name}" >&2
    exit 2
  }
done

mkdir -p -- "${PATCHED_INCLUDE}/fstcpp"
install -m 0644 "${FSTCPP_INCLUDE}/fstcpp/fstcpp_variable_info.h" \
  "${PATCHED_INCLUDE}/fstcpp/fstcpp_variable_info.h"
patch --batch --forward --fuzz=0 -d "${WORK_DIR}" -p1 \
  < "${FIXTURE_DIR}/fstcpp-four-state-vector.patch"
"${GXX_BIN}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[@]}" \
  -I"${PATCHED_INCLUDE}" -I"${FSTCPP_INCLUDE}" \
  "${FIXTURE_DIR}/generate_xif_event_fst.cpp" \
  "${FSTCPP_INCLUDE}/fstcpp/fstcpp_writer.cpp" \
  "${FSTCPP_INCLUDE}/fstcpp/fstcpp_variable_info.cpp" \
  -llz4 -lz -o "${GENERATOR}"
"${GENERATOR}" "${GENERATED_FST}"

readonly EXPECTED_SHA="$(jq -r '.output_contract.sha256' "${MANIFEST}")"
readonly EXPECTED_SIZE="$(jq -r '.output_contract.size' "${MANIFEST}")"
readonly ACTUAL_SHA="$(sha256sum "${GENERATED_FST}" | awk '{print $1}')"
readonly ACTUAL_SIZE="$(stat -c '%s' "${GENERATED_FST}")"
[[ "${ACTUAL_SHA}" == "${EXPECTED_SHA}" && "${ACTUAL_SIZE}" == "${EXPECTED_SIZE}" ]] || {
  echo "XIF FST 输出漂移: ${ACTUAL_SHA}/${ACTUAL_SIZE}" >&2
  exit 1
}
install -m 0644 "${GENERATED_FST}" "${FIXTURE_DIR}/waves.fst"
(
  cd -- "${FIXTURE_DIR}"
  sha256sum event_*.json xif_event_top.sv generate_xif_event_fst.cpp \
    fstcpp-four-state-vector.patch fixture.manifest.json waves.fst > fixture.sha256
)
sha256sum "${FIXTURE_DIR}/waves.fst"
