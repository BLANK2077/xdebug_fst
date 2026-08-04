// wellen_fst_backend.cpp — Wellen C FFI backend implementation
// BSD-3-Clause License

#include "wellen_fst_backend.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

// Link against wellen_capi (core) and wellenx_capi (extension)
extern "C" {
#include "wellen_capi.h"
#include "wellenx_capi.h"
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

    // Extension handle for bit-string values and time indices
    xdb_ = wellenx_open(path.c_str());
    if (!xdb_) {
        fprintf(stderr, "wellenx_open error: %s\n", path.c_str());
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
    if (xdb_) {
        wellenx_close(xdb_);
        xdb_ = nullptr;
    }
    if (db_) {
        wellen_close(db_);
        db_ = nullptr;
    }
    time_table_.clear();
    name_cache_.clear();
    signal_index_.clear();
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
    // Default: integer time with "ps" unit suffix (xdebug default unit).
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
    if (it != name_cache_.end() && !it->second.empty()) return it->second;

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
    int n = static_cast<int>(wellen_load_signals(db_, refs.data(),
                                                 static_cast<uint32_t>(refs.size())));
    if (xdb_) {
        wellenx_load_signals(xdb_, refs.data(), static_cast<uint32_t>(refs.size()));
    }
    return n;
}

void WellenFstBackend::unload_signals(const std::vector<uint32_t>& refs) {
    if (refs.empty()) return;
    wellen_unload_signals(db_, refs.data(), static_cast<uint32_t>(refs.size()));
    if (xdb_) {
        wellenx_unload_signals(xdb_, refs.data(), static_cast<uint32_t>(refs.size()));
    }
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
    // Prefer the ASCII bit string from the extension FFI (2/4/9-state).
    if (xdb_) {
        SignalInfo info;
        if (signal_info(signal_ref, info) && info.width > 0 && info.width <= 4096) {
            std::string bits(info.width, '\0');
            uint32_t len = 0;
            int rc = wellenx_signal_value_bits_at_offset(
                xdb_, signal_ref, start, element, bits.data(), &len);
            if (rc == 0 && len > 0) {
                bits.resize(len);
                return bits;
            }
            if (rc == -2 && len > 0) {  // string signal
                bits.resize(len);
                return bits;
            }
        }
    }
    // Fallback: raw big-endian bytes as hex
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

    // Per-signal reads via bit-string extension (2/4/9-state aware)
    for (size_t i = 0; i < refs.size(); ++i) {
        IWaveformBackend::SignalOffset off;
        if (!signal_offset_at(refs[i], time_idx, off)) continue;
        out_found[i] = true;
        out_values[i] = signal_value_str(refs[i], off.start, 0);
    }
}

const uint32_t* WellenFstBackend::signal_time_indices(uint32_t signal_ref,
                                                      uint32_t* out_count) const {
    if (out_count) *out_count = 0;
    if (!xdb_) return nullptr;
    SignalInfo info;
    if (!signal_info(signal_ref, info)) return nullptr;
    static thread_local std::vector<uint32_t> s_cache;
    s_cache.resize(info.num_changes);
    int n = wellenx_signal_time_indices(xdb_, signal_ref, s_cache.data());
    if (n < 0) return nullptr;
    s_cache.resize(n);
    if (out_count) *out_count = static_cast<uint32_t>(n);
    return s_cache.data();
}

std::vector<uint32_t> WellenFstBackend::time_indices_of(uint32_t signal_ref) const {
    std::vector<uint32_t> out;
    if (!xdb_) return out;
    SignalInfo info;
    if (!signal_info(signal_ref, info)) return out;
    out.resize(info.num_changes);
    int n = wellenx_signal_time_indices(xdb_, signal_ref, out.data());
    if (n < 0) {
        out.clear();
        return out;
    }
    out.resize(n);
    return out;
}

const uint8_t* WellenFstBackend::signal_data_ptr(uint32_t) const {
    return nullptr;
}

// ── Extended helpers ──

std::string WellenFstBackend::normalize_path(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // Drop a leading "top." prefix produced by Verilator DesignDB naming.
    if (out.rfind("top.", 0) == 0) out = out.substr(4);
    return out;
}

void WellenFstBackend::build_signal_index() const {
    if (!signal_index_.empty() || !db_) return;
    uint32_t n = wellen_root_scope_count(db_);
    for (uint32_t si = 0; si < n; ++si) {
        uint32_t sr = wellen_scope_at(db_, si);
        if (sr == 0) continue;
        uint32_t nv = wellen_scope_var_count(db_, sr);
        for (uint32_t vi = 0; vi < nv; ++vi) {
            uint32_t vr = wellen_scope_var_at(db_, sr, vi);
            if (vr == 0) continue;
            const char* full = wellen_var_full_name(db_, vr);
            if (!full || !*full) continue;
            uint32_t sig = wellen_var_signal_ref(db_, vr);
            if (sig == 0) continue;
            std::string key = normalize_path(full);
            if (!key.empty()) signal_index_[key] = sig;
            // Also register the local (last-path-component) name when unique
            std::string local = normalize_path(wellen_var_name(db_, vr) ? wellen_var_name(db_, vr) : "");
            if (!local.empty() && signal_index_.find(local) == signal_index_.end()) {
                signal_index_[local] = sig;
            }
        }
        // child scopes
        uint32_t nc = wellen_scope_child_count(db_, sr);
        for (uint32_t ci = 0; ci < nc; ++ci) {
            uint32_t cs = wellen_scope_child_at(db_, sr, ci);
            if (cs == 0) continue;
            uint32_t nvc = wellen_scope_var_count(db_, cs);
            for (uint32_t vi = 0; vi < nvc; ++vi) {
                uint32_t vr = wellen_scope_var_at(db_, cs, vi);
                if (vr == 0) continue;
                const char* full = wellen_var_full_name(db_, vr);
                if (!full || !*full) continue;
                uint32_t sig = wellen_var_signal_ref(db_, vr);
                if (sig == 0) continue;
                std::string key = normalize_path(full);
                if (!key.empty()) signal_index_[key] = sig;
            }
        }
    }
}

uint32_t WellenFstBackend::find_signal(const std::string& path) const {
    if (path.empty() || !db_) return 0;
    build_signal_index();
    std::string key = normalize_path(path);
    auto it = signal_index_.find(key);
    if (it != signal_index_.end()) return it->second;
    // Fallback: brute force over all vars
    uint32_t n = wellen_root_scope_count(db_);
    for (uint32_t si = 0; si < n; ++si) {
        uint32_t sr = wellen_scope_at(db_, si);
        if (sr == 0) continue;
        uint32_t nv = wellen_scope_var_count(db_, sr);
        for (uint32_t vi = 0; vi < nv; ++vi) {
            uint32_t vr = wellen_scope_var_at(db_, sr, vi);
            if (vr == 0) continue;
            const char* full = wellen_var_full_name(db_, vr);
            if (full && normalize_path(full) == key) return wellen_var_signal_ref(db_, vr);
        }
    }
    return 0;
}

bool WellenFstBackend::value_at(const std::string& path, uint64_t time,
                                std::string& out_value, uint32_t* out_width,
                                bool* out_time_match, uint32_t* out_time_idx) const {
    uint32_t ref = find_signal(path);
    if (!ref) return false;
    if (!is_loaded(ref)) {
        const_cast<WellenFstBackend*>(this)->load_signals({ref});
    }
    uint32_t ti = time_idx_of(time);
    SignalInfo info;
    if (signal_info(ref, info) && out_width) *out_width = info.width;
    SignalOffset off;
    if (!signal_offset_at(ref, ti, off)) return false;
    out_value = signal_value_str(ref, off.start, 0);
    if (out_time_match) *out_time_match = off.time_match;
    if (out_time_idx) *out_time_idx = ti;
    return true;
}

} // namespace xdebug_fst
