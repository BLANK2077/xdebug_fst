// waveform_backend.h — Abstract waveform backend interface for xdebug-fst
// BSD-3-Clause License
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "waveform/time_contract.h"

namespace xdebug_fst {

/// Abstract interface for waveform data access.
/// Implementations: WellenFstBackend (wellen_capi), NpiFsdbBackend (future).
class IWaveformBackend {
public:
    /// Sentinel returned by find_signal() when a signal is not found.
    /// The C ABI exposes native Wellen SignalRef(N) as N+1, reserving zero.
    static constexpr uint32_t kInvalidSignalRef = 0;

    virtual ~IWaveformBackend() = default;

    // ── Lifecycle ──

    /// Open a waveform file. Returns true on success.
    virtual bool open(const std::string& path) = 0;

    /// Close the file and release all resources.
    virtual void close() = 0;

    /// Returns true if a file is currently open.
    virtual bool is_open() const = 0;

    // ── Time table ──

    /// Number of distinct time points.
    virtual uint32_t time_count() const = 0;

    /// Get the time value at a given index.
    virtual uint64_t time_at(uint32_t idx) const = 0;

    /// Get the minimum time.
    virtual uint64_t min_time() const = 0;

    /// Get the maximum time.
    virtual uint64_t max_time() const = 0;

    /// Tick scale read from the waveform header.
    virtual bool time_scale(WaveformTimeScale& out) const = 0;

    /// Strict physical-time parsing. Unitless input defaults to ns.
    virtual bool parse_time(const std::string& text, uint64_t& ticks,
                            std::string& error,
                            bool allow_max = false) const = 0;

    /// Binary search: find the largest time_idx such that time_at(idx) <= t.
    /// Returns the index, or 0 if t < min_time().
    virtual uint32_t time_idx_of(uint64_t t) const = 0;

    /// Format a time value for display in ns, ps, us, or auto.
    virtual std::string format_time(
        uint64_t t, TimeRenderUnit unit = TimeRenderUnit::Ns) const = 0;

    // ── Hierarchy ──

    /// Number of scopes in the design (all scopes, not just roots).
    virtual uint32_t scope_count() const = 0;

    /// Get scope handle at linear index (0-based). Returns 0 if invalid.
    virtual uint32_t scope_at(uint32_t idx) const = 0;

    /// Number of top-level scopes and their handles.
    virtual uint32_t root_scope_count() const = 0;
    virtual uint32_t root_scope_at(uint32_t idx) const = 0;

    /// Number of child scopes under a given scope.
    virtual uint32_t scope_child_count(uint32_t scope_ref) const = 0;

    /// Get child scope handle at index. Returns 0 if invalid.
    virtual uint32_t scope_child_at(uint32_t scope_ref, uint32_t idx) const = 0;

    /// Number of variables in a scope.
    virtual uint32_t scope_var_count(uint32_t scope_ref) const = 0;

    /// Get variable handle at index in a scope. Returns 0 if invalid.
    virtual uint32_t scope_var_at(uint32_t scope_ref, uint32_t idx) const = 0;

    /// Get scope name (C string from interned data, valid for backend lifetime).
    virtual const char* scope_name(uint32_t scope_ref) = 0;

    /// Get full hierarchical scope name.
    virtual const char* scope_full_name(uint32_t scope_ref) = 0;

    /// Get variable local name.
    virtual const char* var_name(uint32_t var_ref) = 0;

    /// Get variable full hierarchical name.
    virtual const char* var_full_name(uint32_t var_ref) = 0;

    /// Get the 1-based C signal_ref for a variable. Returns 0 if absent.
    virtual uint32_t var_signal_ref(uint32_t var_ref) const = 0;

    /// Find a signal by hierarchical path (case-insensitive, "TOP." prefix
    /// tolerant). Returns kInvalidSignalRef if not found.
    virtual uint32_t find_signal(const std::string& path) const = 0;

    /// Get signal encoding and width for a variable.
    /// encoding: 0=bitvector, 1=real, 2=string, 3=event
    virtual int var_encoding(uint32_t var_ref, uint32_t* out_width) const = 0;

    // ── Signal loading ──

    /// Load signals by ref. Returns the number successfully loaded.
    virtual int load_signals(const std::vector<uint32_t>& refs) = 0;

    /// Unload signals to free memory.
    virtual void unload_signals(const std::vector<uint32_t>& refs) = 0;

    /// Query whether a signal is loaded.
    virtual bool is_loaded(uint32_t signal_ref) const = 0;

    /// Signal metadata after loading.
    enum class ValueKind { BitVector, Real, String, Event };

    struct SignalInfo {
        ValueKind encoding = ValueKind::BitVector;
        uint32_t num_changes = 0;
        uint32_t max_states = 2;   // 2, 4, or 9
        uint32_t width = 0;
        uint32_t bytes_per_entry = 0;
        bool has_meta_byte = false;
    };

    /// Get cached signal info. Returns false if not loaded.
    virtual bool signal_info(uint32_t signal_ref, SignalInfo& out) const = 0;

    struct WaveformValue {
        ValueKind kind = ValueKind::BitVector;
        std::string text;
        double real = 0.0;
    };

    // ── Core queries ──

    /// Result of binary search for a signal at a time index.
    struct SignalOffset {
        uint32_t start = 0;       // offset into signal data
        uint16_t elements = 0;    // number of values at this same time (usually 1)
        bool time_match = false;  // true if signal changed at exactly this time_idx
        uint32_t next_idx = 0;    // TimeTableIdx of next change
        bool has_next = false;    // true if next_idx is valid
    };

    /// Binary search: find signal data offset at time_idx.
    /// Returns false if signal not loaded or before first change.
    virtual bool signal_offset_at(uint32_t signal_ref, uint32_t time_idx,
                                  SignalOffset& out) const = 0;

    /// Read bit-vector value at a data offset. Writes up to 8 bytes.
    /// Returns empty string on error or for non-bitvector signals.
    virtual std::string signal_value_at(uint32_t signal_ref,
                                        uint32_t start, uint16_t element) const = 0;

    /// Read value as a string representation (for display).
    virtual std::string signal_value_str(uint32_t signal_ref,
                                         uint32_t start, uint16_t element) const = 0;

    /// Read a typed value without collapsing real/string/event into bytes.
    virtual bool signal_typed_value_at(uint32_t signal_ref,
                                       uint32_t start, uint16_t element,
                                       WaveformValue& out) const = 0;

    // ── Batch operations ──

    /// Read values for multiple signals at the same time_idx.
    /// Returns per-signal results: (found, raw_bytes).
    virtual void values_at(const std::vector<uint32_t>& refs, uint32_t time_idx,
                           std::vector<std::string>& out_values,
                           std::vector<bool>& out_found) const = 0;

    // ── Signal change iteration ──

    /// Get all time_indices for a loaded signal (for efficient iteration).
    /// Returns a reference to internal data — valid until unload_signals or close.
    /// Returns nullptr if signal not loaded.
    virtual const uint32_t* signal_time_indices(uint32_t signal_ref,
                                                uint32_t* out_count) const = 0;

    /// Copy of the sorted time indices at which a signal changes.
    /// Empty if the signal is not loaded or has no changes.
    virtual std::vector<uint32_t> time_indices_of(uint32_t signal_ref) const = 0;

    /// Get raw data pointer for a loaded signal.
    virtual const uint8_t* signal_data_ptr(uint32_t signal_ref) const = 0;
};

} // namespace xdebug_fst
