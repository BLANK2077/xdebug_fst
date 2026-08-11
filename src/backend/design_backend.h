// design_backend.h — Abstract design database interface for xdebug-fst
// BSD-3-Clause License
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xdebug_fst {

/// Abstract interface for static design analysis (signal resolution,
/// driver/load tracing). Implementation: XddDesignBackend (Verilator --design-db).
class IDesignBackend {
public:
    virtual ~IDesignBackend() = default;

    // ── Lifecycle ──

    /// Open a design database shared library (.so).
    /// Returns true on success.
    virtual bool open(const std::string& so_path) = 0;

    /// Close and release resources.
    virtual void close() = 0;

    /// Returns true if a database is currently open.
    virtual bool is_open() const = 0;

    // ── Signal resolution ──

    /// Total number of signals in the design.
    virtual int signal_count() const = 0;

    /// Resolve a hierarchical signal name. Returns signal index, or -1.
    virtual int resolve(const char* name) const = 0;

    // ── Signal metadata ──

    virtual const char* signal_name(int idx) const = 0;
    virtual const char* signal_type(int idx) const = 0;     // "port", "reg", "wire"
    virtual int         signal_width(int idx) const = 0;
    virtual const char* signal_file(int idx) const = 0;
    virtual int         signal_line(int idx) const = 0;

    /// Signal direction: 0=unknown, 1=input, 2=output, 3=inout
    virtual int signal_direction(int idx) const = 0;

    // ── Driver tracing ──

    struct DriverRecord {
        int         src_signal = -1;   // source signal index, -1 = statement-only
        std::string kind;              // "proc_assign", "cont_assign", "nba"
        std::string dependency_role;   // data/control/statement or event_*
        std::string activation_predicate;  // empty = unavailable, fail closed
        std::string file;
        int         line = 0;
        std::string statement_identity;  // consumer-only hierarchy discriminator
        bool        has_target_loop_range = false;  // consumer-bound selector domain
        int64_t     target_loop_first = 0;
        int64_t     target_loop_last = 0;
        int         rhs_selector_signal = -1;  // consumer-only expression evidence
    };

    /// Number of driver entries for a signal.
    virtual int trace_driver_count(int signal_idx) const = 0;

    /// Get driver entries. Returns count written.
    virtual int trace_driver(int signal_idx,
                             std::vector<DriverRecord>& out) const = 0;

    // ── Load tracing ──

    struct LoadRecord {
        int         consumer = -1;     // signal that consumes this one
        std::string kind;
        std::string file;
        int         line = 0;
    };

    /// Number of load entries for a signal.
    virtual int trace_load_count(int signal_idx) const = 0;

    /// Get load entries.
    virtual int trace_load(int signal_idx,
                           std::vector<LoadRecord>& out) const = 0;

    // ── Port connections ──

    struct PortConnection {
        int         port_signal = -1;
        int         connected_signal = -1;
        std::string kind;  // "port_boundary", "modport_port"
    };

    /// Number of port connection entries for a signal.
    virtual int port_conn_count(int signal_idx) const = 0;

    /// Get port connections.
    virtual int port_connections(int signal_idx,
                                 std::vector<PortConnection>& out) const = 0;
};

} // namespace xdebug_fst
