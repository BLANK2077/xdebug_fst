// Deterministic Verilator harness for the P3-D2 XAMBA APB semantic fixture.
// BSD-3-Clause License

#include "Vxdebug_apb_xamba_fixture_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: Vxdebug_apb_xamba_fixture_top OUTPUT.fst\n";
        return 2;
    }

    VerilatedContext context;
    context.commandArgs(argc, argv);
    context.traceEverOn(true);

    Vxdebug_apb_xamba_fixture_top top{&context};
    VerilatedFstC trace;
    top.trace(&trace, 4);
    trace.open(argv[1]);

    while (!context.gotFinish()) {
        top.eval();
        trace.dump(context.time());
        if (context.gotFinish()) break;
        if (!top.eventsPending()) {
            std::cerr << "APB XAMBA mirror stopped before $finish at "
                      << context.time() << " ticks\n";
            trace.close();
            return 1;
        }
        const std::uint64_t next_time = top.nextTimeSlot();
        if (next_time <= context.time()) {
            std::cerr << "APB XAMBA mirror scheduler did not advance\n";
            trace.close();
            return 1;
        }
        context.time(next_time);
    }

    top.final();
    trace.close();
    return 0;
}
