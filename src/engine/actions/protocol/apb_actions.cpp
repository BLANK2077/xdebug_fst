// apb_actions.cpp — APB protocol actions over the current Wellen FST session.
// BSD-3-Clause
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "engine/actions/value_source_entries.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "api/json_types.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
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
        message = "negedge APB config must omit sample_point";
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
                             Json& error) {
    config = find_config(name);
    if (!config) {
        error = error_response("CONFIG_NOT_FOUND",
            "APB config not found: " + name);
        return false;
    }
    scan = scan_transactions(wf, *config, error);
    return error.is_null();
}

Json recommended_actions() {
    return Json::array({
        {{"action", "value.at"},
         {"purpose", "按一个或多个指定时间读取单信号、命名信号列表或接口配置维护的值。"}},
        {{"action", "apb.query"}, {"purpose", "查询 APB transfer。"}},
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
            if (!config) return error_response("CONFIG_NOT_FOUND",
                "APB config not found: " + name);
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
            return error_response("INVALID_FIELD", message);
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
            data["transactions"] = std::move(transactions);
        } else {
            summary["query_mode"] = "count";
            merge(summary, completeness_summary(scan.complete, false,
                                                 matches.size(), 0));
        }
        return {{"ok", true}, {"summary", summary}, {"data", data}};
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
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error))
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
        return {{"ok", true}, {"summary", summary}, {"data", {
            {"filter", filter}, {"notes", {{"unresolved_transaction_count",
                "因被引用的 address/ID 含 X/Z 或不可解析，导致无法判断是否匹配过滤条件的已完成事务数。"}}}}}};
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
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error))
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
        Json data = Json::object();
        if (found) data["transaction"] =
            transaction_json(*matches[position], *wf, unit, format);
        return {{"ok", true}, {"summary", summary}, {"data", data}};
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
        if (!require_config_and_scan(args.at("name"), *wf, config, scan, error))
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
            return error_response("TIME_RANGE_INVALID", "end is before begin");
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
        return {{"ok", true}, {"summary", summary},
                {"data", {{"transactions", transactions}}}};
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
