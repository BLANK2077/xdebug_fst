// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vstream_top.h for the primary calling header

#include "Vstream_top__pch.h"

bool Vstream_top___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___trigger_anySet__act\n"); );
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

void Vstream_top___024root___trigger_orInto__act_vec_vec(VlUnpacked<QData/*63:0*/, 1> &out, const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___trigger_orInto__act_vec_vec\n"); );
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
VL_ATTR_COLD void Vstream_top___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG

bool Vstream_top___024root___eval_phase__act(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_phase__act\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    {
        // Inlined CFunc: _eval_triggers_vec__act
        vlSelfRef.__VactTriggered[0U] = (QData)((IData)(
                                                        ((IData)(vlSelfRef.clk) 
                                                         & (~ (IData)(vlSelfRef.__Vtrigprevexpr___TOP__clk__0)))));
        vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
    }
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        Vstream_top___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
    }
#endif
    Vstream_top___024root___trigger_orInto__act_vec_vec(vlSelfRef.__VnbaTriggered, vlSelfRef.__VactTriggered);
    return (0U);
}

void Vstream_top___024root___trigger_clear__act(VlUnpacked<QData/*63:0*/, 1> &out) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___trigger_clear__act\n"); );
    // Locals
    IData/*31:0*/ n;
    // Body
    n = 0U;
    do {
        out[n] = 0ULL;
        n = ((IData)(1U) + n);
    } while ((1U > n));
}

bool Vstream_top___024root___eval_phase__nba(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_phase__nba\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*0:0*/ __VnbaExecute;
    // Body
    __VnbaExecute = Vstream_top___024root___trigger_anySet__act(vlSelfRef.__VnbaTriggered);
    if (__VnbaExecute) {
        {
            // Inlined CFunc: _eval_nba
            if ((1ULL & vlSelfRef.__VnbaTriggered[0U])) {
                {
                    // Inlined CFunc: _nba_sequent__TOP__0
                    CData/*1:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__head;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__head = 0;
                    CData/*1:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__tail;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__tail = 0;
                    CData/*2:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count = 0;
                    CData/*7:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__stream_top__DOT__fifo__v0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__stream_top__DOT__fifo__v0 = 0;
                    CData/*1:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__stream_top__DOT__fifo__v0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__stream_top__DOT__fifo__v0 = 0;
                    CData/*0:0*/ __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__stream_top__DOT__fifo__v0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__stream_top__DOT__fifo__v0 = 0;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__head 
                        = vlSelfRef.stream_top__DOT__head;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count 
                        = vlSelfRef.stream_top__DOT__count;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__tail 
                        = vlSelfRef.stream_top__DOT__tail;
                    __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__stream_top__DOT__fifo__v0 = 0U;
                    if (vlSelfRef.reset) {
                        __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__head = 0U;
                        __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count = 0U;
                        __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__tail = 0U;
                    } else {
                        if (((IData)(vlSelfRef.in_valid) 
                             & (IData)(vlSelfRef.in_ready))) {
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count 
                                = (7U & ((IData)(1U) 
                                         + (IData)(vlSelfRef.stream_top__DOT__count)));
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__stream_top__DOT__fifo__v0 
                                = vlSelfRef.in_data;
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__stream_top__DOT__fifo__v0 
                                = vlSelfRef.stream_top__DOT__tail;
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__stream_top__DOT__fifo__v0 = 1U;
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__tail 
                                = (3U & ((IData)(1U) 
                                         + (IData)(vlSelfRef.stream_top__DOT__tail)));
                        }
                        if (((IData)(vlSelfRef.out_valid) 
                             & (IData)(vlSelfRef.out_ready))) {
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__head 
                                = (3U & ((IData)(1U) 
                                         + (IData)(vlSelfRef.stream_top__DOT__head)));
                            __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count 
                                = (7U & ((IData)(vlSelfRef.stream_top__DOT__count) 
                                         - (IData)(1U)));
                        }
                    }
                    vlSelfRef.stream_top__DOT__head 
                        = __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__head;
                    vlSelfRef.stream_top__DOT__count 
                        = __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__count;
                    vlSelfRef.stream_top__DOT__tail 
                        = __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___Vdly__stream_top__DOT__tail;
                    if (__Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlySet__stream_top__DOT__fifo__v0) {
                        vlSelfRef.stream_top__DOT__fifo[__Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyDim0__stream_top__DOT__fifo__v0] 
                            = __Vinline_0__eval_nba___Vinline_0__nba_sequent__TOP__0___VdlyVal__stream_top__DOT__fifo__v0;
                    }
                    vlSelfRef.in_ready = (4U != (IData)(vlSelfRef.stream_top__DOT__count));
                    vlSelfRef.out_valid = (0U != (IData)(vlSelfRef.stream_top__DOT__count));
                    vlSelfRef.out_data = vlSelfRef.stream_top__DOT__fifo
                        [vlSelfRef.stream_top__DOT__head];
                }
                vlSelfRef.__Vm_traceActivity[1U] = 1U;
            }
        }
        Vstream_top___024root___trigger_clear__act(vlSelfRef.__VnbaTriggered);
    }
    return (__VnbaExecute);
}

void Vstream_top___024root___eval(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VnbaIterCount;
    // Body
    __VnbaIterCount = 0U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VnbaIterCount)))) {
#ifdef VL_DEBUG
            Vstream_top___024root___dump_triggers__act(vlSelfRef.__VnbaTriggered, "nba"s);
#endif
            VL_FATAL_MT("stream_top.sv", 2, "", "DIDNOTCONVERGE: NBA region did not converge after '--converge-limit' of 10000 tries");
        }
        __VnbaIterCount = ((IData)(1U) + __VnbaIterCount);
        vlSelfRef.__VactIterCount = 0U;
        do {
            if (VL_UNLIKELY(((0x00002710U < vlSelfRef.__VactIterCount)))) {
#ifdef VL_DEBUG
                Vstream_top___024root___dump_triggers__act(vlSelfRef.__VactTriggered, "act"s);
#endif
                VL_FATAL_MT("stream_top.sv", 2, "", "DIDNOTCONVERGE: Active region did not converge after '--converge-limit' of 10000 tries");
            }
            vlSelfRef.__VactIterCount = ((IData)(1U) 
                                         + vlSelfRef.__VactIterCount);
            vlSelfRef.__VactPhaseResult = Vstream_top___024root___eval_phase__act(vlSelf);
        } while (vlSelfRef.__VactPhaseResult);
        vlSelfRef.__VnbaPhaseResult = Vstream_top___024root___eval_phase__nba(vlSelf);
    } while (vlSelfRef.__VnbaPhaseResult);
}

#ifdef VL_DEBUG
void Vstream_top___024root___eval_debug_assertions(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_debug_assertions\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if (VL_UNLIKELY(((vlSelfRef.clk & 0xfeU)))) {
        Verilated::overWidthError("clk");
    }
    if (VL_UNLIKELY(((vlSelfRef.reset & 0xfeU)))) {
        Verilated::overWidthError("reset");
    }
    if (VL_UNLIKELY(((vlSelfRef.in_valid & 0xfeU)))) {
        Verilated::overWidthError("in_valid");
    }
    if (VL_UNLIKELY(((vlSelfRef.out_ready & 0xfeU)))) {
        Verilated::overWidthError("out_ready");
    }
}
#endif  // VL_DEBUG
