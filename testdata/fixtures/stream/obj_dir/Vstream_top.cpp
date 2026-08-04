// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "Vstream_top__pch.h"
#include "verilated_fst_c.h"

//============================================================
// Constructors

Vstream_top::Vstream_top(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new Vstream_top__Syms(contextp(), _vcname__, this)}
    , clk{vlSymsp->TOP.clk}
    , reset{vlSymsp->TOP.reset}
    , in_valid{vlSymsp->TOP.in_valid}
    , in_ready{vlSymsp->TOP.in_ready}
    , in_data{vlSymsp->TOP.in_data}
    , out_valid{vlSymsp->TOP.out_valid}
    , out_ready{vlSymsp->TOP.out_ready}
    , out_data{vlSymsp->TOP.out_data}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

Vstream_top::Vstream_top(const char* _vcname__)
    : Vstream_top(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

Vstream_top::~Vstream_top() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void Vstream_top___024root___eval_debug_assertions(Vstream_top___024root* vlSelf);
#endif  // VL_DEBUG
void Vstream_top___024root___eval_static(Vstream_top___024root* vlSelf);
void Vstream_top___024root___eval_initial(Vstream_top___024root* vlSelf);
void Vstream_top___024root___eval_settle(Vstream_top___024root* vlSelf);
void Vstream_top___024root___eval(Vstream_top___024root* vlSelf);

void Vstream_top::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate Vstream_top::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    Vstream_top___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        Vstream_top___024root___eval_static(&(vlSymsp->TOP));
        Vstream_top___024root___eval_initial(&(vlSymsp->TOP));
        Vstream_top___024root___eval_settle(&(vlSymsp->TOP));
        vlSymsp->__Vm_didInit = true;
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    Vstream_top___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool Vstream_top::eventsPending() { return false; }

uint64_t Vstream_top::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* Vstream_top::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void Vstream_top___024root___eval_final(Vstream_top___024root* vlSelf);

VL_ATTR_COLD void Vstream_top::final() {
    contextp()->executingFinal(true);
    Vstream_top___024root___eval_final(&(vlSymsp->TOP));
    contextp()->executingFinal(false);
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* Vstream_top::hierName() const { return vlSymsp->name(); }
const char* Vstream_top::modelName() const { return "Vstream_top"; }
unsigned Vstream_top::threads() const { return 1; }
void Vstream_top::prepareClone() const { contextp()->prepareClone(); }
void Vstream_top::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> Vstream_top::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false}};
};

//============================================================
// Trace configuration

void Vstream_top___024root__trace_decl_types(VerilatedFst* tracep);

void Vstream_top___024root__trace_init_top(Vstream_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedFst* tracep, uint32_t code) {
    // Callback from tracep->open()
    Vstream_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vstream_top___024root*>(voidSelf);
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    tracep->pushPrefix(vlSymsp->name(), VerilatedTracePrefixType::SCOPE_MODULE);
    Vstream_top___024root__trace_decl_types(tracep);
    Vstream_top___024root__trace_init_top(vlSelf, tracep);
    tracep->popPrefix();
}

VL_ATTR_COLD void Vstream_top___024root__trace_register(Vstream_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD void Vstream_top::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedFstC* const stfp = dynamic_cast<VerilatedFstC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'Vstream_top::trace()' called on non-VerilatedFstC object;"
            " use --trace-fst with VerilatedFst object, and --trace-vcd with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP), name(), false, 17);
    Vstream_top___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
