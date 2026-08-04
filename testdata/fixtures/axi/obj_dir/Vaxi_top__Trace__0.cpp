// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals

#include "verilated_fst_c.h"
#include "Vaxi_top__Syms.h"


void Vaxi_top___024root__trace_chg_0_sub_0(Vaxi_top___024root* vlSelf, VerilatedFst::Buffer* bufp);

void Vaxi_top___024root__trace_chg_0(void* voidSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root__trace_chg_0\n"); );
    // Body
    Vaxi_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vaxi_top___024root*>(voidSelf);
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    Vaxi_top___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void Vaxi_top___024root__trace_chg_0_sub_0(Vaxi_top___024root* vlSelf, VerilatedFst::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root__trace_chg_0_sub_0\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 0);
    if (VL_UNLIKELY((vlSelfRef.__Vm_traceActivity[1U]))) {
        bufp->chgCData(oldp+0,(vlSelfRef.axi_top__DOT__u_slave__DOT__awid_q),4);
        bufp->chgCData(oldp+1,(vlSelfRef.axi_top__DOT__u_slave__DOT__awaddr_q),8);
        bufp->chgCData(oldp+2,(vlSelfRef.axi_top__DOT__u_slave__DOT__awlen_q),8);
        bufp->chgCData(oldp+3,(vlSelfRef.axi_top__DOT__u_slave__DOT__wcnt),8);
        bufp->chgCData(oldp+4,(vlSelfRef.axi_top__DOT__u_slave__DOT__arid_q),4);
        bufp->chgCData(oldp+5,(vlSelfRef.axi_top__DOT__u_slave__DOT__araddr_q),8);
        bufp->chgCData(oldp+6,(vlSelfRef.axi_top__DOT__u_slave__DOT__arlen_q),8);
        bufp->chgCData(oldp+7,(vlSelfRef.axi_top__DOT__u_slave__DOT__rcnt),8);
        bufp->chgBit(oldp+8,(vlSelfRef.axi_top__DOT__u_slave__DOT__ar_pending));
    }
    bufp->chgBit(oldp+9,(vlSelfRef.aclk));
    bufp->chgBit(oldp+10,(vlSelfRef.aresetn));
    bufp->chgCData(oldp+11,(vlSelfRef.awid),4);
    bufp->chgCData(oldp+12,(vlSelfRef.awaddr),8);
    bufp->chgCData(oldp+13,(vlSelfRef.awlen),8);
    bufp->chgBit(oldp+14,(vlSelfRef.awvalid));
    bufp->chgBit(oldp+15,(vlSelfRef.awready));
    bufp->chgIData(oldp+16,(vlSelfRef.wdata),32);
    bufp->chgCData(oldp+17,(vlSelfRef.wstrb),4);
    bufp->chgBit(oldp+18,(vlSelfRef.wlast));
    bufp->chgBit(oldp+19,(vlSelfRef.wvalid));
    bufp->chgBit(oldp+20,(vlSelfRef.wready));
    bufp->chgCData(oldp+21,(vlSelfRef.bid),4);
    bufp->chgCData(oldp+22,(vlSelfRef.bresp),2);
    bufp->chgBit(oldp+23,(vlSelfRef.bvalid));
    bufp->chgBit(oldp+24,(vlSelfRef.bready));
    bufp->chgCData(oldp+25,(vlSelfRef.arid),4);
    bufp->chgCData(oldp+26,(vlSelfRef.araddr),8);
    bufp->chgCData(oldp+27,(vlSelfRef.arlen),8);
    bufp->chgBit(oldp+28,(vlSelfRef.arvalid));
    bufp->chgBit(oldp+29,(vlSelfRef.arready));
    bufp->chgCData(oldp+30,(vlSelfRef.rid),4);
    bufp->chgIData(oldp+31,(vlSelfRef.rdata),32);
    bufp->chgCData(oldp+32,(vlSelfRef.rresp),2);
    bufp->chgBit(oldp+33,(vlSelfRef.rlast));
    bufp->chgBit(oldp+34,(vlSelfRef.rvalid));
    bufp->chgBit(oldp+35,(vlSelfRef.rready));
}

void Vaxi_top___024root__trace_cleanup(void* voidSelf, VerilatedFst* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root__trace_cleanup\n"); );
    // Body
    Vaxi_top___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<Vaxi_top___024root*>(voidSelf);
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    vlSymsp->__Vm_activity = false;
    vlSymsp->TOP.__Vm_traceActivity[0U] = 0U;
    vlSymsp->TOP.__Vm_traceActivity[1U] = 0U;
}
