// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "Vaxi_top__pch.h"
#include "verilated_fst_c.h"

//============================================================
// Constructors

Vaxi_top::Vaxi_top(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new Vaxi_top__Syms(contextp(), _vcname__, this)}
    , aclk{vlSymsp->TOP.aclk}
    , aresetn{vlSymsp->TOP.aresetn}
    , awid{vlSymsp->TOP.awid}
    , awaddr{vlSymsp->TOP.awaddr}
    , awlen{vlSymsp->TOP.awlen}
    , awvalid{vlSymsp->TOP.awvalid}
    , awready{vlSymsp->TOP.awready}
    , wstrb{vlSymsp->TOP.wstrb}
    , wlast{vlSymsp->TOP.wlast}
    , wvalid{vlSymsp->TOP.wvalid}
    , wready{vlSymsp->TOP.wready}
    , bid{vlSymsp->TOP.bid}
    , bresp{vlSymsp->TOP.bresp}
    , bvalid{vlSymsp->TOP.bvalid}
    , bready{vlSymsp->TOP.bready}
    , arid{vlSymsp->TOP.arid}
    , araddr{vlSymsp->TOP.araddr}
    , arlen{vlSymsp->TOP.arlen}
    , arvalid{vlSymsp->TOP.arvalid}
    , arready{vlSymsp->TOP.arready}
    , rid{vlSymsp->TOP.rid}
    , rresp{vlSymsp->TOP.rresp}
    , rlast{vlSymsp->TOP.rlast}
    , rvalid{vlSymsp->TOP.rvalid}
    , rready{vlSymsp->TOP.rready}
    , wdata{vlSymsp->TOP.wdata}
    , rdata{vlSymsp->TOP.rdata}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

Vaxi_top::Vaxi_top(const char* _vcname__)
    : Vaxi_top(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

Vaxi_top::~Vaxi_top() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void Vaxi_top___024root___eval_debug_assertions(Vaxi_top___024root* vlSelf);
#endif  // VL_DEBUG
void Vaxi_top___024root___eval_static(Vaxi_top___024root* vlSelf);
void Vaxi_top___024root___eval_initial(Vaxi_top___024root* vlSelf);
void Vaxi_top___024root___eval_settle(Vaxi_top___024root* vlSelf);
void Vaxi_top___024root___eval(Vaxi_top___024root* vlSelf);

void Vaxi_top::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate Vaxi_top::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    Vaxi_top___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        Vaxi_top___024root___eval_static(&(vlSymsp->TOP));
        Vaxi_top___024root___eval_initial(&(vlSymsp->TOP));
        Vaxi_top___024root___eval_settle(&(vlSymsp->TOP));
        vlSymsp->__Vm_didInit = true;
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    Vaxi_top___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool Vaxi_top::eventsPending() { return false; }

uint64_t Vaxi_top::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* Vaxi_top::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void Vaxi_top___024root___eval_final(Vaxi_top___024root* vlSelf);

VL_ATTR_COLD void Vaxi_top::final() {
    contextp()->executingFinal(true);
    Vaxi_top___024root___eval_final(&(vlSymsp->TOP));
    contextp()->executingFinal(false);
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* Vaxi_top::hierName() const { return vlSymsp->name(); }
const char* Vaxi_top::modelName() const { return "Vaxi_top"; }
unsigned Vaxi_top::threads() const { return 1; }
void Vaxi_top::prepareClone() const { contextp()->prepareClone(); }
void Vaxi_top::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> Vaxi_top::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false}};
};

//============================================================
// Trace configuration

void Vaxi_top___024root__trace_decl_types(VerilatedFst* tracep);

void Vaxi_top___024root__trace_init_top(Vaxi_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedFst* tracep, uint32_t code) {
    // Callback from tracep->open()
    Vaxi_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vaxi_top___024root*>(voidSelf);
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    tracep->pushPrefix(vlSymsp->name(), VerilatedTracePrefixType::SCOPE_MODULE);
    Vaxi_top___024root__trace_decl_types(tracep);
    Vaxi_top___024root__trace_init_top(vlSelf, tracep);
    tracep->popPrefix();
}

VL_ATTR_COLD void Vaxi_top___024root__trace_register(Vaxi_top___024root* vlSelf, VerilatedFst* tracep);

VL_ATTR_COLD void Vaxi_top::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedFstC* const stfp = dynamic_cast<VerilatedFstC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'Vaxi_top::trace()' called on non-VerilatedFstC object;"
            " use --trace-fst with VerilatedFst object, and --trace-vcd with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP), name(), false, 39);
    Vaxi_top___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
