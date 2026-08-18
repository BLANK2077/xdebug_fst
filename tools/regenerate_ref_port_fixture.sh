#!/usr/bin/env bash
# Regenerate the ref-port raw FST and DesignDB fixture.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/ref_port"

cd -- "${FIXTURE_DIR}"
"${VERILATOR_BIN}" \
    --cc --exe --build --trace-fst --design-db \
    --top-module ref_port_top \
    --Mdir obj_dir \
    ref_port_top.sv tb_ref_port.cpp \
    -CFLAGS -fPIC

"${GXX_BIN}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    -I"${VERILATOR_INCLUDE}" \
    -o obj_dir/libVref_port_top__DesignDb.so \
    obj_dir/Vref_port_top__DesignDb.cpp

./obj_dir/Vref_port_top
