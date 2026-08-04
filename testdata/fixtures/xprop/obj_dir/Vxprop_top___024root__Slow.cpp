// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vxprop_top.h for the primary calling header

#include "Vxprop_top__pch.h"

void Vxprop_top___024root___ctor_var_reset(Vxprop_top___024root* vlSelf);

Vxprop_top___024root::Vxprop_top___024root(Vxprop_top__Syms* symsp, const char* namep)
 {
    vlSymsp = symsp;
    vlNamep = strdup(namep);
    // Reset structure values
    Vxprop_top___024root___ctor_var_reset(this);
}

void Vxprop_top___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

Vxprop_top___024root::~Vxprop_top___024root() {
    VL_DO_DANGLING(std::free(const_cast<char*>(vlNamep)), vlNamep);
}
