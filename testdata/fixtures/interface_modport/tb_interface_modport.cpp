// tb_interface_modport.cpp — direct FST generator for interface/modport fixture
// BSD-3-Clause License

#include "Vinterface_modport_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vinterface_modport_top top;
    VerilatedFstC trace;
    top.trace(&trace, 99);
    trace.open("waves.fst");

    for (int step = 0; step < 4; ++step) {
        top.source = static_cast<unsigned>(0x11 + step * 0x22);
        top.eval();
        trace.dump(static_cast<uint64_t>(step) * 10);
    }

    trace.close();
    return 0;
}
