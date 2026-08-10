#include "Voutput_mixed_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Voutput_mixed_top top;
    VerilatedFstC trace;
    top.trace(&trace, 99);
    trace.open("waves.fst");

    top.child_data = 0x5a;
    top.parent_data = 0x5a;
    top.eval();
    trace.dump(0);

    top.child_data = 0xa5;
    top.parent_data = 0xa5;
    top.eval();
    trace.dump(10);

    trace.close();
    return 0;
}
