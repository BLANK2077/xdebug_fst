#include "Vactive_zero_evidence_tb.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv) {
    VerilatedContext context;
    context.commandArgs(argc, argv);
    context.traceEverOn(true);
    Vactive_zero_evidence_tb top{&context};
    VerilatedFstC trace{&context};
    top.top_input_i = 0;
    top.trace(&trace, 99);
    trace.open("waves.fst");
    while (!context.gotFinish()) {
        top.eval();
        trace.dump(context.time());
        if (!top.eventsPending()) break;
        context.time(top.nextTimeSlot());
    }
    top.final();
    trace.close();
    return context.gotFinish() ? 0 : 2;
}
