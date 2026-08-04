// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Symbol table internal header
//
// Internal details; most calling programs do not need this header,
// unless using verilator public meta comments.

#ifndef VERILATED_VCOUNTER_TOP__SYMS_H_
#define VERILATED_VCOUNTER_TOP__SYMS_H_  // guard

#include "verilated.h"

// INCLUDE MODEL CLASS

#include "Vcounter_top.h"

// INCLUDE MODULE CLASSES
#include "Vcounter_top___024root.h"

// SYMS CLASS (contains all model state)
class alignas(VL_CACHE_LINE_BYTES) Vcounter_top__Syms final : public VerilatedSyms {
  public:
    // INTERNAL STATE
    Vcounter_top* const __Vm_modelp;
    bool __Vm_activity = false;  ///< Used by trace routines to determine change occurred
    uint32_t __Vm_baseCode = 0;  ///< Used by trace routines when tracing multiple models
    VlDeleter __Vm_deleter;
    bool __Vm_didInit = false;

    // MODULE INSTANCE STATE
    Vcounter_top___024root         TOP;

    // CONSTRUCTORS
    Vcounter_top__Syms(VerilatedContext* contextp, const char* namep, Vcounter_top* modelp);
    ~Vcounter_top__Syms();

    // METHODS
    const char* name() const { return TOP.vlNamep; }
};

#endif  // guard
