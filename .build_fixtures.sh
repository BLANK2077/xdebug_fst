#!/bin/bash
# Build all Verilator FST fixtures (Phase 5)
set -e
VERILATOR=${VERILATOR_HOME}/bin/verilator
cd ${REPO_ROOT}/testdata/fixtures

for fix in counter apb axi stream; do
  echo "=== building $fix ==="
  cd $fix
  TOP=$(ls *_top.sv | head -1)
  TOPMOD=$(basename $TOP .sv)
  TB=$(ls tb_*.cpp | head -1)
  rm -rf obj_dir
  $VERILATOR --cc --trace-fst --design-db --top-module $TOPMOD $TOP --exe --build $TB -o sim_$TOPMOD > /tmp/verilator_$fix.log 2>&1 || { tail -20 /tmp/verilator_$fix.log; exit 1; }
  g++ -shared -fPIC -O2 -I${VERILATOR_HOME}/include obj_dir/V${TOPMOD}__DesignDb.cpp -o obj_dir/libV${TOPMOD}__DesignDb.so
  ./obj_dir/sim_${TOPMOD}
  ls -la waves.fst obj_dir/libV${TOPMOD}__DesignDb.so
  cd ..
done
echo "ALL FIXTURES BUILT"
