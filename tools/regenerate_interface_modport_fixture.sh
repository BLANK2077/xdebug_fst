#!/usr/bin/env bash
# Regenerate the interface/modport raw FST and DesignDB fixture.
# BSD-3-Clause License

set -euo pipefail

: "${XDEBUG_VERILATOR_REPO:?set XDEBUG_VERILATOR_REPO to the absolute Verilator repository path}"
case "${XDEBUG_VERILATOR_REPO}" in
    /*) ;;
    *)
        echo "XDEBUG_VERILATOR_REPO must be an absolute path" >&2
        exit 2
        ;;
esac

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/interface_modport"
readonly VERILATOR_BIN="${XDEBUG_VERILATOR_REPO}/bin/verilator"

if [[ ! -x "${VERILATOR_BIN}" ]]; then
    echo "Verilator executable is unavailable: ${VERILATOR_BIN}" >&2
    exit 2
fi
if [[ ! -f "${XDEBUG_VERILATOR_REPO}/include/xdd_api.h" ]]; then
    echo "Verilator DesignDB header is unavailable under XDEBUG_VERILATOR_REPO" >&2
    exit 2
fi

cd -- "${FIXTURE_DIR}"
"${VERILATOR_BIN}" \
    --cc --exe --build --trace-fst --design-db \
    --top-module interface_modport_top \
    --Mdir obj_dir \
    interface_modport_top.sv tb_interface_modport.cpp \
    -CFLAGS -fPIC

g++ -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    -I"${XDEBUG_VERILATOR_REPO}/include" \
    -o obj_dir/libVinterface_modport_top__DesignDb.so \
    obj_dir/Vinterface_modport_top__DesignDb.cpp

./obj_dir/Vinterface_modport_top
