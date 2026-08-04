// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vapb_top.h for the primary calling header

#include "Vapb_top__pch.h"

void Vapb_top___024root___ctor_var_reset(Vapb_top___024root* vlSelf);

Vapb_top___024root::Vapb_top___024root(Vapb_top__Syms* symsp, const char* namep)
 {
    vlSymsp = symsp;
    vlNamep = strdup(namep);
    // Reset structure values
    Vapb_top___024root___ctor_var_reset(this);
}

void Vapb_top___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

Vapb_top___024root::~Vapb_top___024root() {
    VL_DO_DANGLING(std::free(const_cast<char*>(vlNamep)), vlNamep);
}
