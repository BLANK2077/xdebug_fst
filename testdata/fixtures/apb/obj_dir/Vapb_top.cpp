// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "Vapb_top__pch.h"
#include "verilated_fst_c.h"

//============================================================
// Constructors

Vapb_top::Vapb_top(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new Vapb_top__Syms(contextp(), _vcname__, this)}
    , pclk{vlSymsp->TOP.pclk}
    , presetn{vlSymsp->TOP.presetn}
    , psel{vlSymsp->TOP.psel}
    , penable{vlSymsp->TOP.penable}
    , pwrite{vlSymsp->TOP.pwrite}
    , paddr{vlSymsp->TOP.paddr}
    , pwdata{vlSymsp->TOP.pwdata}
    , prdata{vlSymsp->TOP.prdata}
    , pready{vlSymsp->TOP.pready}
    , pslverr{vlSymsp->TOP.pslverr}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

Vapb_top::Vapb_top(const char* _vcname__)
    : Vapb_top(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

Vapb_top::~Vapb_top() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void Vapb_top___024root___eval_debug_assertions(Vapb_top___024root* vlSelf);
#endif  // VL_DEBUG
void Vapb_top___024root___eval_static(Vapb_top___024root* vlSelf);
void Vapb_top___024root___eval_initial(Vapb_top___024root* vlSelf);
void Vapb_top___024root___eval_settle(Vapb_top___024root* vlSelf);
void Vapb_top___024root___eval(Vapb_top___024root* vlSelf);

void Vapb_top::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate Vapb_top::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    Vapb_top___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        Vapb_top___024root___eval_static(&(vlSymsp->TOP));
        Vapb_top___024root___eval_initial(&(vlSymsp->TOP));
        Vapb_top___024root___eval_settle(&(vlSymsp->TOP));
        vlSymsp->__Vm_didInit = true;
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    Vapb_top___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool Vapb_top::eventsPending() { return false; }

uint64_t Vapb_top::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* Vapb_top::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void Vapb_top___024root___eval_final(Vapb_top___024root* vlSelf);

VL_ATTR_COLD void Vapb_top::final() {
    contextp()->executingFinal(true);
    Vapb_top___024root___eval_final(&(vlSymsp->TOP));
    contextp()->executingFinal(false);
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* Vapb_top::hierName() const { return vlSymsp->name(); }
const char* Vapb_top::modelName() const { return "Vapb_top"; }
unsigned Vapb_top::threads() const { return 1; }
void Vapb_top::prepareClone() const { contextp()->prepareClone(); }
void Vapb_top::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> Vapb_top::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false}};
};

//============================================================
// Trace configuration

void Vapb_top___024root__trace_decl_types(VerilatedFst* tracep);

void Vapb_top___024root__trace_init_top(Vapb_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedFst* tracep, uint32_t code) {
    // Callback from tracep->open()
    Vapb_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vapb_top___024root*>(voidSelf);
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    tracep->pushPrefix(vlSymsp->name(), VerilatedTracePrefixType::SCOPE_MODULE);
    Vapb_top___024root__trace_decl_types(tracep);
    Vapb_top___024root__trace_init_top(vlSelf, tracep);
    tracep->popPrefix();
}

VL_ATTR_COLD void Vapb_top___024root__trace_register(Vapb_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD void Vapb_top::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedFstC* const stfp = dynamic_cast<VerilatedFstC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'Vapb_top::trace()' called on non-VerilatedFstC object;"
            " use --trace-fst with VerilatedFst object, and --trace-vcd with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP), name(), false, 14);
    Vapb_top___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
