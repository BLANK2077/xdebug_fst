#include "common/env_config.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace xdebug_core {

namespace {

bool empty_env(const char* raw) {
    return raw == nullptr || raw[0] == '\0';
}

std::string range_error(const char* name, const std::string& type, long long min_value, long long max_value) {
    return std::string(name) + " must be a " + type + " in range [" +
           std::to_string(min_value) + ", " + std::to_string(max_value) + "]";
}

bool parse_ll_value(const char* raw, long long& value) {
    errno = 0;
    char* end = nullptr;
    long long parsed = std::strtoll(raw, &end, 10);
    if (errno != 0 || end == raw || (end && *end != '\0')) return false;
    value = parsed;
    return true;
}

} // namespace

std::string env_raw_string(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string env_string(const char* name, const std::string& default_value) {
    std::string value = env_raw_string(name);
    return value.empty() ? default_value : value;
}

bool env_bool(const char* name, bool default_value, bool& value, std::string& error) {
    const char* raw = std::getenv(name);
    if (empty_env(raw)) {
        value = default_value;
        return true;
    }
    if (std::strcmp(raw, "1") == 0 || strcasecmp(raw, "true") == 0 ||
        strcasecmp(raw, "yes") == 0 || strcasecmp(raw, "on") == 0) {
        value = true;
        return true;
    }
    if (std::strcmp(raw, "0") == 0 || strcasecmp(raw, "false") == 0 ||
        strcasecmp(raw, "no") == 0 || strcasecmp(raw, "off") == 0) {
        value = false;
        return true;
    }
    error = std::string(name) + " must be a boolean value";
    return false;
}

bool env_int(const char* name, int default_value, int min_value, int max_value,
             int& value, std::string& error) {
    long long parsed = default_value;
    std::string local_error;
    if (!env_ll(name, default_value, min_value, max_value, parsed, local_error)) {
        error = local_error;
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool env_ll(const char* name, long long default_value, long long min_value, long long max_value,
            long long& value, std::string& error) {
    const char* raw = std::getenv(name);
    if (empty_env(raw)) {
        value = default_value;
        return true;
    }
    long long parsed = 0;
    if (!parse_ll_value(raw, parsed) || parsed < min_value || parsed > max_value) {
        error = range_error(name, "integer", min_value, max_value);
        return false;
    }
    value = parsed;
    return true;
}

bool env_bool_or_default(const char* name, bool default_value) {
    bool value = default_value;
    std::string error;
    return env_bool(name, default_value, value, error) ? value : default_value;
}

int env_int_or_default(const char* name, int default_value, int min_value, int max_value) {
    int value = default_value;
    std::string error;
    return env_int(name, default_value, min_value, max_value, value, error) ? value : default_value;
}

long long env_ll_or_default(const char* name,
                            long long default_value,
                            long long min_value,
                            long long max_value) {
    long long value = default_value;
    std::string error;
    return env_ll(name, default_value, min_value, max_value, value, error) ? value : default_value;
}

std::string xdebug_transport() {
    return env_string("XDEBUG_TRANSPORT", "uds");
}

bool xdebug_debug_enabled() {
    return env_bool_or_default("XDEBUG_DEBUG", false);
}

int xdebug_session_start_timeout_sec(std::string& error) {
    int value = 300;
    if (!env_int("XDEBUG_SESSION_START_TIMEOUT_SEC", 300, 1, INT_MAX, value, error)) return -1;
    return value;
}

int xdebug_session_idle_timeout_sec(std::string& error) {
    int value = 86400;
    if (!env_int("XDEBUG_SESSION_IDLE_TIMEOUT_SEC", 86400, 1, INT_MAX, value, error)) return -1;
    return value;
}

int xdebug_file_transport_timeout_ms() {
    return env_int_or_default("XDEBUG_FILE_TRANSPORT_TIMEOUT_MS", 300000, 1, INT_MAX);
}

int xdebug_file_transport_ping_timeout_ms() {
    return env_int_or_default("XDEBUG_FILE_TRANSPORT_PING_TIMEOUT_MS", 2000, 1, INT_MAX);
}

int xdebug_file_poll_interval_ms() {
    return env_int_or_default("XDEBUG_FILE_POLL_INTERVAL_MS", 20, 1, 10000);
}

int xdebug_file_max_json_bytes() {
    return env_int_or_default("XDEBUG_FILE_MAX_JSON_BYTES", 67108864, 1, 1024 * 1024 * 1024);
}

int xdebug_file_claim_timeout_ms(int request_timeout_ms) {
    if (request_timeout_ms <= 0) request_timeout_ms = xdebug_file_transport_timeout_ms();
    long long fallback = request_timeout_ms > 300000 ? 2LL * request_timeout_ms : 600000LL;
    return static_cast<int>(env_ll_or_default("XDEBUG_FILE_CLAIM_TIMEOUT_MS",
                                              fallback,
                                              1,
                                              24LL * 60LL * 60LL * 1000LL));
}

bool xdebug_file_keep_history() {
    return env_bool_or_default("XDEBUG_FILE_KEEP_HISTORY", true);
}

long long xdebug_file_done_ttl_sec() {
    return env_ll_or_default("XDEBUG_FILE_DONE_TTL_SEC", 7LL * 24LL * 60LL * 60LL, 0, LLONG_MAX);
}

long long xdebug_file_failed_ttl_sec() {
    return env_ll_or_default("XDEBUG_FILE_FAILED_TTL_SEC", 30LL * 24LL * 60LL * 60LL, 0, LLONG_MAX);
}

std::string xdebug_common_blocks_path() {
    return env_string("XDEBUG_COMMON_BLOCKS");
}

long long xdebug_log_max_bytes() {
    return env_ll_or_default("XDEBUG_LOG_MAX_BYTES", 0, 0, LLONG_MAX);
}

long long xdebug_log_max_files() {
    return env_ll_or_default("XDEBUG_LOG_MAX_FILES", 3, 1, LLONG_MAX);
}

std::string xdebug_log_path_mode() {
    return env_string("XDEBUG_LOG_PATH_MODE");
}

bool xdebug_log_redact_enabled() {
    return env_bool_or_default("XDEBUG_LOG_REDACT", false);
}

bool xdebug_engine_test_crash_marker_enabled() {
    return env_bool_or_default("XDEBUG_ENGINE_TEST_CRASH_MARKER", false);
}

std::string xdebug_engine_test_crash_action() {
    return env_string("XDEBUG_ENGINE_TEST_CRASH_ACTION");
}

std::string xdebug_engine_test_crash_request_id() {
    return env_string("XDEBUG_ENGINE_TEST_CRASH_REQUEST_ID");
}

bool xdebug_engine_test_npi_init_fail_enabled() {
    return env_bool_or_default("XDEBUG_ENGINE_TEST_NPI_INIT_FAIL", false);
}

bool xdebug_engine_test_npi_load_design_fail_enabled() {
    return env_bool_or_default("XDEBUG_ENGINE_TEST_NPI_LOAD_DESIGN_FAIL", false);
}

bool xdebug_engine_test_npi_fsdb_open_fail_enabled() {
    return env_bool_or_default("XDEBUG_ENGINE_TEST_NPI_FSDB_OPEN_FAIL", false);
}

int xdebug_trace_source_context_lines() {
    return env_int_or_default("XDEBUG_TRACE_SOURCE_CONTEXT_LINES", 3, 0, 1000);
}

int xdebug_trace_source_merge_threshold_lines() {
    return env_int_or_default("XDEBUG_TRACE_SOURCE_MERGE_THRESHOLD_LINES", 10, 1, 1000000);
}

} // namespace xdebug_core
