// wellenx_capi.h — Minimal C FFI extension over Wellen (FST/VCD/GHW)
// BSD-3-Clause License
//
// xdebug-fst extension over wellen_capi: ASCII bit-string value reads
// (2/4/9-state) and change-time index iteration.

#ifndef WELLENX_CAPI_H
#define WELLENX_CAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Opaque handle ──
typedef struct WellenxDb WellenxDb;

// ── Lifecycle ──

/// Open a waveform file (FST/VCD/GHW auto-detected). NULL on error.
WellenxDb* wellenx_open(const char* path);

/// Close and free all resources.
void wellenx_close(WellenxDb* db);

// ── Signal loading ──

/// Load signals by ref. Returns number loaded.
int32_t wellenx_load_signals(WellenxDb* db, const uint32_t* refs, uint32_t count);

/// Unload signals to free memory.
void wellenx_unload_signals(WellenxDb* db, const uint32_t* refs, uint32_t count);

// ── Core queries ──

/// Read signal value as ASCII bit string ('0','1','x','z','h','u','w','l','-').
/// out_str must have capacity >= signal width.
/// Returns 0 on success, -1 on error, -2 for string signals, -3 for real.
int32_t wellenx_signal_value_bits_at_offset(const WellenxDb* db,
                                            uint32_t signal_ref,
                                            uint32_t start, uint16_t element,
                                            char* out_str, uint32_t* out_len);

/// Copy the time indices at which a signal changes into out[].
/// out must have capacity >= num_changes. Returns count copied, -1 on error.
int32_t wellenx_signal_time_indices(const WellenxDb* db, uint32_t signal_ref,
                                    uint32_t* out);

#ifdef __cplusplus
}
#endif

#endif  // WELLENX_CAPI_H
