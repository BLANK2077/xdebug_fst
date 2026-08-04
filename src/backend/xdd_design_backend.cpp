// xdd_design_backend.cpp — Verilator DesignDB backend via dlopen/dlsym
// BSD-3-Clause License

#include "xdd_design_backend.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace xdebug_fst {

XddDesignBackend::XddDesignBackend() = default;

XddDesignBackend::~XddDesignBackend() {
    close();
}

void XddDesignBackend::load_symbols() {
    if (!so_handle_) return;

    fn_init_    = reinterpret_cast<decltype(fn_init_)>(dlsym(so_handle_, "xdd_init"));
    fn_close_   = reinterpret_cast<decltype(fn_close_)>(dlsym(so_handle_, "xdd_close"));
    fn_count_   = reinterpret_cast<decltype(fn_count_)>(dlsym(so_handle_, "xdd_signal_count"));
    fn_resolve_ = reinterpret_cast<decltype(fn_resolve_)>(dlsym(so_handle_, "xdd_resolve"));
    fn_name_    = reinterpret_cast<decltype(fn_name_)>(dlsym(so_handle_, "xdd_signal_name"));
    fn_type_    = reinterpret_cast<decltype(fn_type_)>(dlsym(so_handle_, "xdd_signal_type"));
    fn_width_   = reinterpret_cast<decltype(fn_width_)>(dlsym(so_handle_, "xdd_signal_width"));
    fn_file_    = reinterpret_cast<decltype(fn_file_)>(dlsym(so_handle_, "xdd_signal_file"));
    fn_line_    = reinterpret_cast<decltype(fn_line_)>(dlsym(so_handle_, "xdd_signal_line"));

    // Optional symbols (may not exist in older DesignDB)
    fn_dir_     = reinterpret_cast<decltype(fn_dir_)>(dlsym(so_handle_, "xdd_signal_direction"));
    fn_drv_cnt_ = reinterpret_cast<decltype(fn_drv_cnt_)>(dlsym(so_handle_, "xdd_trace_driver_count"));
    fn_drv_     = reinterpret_cast<decltype(fn_drv_)>(dlsym(so_handle_, "xdd_trace_driver"));
    fn_ld_cnt_  = reinterpret_cast<decltype(fn_ld_cnt_)>(dlsym(so_handle_, "xdd_trace_load_count"));
    fn_ld_      = reinterpret_cast<decltype(fn_ld_)>(dlsym(so_handle_, "xdd_trace_load"));
}

bool XddDesignBackend::open(const std::string& so_path) {
    close();

    so_handle_ = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!so_handle_) {
        fprintf(stderr, "xdd_design_backend: dlopen failed: %s\n", dlerror());
        return false;
    }

    load_symbols();

    if (!fn_init_ || !fn_resolve_) {
        fprintf(stderr, "xdd_design_backend: missing required symbols in %s\n",
                so_path.c_str());
        dlclose(so_handle_);
        so_handle_ = nullptr;
        return false;
    }

    db_ = reinterpret_cast<XddDb*>(fn_init_());
    if (!db_) {
        fprintf(stderr, "xdd_design_backend: xdd_init failed\n");
        dlclose(so_handle_);
        so_handle_ = nullptr;
        return false;
    }
    return true;
}

void XddDesignBackend::close() {
    if (db_ && fn_close_) {
        fn_close_(db_);
        db_ = nullptr;
    }
    if (so_handle_) {
        dlclose(so_handle_);
        so_handle_ = nullptr;
    }
}

// ── Signal resolution ──

int XddDesignBackend::signal_count() const {
    return fn_count_ ? fn_count_(db_) : 0;
}

int XddDesignBackend::resolve(const char* name) const {
    return fn_resolve_ ? fn_resolve_(db_, name) : -1;
}

// ── Metadata ──

const char* XddDesignBackend::signal_name(int idx) const {
    return fn_name_ ? fn_name_(db_, idx) : nullptr;
}

const char* XddDesignBackend::signal_type(int idx) const {
    return fn_type_ ? fn_type_(db_, idx) : nullptr;
}

int XddDesignBackend::signal_width(int idx) const {
    return fn_width_ ? fn_width_(db_, idx) : 0;
}

const char* XddDesignBackend::signal_file(int idx) const {
    return fn_file_ ? fn_file_(db_, idx) : nullptr;
}

int XddDesignBackend::signal_line(int idx) const {
    return fn_line_ ? fn_line_(db_, idx) : 0;
}

int XddDesignBackend::signal_direction(int idx) const {
    // Native symbol first, then inferred (see inference below)
    return signal_direction_inferred(idx);
}

// ── Driver tracing ──

int XddDesignBackend::trace_driver_count(int signal_idx) const {
    return fn_drv_cnt_ ? fn_drv_cnt_(db_, signal_idx) : 0;
}

int XddDesignBackend::trace_driver(int signal_idx,
                                   std::vector<DriverRecord>& out) const {
    if (!fn_drv_cnt_ || !fn_drv_) return 0;
    int n = fn_drv_cnt_(db_, signal_idx);
    out.clear();
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        int src = -1;
        const char* kind = nullptr;
        const char* file = nullptr;
        int line = 0;
        fn_drv_(db_, signal_idx, i, &src, &kind, &file, &line);
        DriverRecord rec;
        rec.src_signal = src;
        rec.kind = kind ? kind : "";
        rec.file = file ? file : "";
        rec.line = line;
        out.push_back(rec);
    }
    return n;
}

// ── Load tracing ──

int XddDesignBackend::trace_load_count(int signal_idx) const {
    return fn_ld_cnt_ ? fn_ld_cnt_(db_, signal_idx) : 0;
}

int XddDesignBackend::trace_load(int signal_idx,
                                 std::vector<LoadRecord>& out) const {
    if (!fn_ld_cnt_ || !fn_ld_) return 0;
    int n = fn_ld_cnt_(db_, signal_idx);
    out.clear();
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        int consumer = -1;
        const char* kind = nullptr;
        const char* file = nullptr;
        int line = 0;
        fn_ld_(db_, signal_idx, i, &consumer, &kind, &file, &line);
        LoadRecord rec;
        rec.consumer = consumer;
        rec.kind = kind ? kind : "";
        rec.file = file ? file : "";
        rec.line = line;
        out.push_back(rec);
    }
    return n;
}

// ── Direction inference (Phase 3) ──
//
// The Verilator --design-db .so does not (yet) export xdd_signal_direction.
// We infer port direction from the driver/load tables:
//   * a port that drives an internal wire via cont_assign (i.e. it appears as
//     the src of a cont_assign whose target is an internal signal) is an
//     input (value flows into the design)
//   * a port whose driver table contains a non-cont_assign entry (nba /
//     proc_assign) or a src from an internal signal is an output (value flows
//     out of the design)
//   * both → inout; neither → unknown (keep the native symbol when present)

static bool is_port_type(const char* type) {
    return type && std::strcmp(type, "port") == 0;
}

int XddDesignBackend::signal_direction_inferred(int idx) const {
    // Prefer a native symbol when the .so provides it
    if (fn_dir_) {
        int d = fn_dir_(db_, idx);
        if (d != 0) return d;
    }
    if (!is_port_type(signal_type(idx))) return 0;

    // Output feature: the port is driven by internal sequential/process
    // logic (nba / proc_assign records in its own driver table).
    bool has_proc_driver = false;
    std::vector<DriverRecord> drivers;
    trace_driver(idx, drivers);
    for (const auto& d : drivers) {
        if (d.kind == "nba" || d.kind == "proc_assign") has_proc_driver = true;
    }

    // Input feature: the port drives an internal wire via cont_assign
    // (value flows from the port into the design).
    bool drives_internal = false;
    int n = signal_count();
    for (int i = 0; i < n; ++i) {
        if (i == idx) continue;
        std::vector<DriverRecord> ds;
        trace_driver(i, ds);
        for (const auto& d : ds) {
            if (d.src_signal == idx && d.kind == "cont_assign" &&
                !is_port_type(signal_type(i))) {
                drives_internal = true;
            }
        }
    }

    if (has_proc_driver) return 2;                     // output (aliasing cont_assign entries are the same net)
    if (drives_internal) return 1;                     // input
    return 0;                                          // unknown
}

// ── Port connections (Phase 3) ──
//
// The .so does not export port connections yet; infer them from the driver
// table: a cont_assign entry linking a port to an internal signal is a port
// boundary connection.

int XddDesignBackend::port_conn_count(int signal_idx) const {
    return port_connections(signal_idx, conn_cache_);
}

int XddDesignBackend::port_connections(int signal_idx,
                                       std::vector<PortConnection>& out) const {
    out.clear();
    if (!is_port_type(signal_type(signal_idx))) return 0;

    // 1. this port drives internal wires (input connections)
    int n = signal_count();
    for (int i = 0; i < n; ++i) {
        std::vector<DriverRecord> ds;
        trace_driver(i, ds);
        for (const auto& d : ds) {
            if (d.src_signal == signal_idx && d.kind == "cont_assign" &&
                !is_port_type(signal_type(i))) {
                out.push_back({signal_idx, i, "port_boundary"});
            }
        }
    }
    // 2. internal signals drive this port (output connections)
    std::vector<DriverRecord> drivers;
    trace_driver(signal_idx, drivers);
    for (const auto& d : drivers) {
        if (d.src_signal >= 0 && d.kind == "cont_assign" &&
            !is_port_type(signal_type(d.src_signal))) {
            out.push_back({signal_idx, d.src_signal, "port_boundary"});
        }
    }
    return static_cast<int>(out.size());
}

} // namespace xdebug_fst
