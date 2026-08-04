// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vaxi_top.h for the primary calling header

#include "Vaxi_top__pch.h"

void Vaxi_top___024root___ctor_var_reset(Vaxi_top___024root* vlSelf);

Vaxi_top___024root::Vaxi_top___024root(Vaxi_top__Syms* symsp, const char* namep)
 {
    vlSymsp = symsp;
    vlNamep = strdup(namep);
    // Reset structure values
    Vaxi_top___024root___ctor_var_reset(this);
}

void Vaxi_top___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

Vaxi_top___024root::~Vaxi_top___024root() {
    VL_DO_DANGLING(std::free(const_cast<char*>(vlNamep)), vlNamep);
}
