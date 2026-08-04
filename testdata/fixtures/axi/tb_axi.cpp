#include "Vaxi_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <cstdio>
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vaxi_top* top = new Vaxi_top;
    VerilatedFstC* tfp = new VerilatedFstC;
    top->trace(tfp, 99);
    tfp->open("waves.fst");
    top->aclk = 0; top->aresetn = 0;
    top->awid=0; top->awaddr=0; top->awlen=0; top->awvalid=0;
    top->wdata=0; top->wstrb=0xF; top->wlast=0; top->wvalid=0;
    top->bready=1; top->arid=0; top->araddr=0; top->arlen=0; top->arvalid=0;
    top->rready=1;
    long t = 0;
    auto tick = [&]() { top->aclk = !top->aclk; top->eval(); tfp->dump(static_cast<uint64_t>(t++) * 10); };
    for (int i = 0; i < 6; ++i) tick();
    top->aresetn = 1;
    // write burst: addr=0x10 len=2 (3 beats) data 11,22,33
    top->awvalid = 1; top->awaddr = 0x10; top->awlen = 2; top->awid = 1;
    tick(); tick();
    top->awvalid = 0;
    top->wvalid = 1; top->wdata = 0x11; top->wlast = 0;
    tick(); tick();
    top->wdata = 0x22;
    tick(); tick();
    top->wdata = 0x33; top->wlast = 1;
    tick(); tick();
    top->wvalid = 0; top->wlast = 0;
    tick(); tick(); tick(); tick();
    // read burst: addr=0x10 len=1 (2 beats)
    top->arvalid = 1; top->araddr = 0x10; top->arlen = 1; top->arid = 2;
    tick(); tick();
    top->arvalid = 0;
    tick(); tick(); tick(); tick(); tick(); tick();
    for (int i = 0; i < 6; ++i) tick();
    tfp->close();
    delete top;
    printf("done\n");
    return 0;
}
