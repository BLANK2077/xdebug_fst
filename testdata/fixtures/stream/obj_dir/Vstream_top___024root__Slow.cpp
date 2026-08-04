// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vstream_top.h for the primary calling header

#include "Vstream_top__pch.h"

void Vstream_top___024root___ctor_var_reset(Vstream_top___024root* vlSelf);

Vstream_top___024root::Vstream_top___024root(Vstream_top__Syms* symsp, const char* namep)
 {
    vlSymsp = symsp;
    vlNamep = strdup(namep);
    // Reset structure values
    Vstream_top___024root___ctor_var_reset(this);
}

void Vstream_top___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

Vstream_top___024root::~Vstream_top___024root() {
    VL_DO_DANGLING(std::free(const_cast<char*>(vlNamep)), vlNamep);
}
