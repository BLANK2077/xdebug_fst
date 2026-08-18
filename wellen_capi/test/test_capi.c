// test_capi.c — Verify the Wellen C FFI works end-to-end
// Build: cc -I../include -L../../../target/debug -lwellen_capi test_capi.c -o test_capi
// Run:   LD_LIBRARY_PATH=../../../target/debug ./test_capi <fst_file>

#include "../include/wellen_capi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); \
        failures++; \
    } else { \
        printf("  OK: " msg "\n", ##__VA_ARGS__); \
    } \
} while(0)

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <fst_or_vcd_file>\n", argv[0]);
        return 1;
    }
    const char* path = argv[1];

    printf("=== Wellen C API Test ===\n");
    printf("File: %s\n\n", path);

    // ── Error handling ──
    WellenDb* invalid = wellen_open(NULL);
    CHECK(invalid == NULL, "null path is rejected");
    const char* open_error = wellen_open_error(invalid);
    CHECK(open_error != NULL && strcmp(open_error, "null path") == 0,
          "null path reports a stable error");
    wellen_close(NULL);

    // ── Open ──
    WellenDb* db = wellen_open(path);
    CHECK(db != NULL, "wellen_open succeeded");
    if (!db) return 1;

    const char* err = wellen_open_error(db);
    CHECK(err == NULL || *err == '\0', "no open error");

    // ── Time table ──
    uint32_t time_count = wellen_time_count(db);
    CHECK(time_count > 0, "time_count = %u", time_count);
    printf("  Time points: %u\n", time_count);

    if (time_count > 0) {
        uint64_t times[5];
        int32_t n = wellen_get_times(db, times, 0, 5);
        CHECK(n > 0, "wellen_get_times returned %d", n);
        printf("  First %d times:", n);
        for (int i = 0; i < n && i < 5; i++) {
            printf(" %lu", (unsigned long)times[i]);
        }
        printf("\n");
    }
    uint32_t time_factor = 0;
    int32_t time_exponent = 0;
    CHECK(wellen_timescale(db, &time_factor, &time_exponent) == 0,
          "timescale is available");
    CHECK(time_factor == 1 && time_exponent == -9,
          "timescale is 1ns (factor=%u exponent=%d)",
          time_factor, time_exponent);

    // ── Hierarchy ──
    uint32_t root_count = wellen_root_scope_count(db);
    uint32_t scope_count = wellen_scope_count(db);
    CHECK(root_count == 1, "root_scope_count = %u", root_count);
    CHECK(scope_count == 2, "recursive scope_count = %u", scope_count);
    CHECK(scope_count > root_count,
          "all-scope traversal includes descendants");
    uint32_t root_scope = wellen_root_scope_at(db, 0);
    CHECK(root_scope != 0, "root_scope_at returns a valid root");
    const char* root_full_name = wellen_scope_full_name(db, root_scope);
    CHECK(root_full_name != NULL && strcmp(root_full_name, "clkdiv2n_tb") == 0,
          "root full name is stable");
    printf("  Total scopes: %u\n", scope_count);

    uint32_t child_scope = wellen_scope_child_at(db, root_scope, 0);
    CHECK(child_scope != 0, "root exposes its child scope");
    const char* child_full_name = wellen_scope_full_name(db, child_scope);
    CHECK(child_full_name != NULL &&
              strcmp(child_full_name, "clkdiv2n_tb.t1") == 0,
          "child full name preserves recursive hierarchy");

    // Walk first few scopes
    int walked = 0;
    for (uint32_t i = 0; i < scope_count && walked < 3; i++) {
        uint32_t sr = wellen_scope_at(db, i);
        if (sr == 0) continue;
        walked++;

        const char* sname = wellen_scope_name(db, sr);
        uint32_t nvars = wellen_scope_var_count(db, sr);
        uint32_t nkids = wellen_scope_child_count(db, sr);
        printf("  Scope[%u] '%s': %u vars, %u children\n",
               i, sname ? sname : "(null)", nvars, nkids);

        // Show first few vars
        for (uint32_t j = 0; j < nvars && j < 5; j++) {
            uint32_t vr = wellen_scope_var_at(db, sr, j);
            if (vr == 0) continue;
            const char* vname = wellen_var_name(db, vr);
            const char* vfull = wellen_var_full_name(db, vr);
            uint32_t width = 0;
            WellenSignalEncoding enc = wellen_var_encoding(db, vr, &width);
            uint32_t sig_ref = wellen_var_signal_ref(db, vr);
            printf("    var '%s' full='%s' width=%u enc=%d sig_ref=%u\n",
                   vname ? vname : "?", vfull ? vfull : "?", width, enc, sig_ref);
        }
    }

    // ── Load a signal ──
    // Find any signal
    uint32_t test_sig = 0;
    for (uint32_t i = 0; i < scope_count; i++) {
        uint32_t sr = wellen_scope_at(db, i);
        if (sr == 0) continue;
        uint32_t nvars = wellen_scope_var_count(db, sr);
        for (uint32_t j = 0; j < nvars; j++) {
            uint32_t vr = wellen_scope_var_at(db, sr, j);
            if (vr == 0) continue;
            uint32_t candidate = wellen_var_signal_ref(db, vr);
            if (candidate != 0 && (test_sig == 0 || candidate < test_sig)) {
                test_sig = candidate;
            }
        }
    }

    if (test_sig > 0) {
        CHECK(test_sig == 1,
              "native SignalRef(0) is exposed as C signal reference 1");
        printf("\n--- Signal Loading ---\n");
        int32_t n_loaded = wellen_load_signals(db, &test_sig, 1);
        CHECK(n_loaded == 1, "loaded signal %u", test_sig);

        WellenSignalInfo info;
        int32_t info_ok = wellen_signal_info(db, test_sig, &info);
        CHECK(info_ok == 0, "signal_info: %u changes, width=%u",
              info.num_changes, info.width);
        CHECK(info.encoding == WELLEN_ENCODING_BITVECTOR,
              "signal_info preserves bit-vector encoding");

        // Query at time_idx 0
        uint32_t start = 0;
        uint16_t elements = 0;
        int32_t time_match = 0;
        uint32_t next_idx = 0;
        int32_t has_next = 0;

        int32_t off_ok = wellen_signal_offset_at(db, test_sig, 0,
            &start, &elements, &time_match, &next_idx, &has_next);
        CHECK(off_ok == 0, "signal_offset_at time=0: start=%u match=%d next=%u has_next=%d",
              start, time_match, next_idx, has_next);

        if (off_ok == 0) {
            uint8_t value[8] = {0};
            uint32_t value_len = 0;
            int32_t val_ok = wellen_signal_value_at_offset(db, test_sig,
                start, 0, value, &value_len);
            CHECK(val_ok == 0, "value_at_offset: len=%u bytes=[%02x %02x %02x %02x]",
                  value_len, value[0], value[1], value[2], value[3]);

            uint32_t text_len = 0;
            double real_value = 0.0;
            WellenSignalEncoding encoding = WELLEN_ENCODING_EVENT;
            CHECK(wellen_signal_typed_value_at_offset(
                      db, test_sig, start, 0, NULL, 0, &text_len,
                      &real_value, &encoding) == -2,
                  "typed value reports required bit-string length");
            char* text = malloc(text_len);
            CHECK(text != NULL, "typed value buffer allocated");
            if (text != NULL) {
                CHECK(wellen_signal_typed_value_at_offset(
                          db, test_sig, start, 0, text, text_len, &text_len,
                          &real_value, &encoding) == 0,
                      "typed bit-string value read succeeds");
                CHECK(encoding == WELLEN_ENCODING_BITVECTOR && text_len == 1 &&
                          strchr("01xzhuwl-", text[0]) != NULL,
                      "typed value preserves bit state and encoding");
                free(text);
            }
        }

        // Query at last time_idx
        if (time_count > 0) {
            uint32_t last_ti = time_count - 1;
            off_ok = wellen_signal_offset_at(db, test_sig, last_ti,
                &start, &elements, &time_match, &next_idx, &has_next);
            if (off_ok == 0) {
                printf("  At last time_idx=%u: start=%u match=%d next=%u has_next=%d\n",
                       last_ti, start, time_match, next_idx, has_next);
            }
        }

        // Unload
        wellen_unload_signals(db, &test_sig, 1);
        CHECK(wellen_signal_info(db, test_sig, &info) == -1,
              "unloaded signal no longer has cached info");
        printf("  Unloaded signal\n");
    } else {
        CHECK(0, "fixture exposes at least one signal");
    }

    // ── Close ──
    wellen_close(db);
    printf("\n=== %s ===\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures > 0 ? 1 : 0;
}
