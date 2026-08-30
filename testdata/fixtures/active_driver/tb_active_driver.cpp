#include "Vactive_driver_tb.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv) {
    VerilatedContext context;
    context.commandArgs(argc, argv);
    context.traceEverOn(true);
    Vactive_driver_tb top{&context};
    VerilatedFstC trace{&context};
    top.trace(&trace, 99);
    trace.open("waves.fst");
    while (!context.gotFinish() && context.time() <= 100000) {
        top.eval();
        trace.dump(context.time());
        context.timeInc(1);
    }
    top.final();
    trace.close();
    return context.gotFinish() ? 0 : 2;
}
