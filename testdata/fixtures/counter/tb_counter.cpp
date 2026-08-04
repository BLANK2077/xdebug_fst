#include "Vcounter_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <cstdio>
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vcounter_top* top = new Vcounter_top;
    VerilatedFstC* tfp = new VerilatedFstC;
    top->trace(tfp, 99);
    tfp->open("waves.fst");
    top->clk = 0; top->reset = 1;
    for (int i = 0; i < 50; ++i) {
        if (i == 10) top->reset = 0;
        top->clk = !top->clk;
        top->eval();
        tfp->dump(i * 10);
    }
    tfp->close();
    delete top;
    printf("done\n");
    return 0;
}
