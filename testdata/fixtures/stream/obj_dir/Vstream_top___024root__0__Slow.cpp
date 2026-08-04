// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vstream_top.h for the primary calling header

#include "Vstream_top__pch.h"

VL_ATTR_COLD void Vstream_top___024root___eval_static(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_static\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
}

VL_ATTR_COLD void Vstream_top___024root___eval_initial(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_initial\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
}

VL_ATTR_COLD void Vstream_top___024root___eval_final(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_final\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
}

#ifdef VL_DEBUG
VL_ATTR_COLD void Vstream_top___024root___dump_triggers__stl(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG
VL_ATTR_COLD bool Vstream_top___024root___eval_phase__stl(Vstream_top___024root* vlSelf);

VL_ATTR_COLD void Vstream_top___024root___eval_settle(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_settle\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VstlIterCount;
    // Body
    __VstlIterCount = 0U;
    vlSelfRef.__VstlFirstIteration = 1U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VstlIterCount)))) {
#ifdef VL_DEBUG
            Vstream_top___024root___dump_triggers__stl(vlSelfRef.__VstlTriggered, "stl"s);
#endif
            VL_FATAL_MT("stream_top.sv", 2, "", "DIDNOTCONVERGE: Settle region did not converge after '--converge-limit' of 10000 tries");
        }
        __VstlIterCount = ((IData)(1U) + __VstlIterCount);
        vlSelfRef.__VstlPhaseResult = Vstream_top___024root___eval_phase__stl(vlSelf);
        vlSelfRef.__VstlFirstIteration = 0U;
    } while (vlSelfRef.__VstlPhaseResult);
}

VL_ATTR_COLD bool Vstream_top___024root___trigger_anySet__stl(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vstream_top___024root___dump_triggers__stl(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___dump_triggers__stl\n"); );
    // Body
    if ((1U & (~ (IData)(Vstream_top___024root___trigger_anySet__stl(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: Internal 'stl' trigger - first iteration\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD bool Vstream_top___024root___trigger_anySet__stl(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___trigger_anySet__stl\n"); );
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

VL_ATTR_COLD bool Vstream_top___024root___eval_phase__stl(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___eval_phase__stl\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    CData/*0:0*/ __VstlExecute;
    // Body
    {
        // Inlined CFunc: _eval_triggers_vec__stl
        vlSelfRef.__VstlTriggered[0U] = ((0xfffffffffffffffeULL 
                                          & vlSelfRef.__VstlTriggered[0U]) 
                                         | (IData)((IData)(vlSelfRef.__VstlFirstIteration)));
    }
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        Vstream_top___024root___dump_triggers__stl(vlSelfRef.__VstlTriggered, "stl"s);
    }
#endif
    __VstlExecute = Vstream_top___024root___trigger_anySet__stl(vlSelfRef.__VstlTriggered);
    if (__VstlExecute) {
        {
            // Inlined CFunc: _eval_stl
            if ((1ULL & vlSelfRef.__VstlTriggered[0U])) {
                {
                    // Inlined CFunc: _stl_sequent__TOP__0
                    vlSelfRef.in_ready = (4U != (IData)(vlSelfRef.stream_top__DOT__count));
                    vlSelfRef.out_valid = (0U != (IData)(vlSelfRef.stream_top__DOT__count));
                    vlSelfRef.out_data = vlSelfRef.stream_top__DOT__fifo
                        [vlSelfRef.stream_top__DOT__head];
                }
            }
        }
    }
    return (__VstlExecute);
}

bool Vstream_top___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vstream_top___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___dump_triggers__act\n"); );
    // Body
    if ((1U & (~ (IData)(Vstream_top___024root___trigger_anySet__act(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: @(posedge clk)\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD void Vstream_top___024root___ctor_var_reset(Vstream_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vstream_top___024root___ctor_var_reset\n"); );
    Vstream_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    const uint64_t __VscopeHash = VL_MURMUR64_HASH(vlSelf->vlNamep);
    vlSelf->clk = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 16707436170211756652ull);
    vlSelf->reset = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 9928399931838511862ull);
    vlSelf->in_valid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 2339549897027650563ull);
    vlSelf->in_ready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 1122049356863891575ull);
    vlSelf->in_data = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 4057622023130387117ull);
    vlSelf->out_valid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 2886291494070200219ull);
    vlSelf->out_ready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 17332470166291283643ull);
    vlSelf->out_data = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 10144880484820144978ull);
    for (int __Vi0 = 0; __Vi0 < 4; ++__Vi0) {
        vlSelf->stream_top__DOT__fifo[__Vi0] = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 16226182944792841353ull);
    }
    vlSelf->stream_top__DOT__head = VL_SCOPED_RAND_RESET_I(2, __VscopeHash, 2875458233695517733ull);
    vlSelf->stream_top__DOT__tail = VL_SCOPED_RAND_RESET_I(2, __VscopeHash, 18364410669406770121ull);
    vlSelf->stream_top__DOT__count = VL_SCOPED_RAND_RESET_I(3, __VscopeHash, 10404522760089413603ull);
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VstlTriggered[__Vi0] = 0;
    }
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VactTriggered[__Vi0] = 0;
    }
    vlSelf->__Vtrigprevexpr___TOP__clk__0 = 0;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VnbaTriggered[__Vi0] = 0;
    }
    for (int __Vi0 = 0; __Vi0 < 2; ++__Vi0) {
        vlSelf->__Vm_traceActivity[__Vi0] = 0;
    }
}
