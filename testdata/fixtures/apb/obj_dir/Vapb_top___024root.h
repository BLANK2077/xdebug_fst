// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vapb_top.h for the primary calling header

#ifndef VERILATED_VAPB_TOP___024ROOT_H_
#define VERILATED_VAPB_TOP___024ROOT_H_  // guard

#include "verilated.h"


class Vapb_top__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vapb_top___024root final {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(pclk,0,0);
    VL_IN8(presetn,0,0);
    VL_IN8(psel,0,0);
    VL_IN8(penable,0,0);
    VL_IN8(pwrite,0,0);
    VL_IN8(paddr,7,0);
    VL_IN8(pwdata,7,0);
    VL_OUT8(prdata,7,0);
    VL_OUT8(pready,0,0);
    VL_OUT8(pslverr,0,0);
    CData/*0:0*/ __Vtrigprevexpr___TOP__pclk__0;
    CData/*0:0*/ __VactPhaseResult;
    CData/*0:0*/ __VnbaPhaseResult;
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<CData/*7:0*/, 4> apb_top__DOT__u_slave__DOT__mem;
    VlUnpacked<QData/*63:0*/, 1> __VactTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VnbaTriggered;

    // INTERNAL VARIABLES
    Vapb_top__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vapb_top___024root(Vapb_top__Syms* symsp, const char* namep);
    ~Vapb_top___024root();
    VL_UNCOPYABLE(Vapb_top___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
