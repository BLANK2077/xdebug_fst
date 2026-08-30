#!/usr/bin/env bash
# Regenerate the locked-original active-driver/interface mirrors as raw FST.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
readonly WORK_ROOT="${REPO_DIR}/build/fixtures/p3b-active"

case "${WORK_ROOT}" in
    "${REPO_DIR}"/*) ;;
    *) echo "P3-B fixture 构建目录逃逸当前仓库" >&2; exit 2 ;;
esac

build_fixture() {
    local fixture_name="$1"
    local top_name="$2"
    local rtl_name="$3"
    local harness_name="$4"
    local fixture_dir="${REPO_DIR}/testdata/fixtures/${fixture_name}"
    local work_dir="${WORK_ROOT}/${fixture_name}"
    local design_cpp="V${top_name}__DesignDb.cpp"

    case "${fixture_dir}" in
        "${REPO_DIR}"/*) ;;
        *) echo "P3-B fixture 输出逃逸当前仓库" >&2; exit 2 ;;
    esac
    mkdir -p -- "${work_dir}/obj_dir"
    (
        cd -- "${REPO_DIR}"
        "${VERILATOR_BIN}" -Wno-fatal --cc --exe --build --timing \
            --trace-fst --design-db --top-module "${top_name}" \
            --Mdir "${work_dir}/obj_dir" \
            "testdata/fixtures/${fixture_name}/${rtl_name}" \
            "testdata/fixtures/${fixture_name}/${harness_name}"
    )
    (
        cd -- "${work_dir}"
        "./obj_dir/V${top_name}"
    )
    install -m 0644 "${work_dir}/waves.fst" "${fixture_dir}/waves.fst"
    install -m 0644 "${work_dir}/obj_dir/${design_cpp}" \
        "${fixture_dir}/${design_cpp}"
    (
        cd -- "${fixture_dir}"
        sha256sum "${rtl_name}" "${harness_name}" "${design_cpp}" waves.fst \
            > fixture.sha256
    )
}

build_fixture active_driver active_driver_tb active_driver_tb.sv \
    tb_active_driver.cpp
build_fixture interface_port_root if_root_tb if_root_tb.sv tb_if_root.cpp
build_fixture active_zero_evidence active_zero_evidence_tb \
    active_zero_evidence_tb.v tb_active_zero_evidence.cpp
