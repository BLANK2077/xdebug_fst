// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vstream_top.h for the primary calling header

#ifndef VERILATED_VSTREAM_TOP___024ROOT_H_
#define VERILATED_VSTREAM_TOP___024ROOT_H_  // guard

#include "verilated.h"


class Vstream_top__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vstream_top___024root final {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(clk,0,0);
    VL_IN8(reset,0,0);
    VL_IN8(in_valid,0,0);
    VL_OUT8(in_ready,0,0);
    VL_IN8(in_data,7,0);
    VL_OUT8(out_valid,0,0);
    VL_IN8(out_ready,0,0);
    VL_OUT8(out_data,7,0);
    CData/*1:0*/ stream_top__DOT__head;
    CData/*1:0*/ stream_top__DOT__tail;
    CData/*2:0*/ stream_top__DOT__count;
    CData/*0:0*/ __VstlFirstIteration;
    CData/*0:0*/ __VstlPhaseResult;
    CData/*0:0*/ __Vtrigprevexpr___TOP__clk__0;
    CData/*0:0*/ __VactPhaseResult;
    CData/*0:0*/ __VnbaPhaseResult;
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<CData/*7:0*/, 4> stream_top__DOT__fifo;
    VlUnpacked<QData/*63:0*/, 1> __VstlTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VactTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VnbaTriggered;
    VlUnpacked<CData/*0:0*/, 2> __Vm_traceActivity;

    // INTERNAL VARIABLES
    Vstream_top__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vstream_top___024root(Vstream_top__Syms* symsp, const char* namep);
    ~Vstream_top___024root();
    VL_UNCOPYABLE(Vstream_top___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
