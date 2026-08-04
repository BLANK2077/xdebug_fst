// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See Vaxi_top.h for the primary calling header

#ifndef VERILATED_VAXI_TOP___024ROOT_H_
#define VERILATED_VAXI_TOP___024ROOT_H_  // guard

#include "verilated.h"


class Vaxi_top__Syms;

class alignas(VL_CACHE_LINE_BYTES) Vaxi_top___024root final {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(aclk,0,0);
    VL_IN8(aresetn,0,0);
    VL_IN8(awid,3,0);
    VL_IN8(awaddr,7,0);
    VL_IN8(awlen,7,0);
    VL_IN8(awvalid,0,0);
    VL_OUT8(awready,0,0);
    VL_IN8(wstrb,3,0);
    VL_IN8(wlast,0,0);
    VL_IN8(wvalid,0,0);
    VL_OUT8(wready,0,0);
    VL_OUT8(bid,3,0);
    VL_OUT8(bresp,1,0);
    VL_OUT8(bvalid,0,0);
    VL_IN8(bready,0,0);
    VL_IN8(arid,3,0);
    VL_IN8(araddr,7,0);
    VL_IN8(arlen,7,0);
    VL_IN8(arvalid,0,0);
    VL_OUT8(arready,0,0);
    VL_OUT8(rid,3,0);
    VL_OUT8(rresp,1,0);
    VL_OUT8(rlast,0,0);
    VL_OUT8(rvalid,0,0);
    VL_IN8(rready,0,0);
    CData/*3:0*/ axi_top__DOT__u_slave__DOT__awid_q;
    CData/*7:0*/ axi_top__DOT__u_slave__DOT__awaddr_q;
    CData/*7:0*/ axi_top__DOT__u_slave__DOT__awlen_q;
    CData/*7:0*/ axi_top__DOT__u_slave__DOT__wcnt;
    CData/*3:0*/ axi_top__DOT__u_slave__DOT__arid_q;
    CData/*7:0*/ axi_top__DOT__u_slave__DOT__araddr_q;
    CData/*7:0*/ axi_top__DOT__u_slave__DOT__arlen_q;
    CData/*7:0*/ axi_top__DOT__u_slave__DOT__rcnt;
    CData/*0:0*/ axi_top__DOT__u_slave__DOT__ar_pending;
    CData/*0:0*/ __VstlFirstIteration;
    CData/*0:0*/ __VstlPhaseResult;
    CData/*0:0*/ __Vtrigprevexpr___TOP__aclk__0;
    CData/*0:0*/ __VactPhaseResult;
    CData/*0:0*/ __VnbaPhaseResult;
    VL_IN(wdata,31,0);
    VL_OUT(rdata,31,0);
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<IData/*31:0*/, 256> axi_top__DOT__u_slave__DOT__mem;
    VlUnpacked<QData/*63:0*/, 1> __VstlTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VactTriggered;
    VlUnpacked<QData/*63:0*/, 1> __VnbaTriggered;
    VlUnpacked<CData/*0:0*/, 2> __Vm_traceActivity;

    // INTERNAL VARIABLES
    Vaxi_top__Syms* vlSymsp;
    const char* vlNamep;

    // CONSTRUCTORS
    Vaxi_top___024root(Vaxi_top__Syms* symsp, const char* namep);
    ~Vaxi_top___024root();
    VL_UNCOPYABLE(Vaxi_top___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
