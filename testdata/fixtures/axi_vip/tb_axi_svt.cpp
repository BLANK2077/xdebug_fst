#include "Vaxi_vip_fixture_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: Vaxi_vip_fixture_top OUTPUT.fst\n";
        return 2;
    }
    VerilatedContext context;
    context.commandArgs(argc, argv);
    context.traceEverOn(true);
    Vaxi_vip_fixture_top top{&context};
    VerilatedFstC trace;
    top.trace(&trace, 4);
    trace.open(argv[1]);
    while (!context.gotFinish()) {
        top.eval();
        trace.dump(context.time());
        if (context.gotFinish()) break;
        if (!top.eventsPending()) {
            std::cerr << "SVT AXI mirror stopped before $finish\n";
            return 1;
        }
        const std::uint64_t next = top.nextTimeSlot();
        if (next <= context.time()) return 1;
        context.time(next);
    }
    top.final();
    trace.close();
    return 0;
}
