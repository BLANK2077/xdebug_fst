#!/usr/bin/env bash
# Regenerate the direct four-state ai_complex FST inside this repository only.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/ai_complex"
readonly WORK_DIR="${REPO_DIR}/build/fixtures/ai_complex"
readonly FSTCPP_INCLUDE="${XDEBUG_FIXTURE_BUILD_DIR}/_deps/verilator-src/include"
readonly FSTCPP_SOURCE_HEADER="${FSTCPP_INCLUDE}/fstcpp/fstcpp_variable_info.h"
readonly FSTCPP_PATCH="${FIXTURE_DIR}/fstcpp-four-state-vector.patch"
readonly PATCHED_INCLUDE="${WORK_DIR}/include"
readonly GENERATOR="${WORK_DIR}/generate-ai-complex-fst"
readonly GENERATED_FST="${WORK_DIR}/waves.fst"
readonly FSTCPP_HEADER_SHA256="dd476ccc747ab5a9e8d409e9d22c37d08baf945b61b69f1eb1e59c088faf0a66"

case "${FIXTURE_DIR}" in
    "${REPO_DIR}"/*) ;;
    *) echo "fixture 输出逃逸当前仓库" >&2; exit 2 ;;
esac
case "${WORK_DIR}" in
    "${REPO_DIR}"/*) ;;
    *) echo "fixture 构建目录逃逸当前仓库" >&2; exit 2 ;;
esac
[[ -f "${FSTCPP_INCLUDE}/fstcpp/fstcpp_writer.cpp" ]] || {
    echo "统一构建缺少锁定 fstcpp writer；禁止工具 fallback" >&2
    exit 2
}
[[ "$(sha256sum "${FSTCPP_SOURCE_HEADER}" | awk '{print $1}')" = \
    "${FSTCPP_HEADER_SHA256}" ]] || {
    echo "锁定 fstcpp header 已漂移；禁止在未知输入上应用 fixture patch" >&2
    exit 2
}

mkdir -p -- "${PATCHED_INCLUDE}/fstcpp"
install -m 0644 "${FSTCPP_SOURCE_HEADER}" \
    "${PATCHED_INCLUDE}/fstcpp/fstcpp_variable_info.h"
patch --batch --forward --fuzz=0 -d "${WORK_DIR}" -p1 < "${FSTCPP_PATCH}"
"${GXX_BIN}" -std=c++17 -O2 -Wall -Wextra -Werror \
    "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[@]}" \
    -I"${PATCHED_INCLUDE}" \
    -I"${FSTCPP_INCLUDE}" \
    "${FIXTURE_DIR}/generate_ai_complex_fst.cpp" \
    "${FSTCPP_INCLUDE}/fstcpp/fstcpp_writer.cpp" \
    "${FSTCPP_INCLUDE}/fstcpp/fstcpp_variable_info.cpp" \
    -llz4 -lz -o "${GENERATOR}"

"${GENERATOR}" "${GENERATED_FST}"
install -m 0644 "${GENERATED_FST}" "${FIXTURE_DIR}/waves.fst"
(
    cd -- "${FIXTURE_DIR}"
    sha256sum ai_complex_top.sv generate_ai_complex_fst.cpp \
        fstcpp-four-state-vector.patch waves.fst \
        > fixture.sha256
)
