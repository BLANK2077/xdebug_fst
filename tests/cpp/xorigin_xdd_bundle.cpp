// xorigin_xdd_bundle.cpp — deterministic XDD facts for the Wellen GCD FST
// BSD-3-Clause License

#include "xdd_api.h"

#include <cstring>

namespace {

#if defined(XDEBUG_TEST_PRIMITIVE_OUTPUT)

const XddSignalInfo kSignals[] = {
    {"GCD.T_14", "wire", 33, "primitive_output.sv", 4},
    {"GCD.y", "port", 32, "primitive_output.sv", 2},
};

const int kDirections[] = {0, 1};

const XddDriverRec kDrivers[] = {
    {0, 1, "primitive", "rhs", "primitive_output.sv", 4},
};

const int kDriverStart[] = {0, 1};

const XddLoadRec kLoads[] = {
    {1, 0, "rhs_use", "primitive_output.sv", 4},
};

const int kLoadStart[] = {0, 0};

constexpr int kSignalCount = 2;
constexpr int kDriverCount = 1;
constexpr int kLoadCount = 1;

#elif defined(XDEBUG_TEST_X_LOOP_BRANCH)

const XddSignalInfo kSignals[] = {
    {"GCD.T_14", "wire", 33, "xorigin_loop_branch.sv", 2},
    {"GCD.GEN_0", "wire", 32, "xorigin_loop_branch.sv", 3},
    {"GCD.y", "port", 32, "xorigin_loop_branch.sv", 1},
};

const int kDirections[] = {0, 0, 1};

const XddDriverRec kDrivers[] = {
    {0, 1, "cont_assign", "rhs", "xorigin_loop_branch.sv", 2},
    {1, 0, "cont_assign", "rhs", "xorigin_loop_branch.sv", 3},
    {1, 2, "cont_assign", "rhs", "xorigin_loop_branch.sv", 3},
};

const int kDriverStart[] = {0, 1, 3};

const XddLoadRec kLoads[] = {
    {0, 1, "rhs_use", "xorigin_loop_branch.sv", 3},
    {1, 0, "rhs_use", "xorigin_loop_branch.sv", 2},
    {2, 1, "rhs_use", "xorigin_loop_branch.sv", 3},
};

const int kLoadStart[] = {0, 1, 2};

constexpr int kSignalCount = 3;
constexpr int kDriverCount = 3;
constexpr int kLoadCount = 3;

#elif defined(XDEBUG_TEST_X_LOOP)

const XddSignalInfo kSignals[] = {
    {"GCD.T_14", "wire", 33, "xorigin_loop.sv", 2},
    {"GCD.GEN_0", "wire", 32, "xorigin_loop.sv", 3},
};

const int kDirections[] = {0, 0};

const XddDriverRec kDrivers[] = {
    {0, 1, "cont_assign", "rhs", "xorigin_loop.sv", 2},
    {1, 0, "cont_assign", "rhs", "xorigin_loop.sv", 3},
};

const int kDriverStart[] = {0, 1};

const XddLoadRec kLoads[] = {
    {0, 1, "rhs_use", "xorigin_loop.sv", 3},
    {1, 0, "rhs_use", "xorigin_loop.sv", 2},
};

const int kLoadStart[] = {0, 1};

constexpr int kSignalCount = 2;
constexpr int kDriverCount = 2;
constexpr int kLoadCount = 2;

#elif defined(XDEBUG_TEST_ALIAS_COALESCE) || defined(XDEBUG_TEST_MODPORT_ALIAS)

const XddSignalInfo kSignals[] = {
    {"GCD.T_14", "wire", 33, "xorigin_alias.sv", 2},
    {"GCD.GEN_0", "wire", 32, "xorigin_alias.sv", 3},
    {"GCD.GEN_1", "wire", 32, "xorigin_alias.sv", 4},
    {"GCD.x", "wire", 32, "xorigin_alias.sv", 5},
    {"GCD.io_a", "port", 32, "xorigin_alias.sv", 1},
    {"GCD.y", "port", 32, "xorigin_alias.sv", 1},
};

const int kDirections[] = {0, 0, 0, 0, 1, 1};

const XddDriverRec kDrivers[] = {
    {3, 4, "cont_assign", "rhs", "xorigin_alias.sv", 5},
    {3, 5, "cont_assign", "rhs", "xorigin_alias.sv", 5},
};

const int kDriverStart[] = {0, 0, 0, 0, 2, 2};

const XddLoadRec kLoads[] = {
    {4, 3, "rhs_use", "xorigin_alias.sv", 5},
    {5, 3, "rhs_use", "xorigin_alias.sv", 5},
};

const int kLoadStart[] = {0, 0, 0, 0, 0, 1};

constexpr int kSignalCount = 6;
constexpr int kDriverCount = 2;
constexpr int kLoadCount = 2;

#elif defined(XDEBUG_TEST_X_TIME_LIMIT)

const XddSignalInfo kSignals[] = {
    {"AXI_top_tb_from_compiled.dut.a_regex_coprocessor.genblk1.a_topology.genblk1[0].genblk1[0].engine_and_station_i.anEngine.anEngine.g.aregex_cpu.EXE2_Instr",
     "wire", 16, "xorigin_time_limit.sv", 2},
    {"AXI_top_tb_from_compiled.dut.a_regex_coprocessor.genblk1.a_topology.genblk1[0].genblk1[0].engine_and_station_i.anEngine.anEngine.g.aregex_cpu.current_character",
     "port", 8, "xorigin_time_limit.sv", 1},
    {"AXI_top_tb_from_compiled.dut.a_regex_coprocessor.genblk1.a_topology.genblk1[0].genblk1[0].engine_and_station_i.anEngine.memory.in.data",
     "wire", 64, "xorigin_time_limit.sv", 3},
};

const int kDirections[] = {0, 1, 0};

const XddDriverRec kDrivers[] = {
    {0, 1, "cont_assign", "rhs", "xorigin_time_limit.sv", 2},
};

const int kDriverStart[] = {0, 1, 1};

const XddLoadRec kLoads[] = {
    {1, 0, "rhs_use", "xorigin_time_limit.sv", 2},
};

const int kLoadStart[] = {0, 0, 1};

constexpr int kSignalCount = 3;
constexpr int kDriverCount = 1;
constexpr int kLoadCount = 1;

#else

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

#endif

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

int xdd_port_connection_count(XddDb*, int index) {
#if defined(XDEBUG_TEST_ALIAS_COALESCE) || defined(XDEBUG_TEST_MODPORT_ALIAS)
    return index >= 0 && index <= 3 ? 2 : 0;
#else
    (void)index;
    return 0;
#endif
}

void xdd_port_connection(XddDb*, int index, int offset, int* connected,
                         const char** kind) {
    *connected = -1;
    *kind = nullptr;
#if defined(XDEBUG_TEST_ALIAS_COALESCE) || defined(XDEBUG_TEST_MODPORT_ALIAS)
    static const int kConnections[4][2] = {
        {1, 2}, {0, 3}, {0, 3}, {1, 2},
    };
    if (index >= 0 && index <= 3 && offset >= 0 && offset < 2) {
        *connected = kConnections[index][offset];
#if defined(XDEBUG_TEST_MODPORT_ALIAS)
        *kind = "interface_modport_member";
#else
        *kind = "module_port";
#endif
    }
#else
    (void)index;
    (void)offset;
#endif
}

}  // extern "C"
