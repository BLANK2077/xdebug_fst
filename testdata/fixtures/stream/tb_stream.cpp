#include "Vstream_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <cstdio>
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vstream_top* top = new Vstream_top;
    VerilatedFstC* tfp = new VerilatedFstC;
    top->trace(tfp, 99);
    tfp->open("waves.fst");
    top->clk = 0; top->reset = 1;
    top->in_valid = 0; top->in_data = 0; top->out_ready = 0;
    long t = 0;
    auto tick = [&]() { top->clk = !top->clk; top->eval(); tfp->dump(static_cast<uint64_t>(t++) * 10); };
    for (int i = 0; i < 4; ++i) tick();
    top->reset = 0;
    // push 5 bytes with stalls
    const unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    for (int i = 0; i < 5; ++i) {
        top->in_valid = 1; top->in_data = data[i];
        tick(); tick();
        if (i == 2) { top->in_valid = 0; tick(); tick(); }  // stall mid-stream
    }
    top->in_valid = 0;
    tick(); tick();
    // drain with back-pressure
    top->out_ready = 1;
    for (int i = 0; i < 8; ++i) {
        if (i == 4) { top->out_ready = 0; tick(); tick(); tick(); tick(); top->out_ready = 1; }
        tick(); tick();
    }
    top->out_ready = 0;
    for (int i = 0; i < 4; ++i) tick();
    tfp->close();
    delete top;
    printf("done\n");
    return 0;
}
