#!/usr/bin/env bash
# Regenerate the matches raw FST and DesignDB fixture.
# BSD-3-Clause License

set -euo pipefail

: "${XDEBUG_VERILATOR_REPO:?set XDEBUG_VERILATOR_REPO to the absolute Verilator repository path}"
: "${XDEBUG_GCC_TOOLCHAIN:?set XDEBUG_GCC_TOOLCHAIN to the absolute GCC toolchain path}"
case "${XDEBUG_VERILATOR_REPO}" in
    /*) ;;
    *)
        echo "XDEBUG_VERILATOR_REPO must be an absolute path" >&2
        exit 2
        ;;
esac
case "${XDEBUG_GCC_TOOLCHAIN}" in
    /*) ;;
    *)
        echo "XDEBUG_GCC_TOOLCHAIN must be an absolute path" >&2
        exit 2
        ;;
esac

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/matches"
readonly VERILATOR_BIN="${XDEBUG_VERILATOR_REPO}/bin/verilator"
readonly GCC_BIN="${XDEBUG_GCC_TOOLCHAIN}/bin/gcc"
readonly GXX_BIN="${XDEBUG_GCC_TOOLCHAIN}/bin/g++"
readonly BUILD_DIR="$(mktemp -d /tmp/xdebug-matches-fixture.XXXXXX)"

cleanup() {
    rm -rf -- "${BUILD_DIR}"
}
trap cleanup EXIT

if [[ ! -x "${VERILATOR_BIN}" ]]; then
    echo "Verilator executable is unavailable: ${VERILATOR_BIN}" >&2
    exit 2
fi
if [[ ! -f "${XDEBUG_VERILATOR_REPO}/include/xdd_api.h" ]]; then
    echo "Verilator DesignDB header is unavailable under XDEBUG_VERILATOR_REPO" >&2
    exit 2
fi
if [[ ! -x "${GCC_BIN}" || ! -x "${GXX_BIN}" ]]; then
    echo "GCC toolchain is unavailable under XDEBUG_GCC_TOOLCHAIN" >&2
    exit 2
fi

export CC="${GCC_BIN}"
export CXX="${GXX_BIN}"
export PATH="${XDEBUG_GCC_TOOLCHAIN}/bin:${PATH}"
export LD_LIBRARY_PATH="${XDEBUG_GCC_TOOLCHAIN}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

"${VERILATOR_BIN}" \
    --cc --exe --build --trace-fst --design-db \
    --top-module matches_top \
    --Mdir "${BUILD_DIR}/obj_dir" \
    -o sim_matches_top \
    "${FIXTURE_DIR}/matches_top.sv" \
    "${FIXTURE_DIR}/tb_matches.cpp"

"${GXX_BIN}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    -I"${XDEBUG_VERILATOR_REPO}/include" \
    -o "${BUILD_DIR}/libVmatches_top__DesignDb.so" \
    "${BUILD_DIR}/obj_dir/Vmatches_top__DesignDb.cpp"

(
    cd -- "${BUILD_DIR}"
    "${BUILD_DIR}/obj_dir/sim_matches_top"
)

# The FST writer records wall-clock time in the informational 26-byte date
# header.  Normalize only that header during fixture generation so identical
# waveform facts produce an identical fixture; production FST input is never
# rewritten or converted.  Offset 202 is the standard FST header date field:
# 9-byte block header + 193-byte HeaderInfo::Offset::date.
printf 'Thu Jan  1 00:00:00 1970\n' | \
    dd of="${BUILD_DIR}/waves.fst" bs=1 seek=202 count=25 conv=notrunc \
        status=none

install -m 0644 "${BUILD_DIR}/waves.fst" "${FIXTURE_DIR}/waves.fst"
install -m 0644 "${BUILD_DIR}/libVmatches_top__DesignDb.so" \
    "${FIXTURE_DIR}/obj_dir/libVmatches_top__DesignDb.so"

(
    cd -- "${FIXTURE_DIR}"
    sha256sum waves.fst obj_dir/libVmatches_top__DesignDb.so \
        > fixture.sha256
)
