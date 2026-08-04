// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals

#include "verilated_fst_c.h"
#include "Vstream_top__Syms.h"


void Vstream_top___024root__trace_chg_0_sub_0(Vstream_top___024root* vlSelf, VerilatedFst::Buffer* bufp);

void Vstream_top___024root__trace_chg_0(void* voidSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root__trace_chg_0\n"); );
    // Body
    Vstream_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vstream_top___024root*>(voidSelf);
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    Vstream_top___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void Vstream_top___024root__trace_chg_dtype____0(Vstream_top___024root* vlSelf, VerilatedFst::Buffer* bufp, uint32_t offset, const VlUnpacked<CData/*7:0*/, 4>& __VdtypeVar);

void Vstream_top___024root__trace_chg_0_sub_0(Vstream_top___024root* vlSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root__trace_chg_0_sub_0\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 0);
    if (VL_UNLIKELY((vlSelfRef.__Vm_traceActivity[1U]))) {
        Vstream_top___024root__trace_chg_dtype____0(vlSelf, bufp, 0, vlSelfRef.stream_top__DOT__fifo);
        bufp->chgCData(oldp+4,(vlSelfRef.stream_top__DOT__head),2);
        bufp->chgCData(oldp+5,(vlSelfRef.stream_top__DOT__tail),2);
        bufp->chgCData(oldp+6,(vlSelfRef.stream_top__DOT__count),3);
        bufp->chgBit(oldp+7,((4U == (IData)(vlSelfRef.stream_top__DOT__count))));
        bufp->chgBit(oldp+8,((0U == (IData)(vlSelfRef.stream_top__DOT__count))));
    }
    bufp->chgBit(oldp+9,(vlSelfRef.clk));
    bufp->chgBit(oldp+10,(vlSelfRef.reset));
    bufp->chgBit(oldp+11,(vlSelfRef.in_valid));
    bufp->chgBit(oldp+12,(vlSelfRef.in_ready));
    bufp->chgCData(oldp+13,(vlSelfRef.in_data),8);
    bufp->chgBit(oldp+14,(vlSelfRef.out_valid));
    bufp->chgBit(oldp+15,(vlSelfRef.out_ready));
    bufp->chgCData(oldp+16,(vlSelfRef.out_data),8);
}

void Vstream_top___024root__trace_chg_dtype____0(Vstream_top___024root* vlSelf, VerilatedFst::Buffer* bufp, uint32_t offset, const VlUnpacked<CData/*7:0*/, 4>& __VdtypeVar) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root__trace_chg_dtype____0\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode +  offset);
    bufp->chgCData(oldp+0,(__VdtypeVar[0]),8);
    bufp->chgCData(oldp+1,(__VdtypeVar[1]),8);
    bufp->chgCData(oldp+2,(__VdtypeVar[2]),8);
    bufp->chgCData(oldp+3,(__VdtypeVar[3]),8);
}

void Vstream_top___024root__trace_cleanup(void* voidSelf, VerilatedFst* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root__trace_cleanup\n"); );
    // Body
    Vstream_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vstream_top___024root*>(voidSelf);
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    vlSymsp->__Vm_activity = false;
    vlSymsp->TOP.__Vm_traceActivity[0U] = 0U;
    vlSymsp->TOP.__Vm_traceActivity[1U] = 0U;
}
