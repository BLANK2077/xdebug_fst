// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See Vcounter_top.h for the primary calling header

#include "Vcounter_top__pch.h"

void Vcounter_top___024root___ctor_var_reset(Vcounter_top___024root* vlSelf);

Vcounter_top___024root::Vcounter_top___024root(Vcounter_top__Syms* symsp, const char* namep)
 {
    vlSymsp = symsp;
    vlNamep = strdup(namep);
    // Reset structure values
    Vcounter_top___024root___ctor_var_reset(this);
}

void Vcounter_top___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

Vcounter_top___024root::~Vcounter_top___024root() {
    VL_DO_DANGLING(std::free(const_cast<char*>(vlNamep)), vlNamep);
}
