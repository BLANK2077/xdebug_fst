#!/usr/bin/env bash
# Shared fail-closed environment for fixture regeneration.

set -euo pipefail

readonly XDEBUG_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly XDEBUG_FIXTURE_BUILD_DIR="${XDEBUG_BUILD_DIR:-${XDEBUG_REPO_ROOT}/build}"
readonly XDEBUG_FIXTURE_TOOLCHAIN="${XDEBUG_TOOLCHAIN_ROOT:-${XDEBUG_REPO_ROOT}/../.toolchains/gcc-13}"
readonly VERILATOR_BIN="${XDEBUG_FIXTURE_BUILD_DIR}/tools/verilator/bin/verilator"
readonly VERILATOR_INCLUDE="${XDEBUG_FIXTURE_BUILD_DIR}/_deps/verilator-src/include"
readonly GCC_BIN="${XDEBUG_FIXTURE_TOOLCHAIN}/bin/gcc"
readonly GXX_BIN="${XDEBUG_FIXTURE_TOOLCHAIN}/bin/g++"
readonly -a XDEBUG_FIXTURE_PREFIX_MAP_FLAGS=(
    "-ffile-prefix-map=${XDEBUG_REPO_ROOT}=."
    "-fdebug-prefix-map=${XDEBUG_REPO_ROOT}=."
    "-fmacro-prefix-map=${XDEBUG_REPO_ROOT}=."
)

[[ "${XDEBUG_FIXTURE_BUILD_DIR}" = /* ]] || {
    echo "XDEBUG_BUILD_DIR 必须是绝对路径" >&2
    exit 2
}
[[ -x "${VERILATOR_BIN}" && -f "${VERILATOR_INCLUDE}/xdd_api.h" ]] || {
    echo "缺少统一构建的 patched Verilator；请先运行 tools/build.sh" >&2
    exit 2
}
[[ -x "${GCC_BIN}" && -x "${GXX_BIN}" && "$("${GCC_BIN}" -dumpfullversion)" = "13.3.1" ]] || {
    echo "缺少私有 GCC/G++ 13.3.1；禁止使用系统编译器" >&2
    exit 2
}

export CC="${GCC_BIN}"
export CXX="${GXX_BIN}"
export PATH="${XDEBUG_FIXTURE_TOOLCHAIN}/bin:${PATH}"
export LD_LIBRARY_PATH="${XDEBUG_FIXTURE_TOOLCHAIN}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
