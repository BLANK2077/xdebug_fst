// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vapb_top.h for the primary calling header

#include "Vapb_top__pch.h"

bool Vapb_top___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___trigger_anySet__act\n"); );
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

void Vapb_top___024root___trigger_orInto__act_vec_vec(VlUnpacked<QData/*63:0*/, 1> &out, const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___trigger_orInto__act_vec_vec\n"); );
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
VL_ATTR_COLD void Vapb_top___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG

bool Vapb_top___024root___eval_phase__act(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_phase__act\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    {
        // Inlined CFunc: _eval_triggers_vec__act
        vlSelfRef.__VactTriggered[0U] = (QData)((IData)(
                                                        ((IData)(vlSelfRef.pclk) 
                                                         & (~ (IData)(vlSelfRef.__Vtrigprevexpr___TOP__pclk__0)))));
        vlSelfRef.__Vtrigprevexpr___TOP__pclk__0 = vlSelfRef.pclk;
    }
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        Vapb_top___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
    }
#endif
    Vapb_top___024root___trigger_orInto__act_vec_vec(vlSelfRef.__VnbaTriggered, vlSelfRef.__VactTriggered);
    return (0U);
}

void Vapb_top___024root___trigger_clear__act(VlUnpacked<QData/*63:0*/, 1> &out) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___trigger_clear__act\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        out[n] = 0ULL;
        n = ((IData)(1U) + n);
    } while ((1U > n));
}

bool Vapb_top___024root___eval_phase__nba(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_phase__nba\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*0:0*/ __VnbaExecute;
    // Body
    __VnbaExecute = Vapb_top___024root___trigger_anySet__act(vlSelfRef.__VnbaTriggered);
    if (__VnbaExecute) {
        {
            // Inlined CFunc: _eval_nba
            if ((1ULL & vlSelfRef.__VnbaTriggered[0U])) {
                {
                    // Inlined CFunc: _nba_sequent__TOP__0
                    CData/*7:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__apb_top__DOT__u_slave__DOT__mem__v0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__apb_top__DOT__u_slave__DOT__mem__v0 = 0;
                    CData/*1:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__apb_top__DOT__u_slave__DOT__mem__v0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__apb_top__DOT__u_slave__DOT__mem__v0 = 0;
                    CData/*0:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v0 = 0;
                    CData/*0:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v1;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v1 = 0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v0 = 0U;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v1 = 0U;
                    if (vlSelfRef.presetn) {
                        if ((((IData)(vlSelfRef.psel) 
                              & (IData)(vlSelfRef.penable)) 
                             & (IData)(vlSelfRef.pwrite))) {
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__apb_top__DOT__u_slave__DOT__mem__v0 
                                = vlSelfRef.pwdata;
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__apb_top__DOT__u_slave__DOT__mem__v0 
                                = (3U & (IData)(vlSelfRef.paddr));
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v0 = 1U;
                        }
                        if ((((IData)(vlSelfRef.psel) 
                              & (IData)(vlSelfRef.penable)) 
                             & (~ (IData)(vlSelfRef.pwrite)))) {
                            vlSelfRef.prdata = vlSelfRef.apb_top__DOT__u_slave__DOT__mem
                                [(3U & (IData)(vlSelfRef.paddr))];
                        }
                    } else {
                        __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v1 = 1U;
                        vlSelfRef.prdata = 0U;
                    }
                    if (__Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v0) {
                        vlSelfRef.apb_top__DOT__u_slave__DOT__mem[__Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__apb_top__DOT__u_slave__DOT__mem__v0] 
                            = __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__apb_top__DOT__u_slave__DOT__mem__v0;
                    }
                    if (__Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__apb_top__DOT__u_slave__DOT__mem__v1) {
                        vlSelfRef.apb_top__DOT__u_slave__DOT__mem[0U] = 0U;
                        vlSelfRef.apb_top__DOT__u_slave__DOT__mem[1U] = 0U;
                        vlSelfRef.apb_top__DOT__u_slave__DOT__mem[2U] = 0U;
                        vlSelfRef.apb_top__DOT__u_slave__DOT__mem[3U] = 0U;
                    }
                }
            }
        }
        Vapb_top___024root___trigger_clear__act(vlSelfRef.__VnbaTriggered);
    }
    return (__VnbaExecute);
}

void Vapb_top___024root___eval(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VnbaIterCount;
    // Body
    __VnbaIterCount = 0U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VnbaIterCount)))) {
#ifdef VL_DEBUG
            Vapb_top___024root___dump_triggers__act(vlSelfRef.__VnbaTriggered, "nba"s);
#endif
            VL_FATAL_MT("apb_top.sv", 30, "", "DIDNOTCONVERGE: NBA region did not converge after '--converge-limit' of 10000 tries");
        }
        __VnbaIterCount = ((IData)(1U) + __VnbaIterCount);
        vlSelfRef.__VactIterCount = 0U;
        do {
            if (VL_UNLIKELY(((0x00002710U < vlSelfRef.__VactIterCount)))) {
#ifdef VL_DEBUG
                Vapb_top___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
#endif
                VL_FATAL_MT("apb_top.sv", 30, "", "DIDNOTCONVERGE: Active region did not converge after '--converge-limit' of 10000 tries");
            }
            vlSelfRef.__VactIterCount = ((IData)(1U) 
                                         + vlSelfRef.__VactIterCount);
            vlSelfRef.__VactPhaseResult = Vapb_top___024root___eval_phase__act(vlSelf);
        } while (vlSelfRef.__VactPhaseResult);
        vlSelfRef.__VnbaPhaseResult = Vapb_top___024root___eval_phase__nba(vlSelf);
    } while (vlSelfRef.__VnbaPhaseResult);
}

#ifdef VL_DEBUG
void Vapb_top___024root___eval_debug_assertions(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_debug_assertions\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if (VL_UNLIKELY(((vlSelfRef.pclk & 0xfeU)))) {
        Verilated::overWidthError("pclk");
    }
    if (VL_UNLIKELY(((vlSelfRef.presetn & 0xfeU)))) {
        Verilated::overWidthError("presetn");
    }
    if (VL_UNLIKELY(((vlSelfRef.psel & 0xfeU)))) {
        Verilated::overWidthError("psel");
    }
    if (VL_UNLIKELY(((vlSelfRef.penable & 0xfeU)))) {
        Verilated::overWidthError("penable");
    }
    if (VL_UNLIKELY(((vlSelfRef.pwrite & 0xfeU)))) {
        Verilated::overWidthError("pwrite");
    }
}
#endif  // VL_DEBUG
