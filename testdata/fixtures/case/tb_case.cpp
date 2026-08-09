#include "Vcase_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vcase_top top;
    VerilatedFstC trace;
    top.trace(&trace, 99);
    trace.open("waves.fst");

    top.clk = 0;
    top.reset = 1;
    top.sel = 0;
    top.data = 0x20;
    for (int step = 0; step < 10; ++step) {
        if (step == 2) top.reset = 0;
        if (step == 4) top.sel = 1;
        if (step == 6) top.sel = 2;
        top.clk = !top.clk;
        top.eval();
        trace.dump(static_cast<uint64_t>(step) * 10);
    }

    trace.close();
    return 0;
}
