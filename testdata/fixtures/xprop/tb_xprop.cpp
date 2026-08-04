#include "Vxprop_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <cstdio>
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vxprop_top* top = new Vxprop_top;
    VerilatedFstC* tfp = new VerilatedFstC;
    top->trace(tfp, 99);
    tfp->open("waves.fst");
    top->clk = 0; top->reset = 0;
    for (int i = 0; i < 40; ++i) {
        if (i == 6) top->reset = 1;
        if (i == 12) top->reset = 0;
        top->clk = !top->clk;
        top->eval();
        tfp->dump(i * 10);
    }
    tfp->close();
    delete top;
    printf("done\n");
    return 0;
}
