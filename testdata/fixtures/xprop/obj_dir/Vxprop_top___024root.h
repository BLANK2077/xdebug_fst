// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vxprop_top.h for the primary calling header

#ifndef VERILATED_VXPROP_TOP___024ROOT_H_
#define VERILATED_VXPROP_TOP___024ROOT_H_  // guard

#include "verilated.h"


class Vxprop_top__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vxprop_top___024root final {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(clk,0,0);
    VL_IN8(reset,0,0);
    VL_OUT8(out,7,0);
    CData/*7:0*/ xprop_top__DOT____Vxrand___0;
    CData/*7:0*/ xprop_top__DOT__a;
    CData/*7:0*/ xprop_top__DOT__y;
    CData/*0:0*/ __VstlFirstIteration;
    CData/*0:0*/ __VstlPhaseResult;
    CData/*0:0*/ __Vtrigprevexpr___TOP__clk__0;
    CData/*0:0*/ __VactPhaseResult;
    CData/*0:0*/ __VnbaPhaseResult;
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<QData/*63:0*/, 1> __VstlTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VactTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VnbaTriggered;

    // INTERNAL VARIABLES
    Vxprop_top__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vxprop_top___024root(Vxprop_top__Syms* symsp, const char* namep);
    ~Vxprop_top___024root();
    VL_UNCOPYABLE(Vxprop_top___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
