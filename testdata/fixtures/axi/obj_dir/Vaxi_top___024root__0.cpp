// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vaxi_top.h for the primary calling header

#include "Vaxi_top__pch.h"

bool Vaxi_top___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___trigger_anySet__act\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        if (in[n]) {
            return (1U);
        }
        n = ((IData)(1U) + n);
    } while ((1U > n));
    return (0U);
}

void Vaxi_top___024root___nba_sequent__TOP__0(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___nba_sequent__TOP__0\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*7:0*/ __Vdly__axi_top__DOT__u_slave__DOT__wcnt;
    __Vdly__axi_top__DOT__u_slave__DOT__wcnt = 0;
    CData/*0:0*/ __Vdly__bvalid;
    __Vdly__bvalid = 0;
    CData/*0:0*/ __Vdly__axi_top__DOT__u_slave__DOT__ar_pending;
    __Vdly__axi_top__DOT__u_slave__DOT__ar_pending = 0;
    CData/*0:0*/ __Vdly__rvalid;
    __Vdly__rvalid = 0;
    CData/*0:0*/ __Vdly__rlast;
    __Vdly__rlast = 0;
    CData/*7:0*/ __Vdly__axi_top__DOT__u_slave__DOT__rcnt;
    __Vdly__axi_top__DOT__u_slave__DOT__rcnt = 0;
    IData/*31:0*/ __VdlyVal__axi_top__DOT__u_slave__DOT__mem__v0;
    __VdlyVal__axi_top__DOT__u_slave__DOT__mem__v0 = 0;
    CData/*7:0*/ __VdlyDim0__axi_top__DOT__u_slave__DOT__mem__v0;
    __VdlyDim0__axi_top__DOT__u_slave__DOT__mem__v0 = 0;
    CData/*0:0*/ __VdlySet__axi_top__DOT__u_slave__DOT__mem__v0;
    __VdlySet__axi_top__DOT__u_slave__DOT__mem__v0 = 0;
    // Body
    __Vdly__bvalid = vlSelfRef.bvalid;
    __Vdly__axi_top__DOT__u_slave__DOT__wcnt = vlSelfRef.axi_top__DOT__u_slave__DOT__wcnt;
    __VdlySet__axi_top__DOT__u_slave__DOT__mem__v0 = 0U;
    __Vdly__axi_top__DOT__u_slave__DOT__ar_pending 
        = vlSelfRef.axi_top__DOT__u_slave__DOT__ar_pending;
    __Vdly__rlast = vlSelfRef.rlast;
    __Vdly__axi_top__DOT__u_slave__DOT__rcnt = vlSelfRef.axi_top__DOT__u_slave__DOT__rcnt;
    __Vdly__rvalid = vlSelfRef.rvalid;
    if (vlSelfRef.aresetn) {
        if (((IData)(vlSelfRef.wvalid) & (IData)(vlSelfRef.wlast))) {
            __Vdly__bvalid = 1U;
        }
        if (((IData)(vlSelfRef.bvalid) & (IData)(vlSelfRef.bready))) {
            __Vdly__bvalid = 0U;
        }
        if (vlSelfRef.awvalid) {
            __Vdly__axi_top__DOT__u_slave__DOT__wcnt = 0U;
        }
        if (vlSelfRef.wvalid) {
            __VdlyVal__axi_top__DOT__u_slave__DOT__mem__v0 
                = vlSelfRef.wdata;
            __VdlyDim0__axi_top__DOT__u_slave__DOT__mem__v0 
                = (0x000000ffU & ((IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__awaddr_q) 
                                  + (IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__wcnt)));
            __VdlySet__axi_top__DOT__u_slave__DOT__mem__v0 = 1U;
            __Vdly__axi_top__DOT__u_slave__DOT__wcnt 
                = (0x000000ffU & ((IData)(1U) + (IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__wcnt)));
        }
        if (((IData)(vlSelfRef.wvalid) & (IData)(vlSelfRef.wlast))) {
            __Vdly__axi_top__DOT__u_slave__DOT__wcnt = 0U;
            vlSelfRef.bresp = 0U;
            vlSelfRef.bid = vlSelfRef.axi_top__DOT__u_slave__DOT__awid_q;
        }
        if (vlSelfRef.awvalid) {
            vlSelfRef.axi_top__DOT__u_slave__DOT__awlen_q 
                = vlSelfRef.awlen;
            vlSelfRef.axi_top__DOT__u_slave__DOT__awaddr_q 
                = vlSelfRef.awaddr;
            vlSelfRef.axi_top__DOT__u_slave__DOT__awid_q 
                = vlSelfRef.awid;
        }
        if (((IData)(vlSelfRef.arvalid) & (IData)(vlSelfRef.arready))) {
            __Vdly__axi_top__DOT__u_slave__DOT__ar_pending = 1U;
        }
        if (((IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__ar_pending) 
             & (~ (IData)(vlSelfRef.rvalid)))) {
            __Vdly__rvalid = 1U;
            vlSelfRef.rid = vlSelfRef.axi_top__DOT__u_slave__DOT__arid_q;
            vlSelfRef.rresp = 0U;
            vlSelfRef.rdata = vlSelfRef.axi_top__DOT__u_slave__DOT__mem
                [vlSelfRef.axi_top__DOT__u_slave__DOT__araddr_q];
            __Vdly__rlast = (0U == (IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__arlen_q));
            __Vdly__axi_top__DOT__u_slave__DOT__rcnt = 1U;
            __Vdly__axi_top__DOT__u_slave__DOT__ar_pending = 0U;
        }
        if (((IData)(vlSelfRef.rvalid) & (IData)(vlSelfRef.rready))) {
            if (vlSelfRef.rlast) {
                __Vdly__rvalid = 0U;
                __Vdly__rlast = 0U;
            } else {
                vlSelfRef.rdata = vlSelfRef.axi_top__DOT__u_slave__DOT__mem
                    [(0x000000ffU & ((IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__araddr_q) 
                                     + (IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__rcnt)))];
                __Vdly__rlast = ((IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__rcnt) 
                                 == (IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__arlen_q));
                __Vdly__axi_top__DOT__u_slave__DOT__rcnt 
                    = (0x000000ffU & ((IData)(1U) + (IData)(vlSelfRef.axi_top__DOT__u_slave__DOT__rcnt)));
            }
        }
        if (((IData)(vlSelfRef.arvalid) & (IData)(vlSelfRef.arready))) {
            vlSelfRef.axi_top__DOT__u_slave__DOT__arid_q 
                = vlSelfRef.arid;
            vlSelfRef.axi_top__DOT__u_slave__DOT__araddr_q 
                = vlSelfRef.araddr;
            vlSelfRef.axi_top__DOT__u_slave__DOT__arlen_q 
                = vlSelfRef.arlen;
        }
    } else {
        __Vdly__bvalid = 0U;
        __Vdly__axi_top__DOT__u_slave__DOT__wcnt = 0U;
        vlSelfRef.axi_top__DOT__u_slave__DOT__awlen_q = 0U;
        vlSelfRef.bresp = 0U;
        vlSelfRef.bid = 0U;
        __Vdly__axi_top__DOT__u_slave__DOT__rcnt = 0U;
        __Vdly__rlast = 0U;
        __Vdly__axi_top__DOT__u_slave__DOT__ar_pending = 0U;
        __Vdly__rvalid = 0U;
        vlSelfRef.rid = 0U;
        vlSelfRef.rdata = 0U;
        vlSelfRef.rresp = 0U;
        vlSelfRef.axi_top__DOT__u_slave__DOT__awaddr_q = 0U;
        vlSelfRef.axi_top__DOT__u_slave__DOT__awid_q = 0U;
        vlSelfRef.axi_top__DOT__u_slave__DOT__arid_q = 0U;
        vlSelfRef.axi_top__DOT__u_slave__DOT__araddr_q = 0U;
        vlSelfRef.axi_top__DOT__u_slave__DOT__arlen_q = 0U;
    }
    vlSelfRef.bvalid = __Vdly__bvalid;
    vlSelfRef.axi_top__DOT__u_slave__DOT__wcnt = __Vdly__axi_top__DOT__u_slave__DOT__wcnt;
    vlSelfRef.axi_top__DOT__u_slave__DOT__ar_pending 
        = __Vdly__axi_top__DOT__u_slave__DOT__ar_pending;
    vlSelfRef.rlast = __Vdly__rlast;
    vlSelfRef.axi_top__DOT__u_slave__DOT__rcnt = __Vdly__axi_top__DOT__u_slave__DOT__rcnt;
    if (__VdlySet__axi_top__DOT__u_slave__DOT__mem__v0) {
        vlSelfRef.axi_top__DOT__u_slave__DOT__mem[__VdlyDim0__axi_top__DOT__u_slave__DOT__mem__v0] 
            = __VdlyVal__axi_top__DOT__u_slave__DOT__mem__v0;
    }
    vlSelfRef.rvalid = __Vdly__rvalid;
    vlSelfRef.arready = (1U & (~ (IData)(vlSelfRef.rvalid)));
}

void Vaxi_top___024root___trigger_orInto__act_vec_vec(VlUnpacked<QData/*63:0*/, 1> &out, const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___trigger_orInto__act_vec_vec\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        out[n] = (out[n] | in[n]);
        n = ((IData)(1U) + n);
    } while ((0U >= n));
}

#ifdef VL_DEBUG
VL_ATTR_COLD void Vaxi_top___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG

bool Vaxi_top___024root___eval_phase__act(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_phase__act\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    {
        // Inlined CFunc: _eval_triggers_vec__act
        vlSelfRef.__VactTriggered[0U] = (QData)((IData)(
                                                        ((IData)(vlSelfRef.aclk) 
                                                         & (~ (IData)(vlSelfRef.__Vtrigprevexpr___TOP__aclk__0)))));
        vlSelfRef.__Vtrigprevexpr___TOP__aclk__0 = vlSelfRef.aclk;
    }
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        Vaxi_top___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
    }
#endif
    Vaxi_top___024root___trigger_orInto__act_vec_vec(vlSelfRef.__VnbaTriggered, vlSelfRef.__VactTriggered);
    return (0U);
}

void Vaxi_top___024root___trigger_clear__act(VlUnpacked<QData/*63:0*/, 1> &out) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___trigger_clear__act\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        out[n] = 0ULL;
        n = ((IData)(1U) + n);
    } while ((1U > n));
}

bool Vaxi_top___024root___eval_phase__nba(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_phase__nba\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*0:0*/ __VnbaExecute;
    // Body
    __VnbaExecute = Vaxi_top___024root___trigger_anySet__act(vlSelfRef.__VnbaTriggered);
    if (__VnbaExecute) {
        {
            // Inlined CFunc: _eval_nba
            if ((1ULL & vlSelfRef.__VnbaTriggered[0U])) {
                Vaxi_top___024root___nba_sequent__TOP__0(vlSelf);
                vlSelfRef.__Vm_traceActivity[1U] = 1U;
            }
        }
        Vaxi_top___024root___trigger_clear__act(vlSelfRef.__VnbaTriggered);
    }
    return (__VnbaExecute);
}

void Vaxi_top___024root___eval(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VnbaIterCount;
    // Body
    __VnbaIterCount = 0U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VnbaIterCount)))) {
#ifdef VL_DEBUG
            Vaxi_top___024root___dump_triggers__act(vlSelfRef.__VnbaTriggered, "nba"s);
#endif
            VL_FATAL_MT("axi_top.sv", 98, "", "DIDNOTCONVERGE: NBA region did not converge after '--converge-limit' of 10000 tries");
        }
        __VnbaIterCount = ((IData)(1U) + __VnbaIterCount);
        vlSelfRef.__VactIterCount = 0U;
        do {
            if (VL_UNLIKELY(((0x00002710U < vlSelfRef.__VactIterCount)))) {
#ifdef VL_DEBUG
                Vaxi_top___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
#endif
                VL_FATAL_MT("axi_top.sv", 98, "", "DIDNOTCONVERGE: Active region did not converge after '--converge-limit' of 10000 tries");
            }
            vlSelfRef.__VactIterCount = ((IData)(1U) 
                                         + vlSelfRef.__VactIterCount);
            vlSelfRef.__VactPhaseResult = Vaxi_top___024root___eval_phase__act(vlSelf);
        } while (vlSelfRef.__VactPhaseResult);
        vlSelfRef.__VnbaPhaseResult = Vaxi_top___024root___eval_phase__nba(vlSelf);
    } while (vlSelfRef.__VnbaPhaseResult);
}

#ifdef VL_DEBUG
void Vaxi_top___024root___eval_debug_assertions(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_debug_assertions\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if (VL_UNLIKELY(((vlSelfRef.aclk & 0xfeU)))) {
        Verilated::overWidthError("aclk");
    }
    if (VL_UNLIKELY(((vlSelfRef.aresetn & 0xfeU)))) {
        Verilated::overWidthError("aresetn");
    }
    if (VL_UNLIKELY(((vlSelfRef.awid & 0xf0U)))) {
        Verilated::overWidthError("awid");
    }
    if (VL_UNLIKELY(((vlSelfRef.awvalid & 0xfeU)))) {
        Verilated::overWidthError("awvalid");
    }
    if (VL_UNLIKELY(((vlSelfRef.wstrb & 0xf0U)))) {
        Verilated::overWidthError("wstrb");
    }
    if (VL_UNLIKELY(((vlSelfRef.wlast & 0xfeU)))) {
        Verilated::overWidthError("wlast");
    }
    if (VL_UNLIKELY(((vlSelfRef.wvalid & 0xfeU)))) {
        Verilated::overWidthError("wvalid");
    }
    if (VL_UNLIKELY(((vlSelfRef.bready & 0xfeU)))) {
        Verilated::overWidthError("bready");
    }
    if (VL_UNLIKELY(((vlSelfRef.arid & 0xf0U)))) {
        Verilated::overWidthError("arid");
    }
    if (VL_UNLIKELY(((vlSelfRef.arvalid & 0xfeU)))) {
        Verilated::overWidthError("arvalid");
    }
    if (VL_UNLIKELY(((vlSelfRef.rready & 0xfeU)))) {
        Verilated::overWidthError("rready");
    }
}
#endif  // VL_DEBUG
