// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vcounter_top.h for the primary calling header

#ifndef VERILATED_VCOUNTER_TOP___024ROOT_H_
#define VERILATED_VCOUNTER_TOP___024ROOT_H_  // guard

#include "verilated.h"


class Vcounter_top__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vcounter_top___024root final {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(clk,0,0);
    VL_IN8(reset,0,0);
    VL_OUT8(count,7,0);
    VL_OUT8(overflow,0,0);
    CData/*0:0*/ __Vtrigprevexpr___TOP__clk__0;
    CData/*0:0*/ __VactPhaseResult;
    CData/*0:0*/ __VnbaPhaseResult;
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<QData/*63:0*/, 1> __VactTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VnbaTriggered;

    // INTERNAL VARIABLES
    Vcounter_top__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vcounter_top___024root(Vcounter_top__Syms* symsp, const char* namep);
    ~Vcounter_top___024root();
    VL_UNCOPYABLE(Vcounter_top___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
