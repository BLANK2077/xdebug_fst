// wellen_fst_backend.h — Wellen C FFI backend implementation
// BSD-3-Clause License
#pragma once

#include "waveform_backend.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations from wellen_capi.h / wellenx_capi.h
struct WellenDb;
struct WellenxDb;

namespace xdebug_fst {

/// Waveform backend backed by the Wellen C FFI library (libwellen_capi.so)
/// plus the minimal wellenx_capi extension (bit-string values, time indices).
class WellenFstBackend final : public IWaveformBackend {
public:
    WellenFstBackend();
    ~WellenFstBackend() override;

    // ── IWaveformBackend ──

    bool open(const std::string& path) override;
    void close() override;
    bool is_open() const override { return db_ != nullptr; }

    uint32_t time_count() const override;
    uint64_t time_at(uint32_t idx) const override;
    uint64_t min_time() const override;
    uint64_t max_time() const override;
    bool time_scale(WaveformTimeScale& out) const override;
    bool parse_time(const std::string& text, uint64_t& ticks,
                    std::string& error, bool allow_max = false) const override;
    uint32_t time_idx_of(uint64_t t) const override;
    std::string format_time(
        uint64_t t, TimeRenderUnit unit = TimeRenderUnit::Ns) const override;

    uint32_t scope_count() const override;
    uint32_t scope_at(uint32_t idx) const override;
    uint32_t root_scope_count() const override;
    uint32_t root_scope_at(uint32_t idx) const override;
    uint32_t scope_child_count(uint32_t scope_ref) const override;
    uint32_t scope_child_at(uint32_t scope_ref, uint32_t idx) const override;
    uint32_t scope_var_count(uint32_t scope_ref) const override;
    uint32_t scope_var_at(uint32_t scope_ref, uint32_t idx) const override;
    const char* scope_name(uint32_t scope_ref) override;
    const char* scope_full_name(uint32_t scope_ref) override;
    const char* var_name(uint32_t var_ref) override;
    const char* var_full_name(uint32_t var_ref) override;
    uint32_t var_signal_ref(uint32_t var_ref) const override;
    int var_encoding(uint32_t var_ref, uint32_t* out_width) const override;

    int load_signals(const std::vector<uint32_t>& refs) override;
    void unload_signals(const std::vector<uint32_t>& refs) override;
    bool is_loaded(uint32_t signal_ref) const override;
    bool signal_info(uint32_t signal_ref, SignalInfo& out) const override;

    bool signal_offset_at(uint32_t signal_ref, uint32_t time_idx,
                          SignalOffset& out) const override;
    std::string signal_value_at(uint32_t signal_ref,
                                uint32_t start, uint16_t element) const override;
    std::string signal_value_str(uint32_t signal_ref,
                                 uint32_t start, uint16_t element) const override;
    bool signal_typed_value_at(uint32_t signal_ref,
                               uint32_t start, uint16_t element,
                               WaveformValue& out) const override;

    void values_at(const std::vector<uint32_t>& refs, uint32_t time_idx,
                   std::vector<std::string>& out_values,
                   std::vector<bool>& out_found) const override;

    const uint32_t* signal_time_indices(uint32_t signal_ref,
                                        uint32_t* out_count) const override;
    std::vector<uint32_t> time_indices_of(uint32_t signal_ref) const override;
    const uint8_t* signal_data_ptr(uint32_t signal_ref) const override;

    /// Find a signal by hierarchical path (case-insensitive, "TOP." prefix
    /// tolerant). Returns kInvalidSignalRef if not found.
    uint32_t find_signal(const std::string& path) const;

    /// Direct signal-value query at an absolute time (ns-style integer).
    /// Returns false if not available.
    bool value_at(const std::string& path, uint64_t time,
                  std::string& out_value, uint32_t* out_width,
                  bool* out_time_match, uint32_t* out_time_idx) const;

private:
    WellenDb* db_ = nullptr;
    WellenxDb* xdb_ = nullptr;  // extension handle (bit strings, time indices)

    // Cached time table for O(log N) binary search
    std::vector<uint64_t> time_table_;
    WaveformTimeScale time_scale_;

    // String caches (owned by C FFI, valid for db_ lifetime)
    std::unordered_map<uint32_t, std::string> name_cache_;

    // Name → signal_ref lookup index (built lazily on first find)
    mutable std::unordered_map<std::string, uint32_t> signal_index_;

    // Name lookup
    std::string get_or_cache_name(uint32_t ref, bool is_var, bool full);

    // Build signal_index_ (idempotent)
    void build_signal_index() const;

    /// Normalize a hierarchical path: lowercase, drop leading "TOP."
    static std::string normalize_path(const std::string& path);
};

} // namespace xdebug_fst
