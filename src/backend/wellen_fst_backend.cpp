// wellen_fst_backend.cpp — Wellen C FFI backend implementation
// BSD-3-Clause License

#include "wellen_fst_backend.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// Link against wellen_capi
extern "C" {
#include "wellen_capi.h"
}

namespace xdebug_fst {

WellenFstBackend::WellenFstBackend() = default;

WellenFstBackend::~WellenFstBackend() {
    close();
}

// ── Lifecycle ──

bool WellenFstBackend::open(const std::string& path) {
    close();
    db_ = wellen_open(path.c_str());
    if (!db_) {
        const char* err = wellen_open_error(nullptr);
        if (err && *err) {
            fprintf(stderr, "wellen_open error: %s\n", err);
        }
        return false;
    }

    // Cache the time table
    uint32_t n = wellen_time_count(db_);
    time_table_.resize(n);
    if (n > 0) {
        wellen_get_times(db_, time_table_.data(), 0, n);
    }
    return true;
}

void WellenFstBackend::close() {
    if (db_) {
        wellen_close(db_);
        db_ = nullptr;
    }
    time_table_.clear();
    name_cache_.clear();
}

// ── Time ──

uint32_t WellenFstBackend::time_count() const {
    return static_cast<uint32_t>(time_table_.size());
}

uint64_t WellenFstBackend::time_at(uint32_t idx) const {
    if (idx >= time_table_.size()) return 0;
    return time_table_[idx];
}

uint64_t WellenFstBackend::min_time() const {
    return time_table_.empty() ? 0 : time_table_.front();
}

uint64_t WellenFstBackend::max_time() const {
    return time_table_.empty() ? 0 : time_table_.back();
}

uint32_t WellenFstBackend::time_idx_of(uint64_t t) const {
    if (time_table_.empty()) return 0;
    auto it = std::upper_bound(time_table_.begin(), time_table_.end(), t);
    if (it == time_table_.begin()) return 0;
    return static_cast<uint32_t>(std::distance(time_table_.begin(), it) - 1);
}

std::string WellenFstBackend::format_time(uint64_t t) const {
    // Simple default: show as integer with "ps" suffix
    return std::to_string(t);
}

// ── Hierarchy ──

uint32_t WellenFstBackend::scope_count() const {
    return wellen_root_scope_count(db_);
}

uint32_t WellenFstBackend::scope_at(uint32_t idx) const {
    return wellen_scope_at(db_, idx);
}

uint32_t WellenFstBackend::scope_child_count(uint32_t scope_ref) const {
    return wellen_scope_child_count(db_, scope_ref);
}

uint32_t WellenFstBackend::scope_child_at(uint32_t scope_ref, uint32_t idx) const {
    return wellen_scope_child_at(db_, scope_ref, idx);
}

uint32_t WellenFstBackend::scope_var_count(uint32_t scope_ref) const {
    return wellen_scope_var_count(db_, scope_ref);
}

uint32_t WellenFstBackend::scope_var_at(uint32_t scope_ref, uint32_t idx) const {
    return wellen_scope_var_at(db_, scope_ref, idx);
}

std::string WellenFstBackend::get_or_cache_name(uint32_t ref, bool is_var, bool full) {
    // Build a composite key
    uint32_t key = ref;
    if (is_var) key |= 0x80000000;
    if (full)   key |= 0x40000000;

    auto it = name_cache_.find(key);
    if (it != name_cache_.end()) return it->second;

    const char* cstr = nullptr;
    if (is_var) {
        cstr = full ? wellen_var_full_name(db_, ref) : wellen_var_name(db_, ref);
    } else {
        cstr = wellen_scope_name(db_, ref);
    }
    std::string result = cstr ? std::string(cstr) : std::string();
    name_cache_[key] = result;
    return result;
}

const char* WellenFstBackend::scope_name(uint32_t scope_ref) {
    auto& s = name_cache_[scope_ref];
    if (s.empty()) s = get_or_cache_name(scope_ref, false, false);
    return s.c_str();
}

const char* WellenFstBackend::var_name(uint32_t var_ref) {
    uint32_t key = var_ref | 0x80000000;
    auto& s = name_cache_[key];
    if (s.empty()) s = get_or_cache_name(var_ref, true, false);
    return s.c_str();
}

const char* WellenFstBackend::var_full_name(uint32_t var_ref) {
    uint32_t key = var_ref | 0xC0000000;
    auto& s = name_cache_[key];
    if (s.empty()) s = get_or_cache_name(var_ref, true, true);
    return s.c_str();
}

uint32_t WellenFstBackend::var_signal_ref(uint32_t var_ref) const {
    return wellen_var_signal_ref(db_, var_ref);
}

int WellenFstBackend::var_encoding(uint32_t var_ref, uint32_t* out_width) const {
    return static_cast<int>(wellen_var_encoding(db_, var_ref, out_width));
}

// ── Signal loading ──

int WellenFstBackend::load_signals(const std::vector<uint32_t>& refs) {
    if (refs.empty()) return 0;
    return static_cast<int>(wellen_load_signals(db_, refs.data(),
                                                static_cast<uint32_t>(refs.size())));
}

void WellenFstBackend::unload_signals(const std::vector<uint32_t>& refs) {
    if (refs.empty()) return;
    wellen_unload_signals(db_, refs.data(), static_cast<uint32_t>(refs.size()));
}

bool WellenFstBackend::is_loaded(uint32_t signal_ref) const {
    WellenSignalInfo info;
    return wellen_signal_info(db_, signal_ref, &info) == 0;
}

bool WellenFstBackend::signal_info(uint32_t signal_ref,
                                   SignalInfo& out) const {
    WellenSignalInfo info;
    if (wellen_signal_info(db_, signal_ref, &info) != 0) return false;
    out.num_changes = info.num_changes;
    out.max_states = info.max_states;
    out.width = info.width;
    out.bytes_per_entry = info.bytes_per_entry;
    out.has_meta_byte = info.has_meta_byte != 0;
    return true;
}

// ── Core queries ──

bool WellenFstBackend::signal_offset_at(uint32_t signal_ref, uint32_t time_idx,
                                        SignalOffset& out) const {
    uint32_t start = 0;
    uint16_t elements = 0;
    int32_t time_match = 0;
    uint32_t next_idx = 0;
    int32_t has_next = 0;

    if (wellen_signal_offset_at(db_, signal_ref, time_idx,
                                &start, &elements, &time_match,
                                &next_idx, &has_next) != 0) {
        return false;
    }
    out.start = start;
    out.elements = elements;
    out.time_match = time_match != 0;
    out.next_idx = next_idx;
    out.has_next = has_next != 0;
    return true;
}

std::string WellenFstBackend::signal_value_at(uint32_t signal_ref,
                                              uint32_t start,
                                              uint16_t element) const {
    uint8_t buf[8] = {0};
    uint32_t len = 0;
    if (wellen_signal_value_at_offset(db_, signal_ref, start, element,
                                      buf, &len) != 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(buf), len);
}

std::string WellenFstBackend::signal_value_str(uint32_t signal_ref,
                                               uint32_t start,
                                               uint16_t element) const {
    // For now, return a hex string representation
    auto raw = signal_value_at(signal_ref, start, element);
    if (raw.empty()) return "x";
    std::string hex;
    for (unsigned char c : raw) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", c);
        hex += buf;
    }
    return hex;
}

// ── Batch ──

void WellenFstBackend::values_at(const std::vector<uint32_t>& refs,
                                 uint32_t time_idx,
                                 std::vector<std::string>& out_values,
                                 std::vector<bool>& out_found) const {
    out_values.resize(refs.size());
    out_found.resize(refs.size(), false);

    // Use a stride of 8 bytes per value
    std::vector<uint8_t> buf(refs.size() * 8, 0);
    std::vector<int32_t> found(refs.size(), 0);

    wellen_values_at(db_, refs.data(), static_cast<uint32_t>(refs.size()),
                     time_idx, buf.data(), found.data(), 8);

    for (size_t i = 0; i < refs.size(); ++i) {
        out_found[i] = found[i] != 0;
        if (found[i]) {
            out_values[i] = std::string(
                reinterpret_cast<const char*>(buf.data() + i * 8), 8);
        }
    }
}

const uint32_t* WellenFstBackend::signal_time_indices(uint32_t, uint32_t*) const {
    // Not directly exposed via C FFI yet — return nullptr
    return nullptr;
}

const uint8_t* WellenFstBackend::signal_data_ptr(uint32_t) const {
    // Not directly exposed via C FFI yet — return nullptr
    return nullptr;
}

} // namespace xdebug_fst
