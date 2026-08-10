#!/usr/bin/env bash
# Regenerate the phase-5 raw FST and DesignDB fixture.
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
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/phase5"
readonly VERILATOR_BIN="${XDEBUG_VERILATOR_REPO}/bin/verilator"
readonly GCC_BIN="${XDEBUG_GCC_TOOLCHAIN}/bin/gcc"
readonly GXX_BIN="${XDEBUG_GCC_TOOLCHAIN}/bin/g++"

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

cd -- "${FIXTURE_DIR}"
"${VERILATOR_BIN}" \
    --cc --exe --build --trace-fst --trace-structs --design-db \
    --top-module phase5_dut \
    --Mdir obj_dir \
    -Wno-fatal \
    phase5_dut.sv tb_phase5.cpp \
    -CFLAGS -fPIC

"${GXX_BIN}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    -I"${XDEBUG_VERILATOR_REPO}/include" \
    -o obj_dir/libVphase5_dut__DesignDb.so \
    obj_dir/Vphase5_dut__DesignDb.cpp

./obj_dir/Vphase5_dut
