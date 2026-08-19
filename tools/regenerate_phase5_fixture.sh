#!/usr/bin/env bash
# Regenerate the phase-5 raw FST and DesignDB fixture.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/phase5"

cd -- "${FIXTURE_DIR}"
"${VERILATOR_BIN}" \
    --cc --exe --build --trace-fst --trace-structs --design-db \
    --top-module phase5_dut \
    --Mdir obj_dir \
    -Wno-fatal \
    phase5_dut.sv tb_phase5.cpp \
    -CFLAGS -fPIC

"${GXX_BIN}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[@]}" \
    -I"${VERILATOR_INCLUDE}" \
    -o obj_dir/libVphase5_dut__DesignDb.so \
    obj_dir/Vphase5_dut__DesignDb.cpp

./obj_dir/Vphase5_dut
