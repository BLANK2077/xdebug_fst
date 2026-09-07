#!/usr/bin/env bash
set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="${REPO_ROOT}/build"
BUILD_TYPE="RelWithDebInfo"
SANITIZER=""
PYTHON="${XDEBUG_PYTHON:-python3}"
JOBS="${XDEBUG_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"

usage() {
    echo "用法: WELLEN_HOME=/path VERILATOR_HOME=/path $0 [--build-dir PATH] [--jobs N] [--asan|--ubsan]" >&2
}

while (($#)); do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || { usage; exit 2; }
            JOBS="$2"
            shift 2
            ;;
        --asan)
            [[ -z "${SANITIZER}" ]] || { echo "只能选择一种 sanitizer" >&2; exit 2; }
            SANITIZER="asan"
            shift
            ;;
        --ubsan)
            [[ -z "${SANITIZER}" ]] || { echo "只能选择一种 sanitizer" >&2; exit 2; }
            SANITIZER="ubsan"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "未知参数: $1" >&2
            usage
            exit 2
            ;;
    esac
done

: "${WELLEN_HOME:?必须设置 WELLEN_HOME 为官方 Wellen 本地 Git 仓库的绝对路径}"
: "${VERILATOR_HOME:?必须设置 VERILATOR_HOME 为官方 Verilator 本地 Git 仓库的绝对路径}"
[[ "${WELLEN_HOME}" = /* ]] || { echo "WELLEN_HOME 必须是绝对路径" >&2; exit 2; }
[[ "${VERILATOR_HOME}" = /* ]] || { echo "VERILATOR_HOME 必须是绝对路径" >&2; exit 2; }

TOOLCHAIN_INPUT="${XDEBUG_TOOLCHAIN_ROOT:-${REPO_ROOT}/../.toolchains/gcc-13}"
if [[ "${TOOLCHAIN_INPUT}" != /* || ! -d "${TOOLCHAIN_INPUT}" ]]; then
    echo "缺少绝对路径私有工具链 ${TOOLCHAIN_INPUT}；禁止回退到系统 GCC" >&2
    exit 2
fi
readonly TOOLCHAIN_ROOT="$(cd "${TOOLCHAIN_INPUT}" && pwd -P)"
readonly PRIVATE_CC="${TOOLCHAIN_ROOT}/bin/gcc"
readonly PRIVATE_CXX="${TOOLCHAIN_ROOT}/bin/g++"
readonly PRIVATE_LIB="${TOOLCHAIN_ROOT}/lib64"
[[ -x "${PRIVATE_CC}" && -x "${PRIVATE_CXX}" && -d "${PRIVATE_LIB}" ]] || {
    echo "私有 GCC 工具链不完整: ${TOOLCHAIN_ROOT}" >&2
    exit 2
}
[[ "$(readlink -f "${PRIVATE_CC}")" = "${PRIVATE_CC}" ]] || {
    echo "私有 gcc 真实路径不一致: ${PRIVATE_CC}" >&2
    exit 2
}
[[ "$(readlink -f "${PRIVATE_CXX}")" = "${PRIVATE_CXX}" ]] || {
    echo "私有 g++ 真实路径不一致: ${PRIVATE_CXX}" >&2
    exit 2
}
[[ "$("${PRIVATE_CC}" -dumpfullversion)" = "13.3.1" ]] || {
    echo "私有 gcc 必须为 13.3.1" >&2
    exit 2
}
[[ "$("${PRIVATE_CXX}" -dumpfullversion)" = "13.3.1" ]] || {
    echo "私有 g++ 必须为 13.3.1" >&2
    exit 2
}

export CC="${PRIVATE_CC}"
export CXX="${PRIVATE_CXX}"
export PATH="${TOOLCHAIN_ROOT}/bin:${PATH}"
export LD_LIBRARY_PATH="${PRIVATE_LIB}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export CARGO_TARGET_DIR="${BUILD_DIR}/cargo-target"

mkdir -p "${BUILD_DIR}"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd -P)"

cache_compiler() {
    local key="$1"
    local cache="${BUILD_DIR}/CMakeCache.txt"
    [[ -f "${cache}" ]] || return 0
    sed -n "s#^${key}:[^=]*=##p" "${cache}"
}

readonly CACHED_CC="$(cache_compiler CMAKE_C_COMPILER)"
readonly CACHED_CXX="$(cache_compiler CMAKE_CXX_COMPILER)"
if [[ -n "${CACHED_CC}" && "$(readlink -f "${CACHED_CC}")" != "${PRIVATE_CC}" ]]; then
    echo "构建目录已缓存其他 C 编译器 ${CACHED_CC}；请指定新的 --build-dir，禁止自动清理或 fallback" >&2
    exit 2
fi
if [[ -n "${CACHED_CXX}" && "$(readlink -f "${CACHED_CXX}")" != "${PRIVATE_CXX}" ]]; then
    echo "构建目录已缓存其他 C++ 编译器 ${CACHED_CXX}；请指定新的 --build-dir，禁止自动清理或 fallback" >&2
    exit 2
fi

repo_state() {
    local repo="$1"
    {
        git -C "${repo}" rev-parse HEAD
        git -C "${repo}" status --porcelain=v1 --untracked-files=all
    } | sha256sum | awk '{print $1}'
}

readonly WELLEN_STATE_BEFORE="$(repo_state "${WELLEN_HOME}")"
readonly VERILATOR_STATE_BEFORE="$(repo_state "${VERILATOR_HOME}")"
verify_home_unchanged() {
    local failed=0
    [[ "$(repo_state "${WELLEN_HOME}")" = "${WELLEN_STATE_BEFORE}" ]] || {
        echo "错误：构建期间 WELLEN_HOME 状态发生变化" >&2
        failed=1
    }
    [[ "$(repo_state "${VERILATOR_HOME}")" = "${VERILATOR_STATE_BEFORE}" ]] || {
        echo "错误：构建期间 VERILATOR_HOME 状态发生变化" >&2
        failed=1
    }
    return "${failed}"
}
trap verify_home_unchanged EXIT

"${PYTHON}" "${REPO_ROOT}/tools/check_environment.py" build --toolchain "${TOOLCHAIN_ROOT}" \
    --output "${BUILD_DIR}/build-environment.json"

"${PYTHON}" "${REPO_ROOT}/tools/prepare_dependencies.py" \
    --repo-root "${REPO_ROOT}" --build-dir "${BUILD_DIR}"

readonly WELLEN_SOURCE="${BUILD_DIR}/_deps/wellen-src"
readonly VERILATOR_SOURCE="${BUILD_DIR}/_deps/verilator-src"
mkdir -p "${BUILD_DIR}/lib" "${BUILD_DIR}/tools/verilator"

RUSTFLAGS="${RUSTFLAGS:-} --remap-path-prefix=${REPO_ROOT}=. --remap-path-prefix=${BUILD_DIR}=/build --remap-path-prefix=${CARGO_HOME:-${HOME}/.cargo}=/cargo" \
cargo build --release --locked --offline --jobs "${JOBS}" \
    --manifest-path "${WELLEN_SOURCE}/Cargo.toml" \
    -p wellen-capi -p wellenx-capi
cmake -E copy_if_different "${CARGO_TARGET_DIR}/release/libwellen_capi.so" "${BUILD_DIR}/lib/"
cmake -E copy_if_different "${CARGO_TARGET_DIR}/release/libwellenx_capi.so" "${BUILD_DIR}/lib/"

readonly DEPENDENCY_FINGERPRINT="$("${PYTHON}" -c 'import json,sys; print(json.load(open(sys.argv[1]))["verilator"]["fingerprint"])' "${BUILD_DIR}/dependencies.resolved.json")"
readonly VERILATOR_STAMP="${BUILD_DIR}/tools/verilator/.xdebug-build-stamp"
readonly VERILATOR_BUILD_ID="${DEPENDENCY_FINGERPRINT}:gcc-13.3.1:min-install-v3-relocatable"
if [[ ! -f "${VERILATOR_STAMP}" || "$(<"${VERILATOR_STAMP}")" != "${VERILATOR_BUILD_ID}" || ! -x "${BUILD_DIR}/tools/verilator/bin/verilator" || ! -x "${BUILD_DIR}/tools/verilator/share/verilator/bin/verilator_includer" ]]; then
    (
        cd "${VERILATOR_SOURCE}"
        autoconf
        ./configure --prefix="${BUILD_DIR}/tools/verilator"
        # 统一产物只需要优化版 compiler、入口脚本及运行数据，不构建 debug、
        # coverage 和 man page，避免把非运行时工具纳入一次构建的依赖闭包。
        make -C src -j"${JOBS}" VERILATOR_ROOT=/xdebug-fst-verilator opt
        make installdata
        install -d "${BUILD_DIR}/tools/verilator/bin"
        install -d "${BUILD_DIR}/tools/verilator/share/verilator/bin"
        install -m 755 bin/verilator bin/verilator_bin "${BUILD_DIR}/tools/verilator/bin/"
        install -m 755 bin/verilator_includer \
            "${BUILD_DIR}/tools/verilator/share/verilator/bin/"
        perl -p -i -e 'use File::Spec;' \
            -e 's/my \$verilator_pkgdatadir_relpath = .*/my \$verilator_pkgdatadir_relpath = "..\/share\/verilator";/' \
            "${BUILD_DIR}/tools/verilator/bin/verilator"
    )
    printf '%s\n' "${VERILATOR_BUILD_ID}" >"${VERILATOR_STAMP}"
fi

"${PYTHON}" -c 'import json,sys; json.dump({"cc":sys.argv[1],"cxx":sys.argv[2],"version":"13.3.1","root":sys.argv[3]},open(sys.argv[4],"w"),indent=2,sort_keys=True); open(sys.argv[4],"a").write("\n")' \
    "${PRIVATE_CC}" "${PRIVATE_CXX}" "${TOOLCHAIN_ROOT}" "${BUILD_DIR}/toolchain.resolved.json"

CMAKE_ARGS=(
    -S "${REPO_ROOT}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DCMAKE_C_COMPILER="${PRIVATE_CC}"
    -DCMAKE_CXX_COMPILER="${PRIVATE_CXX}"
    -DXDEBUG_TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT}"
    -DXDEBUG_DEPENDENCY_ROOT="${BUILD_DIR}/_deps"
    -DPython3_EXECUTABLE="$(command -v "${PYTHON}")"
    -DXDEBUG_RELEASE_VERSION="${XDEBUG_RELEASE_VERSION:-0.1.0}"
)
if [[ "${SANITIZER}" = "asan" ]]; then
    CMAKE_ARGS+=( -DXDEBUG_ENABLE_ASAN=ON -DXDEBUG_ENABLE_UBSAN=OFF )
elif [[ "${SANITIZER}" = "ubsan" ]]; then
    CMAKE_ARGS+=( -DXDEBUG_ENABLE_ASAN=OFF -DXDEBUG_ENABLE_UBSAN=ON )
else
    CMAKE_ARGS+=( -DXDEBUG_ENABLE_ASAN=OFF -DXDEBUG_ENABLE_UBSAN=OFF )
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

verify_home_unchanged
trap - EXIT
echo "统一构建完成: ${BUILD_DIR}/xdebug-fst"
