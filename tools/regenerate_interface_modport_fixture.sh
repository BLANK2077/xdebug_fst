#!/usr/bin/env bash
# Regenerate the interface/modport raw FST and legacy .so DesignDB fixture.
# New designs use --design-db-binary; this script intentionally preserves the
# xdd-so fixture needed to test the compatibility reader.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/interface_modport"

cd -- "${FIXTURE_DIR}"
"${VERILATOR_BIN}" \
    --cc --exe --build --trace-fst --design-db \
    --top-module interface_modport_top \
    --Mdir obj_dir \
    interface_modport_top.sv tb_interface_modport.cpp \
    -CFLAGS -fPIC

"${GXX_BIN}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[@]}" \
    -I"${VERILATOR_INCLUDE}" \
    -o obj_dir/libVinterface_modport_top__DesignDb.so \
    obj_dir/Vinterface_modport_top__DesignDb.cpp

./obj_dir/Vinterface_modport_top
