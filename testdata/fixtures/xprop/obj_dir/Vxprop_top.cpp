// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "Vxprop_top__pch.h"
#include "verilated_fst_c.h"

//============================================================
// Constructors

Vxprop_top::Vxprop_top(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new Vxprop_top__Syms(contextp(), _vcname__, this)}
    , clk{vlSymsp->TOP.clk}
    , reset{vlSymsp->TOP.reset}
    , out{vlSymsp->TOP.out}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

Vxprop_top::Vxprop_top(const char* _vcname__)
    : Vxprop_top(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

Vxprop_top::~Vxprop_top() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void Vxprop_top___024root___eval_debug_assertions(Vxprop_top___024root* vlSelf);
#endif  // VL_DEBUG
void Vxprop_top___024root___eval_static(Vxprop_top___024root* vlSelf);
void Vxprop_top___024root___eval_initial(Vxprop_top___024root* vlSelf);
void Vxprop_top___024root___eval_settle(Vxprop_top___024root* vlSelf);
void Vxprop_top___024root___eval(Vxprop_top___024root* vlSelf);

void Vxprop_top::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate Vxprop_top::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    Vxprop_top___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        Vxprop_top___024root___eval_static(&(vlSymsp->TOP));
        Vxprop_top___024root___eval_initial(&(vlSymsp->TOP));
        Vxprop_top___024root___eval_settle(&(vlSymsp->TOP));
        vlSymsp->__Vm_didInit = true;
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    Vxprop_top___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool Vxprop_top::eventsPending() { return false; }

uint64_t Vxprop_top::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* Vxprop_top::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void Vxprop_top___024root___eval_final(Vxprop_top___024root* vlSelf);

VL_ATTR_COLD void Vxprop_top::final() {
    contextp()->executingFinal(true);
    Vxprop_top___024root___eval_final(&(vlSymsp->TOP));
    contextp()->executingFinal(false);
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* Vxprop_top::hierName() const { return vlSymsp->name(); }
const char* Vxprop_top::modelName() const { return "Vxprop_top"; }
unsigned Vxprop_top::threads() const { return 1; }
void Vxprop_top::prepareClone() const { contextp()->prepareClone(); }
void Vxprop_top::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> Vxprop_top::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false}};
};

//============================================================
// Trace configuration

void Vxprop_top___024root__trace_decl_types(VerilatedFst* tracep);

void Vxprop_top___024root__trace_init_top(Vxprop_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedFst* tracep, uint32_t code) {
    // Callback from tracep->open()
    Vxprop_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vxprop_top___024root*>(voidSelf);
    Vxprop_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    tracep->pushPrefix(vlSymsp->name(), VerilatedTracePrefixType::SCOPE_MODULE);
    Vxprop_top___024root__trace_decl_types(tracep);
    Vxprop_top___024root__trace_init_top(vlSelf, tracep);
    tracep->popPrefix();
}

VL_ATTR_COLD void Vxprop_top___024root__trace_register(Vxprop_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD void Vxprop_top::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedFstC* const stfp = dynamic_cast<VerilatedFstC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'Vxprop_top::trace()' called on non-VerilatedFstC object;"
            " use --trace-fst with VerilatedFst object, and --trace-vcd with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP), name(), false, 4);
    Vxprop_top___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
