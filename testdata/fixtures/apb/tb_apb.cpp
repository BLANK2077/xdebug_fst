#include "Vapb_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <cstdio>
// Minimal APB master BFM: setup phase (psel=1, penable=0), access phase (penable=1)
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vapb_top* top = new Vapb_top;
    VerilatedFstC* tfp = new VerilatedFstC;
    top->trace(tfp, 99);
    tfp->open("waves.fst");
    top->pclk = 0; top->presetn = 0;
    top->psel = 0; top->penable = 0; top->pwrite = 0;
    top->paddr = 0; top->pwdata = 0;
    long t = 0;
    auto tick = [&]() { top->pclk = !top->pclk; top->eval(); tfp->dump(static_cast<uint64_t>(t++) * 10); };
    // reset 3 cycles
    for (int i = 0; i < 6; ++i) tick();
    top->presetn = 1;
    // APB write: addr=1 data=0xAB
    top->psel = 1; top->penable = 0; top->pwrite = 1; top->paddr = 1; top->pwdata = 0xAB;
    tick(); tick();
    top->penable = 1;
    tick(); tick();
    top->psel = 0; top->penable = 0;
    tick(); tick();
    // APB write: addr=3 data=0x5A
    top->psel = 1; top->penable = 0; top->pwrite = 1; top->paddr = 3; top->pwdata = 0x5A;
    tick(); tick();
    top->penable = 1;
    tick(); tick();
    top->psel = 0; top->penable = 0;
    tick(); tick();
    // APB read: addr=1 expect 0xAB
    top->psel = 1; top->penable = 0; top->pwrite = 0; top->paddr = 1;
    tick(); tick();
    top->penable = 1;
    tick(); tick();
    top->psel = 0; top->penable = 0;
    tick(); tick();
    // APB read: addr=3 expect 0x5A
    top->psel = 1; top->penable = 0; top->pwrite = 0; top->paddr = 3;
    tick(); tick();
    top->penable = 1;
    tick(); tick();
    top->psel = 0; top->penable = 0;
    tick(); tick();
    for (int i = 0; i < 6; ++i) tick();
    tfp->close();
    delete top;
    printf("done\n");
    return 0;
}
