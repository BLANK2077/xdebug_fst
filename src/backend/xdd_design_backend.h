// xdd_design_backend.h — Verilator --design-db SO backend via xdd_api.h
// BSD-3-Clause License
#pragma once

#include "design_backend.h"
#include <cstdint>
#include <string>
#include <vector>

// Forward: opaque XddDb from xdd_api.h
struct XddDb;

namespace xdebug_fst {

/// Design backend backed by a Verilator-generated lib<top>__DesignDb.so.
/// Uses dlopen/dlsym to load the xdd_* C API at runtime.
class XddDesignBackend final : public IDesignBackend {
public:
    XddDesignBackend();
    ~XddDesignBackend() override;

    bool open(const std::string& so_path) override;
    void close() override;
    bool is_open() const override { return db_ != nullptr; }

    int signal_count() const override;
    int resolve(const char* name) const override;

    const char* signal_name(int idx) const override;
    const char* signal_type(int idx) const override;
    int         signal_width(int idx) const override;
    const char* signal_file(int idx) const override;
    int         signal_line(int idx) const override;
    int         signal_direction(int idx) const override;

    int trace_driver_count(int signal_idx) const override;
    int trace_driver(int signal_idx, std::vector<DriverRecord>& out) const override;

    int trace_load_count(int signal_idx) const override;
    int trace_load(int signal_idx, std::vector<LoadRecord>& out) const override;

    int port_conn_count(int signal_idx) const override;
    int port_connections(int signal_idx, std::vector<PortConnection>& out) const override;

private:
    void* so_handle_ = nullptr;       // dlopen handle
    XddDb* db_ = nullptr;             // xdd_init() result

    // Function pointers loaded via dlsym
    int   (*fn_abi_version_)() = nullptr;
    uint64_t (*fn_capabilities_)() = nullptr;
    void* (*fn_init_)() = nullptr;
    void  (*fn_close_)(void*) = nullptr;
    int   (*fn_count_)(void*) = nullptr;
    int   (*fn_resolve_)(void*, const char*) = nullptr;
    const char* (*fn_name_)(void*, int) = nullptr;
    const char* (*fn_type_)(void*, int) = nullptr;
    int   (*fn_width_)(void*, int) = nullptr;
    const char* (*fn_file_)(void*, int) = nullptr;
    int   (*fn_line_)(void*, int) = nullptr;
    int   (*fn_dir_)(void*, int) = nullptr;
    int   (*fn_drv_cnt_)(void*, int) = nullptr;
    void  (*fn_drv_)(void*, int, int, int*, const char**, const char**, int*) = nullptr;
    const char* (*fn_drv_role_)(void*, int, int) = nullptr;
    const char* (*fn_drv_predicate_)(void*, int, int) = nullptr;
    int   (*fn_ld_cnt_)(void*, int) = nullptr;
    void  (*fn_ld_)(void*, int, int, int*, const char**, const char**, int*) = nullptr;
    int   (*fn_conn_cnt_)(void*, int) = nullptr;
    void  (*fn_conn_)(void*, int, int, int*, const char**) = nullptr;

    void load_symbols();
};

} // namespace xdebug_fst
