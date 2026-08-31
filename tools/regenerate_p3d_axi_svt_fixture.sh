#!/usr/bin/env bash
# Regenerate only the repository-local P3-D3 SVT AXI pin-level mirrors.
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/axi_vip"
readonly MANIFEST="${FIXTURE_DIR}/fixture.manifest.json"
readonly DEPENDENCY_LOCK="${REPO_DIR}/build/dependencies.resolved.json"
readonly REQUESTED_WORK_DIR="${XDEBUG_AXI_SVT_WORK_DIR:-${REPO_DIR}/build/fixtures/axi_vip}"
readonly REQUESTED_RUNS="${XDEBUG_AXI_SVT_RUNS:-stress fixed_delay random_seed_7 random_seed_19 random_seed_73}"

mkdir -p -- "${REQUESTED_WORK_DIR}"
readonly WORK_DIR="$(cd -- "${REQUESTED_WORK_DIR}" && pwd -P)"
case "${FIXTURE_DIR}" in "${REPO_DIR}"/*) ;; *) echo "SVT AXI fixture 输出逃逸当前仓库" >&2; exit 2;; esac
case "${WORK_DIR}" in "${REPO_DIR}"/*) ;; *) echo "SVT AXI 构建目录逃逸当前仓库" >&2; exit 2;; esac
[[ -f "${MANIFEST}" && -f "${DEPENDENCY_LOCK}" ]] || {
  echo "SVT AXI fixture 缺少 fixture/tool 身份锁" >&2
  exit 2
}

readonly RUNTIME_ROOT="${WORK_DIR}/runtime"
mkdir -p -- "${RUNTIME_ROOT}/home" "${RUNTIME_ROOT}/tmp" "${RUNTIME_ROOT}/cache"
export HOME="${RUNTIME_ROOT}/home"
export TMPDIR="${RUNTIME_ROOT}/tmp"
export XDG_CACHE_HOME="${RUNTIME_ROOT}/cache"

jq -e --arg revision "$(jq -r '.verilator.revision' "${DEPENDENCY_LOCK}")" \
  --arg tree "$(jq -r '.verilator.tree' "${DEPENDENCY_LOCK}")" \
  --arg fingerprint "$(jq -r '.verilator.fingerprint' "${DEPENDENCY_LOCK}")" \
  --arg patchset "$(jq -r '.verilator.patchset_version' "${DEPENDENCY_LOCK}")" \
  '.build_contract.revision == $revision and
   .build_contract.tree == $tree and
   .build_contract.fingerprint == $fingerprint and
   .build_contract.patchset_version == $patchset and
   .source_contract.original_fixture_id == "xdebug.axi_vip" and
   .source_contract.producer_equivalence == "frozen-pin-level-handshake-mirror" and
   .source_contract.proprietary_vip_used == false and
   .source_contract.fsdb_conversion_used == false and
   .source_contract.export_feedback_used == false and
   .source_contract.external_cache_rebuilt == false' "${MANIFEST}" >/dev/null || {
  echo "SVT AXI manifest 与锁定工具/刺激合同不一致" >&2
  exit 2
}

actual_generator_sha="$(sha256sum "${SCRIPT_DIR}/generate_p3d_axi_svt_mirror.py" | cut -d' ' -f1)"
expected_generator_sha="$(jq -r '.build_contract.generator_sha256' "${MANIFEST}")"
[[ "${actual_generator_sha}" == "${expected_generator_sha}" ]] || {
  echo "SVT AXI generator hash 漂移" >&2
  exit 2
}
actual_harness_sha="$(sha256sum "${FIXTURE_DIR}/tb_axi_svt.cpp" | cut -d' ' -f1)"
expected_harness_sha="$(jq -r '.build_contract.harness_sha256' "${MANIFEST}")"
[[ "${actual_harness_sha}" == "${expected_harness_sha}" ]] || {
  echo "SVT AXI harness hash 漂移" >&2
  exit 2
}
actual_stress_rtl_sha="$(sha256sum "${FIXTURE_DIR}/axi_vip_stress_top.sv" | cut -d' ' -f1)"
expected_stress_rtl_sha="$(jq -r '.build_contract.stress_rtl_sha256' "${MANIFEST}")"
actual_stress_harness_sha="$(sha256sum "${FIXTURE_DIR}/tb_axi_svt_stress.cpp" | cut -d' ' -f1)"
expected_stress_harness_sha="$(jq -r '.build_contract.stress_harness_sha256' "${MANIFEST}")"
[[ "${actual_stress_rtl_sha}" == "${expected_stress_rtl_sha}" &&
   "${actual_stress_harness_sha}" == "${expected_stress_harness_sha}" ]] || {
  echo "SVT AXI stress RTL/harness hash 漂移" >&2
  exit 2
}

for run in ${REQUESTED_RUNS}; do
  jq -e --arg run "${run}" '.runs[$run] != null' "${MANIFEST}" >/dev/null || {
    echo "未知 SVT AXI run: ${run}" >&2
    exit 2
  }
  readonly_run_dir="${WORK_DIR}/${run}"
  object_dir="${readonly_run_dir}/obj_dir"
  generated_sv="${readonly_run_dir}/axi_vip_fixture_top.sv"
  generated_fst="${readonly_run_dir}/waves.fst"
  events="${FIXTURE_DIR}/${run}.events.tsv"
  mkdir -p -- "${readonly_run_dir}" "${object_dir}"
  actual_events_sha="$(sha256sum "${events}" | cut -d' ' -f1)"
  expected_events_sha="$(jq -r --arg run "${run}" '.runs[$run].events_sha256' "${MANIFEST}")"
  [[ "${actual_events_sha}" == "${expected_events_sha}" ]] || {
    echo "SVT AXI ${run} event hash 漂移" >&2
    exit 2
  }
  if [[ "${run}" == "stress" ]]; then
    (
      cd -- "${REPO_DIR}"
      "${VERILATOR_BIN}" -Wno-fatal --public-flat-rw --cc --exe --build \
        --timing --trace-fst --trace-depth 4 --top-module axi_vip_fixture_top \
        --Mdir "${object_dir}" -CFLAGS "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[*]}" \
        "${FIXTURE_DIR}/axi_vip_stress_top.sv" \
        "${FIXTURE_DIR}/tb_axi_svt_stress.cpp"
    )
    "${object_dir}/Vaxi_vip_fixture_top" "${events}" "${generated_fst}"
  else
    python3 "${SCRIPT_DIR}/generate_p3d_axi_svt_mirror.py" render \
      --events "${events}" --output "${generated_sv}"
    (
      cd -- "${REPO_DIR}"
      "${VERILATOR_BIN}" -Wno-fatal --cc --exe --build --timing \
        --trace-fst --trace-depth 4 --top-module axi_vip_fixture_top \
        --Mdir "${object_dir}" -CFLAGS "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[*]}" \
        "${generated_sv}" "${FIXTURE_DIR}/tb_axi_svt.cpp"
    )
    "${object_dir}/Vaxi_vip_fixture_top" "${generated_fst}"
  fi
  actual_sha="$(sha256sum "${generated_fst}" | cut -d' ' -f1)"
  actual_size="$(stat -c '%s' "${generated_fst}")"
  expected_sha="$(jq -r --arg run "${run}" '.runs[$run].fst_sha256' "${MANIFEST}")"
  expected_size="$(jq -r --arg run "${run}" '.runs[$run].fst_size' "${MANIFEST}")"
  [[ "${actual_sha}" == "${expected_sha}" && "${actual_size}" == "${expected_size}" ]] || {
    echo "SVT AXI ${run} FST 漂移: ${actual_sha}/${actual_size}" >&2
    exit 1
  }
  install -m 0644 "${generated_fst}" "${FIXTURE_DIR}/${run}/waves.fst"
  printf '%s  %s\n' "${actual_sha}" "${run}/waves.fst"
done

(
  cd -- "${FIXTURE_DIR}"
  sha256sum -c fixture.sha256
)
