// Verilator harness for the byte-identical original stream_v1 RTL.
// BSD-3-Clause License

#include "Vstream_v1_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: Vstream_v1_top OUTPUT.fst\n";
        return 2;
    }

    VerilatedContext context;
    context.commandArgs(argc, argv);
    context.traceEverOn(true);

    Vstream_v1_top top{&context};
    VerilatedFstC trace;
    top.trace(&trace, 2);
    trace.open(argv[1]);

    while (!context.gotFinish()) {
        top.eval();
        trace.dump(context.time());
        if (context.gotFinish()) {
            break;
        }
        if (!top.eventsPending()) {
            std::cerr << "stream_v1 stopped before $finish at "
                      << context.time() << " ticks\n";
            trace.close();
            return 1;
        }
        const std::uint64_t next_time = top.nextTimeSlot();
        if (next_time <= context.time()) {
            std::cerr << "stream_v1 scheduler did not advance\n";
            trace.close();
            return 1;
        }
        context.time(next_time);
    }

    top.final();
    trace.close();
    return 0;
}
