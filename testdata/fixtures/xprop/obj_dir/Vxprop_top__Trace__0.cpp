// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals

#include "verilated_fst_c.h"
#include "Vxprop_top__Syms.h"


void Vxprop_top___024root__trace_chg_0_sub_0(Vxprop_top___024root* vlSelf, VerilatedFst::Buffer* bufp);

void Vxprop_top___024root__trace_chg_0(void* voidSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vxprop_top___024root__trace_chg_0\n"); );
    // Body
    Vxprop_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vxprop_top___024root*>(voidSelf);
    Vxprop_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    Vxprop_top___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void Vxprop_top___024root__trace_chg_0_sub_0(Vxprop_top___024root* vlSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vxprop_top___024root__trace_chg_0_sub_0\n"); );
    Vxprop_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 0);
    bufp->chgBit(oldp+0,(vlSelfRef.clk));
    bufp->chgBit(oldp+1,(vlSelfRef.reset));
    bufp->chgCData(oldp+2,(vlSelfRef.xprop_top__DOT__y),8);
    bufp->chgCData(oldp+3,(vlSelfRef.xprop_top__DOT__a),8);
}

void Vxprop_top___024root__trace_cleanup(void* voidSelf, VerilatedFst* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vxprop_top___024root__trace_cleanup\n"); );
    // Locals
    VlUnpacked<CData/*0:0*/, 1> __Vm_traceActivity;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        __Vm_traceActivity[__Vi0] = 0;
    }
    // Body
    Vxprop_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vxprop_top___024root*>(voidSelf);
    Vxprop_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    vlSymsp->__Vm_activity = false;
    __Vm_traceActivity[0U] = 0U;
}
