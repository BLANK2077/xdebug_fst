// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vaxi_top.h for the primary calling header

#include "Vaxi_top__pch.h"

VL_ATTR_COLD void Vaxi_top___024root___eval_static(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_static\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__Vtrigprevexpr___TOP__aclk__0 = vlSelfRef.aclk;
}

VL_ATTR_COLD void Vaxi_top___024root___eval_initial(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_initial\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    {
        // Inlined CFunc: _eval_initial__TOP
        vlSelfRef.awready = 1U;
        vlSelfRef.wready = 1U;
    }
}

VL_ATTR_COLD void Vaxi_top___024root___eval_final(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_final\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
}

#ifdef VL_DEBUG
VL_ATTR_COLD void Vaxi_top___024root___dump_triggers__stl(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag);
#endif  // VL_DEBUG
VL_ATTR_COLD bool Vaxi_top___024root___eval_phase__stl(Vaxi_top___024root* vlSelf);

VL_ATTR_COLD void Vaxi_top___024root___eval_settle(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_settle\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Locals
    IData/*31:0*/ __VstlIterCount;
    // Body
    __VstlIterCount = 0U;
    vlSelfRef.__VstlFirstIteration = 1U;
    do {
        if (VL_UNLIKELY(((0x00002710U < __VstlIterCount)))) {
#ifdef VL_DEBUG
            Vaxi_top___024root___dump_triggers__stl(vlSelfRef.__VstlTriggered, "stl"s);
#endif
            VL_FATAL_MT("axi_top.sv", 98, "", "DIDNOTCONVERGE: Settle region did not converge after '--converge-limit' of 10000 tries");
        }
        __VstlIterCount = ((IData)(1U) + __VstlIterCount);
        vlSelfRef.__VstlPhaseResult = Vaxi_top___024root___eval_phase__stl(vlSelf);
        vlSelfRef.__VstlFirstIteration = 0U;
    } while (vlSelfRef.__VstlPhaseResult);
}

VL_ATTR_COLD bool Vaxi_top___024root___trigger_anySet__stl(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vaxi_top___024root___dump_triggers__stl(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___dump_triggers__stl\n"); );
    // Body
    if ((1U & (~ (IData)(Vaxi_top___024root___trigger_anySet__stl(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: Internal 'stl' trigger - first iteration\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD bool Vaxi_top___024root___trigger_anySet__stl(const VlUnpacked<QData/*63:0*/, 1> &in) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___trigger_anySet__stl\n"); );
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

VL_ATTR_COLD bool Vaxi_top___024root___eval_phase__stl(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___eval_phase__stl\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
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
        Vaxi_top___024root___dump_triggers__stl(vlSelfRef.__VstlTriggered, "stl"s);
    }
#endif
    __VstlExecute = Vaxi_top___024root___trigger_anySet__stl(vlSelfRef.__VstlTriggered);
    if (__VstlExecute) {
        {
            // Inlined CFunc: _eval_stl
            if ((1ULL & vlSelfRef.__VstlTriggered[0U])) {
                {
                    // Inlined CFunc: _stl_sequent__TOP__0
                    vlSelfRef.arready = (1U & (~ (IData)(vlSelfRef.rvalid)));
                }
            }
        }
    }
    return (__VstlExecute);
}

bool Vaxi_top___024root___trigger_anySet__act(const VlUnpacked<QData/*63:0*/, 1> &in);

#ifdef VL_DEBUG
VL_ATTR_COLD void Vaxi_top___024root___dump_triggers__act(const VlUnpacked<QData/*63:0*/, 1> &triggers, const std::string &tag) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___dump_triggers__act\n"); );
    // Body
    if ((1U & (~ (IData)(Vaxi_top___024root___trigger_anySet__act(triggers))))) {
        VL_DBG_MSGS("         No '" + tag + "' region triggers active\n");
    }
    if ((1U & (IData)(triggers[0U]))) {
        VL_DBG_MSGS("         '" + tag + "' region trigger index 0 is active: @(posedge aclk)\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD void Vaxi_top___024root___ctor_var_reset(Vaxi_top___024root* vlSelf) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    Vaxi_top___024root___ctor_var_reset\n"); );
    Vaxi_top__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    auto& vlSelfRef = std::ref(*vlSelf).get();
    // Body
    const uint64_t __VscopeHash = VL_MURMUR64_HASH(vlSelf->vlNamep);
    vlSelf->aclk = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 10553736683680568397ull);
    vlSelf->aresetn = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 8123012239402083478ull);
    vlSelf->awid = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 3795331727834872933ull);
    vlSelf->awaddr = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 10741232094138379896ull);
    vlSelf->awlen = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 13823799410002427218ull);
    vlSelf->awvalid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 14077405313628979207ull);
    vlSelf->awready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 9399710217136046492ull);
    vlSelf->wdata = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 12890271867161903902ull);
    vlSelf->wstrb = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 15125268524300477597ull);
    vlSelf->wlast = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 8447900327780902047ull);
    vlSelf->wvalid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 13135585445238253745ull);
    vlSelf->wready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 2694481459927628098ull);
    vlSelf->bid = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 3323000146968766057ull);
    vlSelf->bresp = VL_SCOPED_RAND_RESET_I(2, __VscopeHash, 3607396732575112162ull);
    vlSelf->bvalid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 14655036748745407948ull);
    vlSelf->bready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 16336171827316305440ull);
    vlSelf->arid = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 2554568685633023ull);
    vlSelf->araddr = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 17685200476622543275ull);
    vlSelf->arlen = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 2795619241893056918ull);
    vlSelf->arvalid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 16774798297805906817ull);
    vlSelf->arready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 2587622265565774708ull);
    vlSelf->rid = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 17711635114162165067ull);
    vlSelf->rdata = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 10065165116613087284ull);
    vlSelf->rresp = VL_SCOPED_RAND_RESET_I(2, __VscopeHash, 810448354640171968ull);
    vlSelf->rlast = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 5670148758994432807ull);
    vlSelf->rvalid = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 14125721737830190460ull);
    vlSelf->rready = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 8930348232195030647ull);
    for (int __Vi0 = 0; __Vi0 < 256; ++__Vi0) {
        vlSelf->axi_top__DOT__u_slave__DOT__mem[__Vi0] = VL_SCOPED_RAND_RESET_I(32, __VscopeHash, 4136548972159563895ull);
    }
    vlSelf->axi_top__DOT__u_slave__DOT__awid_q = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 15695690833302100545ull);
    vlSelf->axi_top__DOT__u_slave__DOT__awaddr_q = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 16075915244134261913ull);
    vlSelf->axi_top__DOT__u_slave__DOT__awlen_q = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 2879564087284100013ull);
    vlSelf->axi_top__DOT__u_slave__DOT__wcnt = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 3128072231082977921ull);
    vlSelf->axi_top__DOT__u_slave__DOT__arid_q = VL_SCOPED_RAND_RESET_I(4, __VscopeHash, 9462415171917591243ull);
    vlSelf->axi_top__DOT__u_slave__DOT__araddr_q = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 11242818325478956253ull);
    vlSelf->axi_top__DOT__u_slave__DOT__arlen_q = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 17419702080148962233ull);
    vlSelf->axi_top__DOT__u_slave__DOT__rcnt = VL_SCOPED_RAND_RESET_I(8, __VscopeHash, 10870830351260151667ull);
    vlSelf->axi_top__DOT__u_slave__DOT__ar_pending = VL_SCOPED_RAND_RESET_I(1, __VscopeHash, 9330452012909083419ull);
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VstlTriggered[__Vi0] = 0;
    }
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VactTriggered[__Vi0] = 0;
    }
    vlSelf->__Vtrigprevexpr___TOP__aclk__0 = 0;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        vlSelf->__VnbaTriggered[__Vi0] = 0;
    }
    for (int __Vi0 = 0; __Vi0 < 2; ++__Vi0) {
        vlSelf->__Vm_traceActivity[__Vi0] = 0;
    }
}
