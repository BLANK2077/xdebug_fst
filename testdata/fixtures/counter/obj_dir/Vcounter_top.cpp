// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "Vcounter_top__pch.h"
#include "verilated_fst_c.h"

//============================================================
// Constructors

Vcounter_top::Vcounter_top(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new Vcounter_top__Syms(contextp(), _vcname__, this)}
    , clk{vlSymsp->TOP.clk}
    , reset{vlSymsp->TOP.reset}
    , count{vlSymsp->TOP.count}
    , overflow{vlSymsp->TOP.overflow}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

Vcounter_top::Vcounter_top(const char* _vcname__)
    : Vcounter_top(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

Vcounter_top::~Vcounter_top() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void Vcounter_top___024root___eval_debug_assertions(Vcounter_top___024root* vlSelf);
#endif  // VL_DEBUG
void Vcounter_top___024root___eval_static(Vcounter_top___024root* vlSelf);
void Vcounter_top___024root___eval_initial(Vcounter_top___024root* vlSelf);
void Vcounter_top___024root___eval_settle(Vcounter_top___024root* vlSelf);
void Vcounter_top___024root___eval(Vcounter_top___024root* vlSelf);

void Vcounter_top::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate Vcounter_top::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    Vcounter_top___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        Vcounter_top___024root___eval_static(&(vlSymsp->TOP));
        Vcounter_top___024root___eval_initial(&(vlSymsp->TOP));
        Vcounter_top___024root___eval_settle(&(vlSymsp->TOP));
        vlSymsp->__Vm_didInit = true;
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    Vcounter_top___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool Vcounter_top::eventsPending() { return false; }

uint64_t Vcounter_top::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* Vcounter_top::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void Vcounter_top___024root___eval_final(Vcounter_top___024root* vlSelf);

VL_ATTR_COLD void Vcounter_top::final() {
    contextp()->executingFinal(true);
    Vcounter_top___024root___eval_final(&(vlSymsp->TOP));
    contextp()->executingFinal(false);
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* Vcounter_top::hierName() const { return vlSymsp->name(); }
const char* Vcounter_top::modelName() const { return "Vcounter_top"; }
unsigned Vcounter_top::threads() const { return 1; }
void Vcounter_top::prepareClone() const { contextp()->prepareClone(); }
void Vcounter_top::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> Vcounter_top::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false}};
};

//============================================================
// Trace configuration

void Vcounter_top___024root__trace_decl_types(VerilatedFst* tracep);

void Vcounter_top___024root__trace_init_top(Vcounter_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedFst* tracep, uint32_t code) {
    // Callback from tracep->open()
    Vcounter_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vcounter_top___024root*>(voidSelf);
    Vcounter_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    tracep->pushPrefix(vlSymsp->name(), VerilatedTracePrefixType::SCOPE_MODULE);
    Vcounter_top___024root__trace_decl_types(tracep);
    Vcounter_top___024root__trace_init_top(vlSelf, tracep);
    tracep->popPrefix();
}

VL_ATTR_COLD void Vcounter_top___024root__trace_register(Vcounter_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD void Vcounter_top::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedFstC* const stfp = dynamic_cast<VerilatedFstC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'Vcounter_top::trace()' called on non-VerilatedFstC object;"
            " use --trace-fst with VerilatedFst object, and --trace-vcd with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP), name(), false, 4);
    Vcounter_top___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
