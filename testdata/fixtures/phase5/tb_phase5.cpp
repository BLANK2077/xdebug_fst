// Direct raw-FST generator for the phase-5 fixture.
// BSD-3-Clause License
#include "Vphase5_dut.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <cstdint>

namespace {

void dump(Vphase5_dut& dut, VerilatedFstC& trace, uint64_t time_ps) {
    dut.eval();
    trace.dump(time_ps);
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vphase5_dut dut;
    VerilatedFstC trace;
    dut.trace(&trace, 99);
    trace.open("waves.fst");

    dut.mask_a = 0xff;
    dut.mask_b = 0xff;
    dut.en0 = 1;
    dut.en1 = 1;
    dut.en2 = 1;
    dut.ctrl_sel = 1;
    dut.ctrl_mode = 0;
    dut.src_a = 0xa0;
    dut.src_b = 0xb0;
    dut.src_c = 0xc0;
    dut.sp_val = 0x50;
    dump(dut, trace, 0);

    dut.ctrl_sel = 0;
    dut.src_b = 0xb1;
    dump(dut, trace, 10000);
    dut.ctrl_sel = 1;
    dump(dut, trace, 20000);
    dut.src_a = 0xa1;
    dump(dut, trace, 30000);
    dut.en1 = 0;
    dump(dut, trace, 40000);
    dut.en1 = 1;
    dump(dut, trace, 41000);
    dut.sp_val = 0x51;
    dump(dut, trace, 50000);
    dut.src_a = 0xa2;
    dut.src_b = 0xb2;
    dump(dut, trace, 60000);
    dut.mask_a &= ~(1U << 2);
    dump(dut, trace, 70000);
    dut.mask_a |= 1U << 2;
    dump(dut, trace, 71000);
    dut.en1 = 0;
    dump(dut, trace, 80000);
    dut.en1 = 1;
    dump(dut, trace, 81000);
    dut.src_a = 0xa3;
    dump(dut, trace, 90000);
    dut.mask_a &= ~(1U << 3);
    dump(dut, trace, 100000);
    dut.mask_a |= 1U << 3;
    dump(dut, trace, 101000);
    dut.ctrl_sel = 0;
    dump(dut, trace, 105000);
    dut.src_b = 0xb5;
    dump(dut, trace, 110000);
    dut.ctrl_mode = 1;
    dump(dut, trace, 115000);
    dut.src_a = 0xa5;
    dump(dut, trace, 120000);
    dut.sp_val = 0x55;
    dump(dut, trace, 130000);

    trace.close();
    dut.final();
    return 0;
}
