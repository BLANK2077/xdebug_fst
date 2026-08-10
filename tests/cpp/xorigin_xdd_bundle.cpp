// xorigin_xdd_bundle.cpp — deterministic XDD facts for the Wellen GCD FST
// BSD-3-Clause License

#include "xdd_api.h"

#include <cstring>

namespace {

const XddSignalInfo kSignals[] = {
    {"GCD.io_z", "port", 32, "gcd_xorigin.sv", 7},
    {"GCD.x", "wire", 32, "gcd_xorigin.sv", 5},
    {"GCD.io_a", "port", 32, "gcd_xorigin.sv", 2},
    {"GCD.GEN_0", "wire", 32, "gcd_xorigin.sv", 9},
    {"GCD.y", "wire", 32, "gcd_xorigin.sv", 5},
    {"GCD.GEN_1", "wire", 32, "gcd_xorigin.sv", 11},
    {"GCD.T_14", "wire", 33, "gcd_xorigin.sv", 14},
    {"GCD.T_13", "wire", 1, "gcd_xorigin.sv", 13},
};

const int kDirections[] = {2, 0, 1, 0, 0, 0, 0, 0};

const XddDriverRec kDrivers[] = {
    {0, 1, "cont_assign", "rhs", "gcd_xorigin.sv", 7},
    {1, 2, "cont_assign", "rhs", "gcd_xorigin.sv", 6},
    {3, 1, "cont_assign", "rhs", "gcd_xorigin.sv", 9},
    {3, 4, "cont_assign", "rhs", "gcd_xorigin.sv", 9},
    {4, 2, "force", "rhs", "gcd_xorigin.sv", 10},
    {5, 1, "cont_assign", "rhs", "gcd_xorigin.sv", 10},
    {5, 4, "cont_assign", "rhs", "gcd_xorigin.sv", 11},
    {6, 1, "cont_assign", "rhs", "gcd_xorigin.sv", 14},
    {6, 4, "cont_assign", "control", "gcd_xorigin.sv", 14},
};

const int kDriverStart[] = {0, 1, 2, 2, 4, 5, 7, 9};

const XddLoadRec kLoads[] = {
    {1, 0, "rhs_use", "gcd_xorigin.sv", 7},
    {1, 3, "rhs_use", "gcd_xorigin.sv", 9},
    {1, 5, "rhs_use", "gcd_xorigin.sv", 10},
    {1, 6, "rhs_use", "gcd_xorigin.sv", 14},
    {2, 1, "rhs_use", "gcd_xorigin.sv", 6},
    {2, 4, "rhs_use", "gcd_xorigin.sv", 10},
    {4, 3, "rhs_use", "gcd_xorigin.sv", 9},
    {4, 5, "rhs_use", "gcd_xorigin.sv", 11},
    {4, 6, "control_use", "gcd_xorigin.sv", 14},
};

const int kLoadStart[] = {0, 0, 4, 6, 6, 9, 9, 9};

constexpr int kSignalCount = 8;
constexpr int kDriverCount = 9;
constexpr int kLoadCount = 9;

}  // namespace

extern "C" {

int xdd_abi_version() { return XDD_ABI_VERSION; }

uint64_t xdd_capabilities() {
    return XDD_CAP_SIGNAL_DIRECTION | XDD_CAP_PORT_CONNECTIONS
           | XDD_CAP_DRIVER_DEPENDENCY_ROLE | XDD_CAP_DRIVER_PREDICATE;
}

XddDb* xdd_init() { return reinterpret_cast<XddDb*>(1); }
void xdd_close(XddDb*) {}
int xdd_signal_count(XddDb*) { return kSignalCount; }

XddSignal xdd_resolve(XddDb*, const char* name) {
    for (int index = 0; index < kSignalCount; ++index) {
        if (std::strcmp(name, kSignals[index].name) == 0) return index;
    }
    return -1;
}

const char* xdd_signal_name(XddDb*, int index) {
    return index >= 0 && index < kSignalCount ? kSignals[index].name : nullptr;
}

const char* xdd_signal_type(XddDb*, int index) {
    return index >= 0 && index < kSignalCount ? kSignals[index].type : nullptr;
}

int xdd_signal_width(XddDb*, int index) {
    return index >= 0 && index < kSignalCount ? kSignals[index].width : 0;
}

const char* xdd_signal_file(XddDb*, int index) {
    return index >= 0 && index < kSignalCount ? kSignals[index].file : nullptr;
}

int xdd_signal_line(XddDb*, int index) {
    return index >= 0 && index < kSignalCount ? kSignals[index].line : 0;
}

int xdd_signal_direction(XddDb*, int index) {
    return index >= 0 && index < kSignalCount ? kDirections[index] : 0;
}

int xdd_trace_driver_count(XddDb*, int index) {
    if (index < 0 || index >= kSignalCount) return 0;
    const int end = index + 1 < kSignalCount ? kDriverStart[index + 1]
                                             : kDriverCount;
    return end - kDriverStart[index];
}

void xdd_trace_driver(XddDb*, int index, int offset, int* source,
                      const char** kind, const char** file, int* line) {
    *source = -1;
    *kind = nullptr;
    *file = nullptr;
    *line = 0;
    if (index < 0 || index >= kSignalCount) return;
    const int start = kDriverStart[index];
    const int end = index + 1 < kSignalCount ? kDriverStart[index + 1]
                                             : kDriverCount;
    if (offset < 0 || offset >= end - start) return;
    const XddDriverRec& driver = kDrivers[start + offset];
    *source = driver.src_signal;
    *kind = driver.kind;
    *file = driver.file;
    *line = driver.line;
}

const char* xdd_trace_driver_role(XddDb*, int index, int offset) {
    if (index < 0 || index >= kSignalCount) return nullptr;
    const int start = kDriverStart[index];
    const int end = index + 1 < kSignalCount ? kDriverStart[index + 1]
                                             : kDriverCount;
    if (offset < 0 || offset >= end - start) return nullptr;
    return kDrivers[start + offset].role;
}

const char* xdd_trace_driver_predicate(XddDb*, int index, int offset) {
    if (index < 0 || index >= kSignalCount) return nullptr;
    const int start = kDriverStart[index];
    const int end = index + 1 < kSignalCount ? kDriverStart[index + 1]
                                             : kDriverCount;
    if (offset < 0 || offset >= end - start) return nullptr;
#if defined(XDEBUG_TEST_MISSING_PREDICATE)
    if (index == 6) return "GCD.not_in_fst";
#elif defined(XDEBUG_TEST_X_PREDICATE)
    if (index == 6) return "GCD.y";
#endif
    return "1";
}

int xdd_trace_load_count(XddDb*, int index) {
    if (index < 0 || index >= kSignalCount) return 0;
    const int end = index + 1 < kSignalCount ? kLoadStart[index + 1]
                                             : kLoadCount;
    return end - kLoadStart[index];
}

void xdd_trace_load(XddDb*, int index, int offset, int* consumer,
                    const char** kind, const char** file, int* line) {
    *consumer = -1;
    *kind = nullptr;
    *file = nullptr;
    *line = 0;
    if (index < 0 || index >= kSignalCount) return;
    const int start = kLoadStart[index];
    const int end = index + 1 < kSignalCount ? kLoadStart[index + 1]
                                             : kLoadCount;
    if (offset < 0 || offset >= end - start) return;
    const XddLoadRec& load = kLoads[start + offset];
    *consumer = load.consumer;
    *kind = load.kind;
    *file = load.file;
    *line = load.line;
}

int xdd_port_connection_count(XddDb*, int) { return 0; }

void xdd_port_connection(XddDb*, int, int, int* connected,
                         const char** kind) {
    *connected = -1;
    *kind = nullptr;
}

}  // extern "C"
