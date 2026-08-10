// tb_ref_port.cpp — direct FST generator for ref-port fixture
// BSD-3-Clause License

#include "Vref_port_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vref_port_top top;
    VerilatedFstC trace;
    top.trace(&trace, 99);
    trace.open("waves.fst");

    for (int step = 0; step < 4; ++step) {
        top.source = static_cast<unsigned>(0x12 + step * 0x21);
        top.eval();
        trace.dump(static_cast<uint64_t>(step) * 10);
    }

    trace.close();
    return 0;
}
