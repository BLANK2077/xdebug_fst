// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vapb_top.h for the primary calling header

#include "Vapb_top__pch.h"

VL_ATTR_COLD void Vapb_top___024root___eval_static(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_static\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__Vtrigprevexpr___TOP__pclk__0 = vlSelfRef.pclk;
}

VL_ATTR_COLD void Vapb_top___024root___eval_initial(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_initial\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    {
        // Inlined CFunc: _eval_initial__TOP
        vlSelfRef.pready = 1U;
        vlSelfRef.pslverr = 0U;
    }
}

VL_ATTR_COLD void Vapb_top___024root___eval_final(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_final\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
}

VL_ATTR_COLD void Vapb_top___024root___eval_settle(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___eval_settle\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
}

bool Vapb_top___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vapb_top___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___dump_triggers__act\n"); );
    // Body
    if ((1U & (~ (IData)(Vapb_top___024root___trigger_anySet__act(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: @(posedge pclk)\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD void Vapb_top___024root___ctor_var_reset(Vapb_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vapb_top___024root___ctor_var_reset\n"); );
    Vapb_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    const uint64_t __VscopeHash = VL_MURMUR64_HASH(vlSelf->vlNamep);
    vlSelf->pclk = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 2198342127515400097ull);
    vlSelf->presetn = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 13603466079958707827ull);
    vlSelf->psel = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 5365422930610402651ull);
    vlSelf->penable = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 10310706929790612258ull);
    vlSelf->pwrite = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 3040259829508521558ull);
    vlSelf->paddr = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 14875722346688934266ull);
    vlSelf->pwdata = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 876536812074066652ull);
    vlSelf->prdata = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 15696981534648657427ull);
    vlSelf->pready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 5373772549111784264ull);
    vlSelf->pslverr = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 8826578892537611518ull);
    for (int __Vi0 = 0; __Vi0 < 4; ++__Vi0) {
        vlSelf->apb_top__DOT__u_slave__DOT__mem[__Vi0] = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 9939256059840816008ull);
    }
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VactTriggered[__Vi0] = 0;
    }
    vlSelf->__Vtrigprevexpr___TOP__pclk__0 = 0;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VnbaTriggered[__Vi0] = 0;
    }
}
