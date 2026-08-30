// xdd_design_backend.cpp — Verilator DesignDB backend via dlopen/dlsym
// BSD-3-Clause License

#include "xdd_design_backend.h"

#include <cstdio>
#include <dlfcn.h>

namespace xdebug_fst {

XddDesignBackend::XddDesignBackend() = default;

XddDesignBackend::~XddDesignBackend() {
    close();
}

void XddDesignBackend::load_symbols() {
    if (!so_handle_) return;

    fn_abi_version_ = reinterpret_cast<decltype(fn_abi_version_)>(
        dlsym(so_handle_, "xdd_abi_version"));
    fn_capabilities_ = reinterpret_cast<decltype(fn_capabilities_)>(
        dlsym(so_handle_, "xdd_capabilities"));
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
    fn_drv_role_ = reinterpret_cast<decltype(fn_drv_role_)>(
        dlsym(so_handle_, "xdd_trace_driver_role"));
    fn_drv_predicate_ = reinterpret_cast<decltype(fn_drv_predicate_)>(
        dlsym(so_handle_, "xdd_trace_driver_predicate"));
    fn_ld_cnt_  = reinterpret_cast<decltype(fn_ld_cnt_)>(dlsym(so_handle_, "xdd_trace_load_count"));
    fn_ld_      = reinterpret_cast<decltype(fn_ld_)>(dlsym(so_handle_, "xdd_trace_load"));
    fn_conn_cnt_ = reinterpret_cast<decltype(fn_conn_cnt_)>(
        dlsym(so_handle_, "xdd_port_connection_count"));
    fn_conn_ = reinterpret_cast<decltype(fn_conn_)>(
        dlsym(so_handle_, "xdd_port_connection"));
}

bool XddDesignBackend::open(const std::string& so_path) {
    close();

    so_handle_ = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!so_handle_) {
        fprintf(stderr, "xdd_design_backend: dlopen failed: %s\n", dlerror());
        return false;
    }

    load_symbols();

    constexpr int kRequiredAbiVersion = 2;
    constexpr uint64_t kRequiredCapabilities = UINT64_C(15);
    if (!fn_abi_version_ || !fn_capabilities_) {
        fprintf(stderr,
                "xdd_design_backend: incompatible legacy bundle without ABI "
                "version/capabilities: %s\n", so_path.c_str());
        dlclose(so_handle_);
        so_handle_ = nullptr;
        return false;
    }
    const int abi_version = fn_abi_version_();
    const uint64_t capabilities = fn_capabilities_();
    if (abi_version != kRequiredAbiVersion ||
        (capabilities & kRequiredCapabilities) != kRequiredCapabilities) {
        fprintf(stderr,
                "xdd_design_backend: incompatible bundle ABI/capabilities "
                "(got version=%d capabilities=0x%llx, required version=%d "
                "capabilities=0x%llx): %s\n",
                abi_version, static_cast<unsigned long long>(capabilities),
                kRequiredAbiVersion,
                static_cast<unsigned long long>(kRequiredCapabilities),
                so_path.c_str());
        dlclose(so_handle_);
        so_handle_ = nullptr;
        return false;
    }
    if (!fn_init_ || !fn_close_ || !fn_count_ || !fn_resolve_ || !fn_name_ ||
        !fn_type_ || !fn_width_ || !fn_file_ || !fn_line_ || !fn_dir_ ||
        !fn_drv_cnt_ || !fn_drv_ || !fn_drv_role_ || !fn_drv_predicate_ ||
        !fn_ld_cnt_ || !fn_ld_ || !fn_conn_cnt_ || !fn_conn_) {
        fprintf(stderr,
                "xdd_design_backend: ABI v2 bundle is missing required symbols: %s\n",
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
    if (!fn_resolve_ || !name || !*name) return -1;
    const int exact = fn_resolve_(db_, name);
    if (exact >= 0) return exact;

    const std::string query(name);
    if (query.rfind("TOP.", 0) == 0) {
        const std::string canonical = "top." + query.substr(4);
        return fn_resolve_(db_, canonical.c_str());
    }
    if (query.rfind("top.", 0) != 0) {
        const std::string canonical = "top." + query;
        const int prefixed = fn_resolve_(db_, canonical.c_str());
        if (prefixed >= 0) return prefixed;
    } else {
        const int unprefixed = fn_resolve_(db_, query.substr(4).c_str());
        if (unprefixed >= 0) return unprefixed;
    }
    return -1;
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
    return fn_dir_ ? fn_dir_(db_, idx) : 0;
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
        const char* role = fn_drv_role_(db_, signal_idx, i);
        rec.dependency_role = role ? role : "";
        const char* predicate = fn_drv_predicate_(db_, signal_idx, i);
        rec.activation_predicate = predicate ? predicate : "";
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

// ── Native cross-hierarchy port connections ──

int XddDesignBackend::port_conn_count(int signal_idx) const {
    return fn_conn_cnt_ ? fn_conn_cnt_(db_, signal_idx) : 0;
}

int XddDesignBackend::port_connections(int signal_idx,
                                       std::vector<PortConnection>& out) const {
    out.clear();
    if (!fn_conn_cnt_ || !fn_conn_) return 0;
    const int count = fn_conn_cnt_(db_, signal_idx);
    out.reserve(count);
    for (int index = 0; index < count; ++index) {
        int connected_signal = -1;
        const char* kind = nullptr;
        fn_conn_(db_, signal_idx, index, &connected_signal, &kind);
        out.push_back({signal_idx, connected_signal, kind ? kind : ""});
    }
    return static_cast<int>(out.size());
}

} // namespace xdebug_fst
