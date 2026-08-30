// wellen_fst_backend.cpp — Wellen C FFI backend implementation
// BSD-3-Clause License

#include "wellen_fst_backend.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <sys/stat.h>

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

namespace {
// wellen panics (rather than returning an error) when the input file does not
// exist; guard all opens with an existence check.
bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool has_fst_suffix(const std::string& path) {
    constexpr const char* suffix = ".fst";
    constexpr size_t suffix_length = 4;
    return path.size() >= suffix_length &&
           path.compare(path.size() - suffix_length, suffix_length, suffix) == 0;
}
}  // namespace

bool WellenFstBackend::open(const std::string& path) {
    close();
    if (!has_fst_suffix(path)) {
        fprintf(stderr, "wellen_open: only .fst waveform input is supported: %s\n",
                path.c_str());
        return false;
    }
    if (!file_exists(path)) {
        fprintf(stderr, "wellen_open: file not found: %s\n", path.c_str());
        return false;
    }
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
        wellen_close(db_);
        db_ = nullptr;
        return false;
    }

    // Cache the time table
    uint32_t n = wellen_time_count(db_);
    time_table_.resize(n);
    if (n > 0) {
        wellen_get_times(db_, time_table_.data(), 0, n);
    }
    uint32_t factor = 0;
    int32_t exponent = 0;
    if (wellen_timescale(db_, &factor, &exponent) == 0) {
        time_scale_.factor = factor;
        time_scale_.exponent = exponent;
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
    time_scale_ = {};
    name_cache_.clear();
    signal_index_.clear();
    signal_index_built_ = false;
    signal_index_build_count_ = 0;
    declared_ranges_.clear();
    packed_selection_index_.clear();
    packed_selections_.clear();
    selected_change_cache_.clear();
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

bool WellenFstBackend::time_scale(WaveformTimeScale& out) const {
    out = time_scale_;
    return out.valid();
}

bool WellenFstBackend::parse_time(const std::string& text, uint64_t& ticks,
                                  std::string& error, bool allow_max) const {
    return parse_waveform_time(
        text, time_scale_, max_time(), allow_max, ticks, error);
}

uint32_t WellenFstBackend::time_idx_of(uint64_t t) const {
    if (time_table_.empty()) return 0;
    auto it = std::upper_bound(time_table_.begin(), time_table_.end(), t);
    if (it == time_table_.begin()) return 0;
    return static_cast<uint32_t>(std::distance(time_table_.begin(), it) - 1);
}

std::string WellenFstBackend::format_time(uint64_t t,
                                         TimeRenderUnit unit) const {
    return format_waveform_time(t, time_scale_, unit);
}

// ── Hierarchy ──

uint32_t WellenFstBackend::scope_count() const {
    return wellen_scope_count(db_);
}

uint32_t WellenFstBackend::scope_at(uint32_t idx) const {
    return wellen_scope_at(db_, idx);
}

uint32_t WellenFstBackend::root_scope_count() const {
    return wellen_root_scope_count(db_);
}

uint32_t WellenFstBackend::root_scope_at(uint32_t idx) const {
    return wellen_root_scope_at(db_, idx);
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

const char* WellenFstBackend::scope_full_name(uint32_t scope_ref) {
    const uint32_t key = scope_ref | 0x40000000;
    auto& value = name_cache_[key];
    if (value.empty()) {
        const char* name = wellen_scope_full_name(db_, scope_ref);
        value = name ? name : "";
    }
    return value.c_str();
}

const char* WellenFstBackend::scope_component(uint32_t scope_ref) {
    const uint32_t key = scope_ref | 0x20000000;
    auto& value = name_cache_[key];
    if (value.empty()) {
        const char* component = wellen_scope_component(db_, scope_ref);
        value = component ? component : "";
    }
    return value.c_str();
}

IWaveformBackend::ScopeKind WellenFstBackend::scope_kind(
    uint32_t scope_ref) const {
    return static_cast<IWaveformBackend::ScopeKind>(
        wellen_scope_kind(db_, scope_ref));
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
    std::vector<uint32_t> native_refs;
    native_refs.reserve(refs.size());
    for (uint32_t ref : refs) {
        const uint32_t native_ref = native_signal_ref(ref);
        if (native_ref == kInvalidSignalRef) return 0;
        native_refs.push_back(native_ref);
    }
    int n = static_cast<int>(wellen_load_signals(
        db_, native_refs.data(), static_cast<uint32_t>(native_refs.size())));
    if (xdb_) {
        wellenx_load_signals(xdb_, native_refs.data(),
                             static_cast<uint32_t>(native_refs.size()));
    }
    return n;
}

void WellenFstBackend::unload_signals(const std::vector<uint32_t>& refs) {
    if (refs.empty()) return;
    std::vector<uint32_t> native_refs;
    native_refs.reserve(refs.size());
    for (uint32_t ref : refs) {
        const uint32_t native_ref = native_signal_ref(ref);
        if (native_ref != kInvalidSignalRef) native_refs.push_back(native_ref);
    }
    if (native_refs.empty()) return;
    for (auto item = selected_change_cache_.begin();
         item != selected_change_cache_.end();) {
        const PackedSelection* const selection = packed_selection(item->first);
        if (selection && std::find(native_refs.begin(), native_refs.end(),
                                   selection->base_ref) != native_refs.end()) {
            item = selected_change_cache_.erase(item);
        } else {
            ++item;
        }
    }
    wellen_unload_signals(db_, native_refs.data(),
                          static_cast<uint32_t>(native_refs.size()));
    if (xdb_) {
        wellenx_unload_signals(xdb_, native_refs.data(),
                               static_cast<uint32_t>(native_refs.size()));
    }
}

bool WellenFstBackend::is_loaded(uint32_t signal_ref) const {
    WellenSignalInfo info;
    return wellen_signal_info(db_, native_signal_ref(signal_ref), &info) == 0;
}

bool WellenFstBackend::signal_info(uint32_t signal_ref,
                                   SignalInfo& out) const {
    WellenSignalInfo info;
    if (wellen_signal_info(db_, native_signal_ref(signal_ref), &info) != 0)
        return false;
    switch (info.encoding) {
    case WELLEN_ENCODING_REAL: out.encoding = ValueKind::Real; break;
    case WELLEN_ENCODING_STRING: out.encoding = ValueKind::String; break;
    case WELLEN_ENCODING_EVENT: out.encoding = ValueKind::Event; break;
    case WELLEN_ENCODING_BITVECTOR:
    default: out.encoding = ValueKind::BitVector; break;
    }
    out.num_changes = info.num_changes;
    out.max_states = info.max_states;
    out.width = info.width;
    out.bytes_per_entry = info.bytes_per_entry;
    out.has_meta_byte = info.has_meta_byte != 0;
    if (const PackedSelection* const selection = packed_selection(signal_ref)) {
        out.width = selection->width;
        out.num_changes =
            static_cast<uint32_t>(selected_changes(signal_ref).size());
        out.bytes_per_entry = (out.width + 7U) / 8U;
        out.has_meta_byte = out.max_states > 2 && out.width >= 8;
    }
    return true;
}

// ── Core queries ──

bool WellenFstBackend::signal_offset_at(uint32_t signal_ref, uint32_t time_idx,
                                        SignalOffset& out) const {
    if (packed_selection(signal_ref)) {
        const std::vector<SelectedChange>& changes =
            selected_changes(signal_ref);
        const auto end = std::upper_bound(
            changes.begin(), changes.end(), time_idx,
            [](uint32_t index, const SelectedChange& change) {
                return index < change.time_idx;
            });
        if (end == changes.begin()) return false;
        size_t last = static_cast<size_t>(std::distance(changes.begin(), end) - 1);
        const uint32_t selected_time = changes[last].time_idx;
        size_t first = last;
        while (first > 0 && changes[first - 1].time_idx == selected_time) --first;
        while (last + 1 < changes.size() &&
               changes[last + 1].time_idx == selected_time) ++last;
        out.start = static_cast<uint32_t>(first);
        out.elements = static_cast<uint16_t>(last - first + 1);
        out.time_match = selected_time == time_idx;
        out.has_next = last + 1 < changes.size();
        out.next_idx = out.has_next ? changes[last + 1].time_idx : 0;
        return true;
    }
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
    if (packed_selection(signal_ref)) {
        WaveformValue value;
        if (!signal_typed_value_at(signal_ref, start, element, value) ||
            value.kind != ValueKind::BitVector) return {};
        return value.text;
    }
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
    WaveformValue value;
    if (!signal_typed_value_at(signal_ref, start, element, value)) return {};
    if (value.kind == ValueKind::Real) {
        std::ostringstream stream;
        stream << std::setprecision(17) << value.real;
        return stream.str();
    }
    if (value.kind == ValueKind::Event) return "event";
    return value.text;
}

bool WellenFstBackend::signal_typed_value_at(
    uint32_t signal_ref, uint32_t start, uint16_t element,
    WaveformValue& out) const {
    if (packed_selection(signal_ref)) {
        const std::vector<SelectedChange>& changes =
            selected_changes(signal_ref);
        const size_t index = static_cast<size_t>(start) + element;
        if (index >= changes.size()) return false;
        out = changes[index].value;
        return true;
    }
    return native_typed_value_at(signal_ref, start, element, out);
}

bool WellenFstBackend::native_typed_value_at(
    uint32_t signal_ref, uint32_t start, uint16_t element,
    WaveformValue& out) const {
    out = {};
    uint32_t length = 0;
    double real = 0.0;
    WellenSignalEncoding encoding = WELLEN_ENCODING_BITVECTOR;
    int result = wellen_signal_typed_value_at_offset(
        db_, signal_ref, start, element, nullptr, 0, &length, &real,
        &encoding);
    if (result != 0 && result != -2) return false;
    std::string text(length, '\0');
    result = wellen_signal_typed_value_at_offset(
        db_, signal_ref, start, element,
        text.empty() ? nullptr : text.data(), length, &length, &real,
        &encoding);
    if (result != 0) return false;
    text.resize(length);
    switch (encoding) {
    case WELLEN_ENCODING_REAL:
        out.kind = ValueKind::Real;
        out.real = real;
        break;
    case WELLEN_ENCODING_STRING:
        out.kind = ValueKind::String;
        out.text = std::move(text);
        break;
    case WELLEN_ENCODING_EVENT:
        out.kind = ValueKind::Event;
        break;
    case WELLEN_ENCODING_BITVECTOR:
    default:
        out.kind = ValueKind::BitVector;
        out.text = std::move(text);
        break;
    }
    return true;
}

bool WellenFstBackend::sampled_value_at(
    uint32_t signal_ref, uint32_t time_idx, ObservationPoint point,
    SampledValue& out) const {
    out = {};
    SignalOffset offset;
    if (!signal_offset_at(signal_ref, time_idx, offset) ||
        offset.elements == 0) {
        return false;
    }

    uint32_t start = offset.start;
    uint16_t element = static_cast<uint16_t>(offset.elements - 1);
    if (point == ObservationPoint::Before && offset.time_match) {
        if (offset.start == 0) return false;
        start = offset.start - 1;
        element = 0;
    }
    if (!signal_typed_value_at(signal_ref, start, element, out.value)) {
        return false;
    }
    out.time_idx = time_idx;
    out.element = element;
    out.elements_at_time = offset.elements;
    out.time_match = offset.time_match;
    return true;
}

bool WellenFstBackend::delta_values_at(
    uint32_t signal_ref, uint32_t time_idx,
    std::vector<WaveformValue>& out, bool& out_time_match) const {
    out.clear();
    out_time_match = false;
    SignalOffset offset;
    if (!signal_offset_at(signal_ref, time_idx, offset) ||
        offset.elements == 0) {
        return false;
    }
    out.reserve(offset.elements);
    for (uint16_t element = 0; element < offset.elements; ++element) {
        WaveformValue value;
        if (!signal_typed_value_at(
                signal_ref, offset.start, element, value)) {
            out.clear();
            return false;
        }
        out.push_back(std::move(value));
    }
    out_time_match = offset.time_match;
    return true;
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
        SampledValue sampled;
        if (!sampled_value_at(refs[i], time_idx,
                              ObservationPoint::Raw, sampled)) continue;
        out_found[i] = true;
        if (sampled.value.kind == ValueKind::Real) {
            std::ostringstream stream;
            stream << std::setprecision(17) << sampled.value.real;
            out_values[i] = stream.str();
        } else if (sampled.value.kind == ValueKind::Event) {
            out_values[i] = "event";
        } else {
            out_values[i] = sampled.value.text;
        }
    }
}

void WellenFstBackend::typed_values_at(
    const std::vector<uint32_t>& refs, uint32_t time_idx,
    ObservationPoint point, std::vector<SampledValue>& out_values,
    std::vector<bool>& out_found) const {
    out_values.assign(refs.size(), {});
    out_found.assign(refs.size(), false);
    for (size_t index = 0; index < refs.size(); ++index) {
        out_found[index] = sampled_value_at(
            refs[index], time_idx, point, out_values[index]);
    }
}

bool WellenFstBackend::signal_change_at(
    uint32_t signal_ref, uint32_t ordinal, SignalChange& out) const {
    out = {};
    std::vector<uint32_t> indices = time_indices_of(signal_ref);
    if (ordinal >= indices.size()) return false;
    const uint32_t time_idx = indices[ordinal];
    uint16_t delta = 0;
    for (uint32_t index = ordinal;
         index > 0 && indices[index - 1] == time_idx; --index) {
        ++delta;
    }
    if (!signal_typed_value_at(signal_ref, ordinal, 0, out.value)) {
        return false;
    }
    out.time_idx = time_idx;
    out.time = time_at(time_idx);
    out.delta = delta;
    return true;
}

bool WellenFstBackend::scan_changes(
    uint32_t signal_ref, uint32_t begin_time_idx, uint32_t end_time_idx,
    uint32_t limit, std::vector<SignalChange>& out,
    ScanDiagnostics& diagnostics) const {
    out.clear();
    diagnostics = {};
    SignalInfo info;
    if (begin_time_idx > end_time_idx || !signal_info(signal_ref, info)) {
        return false;
    }
    diagnostics.width = info.width;
    diagnostics.encoding = info.encoding;
    const std::vector<uint32_t> indices = time_indices_of(signal_ref);
    for (uint32_t ordinal = 0; ordinal < indices.size(); ++ordinal) {
        if (indices[ordinal] < begin_time_idx ||
            indices[ordinal] > end_time_idx) {
            continue;
        }
        ++diagnostics.total_count;
        if (limit != 0 && out.size() >= limit) continue;
        SignalChange change;
        change.time_idx = indices[ordinal];
        change.time = time_at(change.time_idx);
        for (uint32_t index = ordinal;
             index > 0 && indices[index - 1] == change.time_idx; --index) {
            ++change.delta;
        }
        if (!signal_typed_value_at(
                signal_ref, ordinal, 0, change.value)) return false;
        out.push_back(std::move(change));
    }
    diagnostics.returned_count = static_cast<uint32_t>(out.size());
    diagnostics.truncated =
        diagnostics.returned_count < diagnostics.total_count;
    diagnostics.scan_complete = true;
    diagnostics.analysis_complete = true;
    return true;
}

const uint32_t* WellenFstBackend::signal_time_indices(uint32_t signal_ref,
                                                      uint32_t* out_count) const {
    if (out_count) *out_count = 0;
    static thread_local std::vector<uint32_t> s_cache;
    s_cache = time_indices_of(signal_ref);
    if (s_cache.empty()) return nullptr;
    if (out_count) *out_count = static_cast<uint32_t>(s_cache.size());
    return s_cache.data();
}

std::vector<uint32_t> WellenFstBackend::time_indices_of(uint32_t signal_ref) const {
    if (packed_selection(signal_ref)) {
        std::vector<uint32_t> out;
        for (const SelectedChange& change : selected_changes(signal_ref)) {
            out.push_back(change.time_idx);
        }
        return out;
    }
    return native_time_indices_of(signal_ref);
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
    size_t array_separator = 0;
    while ((array_separator = out.find(".[", array_separator)) !=
           std::string::npos) {
        out.erase(array_separator, 1);
    }
    return out;
}

void WellenFstBackend::build_signal_index() const {
    if (signal_index_built_ || !db_) return;
    ++signal_index_build_count_;
    std::unordered_map<std::string, uint32_t> local_candidates;
    std::unordered_map<std::string, DeclaredRange> local_range_candidates;
    std::unordered_set<std::string> ambiguous_locals;
    std::unordered_set<std::string> ambiguous_local_ranges;
    uint32_t n = wellen_scope_count(db_);
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
            int64_t msb = 0;
            int64_t lsb = 0;
            uint32_t width = 0;
            const WellenSignalEncoding encoding =
                wellen_var_encoding(db_, vr, &width);
            const bool has_range =
                encoding == WELLEN_ENCODING_BITVECTOR &&
                wellen_var_index(db_, vr, &msb, &lsb) == 0;
            const DeclaredRange range{msb, lsb, width};
            std::string key = normalize_path(full);
            if (!key.empty()) {
                signal_index_[key] = sig;
                if (has_range) declared_ranges_[key] = range;
            }
            const char* local_name = wellen_var_name(db_, vr);
            std::string local = normalize_path(local_name ? local_name : "");
            if (!local.empty()) {
                auto inserted = local_candidates.emplace(local, sig);
                if (!inserted.second && inserted.first->second != sig) {
                    ambiguous_locals.insert(local);
                }
                if (has_range) {
                    const auto range_inserted =
                        local_range_candidates.emplace(local, range);
                    if (!range_inserted.second &&
                        (range_inserted.first->second.msb != range.msb ||
                         range_inserted.first->second.lsb != range.lsb ||
                         range_inserted.first->second.width != range.width)) {
                        ambiguous_local_ranges.insert(local);
                    }
                } else {
                    ambiguous_local_ranges.insert(local);
                }
            }
        }
    }
    for (const auto& item : local_candidates) {
        if (ambiguous_locals.count(item.first) == 0 &&
            signal_index_.count(item.first) == 0) {
            signal_index_[item.first] = item.second;
            const auto range = local_range_candidates.find(item.first);
            if (range != local_range_candidates.end() &&
                ambiguous_local_ranges.count(item.first) == 0) {
                declared_ranges_[item.first] = range->second;
            }
        }
    }
    signal_index_built_ = true;
}

uint32_t WellenFstBackend::find_signal(const std::string& path) const {
    if (path.empty() || !db_) return kInvalidSignalRef;
    build_signal_index();
    std::string key = normalize_path(path);
    auto it = signal_index_.find(key);
    if (it != signal_index_.end()) return it->second;
    const uint32_t selected = create_packed_selection(key);
    if (selected != kInvalidSignalRef) return selected;
    // build_signal_index() traverses every variable.  A miss after the exact
    // and packed-selection indexes is definitive; rescanning the hierarchy
    // makes every absent DesignDB-only temporary O(number of waveform vars).
    return kInvalidSignalRef;
}

const WellenFstBackend::PackedSelection* WellenFstBackend::packed_selection(
    uint32_t signal_ref) const {
    if ((signal_ref & kVirtualSignalFlag) == 0) return nullptr;
    const uint32_t encoded_index = signal_ref & ~kVirtualSignalFlag;
    if (encoded_index == 0 || encoded_index > packed_selections_.size())
        return nullptr;
    return &packed_selections_[encoded_index - 1];
}

uint32_t WellenFstBackend::native_signal_ref(uint32_t signal_ref) const {
    if (const PackedSelection* const selection = packed_selection(signal_ref))
        return selection->base_ref;
    return (signal_ref & kVirtualSignalFlag) == 0
        ? signal_ref : kInvalidSignalRef;
}

namespace {

bool parse_signed_index(const std::string& text, int64_t& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || end != text.c_str() + text.size()) return false;
    value = static_cast<int64_t>(parsed);
    return true;
}

}  // namespace

uint32_t WellenFstBackend::create_packed_selection(
    const std::string& normalized_path) const {
    const auto cached = packed_selection_index_.find(normalized_path);
    if (cached != packed_selection_index_.end()) return cached->second;
    if (normalized_path.empty() || normalized_path.back() != ']')
        return kInvalidSignalRef;
    const size_t open = normalized_path.rfind('[');
    if (open == std::string::npos || open == 0) return kInvalidSignalRef;
    const std::string base_path = normalized_path.substr(0, open);
    const auto base = signal_index_.find(base_path);
    if (base == signal_index_.end() ||
        (base->second & kVirtualSignalFlag) != 0) return kInvalidSignalRef;
    const auto declared = declared_ranges_.find(base_path);
    if (declared == declared_ranges_.end()) return kInvalidSignalRef;
    const uint64_t declared_width = static_cast<uint64_t>(
        declared->second.msb >= declared->second.lsb
            ? declared->second.msb - declared->second.lsb + 1
            : declared->second.lsb - declared->second.msb + 1);
    if (declared_width == 0 || declared_width != declared->second.width)
        return kInvalidSignalRef;
    const std::string selector = normalized_path.substr(
        open + 1, normalized_path.size() - open - 2);
    const size_t colon = selector.find(':');
    if (colon != std::string::npos && selector.find(':', colon + 1) !=
            std::string::npos) return kInvalidSignalRef;
    int64_t selected_msb = 0;
    int64_t selected_lsb = 0;
    if (!parse_signed_index(selector.substr(0, colon), selected_msb))
        return kInvalidSignalRef;
    if (colon == std::string::npos) {
        selected_lsb = selected_msb;
    } else if (!parse_signed_index(selector.substr(colon + 1), selected_lsb)) {
        return kInvalidSignalRef;
    }
    const int64_t declared_low = std::min(declared->second.msb,
                                          declared->second.lsb);
    const int64_t declared_high = std::max(declared->second.msb,
                                           declared->second.lsb);
    if (selected_msb < declared_low || selected_msb > declared_high ||
        selected_lsb < declared_low || selected_lsb > declared_high)
        return kInvalidSignalRef;
    const uint64_t selected_width = static_cast<uint64_t>(
        selected_msb >= selected_lsb ? selected_msb - selected_lsb + 1
                                     : selected_lsb - selected_msb + 1);
    if (selected_width == 0 || selected_width > UINT32_MAX ||
        packed_selections_.size() + 1 >= kVirtualSignalFlag)
        return kInvalidSignalRef;
    packed_selections_.push_back({base->second, declared->second.msb,
                                  declared->second.lsb, selected_msb,
                                  selected_lsb,
                                  static_cast<uint32_t>(selected_width)});
    const uint32_t handle = kVirtualSignalFlag |
        static_cast<uint32_t>(packed_selections_.size());
    packed_selection_index_.emplace(normalized_path, handle);
    return handle;
}

bool WellenFstBackend::select_packed_bits(
    const PackedSelection& selection, const std::string& source,
    std::string& selected) {
    const uint64_t declared_width = static_cast<uint64_t>(
        selection.declared_msb >= selection.declared_lsb
            ? selection.declared_msb - selection.declared_lsb + 1
            : selection.declared_lsb - selection.declared_msb + 1);
    if (declared_width != source.size()) return false;
    selected.clear();
    selected.reserve(selection.width);
    const int64_t step = selection.selected_msb <= selection.selected_lsb
        ? 1 : -1;
    for (int64_t bit = selection.selected_msb;; bit += step) {
        const uint64_t offset = static_cast<uint64_t>(
            selection.declared_msb >= selection.declared_lsb
                ? selection.declared_msb - bit
                : bit - selection.declared_msb);
        if (offset >= source.size()) return false;
        selected.push_back(source[static_cast<size_t>(offset)]);
        if (bit == selection.selected_lsb) break;
    }
    return true;
}

std::vector<uint32_t> WellenFstBackend::native_time_indices_of(
    uint32_t signal_ref) const {
    std::vector<uint32_t> out;
    if (!xdb_) return out;
    WellenSignalInfo info;
    if (wellen_signal_info(db_, signal_ref, &info) != 0) return out;
    out.resize(info.num_changes);
    const int n = wellenx_signal_time_indices(xdb_, signal_ref, out.data());
    if (n < 0) {
        out.clear();
        return out;
    }
    out.resize(static_cast<size_t>(n));
    return out;
}

const std::vector<WellenFstBackend::SelectedChange>&
WellenFstBackend::selected_changes(uint32_t signal_ref) const {
    static const std::vector<SelectedChange> empty;
    const auto cached = selected_change_cache_.find(signal_ref);
    if (cached != selected_change_cache_.end()) return cached->second;
    const PackedSelection* const selection = packed_selection(signal_ref);
    if (!selection) return empty;
    WellenSignalInfo info;
    if (wellen_signal_info(db_, selection->base_ref, &info) != 0) return empty;
    const std::vector<uint32_t> indices = native_time_indices_of(
        selection->base_ref);
    std::vector<SelectedChange> selected;
    selected.reserve(indices.size());
    std::string previous;
    for (size_t ordinal = 0; ordinal < indices.size(); ++ordinal) {
        WaveformValue source;
        if (!native_typed_value_at(selection->base_ref,
                                   static_cast<uint32_t>(ordinal), 0, source) ||
            source.kind != ValueKind::BitVector) {
            return empty;
        }
        std::string value;
        if (!select_packed_bits(*selection, source.text, value)) {
            return empty;
        }
        if (!selected.empty() && value == previous) continue;
        previous = value;
        selected.push_back({indices[ordinal],
                            WaveformValue{ValueKind::BitVector, value, 0.0}});
    }
    return selected_change_cache_.emplace(signal_ref, std::move(selected))
        .first->second;
}

bool WellenFstBackend::value_at(const std::string& path, uint64_t time,
                                std::string& out_value, uint32_t* out_width,
                                bool* out_time_match, uint32_t* out_time_idx) const {
    uint32_t ref = find_signal(path);
    if (ref == kInvalidSignalRef) return false;
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
