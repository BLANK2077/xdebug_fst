// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals

#include "verilated_fst_c.h"
#include "Vapb_top__Syms.h"


void Vapb_top___024root__trace_chg_0_sub_0(Vapb_top___024root* vlSelf, VerilatedFst::Buffer* bufp);

void Vapb_top___024root__trace_chg_0(void* voidSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root__trace_chg_0\n"); );
    // Body
    Vapb_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vapb_top___024root*>(voidSelf);
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    Vapb_top___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void Vapb_top___024root__trace_chg_dtype____0(Vapb_top___024root* vlSelf, VerilatedFst::Buffer* bufp, uint32_t offset, const VlUnpacked<CData/*7:0*/, 4>& __VdtypeVar);

void Vapb_top___024root__trace_chg_0_sub_0(Vapb_top___024root* vlSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root__trace_chg_0_sub_0\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 0);
    bufp->chgBit(oldp+0,(vlSelfRef.pclk));
    bufp->chgBit(oldp+1,(vlSelfRef.presetn));
    bufp->chgBit(oldp+2,(vlSelfRef.psel));
    bufp->chgBit(oldp+3,(vlSelfRef.penable));
    bufp->chgBit(oldp+4,(vlSelfRef.pwrite));
    bufp->chgCData(oldp+5,(vlSelfRef.paddr),8);
    bufp->chgCData(oldp+6,(vlSelfRef.pwdata),8);
    bufp->chgCData(oldp+7,(vlSelfRef.prdata),8);
    bufp->chgBit(oldp+8,(vlSelfRef.pready));
    bufp->chgBit(oldp+9,(vlSelfRef.pslverr));
    Vapb_top___024root__trace_chg_dtype____0(vlSelf, bufp, 10, vlSelfRef.apb_top__DOT__u_slave__DOT__mem);
}

void Vapb_top___024root__trace_chg_dtype____0(Vapb_top___024root* vlSelf, VerilatedFst::Buffer* bufp, uint32_t offset, const VlUnpacked<CData/*7:0*/, 4>& __VdtypeVar) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root__trace_chg_dtype____0\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode +  offset);
    bufp->chgCData(oldp+0,(__VdtypeVar[0]),8);
    bufp->chgCData(oldp+1,(__VdtypeVar[1]),8);
    bufp->chgCData(oldp+2,(__VdtypeVar[2]),8);
    bufp->chgCData(oldp+3,(__VdtypeVar[3]),8);
}

void Vapb_top___024root__trace_cleanup(void* voidSelf, VerilatedFst* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root__trace_cleanup\n"); );
    // Locals
    VlUnpacked<CData/*0:0*/, 1> __Vm_traceActivity;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        __Vm_traceActivity[__Vi0] = 0;
    }
    // Body
    Vapb_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vapb_top___024root*>(voidSelf);
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    vlSymsp->__Vm_activity = false;
    __Vm_traceActivity[0U] = 0U;
}
