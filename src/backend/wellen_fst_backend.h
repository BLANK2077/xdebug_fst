// wellen_fst_backend.h — Wellen C FFI backend implementation
// BSD-3-Clause License
#pragma once

#include "waveform_backend.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations from wellen_capi.h
struct WellenDb;

namespace xdebug_fst {

/// Waveform backend backed by the Wellen C FFI library (libwellen_capi.so).
/// Wraps wellen_open / wellen_close / wellen_signal_offset_at / etc.
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
    uint32_t time_idx_of(uint64_t t) const override;
    std::string format_time(uint64_t t) const override;

    uint32_t scope_count() const override;
    uint32_t scope_at(uint32_t idx) const override;
    uint32_t scope_child_count(uint32_t scope_ref) const override;
    uint32_t scope_child_at(uint32_t scope_ref, uint32_t idx) const override;
    uint32_t scope_var_count(uint32_t scope_ref) const override;
    uint32_t scope_var_at(uint32_t scope_ref, uint32_t idx) const override;
    const char* scope_name(uint32_t scope_ref) override;
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

    void values_at(const std::vector<uint32_t>& refs, uint32_t time_idx,
                   std::vector<std::string>& out_values,
                   std::vector<bool>& out_found) const override;

    const uint32_t* signal_time_indices(uint32_t signal_ref,
                                        uint32_t* out_count) const override;
    const uint8_t* signal_data_ptr(uint32_t signal_ref) const override;

private:
    WellenDb* db_ = nullptr;

    // Cached time table for O(log N) binary search
    std::vector<uint64_t> time_table_;

    // String caches (owned by C FFI, valid for db_ lifetime)
    std::unordered_map<uint32_t, std::string> name_cache_;

    // Name lookup
    std::string get_or_cache_name(uint32_t ref, bool is_var, bool full);
};

} // namespace xdebug_fst
