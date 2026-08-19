#!/usr/bin/env bash
# Regenerate the matches raw FST and legacy .so DesignDB compatibility fixture.
# New designs use --design-db-binary; this script intentionally preserves the
# xdd-so fixture needed to test the compatibility reader.
# BSD-3-Clause License

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/fixture_build_env.sh"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly FIXTURE_DIR="${REPO_DIR}/testdata/fixtures/matches"
readonly BUILD_DIR="$(mktemp -d /tmp/xdebug-matches-fixture.XXXXXX)"

cleanup() {
    rm -rf -- "${BUILD_DIR}"
}
trap cleanup EXIT

(
    cd -- "${FIXTURE_DIR}"
    "${VERILATOR_BIN}" \
        --cc --exe --build --trace-fst --design-db \
        --top-module matches_top \
        --Mdir "${BUILD_DIR}/obj_dir" \
        -o sim_matches_top \
        matches_top.sv tb_matches.cpp
)

"${GXX_BIN}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
    "${XDEBUG_FIXTURE_PREFIX_MAP_FLAGS[@]}" \
    "-ffile-prefix-map=${BUILD_DIR}=.build" \
    "-fdebug-prefix-map=${BUILD_DIR}=.build" \
    "-fmacro-prefix-map=${BUILD_DIR}=.build" \
    -I"${VERILATOR_INCLUDE}" \
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
