#pragma once

#include <string>

namespace xdebug_core {

struct EnvParseResult {
    bool ok = true;
    std::string error;
};

std::string env_raw_string(const char* name);
std::string env_string(const char* name, const std::string& default_value = std::string());

bool env_bool(const char* name, bool default_value, bool& value, std::string& error);
bool env_int(const char* name, int default_value, int min_value, int max_value,
             int& value, std::string& error);
bool env_ll(const char* name, long long default_value, long long min_value, long long max_value,
            long long& value, std::string& error);

bool env_bool_or_default(const char* name, bool default_value);
int env_int_or_default(const char* name, int default_value, int min_value, int max_value);
long long env_ll_or_default(const char* name,
                            long long default_value,
                            long long min_value,
                            long long max_value);

std::string xdebug_transport();
bool xdebug_debug_enabled();

int xdebug_session_start_timeout_sec(std::string& error);
int xdebug_session_idle_timeout_sec(std::string& error);

int xdebug_file_transport_timeout_ms();
int xdebug_file_transport_ping_timeout_ms();
int xdebug_file_poll_interval_ms();
int xdebug_file_max_json_bytes();
int xdebug_file_claim_timeout_ms(int request_timeout_ms);
bool xdebug_file_keep_history();
long long xdebug_file_done_ttl_sec();
long long xdebug_file_failed_ttl_sec();

std::string xdebug_common_blocks_path();
long long xdebug_log_max_bytes();
long long xdebug_log_max_files();
std::string xdebug_log_path_mode();
bool xdebug_log_redact_enabled();

bool xdebug_engine_test_crash_marker_enabled();
std::string xdebug_engine_test_crash_action();
std::string xdebug_engine_test_crash_request_id();
bool xdebug_engine_test_npi_init_fail_enabled();
bool xdebug_engine_test_npi_load_design_fail_enabled();
bool xdebug_engine_test_npi_fsdb_open_fail_enabled();

int xdebug_trace_source_context_lines();
int xdebug_trace_source_merge_threshold_lines();

} // namespace xdebug_core
