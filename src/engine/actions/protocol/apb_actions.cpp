// apb_actions.cpp — APB protocol actions over the current Wellen FST session.
// BSD-3-Clause
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "engine/actions/value_source_entries.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"
#include "protocol/text_response_builder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

struct ApbConfig {
    std::string name;
    std::string clock;
    std::string edge = "negedge";
    std::string sample_point;
    std::string reset;
    std::string reset_polarity;
    std::string paddr, psel, penable, pwrite, pwdata, prdata;
    std::string pready, pslverr;
};

struct ApbTransaction {
    uint64_t time = 0;
    bool is_write = false;
    bool has_error = false;
    std::string addr;
    std::string data;
    uint32_t addr_width = 0;
    uint32_t data_width = 0;
};

struct ApbScan {
    std::vector<ApbTransaction> transactions;
    bool complete = true;
    size_t unresolved = 0;
};

std::map<std::string, ApbConfig>& apb_configs() {
    static std::map<std::string, ApbConfig> configs;
    return configs;
}

std::map<std::string, size_t>& apb_cursors() {
    static std::map<std::string, size_t> cursors;
    return cursors;
}

Json error_response(const std::string& code, const std::string& message) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}

Json rich_error_response(const std::string& code, const std::string& message,
                         const Json& details = Json::object()) {
    Json error{{"code", code}, {"message", message},
               {"recoverable", true}, {"error_layer", "handler"}};
    for (auto item = details.begin(); item != details.end(); ++item)
        error[item.key()] = item.value();
    return {{"ok", false}, {"error", std::move(error)}};
}

Json action_example(const std::string& action, const Json& args) {
    return {{"api_version", "xdebug.v1"}, {"action", action},
            {"target", {{"session_id", "case_a"}}}, {"args", args}};
}

Json apb_query_example() {
    return action_example("apb.query", {{"name", "apb0"},
        {"direction", "all"}, {"query", {{"line_limit", 8}}}});
}

Json apb_config_load_example() {
    return action_example("apb.config.load", {{"name", "apb0"},
        {"config", {{"clock", "top.u.clk"},
            {"reset", {{"signal", "top.u.rst_n"},
                       {"polarity", "active_low"}}},
            {"paddr", "top.u.paddr"}, {"psel", "top.u.psel"},
            {"penable", "top.u.penable"}, {"pready", "top.u.pready"},
            {"pslverr", "top.u.pslverr"}, {"pwrite", "top.u.pwrite"},
            {"pwdata", "top.u.pwdata"}, {"prdata", "top.u.prdata"}}}});
}

Json apb_window_example() {
    return action_example("apb.transfer_window", {{"name", "if0"},
        {"time_range", {{"begin", "0ns"}, {"end", "100ns"}}}});
}

Json config_not_found_response(const std::string& action,
                               const std::string& name) {
    Json example = action == "apb.query" ? apb_query_example()
                                          : action_example(action, {{"name", "apb0"}});
    return rich_error_response("CONFIG_NOT_FOUND",
        "apb config not found: " + name,
        {{"invalid_arg", "args.name"},
         {"expected", "name of a previously loaded apb config"},
         {"missing_name", name}, {"missing_resource", "apb config"},
         {"correct_example", std::move(example)},
         {"example_note", "Example only; choose an existing config name or load this config before using it."},
         {"next_actions", Json::array({
             "Call apb.config.list to inspect loaded configs.",
             "Call apb.config.load before this action."})}});
}

void set_value_width_complete(Json& summary) {
    summary["value_width_complete"] = true;
    summary["width_diagnostics"] = Json::array();
}

bool parse_analysis_cache_budget(const char* name, uint64_t default_value,
                                 bool allow_zero, uint64_t& value,
                                 std::string& message) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        value = default_value;
        return true;
    }
    if (*raw == '\0') {
        message = std::string(name) + " must be a non-empty unsigned integer";
        return false;
    }
    uint64_t parsed = 0;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(raw);
         *cursor != '\0'; ++cursor) {
        if (!std::isdigit(*cursor)) {
            message = std::string(name) +
                " must contain only unsigned decimal digits";
            return false;
        }
        const uint64_t digit = *cursor - '0';
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            message = std::string(name) + " exceeds uint64 range";
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    if (!allow_zero && parsed == 0) {
        message = std::string(name) + " must be positive";
        return false;
    }
    value = parsed;
    return true;
}

std::string apb_cache_key_summary(const ApbConfig& config) {
    const std::string material = config.name + "\n" + config.clock + "\n" +
        config.edge + "\n" + config.sample_point + "\n" + config.reset +
        "\n" + config.paddr + "\n" + config.psel + "\n" +
        config.penable + "\n" + config.pwrite + "\n" + config.pwdata +
        "\n" + config.prdata + "\n" + config.pready + "\n" +
        config.pslverr;
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : material) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}

Json apb_analysis_budget_error(const ApbConfig& config) {
    uint64_t soft_max_bytes = 0, hard_max_bytes = 0;
    std::string message;
    if (!parse_analysis_cache_budget(
            "XDEBUG_ANALYSIS_CACHE_MAX_BYTES", 1073741824ULL, true,
            soft_max_bytes, message) ||
        !parse_analysis_cache_budget(
            "XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES", 2147483648ULL, false,
            hard_max_bytes, message)) {
        return rich_error_response("INVALID_ENVIRONMENT", message,
                                   {{"recoverable", false}});
    }
    if (soft_max_bytes > hard_max_bytes) {
        return rich_error_response("INVALID_ENVIRONMENT",
            "XDEBUG_ANALYSIS_CACHE_MAX_BYTES must not exceed XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES",
            {{"recoverable", false}});
    }
    if (hard_max_bytes >= sizeof(ApbTransaction)) return Json::object();
    return rich_error_response("ANALYSIS_MEMORY_LIMIT_EXCEEDED",
        "analysis cache build exceeds the configured hard memory limit",
        {{"current_estimated_bytes", 0}, {"hard_max_bytes", hard_max_bytes},
         {"protocol", "apb"}, {"key_summary", apb_cache_key_summary(config)},
         {"next_actions", Json::array({
             "For stream analysis, explicitly retry with cache_scope=range or a smaller time_range.",
             "If range analysis still exceeds the limit, use x-npi for one-off offline analysis."})}});
}

bool xout_scalar(const Json& value) {
    return is_xout_scalar_json(value);
}

void render_tabular_value(TextResponseBuilder& out, const std::string& key,
                          const Json& value) {
    if (xout_scalar(value)) {
        out.emit_kv(key, value);
    } else if (value.is_array() && value.empty()) {
        out.emit_kv(key, "[empty]");
    } else if (value.is_array()) {
        out.emit_section(key);
        if (!value.empty() && value.front().is_object()) {
            out.emit_json_table(value, static_cast<int>(value.size()));
        } else {
            for (const Json& item : value)
                out.emit_row({json_to_xout_value(item)});
        }
    } else if (value.is_object()) {
        bool emitted_direct = false;
        for (auto item = value.begin(); item != value.end(); ++item) {
            if (!xout_scalar(item.value()) &&
                !(item.value().is_array() && item.value().empty())) continue;
            if (!emitted_direct) out.emit_section(key);
            if (xout_scalar(item.value())) out.emit_kv(item.key(), item.value());
            else out.emit_kv(item.key(), "[empty]");
            emitted_direct = true;
        }
        for (auto item = value.begin(); item != value.end(); ++item) {
            if (xout_scalar(item.value()) ||
                (item.value().is_array() && item.value().empty())) continue;
            render_tabular_value(out, key + "." + item.key(), item.value());
        }
    }
}

std::string render_apb_query_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("apb.query");
    const Json summary = response.value("summary", Json::object());
    if (!summary.empty()) {
        out.emit_section("summary");
        for (const char* key : {"name", "direction", "query_mode", "found",
                 "scan_complete", "analysis_complete", "response_truncated",
                 "total_count", "returned_count"}) {
            if (summary.contains(key)) out.emit_kv(key, summary.at(key));
        }
        for (const char* key : {"truncation_scopes", "value_width_complete",
                                "width_diagnostics"}) {
            if (summary.contains(key))
                render_tabular_value(out, key, summary.at(key));
        }
    }
    const Json data = response.value("data", Json::object());
    if (data.contains("filter"))
        render_tabular_value(out, "filter", data.at("filter"));
    if (data.contains("transaction"))
        render_tabular_value(out, "transaction", data.at("transaction"));
    if (data.contains("transactions") && data.at("transactions").is_array() &&
        !data.at("transactions").empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const Json& transaction : data.at("transactions")) {
            rows.push_back({transaction.value("time", std::string()),
                transaction.value("addr", std::string()),
                transaction.value("data", std::string()),
                json_to_xout_value(transaction.value("is_write", Json())),
                json_to_xout_value(transaction.value("has_error", Json()))});
        }
        out.emit_section("transactions");
        out.emit_table({"time", "addr", "data", "is_write", "has_error"},
                       rows);
    }
    return out.str();
}

std::string render_apb_statistics_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("apb.statistics");
    const Json summary = response.value("summary", Json::object());
    out.emit_section("summary");
    for (const char* key : {"name", "scanned_transaction_count",
             "matched_transaction_count", "matched_read_count",
             "matched_write_count", "unresolved_transaction_count",
             "filter_applied", "analysis_quality", "full_scan_count",
             "scan_complete", "analysis_complete", "response_truncated",
             "total_count", "returned_count", "truncation_scopes",
             "value_width_complete", "width_diagnostics"}) {
        if (summary.contains(key)) out.emit_kv(key, summary.at(key));
    }

    const Json data = response.value("data", Json::object());
    const Json filter = data.value("filter", Json::object());
    out.emit_section("filter");
    out.emit_kv("direction", filter.value("direction", std::string("all")));
    if (filter.contains("address")) {
        const Json& address = filter.at("address");
        const std::string mode = address.value("mode", std::string());
        out.emit_kv("address_mode", mode);
        if (mode == "exact") {
            std::string values = "[";
            for (size_t index = 0; index < address.at("values").size(); ++index) {
                if (index) values += ", ";
                values += address.at("values").at(index).get<std::string>();
            }
            out.emit_kv("address_values", values + "]");
        } else if (mode == "range") {
            out.emit_kv("address_begin", address.value("begin", std::string()));
            out.emit_kv("address_end", address.value("end", std::string()));
        } else if (mode == "mask") {
            out.emit_kv("address_value", address.value("value", std::string()));
            out.emit_kv("address_mask", address.value("mask", std::string()));
        }
    }
    out.emit_section("notes");
    out.emit_kv("unresolved_transaction_count",
        "因被引用的 address/ID 含 X/Z 或不可解析，导致无法判断是否匹配过滤条件的已完成事务数。");
    return out.str();
}

std::string render_apb_window_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("apb.transfer_window");
    const Json summary = response.value("summary", Json::object());
    out.emit_section("summary");
    for (const char* key : {"name", "begin", "end", "scan_complete",
             "analysis_complete", "response_truncated", "total_count",
             "returned_count", "value_width_complete"}) {
        if (summary.contains(key)) out.emit_kv(key, summary.at(key));
    }
    const Json transactions = response.value("data", Json::object())
        .value("transactions", Json::array());
    if (!transactions.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const Json& transaction : transactions) {
            rows.push_back({transaction.value("time", std::string()),
                transaction.value("type", std::string()),
                transaction.value("addr", std::string()),
                transaction.value("data", std::string()),
                json_to_xout_value(transaction.value("has_error", Json()))});
        }
        out.emit_section("transactions");
        out.emit_table({"time", "type", "addr", "data", "has_error"}, rows);
    }
    return out.str();
}

std::string render_apb_cursor_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("apb.transaction.cursor");
    const Json summary = response.value("summary", Json::object());
    out.emit_section("summary");
    for (const char* key : {"name", "op", "direction", "found", "index",
             "index_base", "at_begin", "at_end", "scan_complete",
             "analysis_complete", "response_truncated", "total_count",
             "returned_count", "value_width_complete"}) {
        if (summary.contains(key)) out.emit_kv(key, summary.at(key));
    }
    const Json data = response.value("data", Json::object());
    if (data.contains("transaction")) {
        const Json& transaction = data.at("transaction");
        out.emit_section("transaction");
        for (const char* key : {"time", "addr", "data", "is_write",
                                "has_error"}) {
            if (transaction.contains(key)) out.emit_kv(key, transaction.at(key));
        }
    }
    return out.str();
}

Json config_json(const ApbConfig& config) {
    Json result{{"name", config.name}, {"sampling_mode", "clock_edge"},
        {"clock", config.clock}, {"edge", config.edge},
        {"reset", {{"signal", config.reset},
                   {"polarity", config.reset_polarity}}},
        {"paddr", config.paddr}, {"psel", config.psel},
        {"penable", config.penable}, {"pwrite", config.pwrite},
        {"pwdata", config.pwdata}, {"prdata", config.prdata}};
    if (!config.sample_point.empty()) result["sample_point"] = config.sample_point;
    if (!config.pready.empty()) result["pready"] = config.pready;
    if (!config.pslverr.empty()) result["pslverr"] = config.pslverr;
    return result;
}

bool parse_config(const std::string& name, const Json& source,
                  ApbConfig& config, std::string& message) {
    config = {};
    config.name = name;
    config.clock = source.at("clock");
    config.edge = source.value("edge", "negedge");
    config.sample_point = source.value("sample_point", "");
    config.paddr = source.at("paddr");
    config.psel = source.at("psel");
    config.penable = source.at("penable");
    config.pwrite = source.at("pwrite");
    config.pwdata = source.at("pwdata");
    config.prdata = source.at("prdata");
    config.pready = source.value("pready", "");
    config.pslverr = source.value("pslverr", "");
    config.reset = source.at("reset").at("signal");
    config.reset_polarity = source.at("reset").at("polarity");
    if (config.edge == "negedge" && !config.sample_point.empty()) {
        message = "APB config sample_point is only valid with edge:posedge or edge:dual";
        return false;
    }
    if (config.edge != "negedge" && config.sample_point.empty())
        config.sample_point = "before";
    return true;
}

const ApbConfig* find_config(const std::string& name) {
    auto found = apb_configs().find(name);
    return found == apb_configs().end() ? nullptr : &found->second;
}

bool load_ref(IWaveformBackend& wf, const std::string& path,
              const char* role, uint32_t& ref,
              IWaveformBackend::SignalInfo& info, Json& error) {
    ref = wf.find_signal(path);
    if (!ref) {
        error = error_response("CONFIG_SIGNAL_NOT_FOUND",
            std::string("APB ") + role + " signal not found: " + path);
        return false;
    }
    if (!wf.is_loaded(ref) && wf.load_signals({ref}) != 1) {
        error = error_response("VALUE_NOT_AVAILABLE",
            std::string("failed to load APB ") + role + ": " + path);
        return false;
    }
    if (!wf.signal_info(ref, info)) {
        error = error_response("VALUE_NOT_AVAILABLE",
            std::string("APB ") + role + " metadata unavailable: " + path);
        return false;
    }
    return true;
}

IWaveformBackend::ObservationPoint config_point(const ApbConfig& config) {
    if (config.edge == "negedge")
        return IWaveformBackend::ObservationPoint::Raw;
    return config.sample_point == "after"
        ? IWaveformBackend::ObservationPoint::After
        : IWaveformBackend::ObservationPoint::Before;
}

enum class Tri { False, True, Unknown };

Tri truth(const IWaveformBackend::WaveformValue& value) {
    if (value.kind != IWaveformBackend::ValueKind::BitVector ||
        value.text.empty()) return Tri::Unknown;
    bool one = false;
    for (char bit : value.text) {
        if (bit == 'x' || bit == 'X' || bit == 'z' || bit == 'Z')
            return Tri::Unknown;
        if (bit == '1') one = true;
    }
    return one ? Tri::True : Tri::False;
}

bool sample(IWaveformBackend& wf, uint32_t ref, uint32_t time_idx,
            IWaveformBackend::ObservationPoint point,
            IWaveformBackend::WaveformValue& value) {
    IWaveformBackend::SampledValue sampled;
    if (!wf.sampled_value_at(ref, time_idx, point, sampled)) return false;
    value = sampled.value;
    return true;
}

bool selected_edge(const std::string& edge, bool rising, bool falling) {
    return edge == "dual" ? rising || falling
        : edge == "posedge" ? rising : falling;
}

ApbScan scan_transactions(IWaveformBackend& wf, const ApbConfig& config,
                          Json& error) {
    struct Refs {
        uint32_t clock = 0, reset = 0, paddr = 0, psel = 0;
        uint32_t penable = 0, pwrite = 0, pwdata = 0, prdata = 0;
        uint32_t pready = 0, pslverr = 0;
        uint32_t addr_width = 0, write_width = 0, read_width = 0;
    } refs;
    IWaveformBackend::SignalInfo info;
    if (!load_ref(wf, config.clock, "clock", refs.clock, info, error) ||
        !load_ref(wf, config.reset, "reset", refs.reset, info, error) ||
        !load_ref(wf, config.paddr, "paddr", refs.paddr, info, error))
        return {};
    refs.addr_width = info.width;
    if (!load_ref(wf, config.psel, "psel", refs.psel, info, error) ||
        !load_ref(wf, config.penable, "penable", refs.penable, info, error) ||
        !load_ref(wf, config.pwrite, "pwrite", refs.pwrite, info, error) ||
        !load_ref(wf, config.pwdata, "pwdata", refs.pwdata, info, error))
        return {};
    refs.write_width = info.width;
    if (!load_ref(wf, config.prdata, "prdata", refs.prdata, info, error))
        return {};
    refs.read_width = info.width;
    if (!config.pready.empty() &&
        !load_ref(wf, config.pready, "pready", refs.pready, info, error))
        return {};
    if (!config.pslverr.empty() &&
        !load_ref(wf, config.pslverr, "pslverr", refs.pslverr, info, error))
        return {};

    ApbScan result;
    const auto point = config_point(config);
    uint32_t previous_ti = std::numeric_limits<uint32_t>::max();
    size_t selected_edge_ordinal = 0;
    size_t last_accept_edge_ordinal = std::numeric_limits<size_t>::max();
    for (uint32_t ti : wf.time_indices_of(refs.clock)) {
        if (ti == previous_ti) continue;
        previous_ti = ti;
        IWaveformBackend::SampledValue before_clock, raw_clock;
        if (!wf.sampled_value_at(refs.clock, ti,
                IWaveformBackend::ObservationPoint::Before, before_clock) ||
            !wf.sampled_value_at(refs.clock, ti,
                IWaveformBackend::ObservationPoint::Raw, raw_clock))
            continue;
        const bool rising = is_rising_edge(before_clock.value.text,
                                            raw_clock.value.text);
        const bool falling = is_falling_edge(before_clock.value.text,
                                              raw_clock.value.text);
        if (!selected_edge(config.edge, rising, falling)) continue;
        const size_t current_edge_ordinal = selected_edge_ordinal++;

        IWaveformBackend::WaveformValue reset, psel, penable, pwrite;
        IWaveformBackend::WaveformValue paddr, pwdata, prdata, pready, pslverr;
        if (!sample(wf, refs.reset, ti, point, reset) ||
            !sample(wf, refs.psel, ti, point, psel) ||
            !sample(wf, refs.penable, ti, point, penable) ||
            !sample(wf, refs.pwrite, ti, point, pwrite)) {
            result.complete = false;
            ++result.unresolved;
            continue;
        }
        const Tri reset_state = truth(reset);
        const bool reset_asserted = config.reset_polarity == "active_low"
            ? reset_state != Tri::True : reset_state != Tri::False;
        if (reset_asserted) continue;
        const Tri selected = truth(psel), enabled = truth(penable);
        if (selected == Tri::Unknown || enabled == Tri::Unknown) {
            result.complete = false;
            ++result.unresolved;
            continue;
        }
        if (selected != Tri::True || enabled != Tri::True) continue;
        if (refs.pready) {
            if (!sample(wf, refs.pready, ti, point, pready) ||
                truth(pready) == Tri::Unknown) {
                result.complete = false;
                ++result.unresolved;
                continue;
            }
            if (truth(pready) != Tri::True) continue;
        }
        if (!sample(wf, refs.paddr, ti, point, paddr) ||
            !sample(wf, refs.pwdata, ti, point, pwdata) ||
            !sample(wf, refs.prdata, ti, point, prdata) ||
            truth(pwrite) == Tri::Unknown) {
            result.complete = false;
            ++result.unresolved;
            continue;
        }
        if (refs.pslverr && !sample(wf, refs.pslverr, ti, point, pslverr)) {
            result.complete = false;
            ++result.unresolved;
            continue;
        }
        ApbTransaction txn;
        txn.time = wf.time_at(ti);
        txn.is_write = truth(pwrite) == Tri::True;
        txn.has_error = refs.pslverr && truth(pslverr) != Tri::False;
        txn.addr = paddr.text;
        txn.addr_width = refs.addr_width;
        txn.data = txn.is_write ? pwdata.text : prdata.text;
        txn.data_width = txn.is_write ? refs.write_width : refs.read_width;
        const bool duplicate_access_tail =
            last_accept_edge_ordinal != std::numeric_limits<size_t>::max() &&
            last_accept_edge_ordinal + 1 == current_edge_ordinal &&
            !result.transactions.empty() &&
            result.transactions.back().is_write == txn.is_write &&
            result.transactions.back().has_error == txn.has_error &&
            result.transactions.back().addr == txn.addr &&
            result.transactions.back().data == txn.data;
        last_accept_edge_ordinal = current_edge_ordinal;
        if (duplicate_access_tail) continue;
        result.transactions.push_back(std::move(txn));
    }
    return result;
}

Json transaction_json(const ApbTransaction& txn, IWaveformBackend& wf,
                      TimeRenderUnit unit, ValueRenderFormat format,
                      bool window = false) {
    const LogicValue addr = logic_value_from_bits(txn.addr, txn.addr_width);
    const LogicValue data = logic_value_from_bits(txn.data, txn.data_width);
    Json result{{"time", wf.format_time(txn.time, unit)},
        {"addr", render_logic_value(addr, format)},
        {"data", render_logic_value(data, format)},
        {"has_error", txn.has_error}};
    if (window) result["type"] = txn.is_write ? "WR" : "RD";
    else result["is_write"] = txn.is_write;
    return result;
}

bool logic_u64(const std::string& text, uint64_t& value) {
    LogicValue logic;
    if (!parse_sv_literal(text, logic) || !logic.known ||
        logic.bits.size() > 64) return false;
    value = 0;
    for (char bit : logic.bits) {
        value <<= 1;
        if (bit == '1') ++value;
    }
    return true;
}

bool transaction_address(const ApbTransaction& txn, uint64_t& value) {
    if (txn.addr.size() > 64) return false;
    value = 0;
    for (char bit : txn.addr) {
        value <<= 1;
        if (bit == '1') ++value;
        else if (bit != '0') return false;
    }
    return true;
}

enum class FilterResult { No, Yes, Unresolved };

FilterResult address_matches(const ApbTransaction& txn, const Json& address) {
    if (address.is_null() || address.empty()) return FilterResult::Yes;
    uint64_t actual = 0;
    if (!transaction_address(txn, actual)) return FilterResult::Unresolved;
    const std::string mode = address.at("mode");
    if (mode == "exact") {
        for (const Json& candidate : address.at("values")) {
            uint64_t expected = 0;
            if (logic_u64(candidate, expected) && actual == expected)
                return FilterResult::Yes;
        }
        return FilterResult::No;
    }
    if (mode == "range") {
        uint64_t begin = 0, end = 0;
        if (!logic_u64(address.at("begin"), begin) ||
            !logic_u64(address.at("end"), end))
            return FilterResult::Unresolved;
        return actual >= begin && actual <= end
            ? FilterResult::Yes : FilterResult::No;
    }
    uint64_t expected = 0, mask = 0;
    if (!logic_u64(address.at("value"), expected) ||
        !logic_u64(address.at("mask"), mask))
        return FilterResult::Unresolved;
    return (actual & mask) == (expected & mask)
        ? FilterResult::Yes : FilterResult::No;
}

bool direction_matches(const ApbTransaction& txn, const std::string& direction) {
    return direction == "all" ||
        (direction == "write" && txn.is_write) ||
        (direction == "read" && !txn.is_write);
}

Json completeness_summary(bool complete, bool truncated,
                          size_t total, size_t returned) {
    Json scopes = Json::array();
    if (!complete) scopes.push_back("analysis_transactions");
    if (truncated) scopes.push_back("response_transactions");
    return {{"scan_complete", complete}, {"analysis_complete", complete},
        {"response_truncated", truncated}, {"total_count", total},
        {"returned_count", returned}, {"truncation_scopes", scopes}};
}

void merge(Json& target, const Json& source) {
    for (auto it = source.begin(); it != source.end(); ++it)
        target[it.key()] = it.value();
}

bool parse_render(const Json& args, IWaveformBackend& wf,
                  TimeRenderUnit& unit, ValueRenderFormat& format,
                  Json& error) {
    std::string message;
    if (!parse_time_render_unit(args.value("render_time_unit", "ns"),
                                unit, message)) {
        error = error_response("INVALID_TIME_UNIT", message);
        return false;
    }
    if (!parse_value_render_format(args.value("value_format", "hex"), format)) {
        error = error_response("INVALID_FIELD", "invalid value_format");
        return false;
    }
    (void)wf;
    return true;
}

bool require_config_and_scan(const std::string& name, IWaveformBackend& wf,
                             const ApbConfig*& config, ApbScan& scan,
                             Json& error,
                             const std::string& action = "apb.query") {
    config = find_config(name);
    if (!config) {
        error = config_not_found_response(action, name);
        return false;
    }
    Json budget_error = apb_analysis_budget_error(*config);
    if (!budget_error.empty()) {
        error = std::move(budget_error);
        return false;
    }
    error = Json();
    scan = scan_transactions(wf, *config, error);
    return error.is_null();
}

Json recommended_actions() {
    return Json::array({
        {{"action", "value.at"},
         {"purpose", "按一个或多个指定时间读取单信号、命名信号列表或接口配置维护的值。"}},
        {{"action", "apb.query"},
         {"purpose", "按方向以及 exact、range 或 mask 地址条件查询已完成 APB transfer。"}},
        {{"action", "apb.transaction.cursor"},
         {"purpose", "在 APB transfer 间移动游标。"}},
        {{"action", "apb.statistics"},
         {"purpose", "按方向和地址过滤统计已完成 APB 事务。"}},
        {{"action", "apb.transfer_window"},
         {"purpose", "实验性 APB 窗口分析。"}}
    });
}

}  // namespace

bool apb_value_source_entries(const std::string& name,
                              std::vector<ValueSourceEntry>& out) {
    const ApbConfig* config = find_config(name);
    if (!config) return false;
    out = {{"clock", config->clock}, {"reset", config->reset},
        {"paddr", config->paddr},
        {"psel", config->psel}, {"penable", config->penable},
        {"pwrite", config->pwrite}, {"pwdata", config->pwdata},
        {"prdata", config->prdata}};
    if (!config->pready.empty()) out.push_back({"pready", config->pready});
    if (!config->pslverr.empty()) out.push_back({"pslverr", config->pslverr});
    return true;
}

struct ApbConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        const Json args = req.at("args");
        if (args.contains("name")) {
            const std::string name = args.at("name");
            const ApbConfig* config = find_config(name);
            if (!config) return config_not_found_response(action_name(), name);
            return {{"ok", true}, {"summary", {{"name", name}, {"status", "found"}}},
                    {"data", {{"config", config_json(*config)}}}};
        }
        Json configs = Json::array();
        for (const auto& item : apb_configs())
            configs.push_back(config_json(item.second));
        return {{"ok", true}, {"summary", {{"count", configs.size()}}},
                {"data", {{"configs", configs}}}};
    }
};

struct ApbConfigLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        const Json args = req.at("args");
        Json source;
        if (args.contains("config")) source = args.at("config");
        else {
            std::ifstream stream(args.at("config_path").get<std::string>());
            if (!stream) return error_response("INVALID_FIELD",
                "cannot read APB config_path");
            try { stream >> source; }
            catch (const std::exception& exception) {
                return error_response("INVALID_FIELD",
                    std::string("invalid APB config JSON: ") + exception.what());
            }
        }
        ApbConfig config;
        std::string message;
        if (!parse_config(args.at("name"), source, config, message))
            return rich_error_response("INVALID_ARGUMENT", message,
                {{"invalid_arg", "args.config"},
                 {"expected", "strict APB config with only canonical fields and non-empty signal paths"},
                 {"correct_example", apb_config_load_example()},
                 {"example_note", "Example only; replace placeholders with active signal paths, names, and time values."}});
        auto* wf = engine_globals().waveform.get();
        uint32_t reset_ref = 0;
        IWaveformBackend::SignalInfo reset_info;
        Json error;
        if (!load_ref(*wf, config.reset, "reset", reset_ref, reset_info, error))
            return error;
        if (reset_info.width != 1)
            return error_response("INVALID_FIELD",
                "APB reset signal must resolve to one bit");
        apb_configs()[config.name] = config;
        apb_cursors().erase(config.name + ":all");
        apb_cursors().erase(config.name + ":read");
        apb_cursors().erase(config.name + ":write");
        return {{"ok", true},
            {"summary", {{"name", config.name}, {"status", "loaded"}}},
            {"data", {{"config", config_json(config)},
                      {"recommended_actions", recommended_actions()}}}};
    }
};

struct ApbQueryHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.query"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        const Json args = req.at("args");
        auto* wf = engine_globals().waveform.get();
        const ApbConfig* config = nullptr;
        ApbScan scan;
        Json error;
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error))
            return error;
        TimeRenderUnit unit;
        ValueRenderFormat format;
        if (!parse_render(args, *wf, unit, format, error)) return error;
        const std::string direction = args.value("direction", "all");
        const Json address = args.value("address", Json());
        std::vector<const ApbTransaction*> matches;
        for (const ApbTransaction& txn : scan.transactions)
            if (direction_matches(txn, direction) &&
                address_matches(txn, address) == FilterResult::Yes)
                matches.push_back(&txn);
        Json filter{{"direction", direction}};
        if (!address.is_null()) filter["address"] = address;
        const Json query = args.value("query", Json::object());
        const int index = query.value("index", -1);
        const int line_limit = query.value("line_limit", -1);
        const bool last = args.value("last", false);
        Json summary{{"name", args.at("name")}, {"direction", direction}};
        Json data{{"filter", filter}};
        if (last || (index > 0 && line_limit < 0)) {
            const size_t offset = last ? (matches.empty() ? 0 : matches.size() - 1)
                : static_cast<size_t>(index - 1);
            const bool found = !matches.empty() && offset < matches.size();
            summary["query_mode"] = last ? "last" : "index";
            summary["found"] = found;
            if (found) data["transaction"] =
                transaction_json(*matches[offset], *wf, unit, format);
            merge(summary, completeness_summary(scan.complete, false,
                                                 matches.size(), found ? 1 : 0));
            if (found || !address.is_null()) set_value_width_complete(summary);
        } else if (line_limit > 0) {
            const size_t begin = index > 0 ? static_cast<size_t>(index - 1) : 0;
            Json transactions = Json::array();
            for (size_t i = begin; i < matches.size() &&
                 transactions.size() < static_cast<size_t>(line_limit); ++i)
                transactions.push_back(transaction_json(
                    *matches[i], *wf, unit, format));
            summary["query_mode"] = "list";
            const bool truncated = begin + transactions.size() < matches.size();
            merge(summary, completeness_summary(scan.complete, truncated,
                matches.size(), transactions.size()));
            if (!transactions.empty() || !address.is_null())
                set_value_width_complete(summary);
            data["transactions"] = std::move(transactions);
        } else {
            summary["query_mode"] = "count";
            merge(summary, completeness_summary(scan.complete, false,
                                                 matches.size(), 0));
        }
        return {{"ok", true}, {"summary", summary}, {"data", data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_apb_query_xout(response);
    }
};

struct ApbStatisticsHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.statistics"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        const Json args = req.at("args");
        auto* wf = engine_globals().waveform.get();
        const ApbConfig* config = nullptr;
        ApbScan scan;
        Json error;
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error,
                                     action_name()))
            return error;
        const Json input_filter = args.value("filter", Json::object());
        const std::string direction = input_filter.value("direction", "all");
        const Json address = input_filter.value("address", Json());
        size_t reads = 0, writes = 0, unresolved = scan.unresolved;
        for (const ApbTransaction& txn : scan.transactions) {
            if (!direction_matches(txn, direction)) continue;
            const FilterResult matched = address_matches(txn, address);
            if (matched == FilterResult::Unresolved) ++unresolved;
            else if (matched == FilterResult::Yes)
                txn.is_write ? ++writes : ++reads;
        }
        const size_t matched = reads + writes;
        const bool complete = scan.complete && unresolved == 0;
        Json filter{{"direction", direction}};
        if (!address.is_null()) filter["address"] = address;
        Json summary{{"name", args.at("name")},
            {"scanned_transaction_count", scan.transactions.size()},
            {"matched_transaction_count", matched},
            {"matched_read_count", reads}, {"matched_write_count", writes},
            {"unresolved_transaction_count", unresolved},
            {"filter_applied", !input_filter.empty()},
            {"analysis_quality", complete ? "complete" : "ambiguous"},
            {"full_scan_count", 1}};
        merge(summary, completeness_summary(scan.complete, false,
                                            matched, matched));
        if (!address.is_null()) set_value_width_complete(summary);
        return {{"ok", true}, {"summary", summary}, {"data", {
            {"filter", filter}, {"notes", {{"unresolved_transaction_count",
                "因被引用的 address/ID 含 X/Z 或不可解析，导致无法判断是否匹配过滤条件的已完成事务数。"}}}}}};
    }


    std::string render_xout(const Json& response) const override {
        return render_apb_statistics_xout(response);
    }
};

struct ApbTransactionCursorHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.transaction.cursor"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        const Json args = req.at("args");
        auto* wf = engine_globals().waveform.get();
        const ApbConfig* config = nullptr;
        ApbScan scan;
        Json error;
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error,
                                     action_name()))
            return error;
        TimeRenderUnit unit;
        ValueRenderFormat format;
        if (!parse_render(args, *wf, unit, format, error)) return error;
        const std::string direction = args.value("direction", "all");
        std::vector<const ApbTransaction*> matches;
        for (const ApbTransaction& txn : scan.transactions)
            if (direction_matches(txn, direction)) matches.push_back(&txn);
        const std::string key = args.at("name").get<std::string>() + ":" + direction;
        size_t& position = apb_cursors()[key];
        const std::string op = args.at("op");
        bool found = !matches.empty();
        if (found) {
            if (op == "begin") position = 0;
            else if (op == "last") position = matches.size() - 1;
            else if (op == "next") {
                if (position + 1 < matches.size()) ++position;
                else found = false;
            } else {
                if (position > 0) --position;
                else found = false;
            }
        }
        Json summary{{"name", args.at("name")}, {"op", op},
            {"direction", direction}, {"found", found},
            {"index", found ? Json(position + 1) : Json(nullptr)},
            {"index_base", 1}, {"at_begin", found && position == 0},
            {"at_end", found && position + 1 == matches.size()}};
        merge(summary, completeness_summary(scan.complete, false,
                                            matches.size(), found ? 1 : 0));
        if (found) set_value_width_complete(summary);
        Json data = Json::object();
        if (found) data["transaction"] =
            transaction_json(*matches[position], *wf, unit, format);
        return {{"ok", true}, {"summary", summary}, {"data", data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_apb_cursor_xout(response);
    }
};

struct ApbTransferWindowHandler : public EngineActionHandler {
    const char* action_name() const override { return "apb.transfer_window"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        const Json args = req.at("args");
        auto* wf = engine_globals().waveform.get();
        const ApbConfig* config = nullptr;
        ApbScan scan;
        Json error;
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error,
                                     action_name()))
            return error;
        TimeRenderUnit unit;
        ValueRenderFormat format;
        if (!parse_render(args, *wf, unit, format, error)) return error;
        uint64_t begin = wf->min_time(), end = wf->max_time();
        const Json range = args.value("time_range", Json::object());
        std::string message;
        if (range.contains("begin") &&
            !wf->parse_time(range.at("begin"), begin, message))
            return error_response("INVALID_TIME", message);
        if (range.contains("end") &&
            !wf->parse_time(range.at("end"), end, message, true))
            return error_response("INVALID_TIME", message);
        if (begin > end)
            return rich_error_response("TIME_RANGE_INVALID",
                "args.time_range.end is before args.time_range.begin",
                {{"invalid_arg", "args.time_range.end"},
                 {"expected", "time_range.end must be greater than or equal to time_range.begin"},
                 {"correct_example", apb_window_example()}});
        const size_t limit = args.value("line_limit", 1000u);
        size_t total = 0;
        Json transactions = Json::array();
        for (const ApbTransaction& txn : scan.transactions) {
            if (txn.time < begin || txn.time > end) continue;
            ++total;
            if (transactions.size() < limit)
                transactions.push_back(transaction_json(
                    txn, *wf, unit, format, true));
        }
        const bool truncated = transactions.size() < total;
        Json summary{{"name", args.at("name")},
            {"begin", wf->format_time(begin, unit)},
            {"end", wf->format_time(end, unit)}};
        merge(summary, completeness_summary(scan.complete, truncated,
                                            total, transactions.size()));
        if (!transactions.empty()) set_value_width_complete(summary);
        return {{"ok", true}, {"summary", summary},
                {"data", {{"transactions", transactions}}}};
    }

    std::string render_xout(const Json& response) const override {
        return render_apb_window_xout(response);
    }
};

std::unique_ptr<EngineActionHandler> make_apb_config_list_handler() {
    return std::make_unique<ApbConfigListHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_config_load_handler() {
    return std::make_unique<ApbConfigLoadHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_query_handler() {
    return std::make_unique<ApbQueryHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_statistics_handler() {
    return std::make_unique<ApbStatisticsHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_transaction_cursor_handler() {
    return std::make_unique<ApbTransactionCursorHandler>();
}
std::unique_ptr<EngineActionHandler> make_apb_transfer_window_handler() {
    return std::make_unique<ApbTransferWindowHandler>();
}

}  // namespace xdebug_fst
