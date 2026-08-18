// wellen_capi.h — C FFI for the Wellen waveform library (FST/VCD/GHW)
// BSD-3-Clause License

#ifndef WELLEN_CAPI_H
#define WELLEN_CAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Opaque handle ──
typedef struct WellenDb WellenDb;

// ── Enums ──
typedef enum {
    WELLEN_ENCODING_BITVECTOR = 0,
    WELLEN_ENCODING_REAL      = 1,
    WELLEN_ENCODING_STRING    = 2,
    WELLEN_ENCODING_EVENT     = 3,
} WellenSignalEncoding;

// ── Structs ──
typedef struct {
    uint32_t signal_ref;
    WellenSignalEncoding encoding;
    uint32_t num_changes;
    uint32_t max_states;      // 2, 4, or 9
    uint32_t width;
    uint32_t bytes_per_entry;
    int32_t  has_meta_byte;
} WellenSignalInfo;

// ── Lifecycle ──

/// Open a waveform file (FST/VCD/GHW auto-detected).
/// Returns NULL on error; call wellen_open_error() for details.
WellenDb* wellen_open(const char* path);

/// If wellen_open failed, returns the error message.
/// Valid until wellen_close. Returns empty string if open succeeded.
const char* wellen_open_error(const WellenDb* db);

/// Close and free all resources.
void wellen_close(WellenDb* db);

// ── Time table ──

/// Number of distinct time points.
uint32_t wellen_time_count(const WellenDb* db);

/// Copy time values into out[]. Returns number of values copied.
int32_t wellen_get_times(const WellenDb* db, uint64_t* out,
                         uint32_t offset, uint32_t count);

/// Read the waveform tick scale as factor * 10^exponent seconds.
/// Returns 0 on success or -1 when the file has no known timescale.
int32_t wellen_timescale(const WellenDb* db, uint32_t* out_factor,
                         int32_t* out_exponent);

// ── Hierarchy ──

/// Number of top-level scopes in the design.
uint32_t wellen_root_scope_count(const WellenDb* db);

/// Get a top-level scope_ref at index (0-based).
uint32_t wellen_root_scope_at(const WellenDb* db, uint32_t idx);

/// Total number of scopes in the design, including every descendant.
uint32_t wellen_scope_count(const WellenDb* db);

/// Get scope_ref at all-scope linear index (0-based).
/// Returns 0 for invalid index.
uint32_t wellen_scope_at(const WellenDb* db, uint32_t idx);

/// Number of child scopes under `scope_ref`.
/// scope_ref = 1 + scope_index (1-based, 0 = invalid).
uint32_t wellen_scope_child_count(const WellenDb* db, uint32_t scope_ref);

/// Get child scope_ref at index.
uint32_t wellen_scope_child_at(const WellenDb* db, uint32_t scope_ref,
                               uint32_t idx);

/// Number of variables in scope.
uint32_t wellen_scope_var_count(const WellenDb* db, uint32_t scope_ref);

/// Get var_ref at index in scope.
uint32_t wellen_scope_var_at(const WellenDb* db, uint32_t scope_ref,
                             uint32_t idx);

/// Get scope name (C string, valid until wellen_close).
const char* wellen_scope_name(WellenDb* db, uint32_t scope_ref);

/// Get the full hierarchical scope name (valid until wellen_close).
const char* wellen_scope_full_name(WellenDb* db, uint32_t scope_ref);

/// Get var local name.
const char* wellen_var_name(WellenDb* db, uint32_t var_ref);

/// Get var full hierarchical name.
const char* wellen_var_full_name(WellenDb* db, uint32_t var_ref);

/// Get the 1-based C signal reference for a var. 0 = invalid reference.
/// Native Wellen SignalRef indices are translated by adding one.
uint32_t wellen_var_signal_ref(const WellenDb* db, uint32_t var_ref);

/// Get signal encoding and width.
WellenSignalEncoding wellen_var_encoding(const WellenDb* db, uint32_t var_ref,
                                          uint32_t* out_width);

/// Get the declared packed index range for a variable.
/// Returns 0 with signed msb/lsb on success, or -1 when no range is declared.
int32_t wellen_var_index(const WellenDb* db, uint32_t var_ref,
                         int64_t* out_msb, int64_t* out_lsb);

// ── Signal loading ──

/// Load signals by ref. Returns number loaded.
int32_t wellen_load_signals(WellenDb* db, const uint32_t* refs, uint32_t count);

/// Unload signals to free memory.
void wellen_unload_signals(WellenDb* db, const uint32_t* refs, uint32_t count);

/// Get cached signal info. Returns 0 on success, -1 if not loaded.
int32_t wellen_signal_info(const WellenDb* db, uint32_t signal_ref,
                           WellenSignalInfo* info);

// ── Core queries ──

/// Binary search: find signal data offset at time_idx.
/// Returns 0 on success, -1 if signal not loaded or before first change.
///
/// On success:
///   out_start       = offset into signal data
///   out_elements    = number of values at this same time (usually 1)
///   out_time_match  = 1 iff signal changed at exactly this time_idx
///   out_next_idx    = TimeTableIdx of next change (0 if none)
///   out_has_next    = 1 if out_next_idx is valid
int32_t wellen_signal_offset_at(const WellenDb* db, uint32_t signal_ref,
                                 uint32_t time_idx,
                                 uint32_t* out_start, uint16_t* out_elements,
                                 int32_t* out_time_match,
                                 uint32_t* out_next_idx, int32_t* out_has_next);

/// Read bit-vector value at offset. Writes up to 8 bytes into out_bytes[].
/// Returns 0 on success, -1 on error, -2 for string signals.
int32_t wellen_signal_value_at_offset(const WellenDb* db, uint32_t signal_ref,
                                       uint32_t start, uint16_t element,
                                       uint8_t* out_bytes, uint32_t* out_len);

/// Read a typed value without losing bit states, UTF-8 strings, real values,
/// or events. Bit vectors and strings are copied to out_text without a NUL.
/// Set out_text=NULL and text_capacity=0 to query out_text_len first.
/// Returns 0 on success, -1 on invalid input/value, or -2 when the supplied
/// text buffer is smaller than out_text_len.
int32_t wellen_signal_typed_value_at_offset(
    const WellenDb* db, uint32_t signal_ref,
    uint32_t start, uint16_t element,
    char* out_text, uint32_t text_capacity, uint32_t* out_text_len,
    double* out_real, WellenSignalEncoding* out_encoding);

/// Batch read: multiple signals at the same time_idx.
/// out_values: pre-allocated buffer of count * value_stride bytes
/// out_found:  per-signal int32_t[count] (1 = found, 0 = missing)
/// Returns number of signals found.
int32_t wellen_values_at(const WellenDb* db,
                          const uint32_t* refs, uint32_t count,
                          uint32_t time_idx,
                          uint8_t* out_values, int32_t* out_found,
                          uint32_t value_stride);

#ifdef __cplusplus
}
#endif

#endif // WELLEN_CAPI_H
