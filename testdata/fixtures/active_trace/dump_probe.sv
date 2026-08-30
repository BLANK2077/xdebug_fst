`timescale 1ns/1ps

// Fixture-only probe.  It adds no design signal or stimulus and leaves every
// locked-original RTL file byte-identical, while asking patched Verilator to
// emit the raw FST consumed by xdebug-fst.
module xdebug_fst_dump_probe;
  initial begin
    $dumpfile("waves.fst");
    $dumpvars(0, top);
  end
endmodule

bind top xdebug_fst_dump_probe xdebug_fst_dump_probe_i();
