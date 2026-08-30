// stream_actions.cpp — stream.config.list/get/load, stream.describe/query/export/validate
// BSD-3-Clause License
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/expr/expr_eval.h"
#include "api/json_types.h"
#include "engine/actions/value_source_entries.h"
#include "protocol/domain_xout_renderer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace xdebug_fst {

// ── StreamConfig (xdebug-fst simplified config) ──

struct StreamConfig {
    std::string name;
    Json source;
    std::map<std::string,std::string> signals;
    std::string clock;   // clock signal name
    std::string valid;   // valid signal name
    std::string ready;   // ready signal name
    std::string backpressure; // active-high back-pressure signal name
    std::string data;    // data signal name (optional)
    std::string edge = "posedge";
    std::string sample_point = "before";
    std::string reset;
    std::string reset_polarity;
    std::string sop, eop;
    Json beat_fields = Json::object();
    Json packet_stable_fields = Json::object();
    std::string channel_id;
    std::string channel_id_valid = "every_beat";
    bool allow_interleaving = false;
    uint64_t start = 0;  // window begin (0 = full)
    uint64_t end = 0;    // window end (0 = full)
    bool active_high = true;
};

static Json config_to_json(const StreamConfig& cfg) {
    return cfg.source;
}

// ── In-process config store ──

static std::map<std::string, StreamConfig>& config_store() {
    static std::map<std::string, StreamConfig> store;
    return store;
}

/// Clear all user-loaded stream configs (called on session.open).
void clear_stream_configs() {
    config_store().clear();
}


// ── Helpers ──

/// True if bits represent a logic "high" (not all zero, no x/z/X/Z).
/// Per task spec: 位串非全零且不含 x/z（back 位为 '1'）
static bool is_high(const std::string& bits) {
    if (bits.empty()) return false;
    for (char c : bits) {
        if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') return false;
    }
    // Not all zeros?
    for (char c : bits) {
        if (c != '0') return true;
    }
    return false;
}

static bool is_known_binary(const std::string& bits) {
    if (bits.empty()) return false;
    for (char bit : bits)
        if (bit != '0' && bit != '1') return false;
    return true;
}

static std::string signal_bits_at(IWaveformBackend* wf, uint32_t ref,
                                  uint32_t ti) {
    IWaveformBackend::SignalOffset off;
    if (!wf->signal_offset_at(ref, ti, off)) return {};
    return wf->signal_value_str(ref, off.start, 0);
}

static std::string sampled_signal_bits_at(
        IWaveformBackend* wf, uint32_t ref, uint32_t ti,
        IWaveformBackend::ObservationPoint point) {
    IWaveformBackend::SampledValue sampled;
    return ref && wf->sampled_value_at(ref, ti, point, sampled)
        ? sampled.value.text : std::string();
}

/// Check if a signal value at a time index is considered "high".
/// Returns false if signal not found / not loaded.
static bool signal_is_high_at(IWaveformBackend* wf, uint32_t ref, uint32_t ti) {
    IWaveformBackend::SignalOffset off;
    if (!wf->signal_offset_at(ref, ti, off)) return false;
    std::string bits = wf->signal_value_str(ref, off.start, 0);
    return is_high(bits);
}

/// Load a signal by name; return ref (0 if not found). Load if needed.
static uint32_t load_signal(IWaveformBackend* wf,
                           const std::string& name) {
    uint32_t ref = wf->find_signal(name);
    if (ref == IWaveformBackend::kInvalidSignalRef) return IWaveformBackend::kInvalidSignalRef;
    if (!wf->is_loaded(ref)) wf->load_signals({ref});
    return ref;
}

/// Resolve a time value from args or default to waveform bounds.
static bool resolve_time_range(const Json& args, IWaveformBackend* wf,
                               uint64_t& begin, uint64_t& end) {
    begin = 0;
    end = wf->max_time();
    if (args.contains("begin")) {
        if (args["begin"].is_number()) begin = args["begin"].get<uint64_t>();
        else if (args["begin"].is_string()) {
            try { begin = std::stoull(args["begin"].get<std::string>()); }
            catch (...) { return false; }
        } else return false;
    }
    if (args.contains("end")) {
        if (args["end"].is_number()) end = args["end"].get<uint64_t>();
        else if (args["end"].is_string()) {
            try { end = std::stoull(args["end"].get<std::string>()); }
            catch (...) { return false; }
        } else return false;
    }
    return true;
}

/// Check waveform loaded, return error Json if not.
static Json check_waveform(const char* action) {
    auto& g = engine_globals();
    if (!g.has_waveform || !g.waveform) {
        return Json{{"ok", false},
                    {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                               {"message", std::string("action requires waveform file: ") + action}}}};
    }
    return Json::object(); // empty = ok
}

/// Find and load all signals referenced by a config. Returns error Json if any missing.
static Json validate_config_signals(IWaveformBackend* wf, const StreamConfig& cfg,
                                    std::vector<std::string>& missing) {
    missing.clear();
    if (!cfg.clock.empty() && wf->find_signal(cfg.clock) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.clock);
    if (!cfg.valid.empty() && wf->find_signal(cfg.valid) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.valid);
    if (!cfg.ready.empty() && wf->find_signal(cfg.ready) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.ready);
    if (!cfg.backpressure.empty() &&
        wf->find_signal(cfg.backpressure) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.backpressure);
    if (!cfg.data.empty() && wf->find_signal(cfg.data) == IWaveformBackend::kInvalidSignalRef)
        missing.push_back(cfg.data);
    if (!missing.empty()) {
        Json arr = Json::array();
        for (auto& m : missing) arr.push_back(m);
        return Json{{"ok", false},
                    {"error", {{"code", "CONFIG_SIGNAL_NOT_FOUND"},
                               {"message", "some signals not found in waveform"},
                               {"missing_signals", arr}}}};
    }
    return Json::object(); // ok
}

/// Load the named config (defaults to "default"). Returns error if not found.
static bool get_config(const std::string& name, StreamConfig& cfg, Json& err) {
    auto& store = config_store();
    auto it = store.find(name);
    if (it == store.end()) {
        err = Json{{"ok", false},
                   {"error", {{"code", "CONFIG_NOT_FOUND"},
                              {"message", "stream config not found: " + name}}}};
        return false;
    }
    cfg = it->second;
    return true;
}

bool stream_value_source_entries(const std::string& name,
                                 std::vector<ValueSourceEntry>& out) {
    Json error;
    StreamConfig config;
    if (!get_config(name, config, error)) return false;
    out = {{"clock", config.clock}, {"valid", config.valid}};
    if (!config.ready.empty()) out.push_back({"ready",config.ready});
    if (!config.backpressure.empty()) out.push_back({"bp",config.backpressure});
    if (!config.data.empty()) out.push_back({"data", config.data});
    return true;
}

static Json action_error(const std::string& code, const std::string& message) {
    return {{"ok",false},{"error",{{"code",code},{"message",message}}}};
}

static Json invalid_stream_config_error(const std::string& message) {
    return {{"ok",false},{"error",{
        {"code","INVALID_ARGUMENT"},
        {"message",message},
        {"invalid_arg","args.config.streams"},
        {"expected","stream config with signals map and alias-based clock/vld/data fields"},
        {"error_layer","handler"},
        {"recoverable",true},
        {"example_note","Example only; stream config fields must reference aliases, not raw signal paths."},
        {"correct_example",{
            {"api_version","xdebug.v1"},
            {"action","stream.config.load"},
            {"args",{{"config",{{"streams",Json::array({{
                {"name","req_stream"},
                {"signals",{
                    {"clk","top.u.clk"},
                    {"req_data","top.u.req_data"},
                    {"req_vld","top.u.req_vld"},
                }},
                {"clock","clk"},
                {"vld","req_vld"},
                {"data","req_data"},
            }})}}}}},
            {"target",{{"session_id","case_a"}}},
        }},
    }}};
}

static std::string resolve_field_expression(const std::string& expression,
                                            const StreamConfig& cfg);

static bool parse_stream(const Json& input, StreamConfig& cfg, std::string& message) {
    cfg = {};
    cfg.name = input.at("name");
    cfg.signals = input.at("signals").get<std::map<std::string,std::string>>();
    auto resolve = [&](const std::string& alias) -> std::string {
        auto found = cfg.signals.find(alias);
        return found == cfg.signals.end() ? std::string() : found->second;
    };
    auto resolved_expression = [&](const char* key) -> std::string {
        return input.contains(key)
            ? resolve_field_expression(input.at(key).get<std::string>(), cfg)
            : std::string();
    };
    cfg.clock = resolved_expression("clock");
    cfg.valid = resolved_expression("vld");
    cfg.ready = resolved_expression("rdy");
    cfg.backpressure = resolved_expression("bp");
    cfg.sop = resolved_expression("sop");
    cfg.eop = resolved_expression("eop");
    cfg.edge = input.value("edge","posedge");
    cfg.sample_point = input.value("sample_point", cfg.edge == "negedge" ? "" : "before");
    if (input.contains("reset")) {
        cfg.reset = input.at("reset").at("signal");
        cfg.reset_polarity = input.at("reset").at("polarity");
    }
    cfg.beat_fields = input.value("beat_fields",Json::object());
    cfg.packet_stable_fields = input.value(
        "packet_stable_fields", Json::object());
    cfg.channel_id = resolved_expression("channel_id");
    cfg.channel_id_valid = input.value("channel_id_valid","every_beat");
    cfg.allow_interleaving = input.value("allow_interleaving",false);
    if (cfg.clock.empty() || cfg.valid.empty() ||
        (input.contains("rdy")&&cfg.ready.empty()) ||
        (input.contains("bp")&&cfg.backpressure.empty())) {
        message = "stream clock/vld/flow-control alias is missing from signals";
        return false;
    }
    cfg.data.clear();
    if (input.contains("data")) {
        cfg.data = resolved_expression("data");
    } else if (cfg.beat_fields.contains("data") &&
               cfg.beat_fields.at("data").is_string()) {
        cfg.data = resolve(cfg.beat_fields.at("data"));
    }
    if ((cfg.sop.empty()) != (cfg.eop.empty())) {
        message = "stream " + cfg.name +
            " requires sop and eop to be configured together";
        return false;
    }
    if (cfg.allow_interleaving) {
        if (cfg.sop.empty()) {
            message = "stream " + cfg.name +
                " allow_interleaving requires sop/eop";
            return false;
        }
        if (cfg.channel_id.empty()) {
            message = "stream " + cfg.name +
                " allow_interleaving requires channel_id";
            return false;
        }
        if (cfg.channel_id_valid != "every_beat") {
            message = "stream " + cfg.name +
                " allow_interleaving requires channel_id_valid=every_beat";
            return false;
        }
    }
    cfg.source = input;
    cfg.source["edge"] = cfg.edge;
    if (cfg.edge != "negedge") cfg.source["sample_point"] = cfg.sample_point;
    cfg.source["channel_id_valid"] = cfg.channel_id_valid;
    cfg.source["allow_interleaving"] = cfg.allow_interleaving;
    return true;
}

static std::string stream_handshake(const StreamConfig& cfg) {
    if (!cfg.ready.empty()&&!cfg.backpressure.empty()) return "vld/rdy/bp";
    if (!cfg.ready.empty()) return "vld/rdy";
    if (!cfg.backpressure.empty()) return "vld/bp";
    return "vld";
}

static Json validate_stream(IWaveformBackend* wf, const StreamConfig& cfg,
                            bool include_stream) {
    Json signals = Json::array();
    bool ok = true;
    for (const auto& item : cfg.signals) {
        const uint32_t ref = wf->find_signal(item.second);
        Json signal{{"alias",item.first},{"requested_path",item.second}};
        if (!ref) { signal["status"] = "signal_not_found"; ok = false; }
        else {
            if (!wf->is_loaded(ref)) wf->load_signals({ref});
            IWaveformBackend::SignalInfo info;
            if (!wf->signal_info(ref,info)) { signal["status"] = "signal_not_found"; ok = false; }
            else { signal["status"]="ok"; signal["resolved_path"]=item.second; signal["width"]=info.width; }
        }
        signals.push_back(std::move(signal));
    }
    Json result{{"status",ok?"ok":"error"},{"signals",signals},
        {"sampling",{{"clock",cfg.source.at("clock")},{"edge",cfg.edge},
            {"sample_point",cfg.edge == "negedge" ? Json(nullptr) : Json(cfg.sample_point)}}},
        {"packet_rules",{{"packet_enabled",!cfg.sop.empty() && !cfg.eop.empty()},
            {"channel_id_valid",cfg.channel_id_valid},
            {"allow_interleaving",cfg.allow_interleaving}}}};
    if (include_stream) result["stream"] = cfg.name;
    return result;
}

static Json recommendations() {
    return Json::array({
        {{"action","value.at"},{"purpose","按一个或多个指定时间读取单信号、命名信号列表或接口配置维护的值。"}},
        {{"action","stream.describe"},{"purpose","显示 stream 定义和摘要。"}},
        {{"action","stream.validate"},{"purpose","验证 stream 配置；动态验证可显式选择 full 或 range 基础分析缓存范围。"}},
        {{"action","stream.query"},{"purpose","查询通用 valid-ready transfer、stall 或 packet，并对多个自定义字段执行 exact、range 或 mask 过滤。"}},
        {{"action","stream.export"},{"purpose","从显式 full 或 range 基础分析缓存范围导出 stream 查询结果。"}}
    });
}

static std::string resolve_field_expression(const std::string& expression,
                                            const StreamConfig& cfg) {
    std::string resolved;
    for (size_t pos = 0; pos < expression.size();) {
        const unsigned char ch = static_cast<unsigned char>(expression[pos]);
        if (std::isalpha(ch) || expression[pos] == '_') {
            size_t end = pos + 1;
            while (end < expression.size()) {
                const unsigned char next = static_cast<unsigned char>(expression[end]);
                if (!std::isalnum(next) && expression[end] != '_' && expression[end] != '$') break;
                ++end;
            }
            const std::string token = expression.substr(pos,end-pos);
            auto found = cfg.signals.find(token);
            resolved += found == cfg.signals.end() ? token : found->second;
            pos = end;
        } else {
            resolved.push_back(expression[pos++]);
        }
    }
    return resolved;
}

static bool evaluate_field_expression(const std::string& expression,
                                      const StreamConfig& cfg,
                                      const IWaveformBackend& wf,
                                      uint32_t time_idx,
                                      IWaveformBackend::ObservationPoint point,
                                      LogicValue& value,
                                      std::string& message) {
    std::string text = expression;
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if (first == std::string::npos) { message = "empty stream field expression"; return false; }
    text = text.substr(first,last-first+1);
    if (text.front() == '{' && text.back() == '}') {
        const std::string inner = text.substr(1,text.size()-2);
        std::string part;
        std::vector<std::string> parts;
        int square_depth = 0;
        for (char c : inner) {
            if (c == '[') ++square_depth;
            if (c == ']') --square_depth;
            if (c == ',' && square_depth == 0) {
                parts.push_back(part); part.clear();
            } else part.push_back(c);
        }
        parts.push_back(part);
        std::string bits;
        for (const auto& item : parts) {
            LogicValue component;
            if (!evaluate_field_expression(
                    item, cfg, wf, time_idx, point, component, message))
                return false;
            bits += component.bits;
        }
        value = logic_value_from_bits(bits,static_cast<int>(bits.size()));
        return true;
    }
    const std::string resolved = resolve_field_expression(text,cfg);
    std::unique_ptr<ExprNode> root(parse_expression(resolved,message));
    if (!root) return false;
    value = eval_expression(root.get(), wf, time_idx, nullptr, point);
    return !value.bits.empty();
}

struct CompiledStreamExpressions {
    std::unique_ptr<ExprNode> clock;
    std::unique_ptr<ExprNode> valid;
    std::unique_ptr<ExprNode> ready;
    std::unique_ptr<ExprNode> backpressure;
    std::unique_ptr<ExprNode> sop;
    std::unique_ptr<ExprNode> eop;
    std::unique_ptr<ExprNode> data;
    std::unique_ptr<ExprNode> channel_id;
    std::vector<uint32_t> clock_dependencies;
};

static bool compile_stream_expression(
        IWaveformBackend* wf, const std::string& label,
        const std::string& expression, std::unique_ptr<ExprNode>& output,
        std::string& message) {
    if (expression.empty()) return true;
    output.reset(parse_expression(expression,message));
    if (!output) {
        message = label + " expression parse failed: " + message;
        return false;
    }
    for (const std::string& signal : expression_signals(output.get())) {
        const uint32_t ref = load_signal(wf,signal);
        if (ref == IWaveformBackend::kInvalidSignalRef) {
            message = "signal not found for " + label + ": " + signal;
            return false;
        }
    }
    return true;
}

static bool compile_stream_expressions(
        IWaveformBackend* wf, const StreamConfig& cfg,
        CompiledStreamExpressions& compiled, std::string& message) {
    if (!compile_stream_expression(
            wf,"clock",cfg.clock,compiled.clock,message) ||
        !compile_stream_expression(
            wf,"vld",cfg.valid,compiled.valid,message) ||
        !compile_stream_expression(
            wf,"rdy",cfg.ready,compiled.ready,message) ||
        !compile_stream_expression(
            wf,"bp",cfg.backpressure,compiled.backpressure,message) ||
        !compile_stream_expression(
            wf,"sop",cfg.sop,compiled.sop,message) ||
        !compile_stream_expression(
            wf,"eop",cfg.eop,compiled.eop,message) ||
        !compile_stream_expression(
            wf,"data",cfg.data,compiled.data,message) ||
        !compile_stream_expression(
            wf,"channel_id",cfg.channel_id,compiled.channel_id,message)) {
        return false;
    }
    if (!compiled.clock || !compiled.valid) {
        message = "stream clock and vld expressions are required";
        return false;
    }
    for (const std::string& signal : expression_signals(compiled.clock.get())) {
        const uint32_t ref = load_signal(wf,signal);
        if (ref != IWaveformBackend::kInvalidSignalRef)
            compiled.clock_dependencies.push_back(ref);
    }
    if (compiled.clock_dependencies.empty()) {
        message = "clock expression has no waveform dependency";
        return false;
    }
    return true;
}

static std::string expression_bits_at(
        const ExprNode* expression, const IWaveformBackend& wf,
        uint32_t time_idx, IWaveformBackend::ObservationPoint point,
        const std::string& default_bits) {
    if (!expression) return default_bits;
    return eval_expression(expression,wf,time_idx,nullptr,point).bits;
}

static Json expression_json_at(
        const ExprNode* expression, const IWaveformBackend& wf,
        uint32_t time_idx, IWaveformBackend::ObservationPoint point,
        ValueRenderFormat format, bool& complete) {
    if (!expression) return Json();
    const LogicValue value = eval_expression(
        expression,wf,time_idx,nullptr,point);
    if (value.bits.empty()) {
        complete=false;
        return Json();
    }
    return logic_value_json(value,format);
}

static Json stream_fields_at(const StreamConfig& cfg,
                             const CompiledStreamExpressions& compiled,
                             const IWaveformBackend& wf,
                             uint32_t time_idx,
                             IWaveformBackend::ObservationPoint point,
                             ValueRenderFormat format,
                             bool& complete) {
    Json fields = Json::object();
    if (cfg.source.contains("data") && compiled.data) {
        Json value=expression_json_at(
            compiled.data.get(),wf,time_idx,point,format,complete);
        if (!value.is_null()) fields["data"]=std::move(value);
    }
    for (const auto& item : cfg.beat_fields.items()) {
        LogicValue value;
        std::string message;
        if (!item.value().is_string() || !evaluate_field_expression(
                item.value(),cfg,wf,time_idx,point,value,message)) {
            complete = false;
            continue;
        }
        fields[item.key()] = logic_value_json(value,format);
    }
    return fields;
}

struct StreamSample {
    size_t cycle = 0;
    uint32_t time_idx = 0;
    uint64_t time = 0;
    bool vld = false, rdy = false, bp = false, sop = false, eop = false;
    bool transfer = false, stall = false;
    bool control_known = true, data_known = true;
    bool ready_bp_conflict = false;
    Json fields = Json::object();
    Json packet_stable_fields = Json::object();
    Json channel_id;
    int packet_index = -1;
    size_t beat_index = 0;
};

static bool parse_analysis_cache_budget(
        const char* name, uint64_t default_value, bool allow_zero,
        uint64_t& value, std::string& message) {
    const char* raw=std::getenv(name);
    if (raw==nullptr) {
        value=default_value;
        return true;
    }
    if (*raw=='\0') {
        message=std::string(name)+" must be a non-empty unsigned integer";
        return false;
    }
    uint64_t parsed=0;
    for (const unsigned char* cursor=
             reinterpret_cast<const unsigned char*>(raw);
         *cursor!='\0';++cursor) {
        if (!std::isdigit(*cursor)) {
            message=std::string(name)+
                " must contain only unsigned decimal digits";
            return false;
        }
        const uint64_t digit=*cursor-'0';
        if (parsed>(std::numeric_limits<uint64_t>::max()-digit)/10) {
            message=std::string(name)+" exceeds uint64 range";
            return false;
        }
        parsed=parsed*10+digit;
    }
    if (!allow_zero&&parsed==0) {
        message=std::string(name)+" must be positive";
        return false;
    }
    value=parsed;
    return true;
}

static std::string stream_cache_key_summary(
        const StreamConfig& cfg, const Json& args) {
    const std::string material=cfg.name+"\n"+cfg.source.dump()+"\n"+
        args.value("cache_scope",std::string("full"))+"\n"+
        args.value("time_range",Json::object()).dump();
    uint64_t hash=1469598103934665603ULL;
    for (unsigned char byte : material) {
        hash^=static_cast<uint64_t>(byte);
        hash*=1099511628211ULL;
    }
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}

static Json stream_analysis_budget_error(
        const StreamConfig& cfg, const Json& args) {
    uint64_t soft_max_bytes=0,hard_max_bytes=0;
    std::string message;
    if (!parse_analysis_cache_budget(
            "XDEBUG_ANALYSIS_CACHE_MAX_BYTES",1073741824ULL,true,
            soft_max_bytes,message) ||
        !parse_analysis_cache_budget(
            "XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES",2147483648ULL,false,
            hard_max_bytes,message)) {
        return {{"ok",false},{"error",{
            {"code","INVALID_ENVIRONMENT"},
            {"message",message},
            {"error_layer","handler"},
            {"recoverable",false},
        }}};
    }
    if (soft_max_bytes>hard_max_bytes) {
        return {{"ok",false},{"error",{
            {"code","INVALID_ENVIRONMENT"},
            {"message","XDEBUG_ANALYSIS_CACHE_MAX_BYTES must not exceed "
                "XDEBUG_ANALYSIS_CACHE_HARD_MAX_BYTES"},
            {"error_layer","handler"},
            {"recoverable",false},
        }}};
    }
    if (hard_max_bytes>=sizeof(StreamSample)) return Json::object();
    return {{"ok",false},{"error",{
        {"code","ANALYSIS_MEMORY_LIMIT_EXCEEDED"},
        {"message","analysis cache build exceeds the configured hard memory limit"},
        {"recoverable",true},
        {"error_layer","handler"},
        {"current_estimated_bytes",0},
        {"hard_max_bytes",hard_max_bytes},
        {"protocol","stream"},
        {"key_summary",stream_cache_key_summary(cfg,args)},
        {"next_actions",Json::array({
            "For stream analysis, explicitly retry with cache_scope=range or a smaller time_range.",
            "If range analysis still exceeds the limit, use x-npi for one-off offline analysis.",
        })},
    }}};
}

static std::vector<StreamSample> scan_stream_samples(
    IWaveformBackend* wf, const StreamConfig& cfg,
    const CompiledStreamExpressions& compiled,
    uint32_t begin_ti, uint32_t end_ti, ValueRenderFormat format,
    bool& fields_complete) {
    std::vector<StreamSample> samples;
    const uint32_t reset_ref=cfg.reset.empty()?0:load_signal(wf,cfg.reset);
    const IWaveformBackend::ObservationPoint point = cfg.edge == "negedge"
        ? IWaveformBackend::ObservationPoint::Raw
        : (cfg.sample_point == "after"
            ? IWaveformBackend::ObservationPoint::After
            : IWaveformBackend::ObservationPoint::Before);
    size_t cycle = 0;
    std::set<uint32_t> clock_time_indices;
    for (uint32_t ref : compiled.clock_dependencies) {
        const auto indices=wf->time_indices_of(ref);
        clock_time_indices.insert(indices.begin(),indices.end());
    }
    for (uint32_t ti : clock_time_indices) {
        const std::string before_clock=expression_bits_at(
            compiled.clock.get(),*wf,ti,
            IWaveformBackend::ObservationPoint::Before,{});
        const std::string raw_clock=expression_bits_at(
            compiled.clock.get(),*wf,ti,
            IWaveformBackend::ObservationPoint::Raw,{});
        const bool rising = is_rising_edge(
            before_clock, raw_clock);
        const bool falling = is_falling_edge(
            before_clock, raw_clock);
        const bool selected = cfg.edge == "dual" ? (rising || falling)
            : cfg.edge == "posedge" ? rising : falling;
        if (!selected || ti < begin_ti) continue;
        if (ti > end_ti) break;
        StreamSample sample;
        sample.cycle=cycle++; sample.time_idx=ti; sample.time=wf->time_at(ti);
        if (reset_ref) {
            const std::string reset_bits = sampled_signal_bits_at(
                wf, reset_ref, ti, point);
            const bool reset_deasserted = cfg.reset_polarity == "active_low"
                ? reset_bits == "1" : reset_bits == "0";
            if (!reset_deasserted) {
                samples.push_back(std::move(sample));
                continue;
            }
        }
        const std::string vld_bits=expression_bits_at(
            compiled.valid.get(),*wf,ti,point,{});
        const std::string rdy_bits=expression_bits_at(
            compiled.ready.get(),*wf,ti,point,"1");
        const std::string bp_bits=expression_bits_at(
            compiled.backpressure.get(),*wf,ti,point,"0");
        const std::string sop_bits=expression_bits_at(
            compiled.sop.get(),*wf,ti,point,"0");
        const std::string eop_bits=expression_bits_at(
            compiled.eop.get(),*wf,ti,point,"0");
        sample.control_known=is_known_binary(vld_bits)&&
            is_known_binary(rdy_bits)&&is_known_binary(bp_bits)&&
            is_known_binary(sop_bits)&&is_known_binary(eop_bits);
        sample.ready_bp_conflict=compiled.ready&&compiled.backpressure&&
            is_high(rdy_bits)&&is_high(bp_bits);
        const bool vld_now=is_high(vld_bits);
        const bool rdy_now=compiled.ready&&is_high(rdy_bits);
        sample.vld=vld_now;
        sample.rdy=compiled.ready?rdy_now:true;
        sample.bp=compiled.backpressure&&is_high(bp_bits);
        const bool ready = (!compiled.ready || rdy_now) && !sample.bp;
        sample.transfer=vld_now&&ready;
        // At an explicit post-edge observation the producer or consumer may
        // change its control on the same edge that accepted the transfer.
        // Preserve that accepted handshake by joining the post-edge level
        // with the other side's pre-edge level.  A before-edge query must not
        // use this join: it describes only the old values by contract.
        if (point == IWaveformBackend::ObservationPoint::After &&
            compiled.ready && !compiled.backpressure) {
            const bool vld_before=is_high(expression_bits_at(
                compiled.valid.get(),*wf,ti,
                IWaveformBackend::ObservationPoint::Before,{}));
            const bool rdy_before=is_high(expression_bits_at(
                compiled.ready.get(),*wf,ti,
                IWaveformBackend::ObservationPoint::Before,{}));
            sample.transfer=sample.transfer||
                (vld_now&&!vld_before&&rdy_before)||
                (rdy_now&&!rdy_before&&vld_before);
            sample.vld=sample.transfer||vld_now;
            sample.rdy=sample.transfer||rdy_now;
        }
        sample.stall=sample.vld&&!ready;
        sample.sop=sample.transfer&&compiled.sop&&is_high(sop_bits);
        sample.eop=sample.transfer&&compiled.eop&&is_high(eop_bits);
        if (sample.transfer) {
            sample.fields=stream_fields_at(
                cfg, compiled, *wf, ti, point, format, fields_complete);
            for (const auto& item : cfg.packet_stable_fields.items()) {
                LogicValue value;
                std::string message;
                if (item.value().is_string() && evaluate_field_expression(
                        item.value(),cfg,*wf,ti,point,value,message)) {
                    sample.packet_stable_fields[item.key()]=
                        logic_value_json(value,format);
                } else {
                    fields_complete=false;
                }
            }
            if (compiled.channel_id) {
                sample.channel_id=expression_json_at(
                    compiled.channel_id.get(),*wf,ti,point,format,
                    fields_complete);
            }
            for (const auto& field : sample.fields.items())
                if (!field.value().value("known",false)) sample.data_known=false;
        }
        samples.push_back(std::move(sample));
    }
    return samples;
}

static bool known_u64(const Json& value, uint64_t& result) {
    if (!value.value("known",false) || !value.contains("bits")) return false;
    const std::string bits=value.at("bits");
    if (bits.size()>64) return false;
    result=0;
    for (char bit : bits) {
        if (bit!='0'&&bit!='1') return false;
        result=(result<<1)|(bit=='1'?1u:0u);
    }
    return true;
}

static bool literal_u64(const std::string& text, uint64_t& result) {
    LogicValue value;
    if (!parse_sv_literal(text,value) || !value.known || value.bits.size()>64) return false;
    result=0;
    for (char bit : value.bits) result=(result<<1)|(bit=='1'?1u:0u);
    return true;
}

static bool stream_field_matches(const Json& value, const Json& rule) {
    uint64_t actual=0;
    if (!known_u64(value,actual)) return false;
    const std::string mode=rule.at("mode");
    if (mode=="exact") {
        for (const Json& candidate : rule.at("values")) {
            uint64_t expected=0;
            if (literal_u64(candidate,expected)&&actual==expected) return true;
        }
        return false;
    }
    if (mode=="range") {
        uint64_t begin=0,end=0;
        return literal_u64(rule.at("begin"),begin)&&literal_u64(rule.at("end"),end)&&
            actual>=begin&&actual<=end;
    }
    uint64_t expected=0,mask=0;
    return literal_u64(rule.at("value"),expected)&&literal_u64(rule.at("mask"),mask)&&
        (actual&mask)==(expected&mask);
}

enum class StreamFilterResult { Match, NoMatch, Unresolved };

static StreamFilterResult stream_field_filter_result(
        const Json& value, const Json& rule) {
    if (!value.is_object() || !value.contains("bits"))
        return StreamFilterResult::Unresolved;
    if (value.value("known",false))
        return stream_field_matches(value,rule)
            ? StreamFilterResult::Match : StreamFilterResult::NoMatch;
    if (rule.at("mode") != "mask")
        return StreamFilterResult::Unresolved;

    LogicValue expected,mask;
    if (!parse_sv_literal(rule.at("value"),expected) ||
        !parse_sv_literal(rule.at("mask"),mask) || !expected.known ||
        !mask.known)
        return StreamFilterResult::Unresolved;
    const std::string actual=value.at("bits");
    const size_t width=std::max({actual.size(),expected.bits.size(),mask.bits.size()});
    auto lsb_bit=[](const std::string& bits,size_t offset,char fill) {
        return offset<bits.size()?bits[bits.size()-1-offset]:fill;
    };
    for (size_t offset=0;offset<width;++offset) {
        if (lsb_bit(mask.bits,offset,'0')!='1') continue;
        const char actual_bit=lsb_bit(actual,offset,'0');
        if (actual_bit!='0'&&actual_bit!='1')
            return StreamFilterResult::Unresolved;
        if (actual_bit!=lsb_bit(expected.bits,offset,'0'))
            return StreamFilterResult::NoMatch;
    }
    return StreamFilterResult::Match;
}

// ── Helper: scan handshake events on clock edges ──

struct HandshakeEvent {
    uint32_t time_idx;
    uint64_t time;
    std::string data_bits;  // empty if no data signal configured
};

/// Scan for handshake events in [begin_ti, end_ti] using clock edges.
/// A handshake occurs at a rising clock edge where both valid and ready are high.
static std::vector<HandshakeEvent> scan_handshakes(
    IWaveformBackend* wf,
    uint32_t clk_ref, uint32_t vld_ref, uint32_t rdy_ref,
    uint32_t data_ref,    // 0 = no data
    uint32_t begin_ti, uint32_t end_ti,
    int max_rows = 0)     // 0 = no limit
{
    std::vector<HandshakeEvent> events;
    if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});
    if (!wf->is_loaded(vld_ref)) wf->load_signals({vld_ref});
    if (!wf->is_loaded(rdy_ref)) wf->load_signals({rdy_ref});
    if (data_ref && !wf->is_loaded(data_ref)) wf->load_signals({data_ref});

    // Get clock change time indices
    std::vector<uint32_t> clk_tis = wf->time_indices_of(clk_ref);
    if (clk_tis.empty()) return events;

    std::string prev_clk_bits;
    bool have_prev = false;

    // Find previous clock value before begin_ti for edge detection
    for (uint32_t ti : clk_tis) {
        if (ti > begin_ti && !have_prev) {
            // Find the last change before or at begin_ti
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(clk_ref, begin_ti, off)) {
                prev_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);
                have_prev = true;
            }
            break;
        }
    }

    for (uint32_t ti : clk_tis) {
        if (ti < begin_ti) {
            // Update previous value as we scan
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(clk_ref, ti, off) && off.time_match) {
                prev_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);
                have_prev = true;
            }
            continue;
        }
        if (ti > end_ti) break;

        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(clk_ref, ti, off)) continue;
        if (!off.time_match) continue;
        std::string cur_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);

        // Detect rising edge: prev = 0, cur = 1
        bool rising = have_prev && is_rising_edge(prev_clk_bits, cur_clk_bits);

        prev_clk_bits = cur_clk_bits;
        have_prev = true;

        if (!rising) continue;

        // Check valid and ready at this clock edge. Handshakes are level
        // sampled at the edge, but wave dumps record the post-edge state
        // (the slave may deassert ready in the same cycle it samples).
        // Accept cross combinations only when valid or ready just rose.
        bool v_now = signal_is_high_at(wf, vld_ref, ti);
        bool r_now = signal_is_high_at(wf, rdy_ref, ti);
        bool v_prev = ti > 0 && signal_is_high_at(wf, vld_ref, ti - 1);
        bool r_prev = ti > 0 && signal_is_high_at(wf, rdy_ref, ti - 1);
        bool v_rise = v_now && !v_prev;
        bool r_rise = r_now && !r_prev;
        bool hs = (v_now && r_now) || (v_now && v_rise && r_prev) ||
                  (r_now && r_rise && v_prev);
        if (!hs) continue;

        HandshakeEvent ev;
        ev.time_idx = ti;
        ev.time = wf->time_at(ti);

        if (data_ref) {
            IWaveformBackend::SignalOffset doff;
            if (wf->signal_offset_at(data_ref, ti, doff)) {
                ev.data_bits = wf->signal_value_str(data_ref, doff.start, 0);
            }
        }

        events.push_back(ev);

        if (max_rows > 0 && (int)events.size() >= max_rows) break;
    }

    return events;
}

/// Count valid_high, ready_high, handshake on clock edges in window.
/// Returns {valid_high_count, ready_high_count, handshake_count}.
static void count_handshake_stats(
    IWaveformBackend* wf,
    uint32_t clk_ref, uint32_t vld_ref, uint32_t rdy_ref,
    uint32_t begin_ti, uint32_t end_ti,
    uint64_t& valid_high_count, uint64_t& ready_high_count, uint64_t& handshake_count)
{
    valid_high_count = 0;
    ready_high_count = 0;
    handshake_count = 0;

    if (!wf->is_loaded(clk_ref)) wf->load_signals({clk_ref});
    if (!wf->is_loaded(vld_ref)) wf->load_signals({vld_ref});
    if (!wf->is_loaded(rdy_ref)) wf->load_signals({rdy_ref});

    std::vector<uint32_t> clk_tis = wf->time_indices_of(clk_ref);
    if (clk_tis.empty()) return;

    std::string prev_clk_bits;
    bool have_prev = false;

    for (uint32_t ti : clk_tis) {
        if (ti < begin_ti) {
            IWaveformBackend::SignalOffset off;
            if (wf->signal_offset_at(clk_ref, ti, off) && off.time_match) {
                prev_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);
                have_prev = true;
            }
            continue;
        }
        if (ti > end_ti) break;

        IWaveformBackend::SignalOffset off;
        if (!wf->signal_offset_at(clk_ref, ti, off)) continue;
        if (!off.time_match) continue;
        std::string cur_clk_bits = wf->signal_value_str(clk_ref, off.start, 0);

        bool rising = have_prev && is_rising_edge(prev_clk_bits, cur_clk_bits);
        prev_clk_bits = cur_clk_bits;
        have_prev = true;

        if (!rising) continue;

        bool vh = signal_is_high_at(wf, vld_ref, ti);
        bool rh = signal_is_high_at(wf, rdy_ref, ti);
        bool vp = ti > 0 && signal_is_high_at(wf, vld_ref, ti - 1);
        bool rp = ti > 0 && signal_is_high_at(wf, rdy_ref, ti - 1);
        bool hs = (vh && rh) || (vh && !vp && rp) || (rh && !rp && vp);

        if (vh) valid_high_count++;
        if (rh) ready_high_count++;
        if (hs) handshake_count++;
    }
}

// ═══════════════════════════════════════════════════════════════
// 1. stream.config.list
// ═══════════════════════════════════════════════════════════════

struct StreamConfigListHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto& store = config_store();
        const Json args = req.value("args",Json::object());
        const bool verbose = args.value("output",Json::object()).value("verbose",false);
        Json arr = Json::array();
        for (const auto& kv : store) {
            const auto& cfg = kv.second;
            Json item{{"name",cfg.name},{"sampling_mode","clock_edge"},
                {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
                {"handshake",stream_handshake(cfg)},{"packet",(!cfg.sop.empty()&&!cfg.eop.empty())?"sop/eop":"none"},
                {"field_count",cfg.beat_fields.size()+
                    cfg.packet_stable_fields.size()+
                    (cfg.source.contains("data")?1u:0u)},
                {"channel_id_valid",cfg.channel_id_valid},
                {"allow_interleaving",cfg.allow_interleaving}};
            if (cfg.edge != "negedge") item["sample_point"] = cfg.sample_point;
            if (verbose) item["config"] = config_to_json(cfg);
            arr.push_back(std::move(item));
        }
        return {{"ok",true},{"summary",{{"count",arr.size()}}},
            {"data",{{"streams",arr}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 2. stream.config.get
// ═══════════════════════════════════════════════════════════════

struct StreamConfigGetHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.config.get"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("name");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        return {{"ok",true},{"summary",{{"name",name}}},
            {"data",{{"stream",config_to_json(cfg)}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 3. stream.config.load
// ═══════════════════════════════════════════════════════════════

struct StreamConfigLoadHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        Json document;
        if (args.contains("config")) document = args.at("config");
        else {
            std::ifstream stream(args.at("config_path").get<std::string>());
            if (!stream) return action_error("INVALID_FIELD","cannot read stream config_path");
            try { stream >> document; } catch (...) {
                return action_error("INVALID_FIELD","invalid stream config JSON");
            }
        }
        const std::string mode = args.value("mode","replace");
        if (mode == "replace") config_store().clear();
        Json names = Json::array(), validations = Json::array(), issues = Json::array();
        for (const Json& item : document.at("streams")) {
            StreamConfig cfg; std::string message;
            if (!parse_stream(item,cfg,message))
                return invalid_stream_config_error(message);
            Json validation = validate_stream(engine_globals().waveform.get(),cfg,true);
            if (validation.at("status") != "ok")
                return action_error("CONFIG_SIGNAL_NOT_FOUND","stream config signal not found");
            const std::string clock_expression=item.at("clock");
            if (cfg.signals.find(clock_expression)==cfg.signals.end()) {
                issues.push_back({
                    {"code","CLOCK_COMPLEX"},
                    {"message","clock expression is not a plain signal; edge detection uses expression dependency changes"},
                    {"severity","WARNING"},
                    {"stream",cfg.name},
                });
            }
            names.push_back(cfg.name); validations.push_back(validation);
            config_store()[cfg.name] = std::move(cfg);
        }
        return {{"ok",true},{"summary",{{"loaded",names.size()},{"mode",mode}}},
            {"data",{{"streams",names},{"issues",issues},{"validation",validations},
                {"recommended_actions",recommendations()}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 4. stream.describe
// ═══════════════════════════════════════════════════════════════

struct StreamDescribeHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.describe"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        Json validation = validate_stream(engine_globals().waveform.get(),cfg,false);
        Json issues = Json::array();
        if (cfg.signals.find(cfg.source.at("clock").get<std::string>())==
            cfg.signals.end()) {
            issues.push_back({
                {"code","CLOCK_COMPLEX"},
                {"message","clock expression is not a plain signal; edge detection uses expression dependency changes"},
                {"severity","WARNING"},
            });
        }
        return {{"ok",true},{"summary",{{"stream",name},{"handshake",stream_handshake(cfg)},
            {"packet_enabled",!cfg.sop.empty()&&!cfg.eop.empty()}}},
            {"data",{{"config",config_to_json(cfg)},{"issues",issues},
                {"validation",validation},{"semantics",{{"transfer",stream_handshake(cfg)},
                    {"stall",cfg.ready.empty()&&cfg.backpressure.empty()?"none":"enabled"}}}}}};
    }
};

// ═══════════════════════════════════════════════════════════════
// 5. stream.query
// ═══════════════════════════════════════════════════════════════

struct StreamQueryHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.query"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        CompiledStreamExpressions compiled;
        std::string message;
        if (!compile_stream_expressions(wf,cfg,compiled,message))
            return action_error("CONFIG_SIGNAL_NOT_FOUND",message);

        uint64_t begin_time = wf->min_time(), end_time = wf->max_time();
        const Json range = args.value("time_range",Json::object());
        if (range.contains("begin") && !wf->parse_time(range.at("begin"),begin_time,message))
            return action_error("INVALID_TIME",message);
        if (range.contains("end") && !wf->parse_time(range.at("end"),end_time,message,true))
            return action_error("INVALID_TIME",message);
        if (begin_time > end_time) return action_error("TIME_RANGE_INVALID","end is before begin");

        int max_rows = args.value("line_limit",1000);

        uint32_t begin_ti = wf->time_idx_of(begin_time);
        uint32_t end_ti = wf->time_idx_of(end_time);

        ValueRenderFormat fmt = ValueRenderFormat::Hex;
        parse_value_render_format(args.value("value_format", "hex"), fmt);
        TimeRenderUnit unit; parse_time_render_unit(args.value("render_time_unit","ns"),unit,message);

        const std::string query = args.at("query");
        const bool packet_enabled=!cfg.sop.empty()&&!cfg.eop.empty();
        if (!packet_enabled && (query=="first_packet"||query=="last_packet"||
            query=="packet_at"||query=="packet_window"))
            return action_error("PACKET_NOT_CONFIGURED","stream has no sop/eop packet boundaries");

        Json budget_error=stream_analysis_budget_error(cfg,args);
        if (!budget_error.empty()) return budget_error;

        bool fields_complete=true;
        auto samples=scan_stream_samples(
            wf,cfg,compiled,begin_ti,end_ti,fmt,fields_complete);
        std::vector<StreamSample*> transfers;
        size_t vld_cycles=0,stall_cycles=0,control_xz_count=0,
            data_xz_count=0,ready_bp_conflict_count=0;
        for (auto& sample : samples) {
            if (sample.vld) ++vld_cycles;
            if (sample.stall) ++stall_cycles;
            if (sample.transfer) transfers.push_back(&sample);
            if (!sample.control_known) ++control_xz_count;
            if (sample.transfer && !sample.data_known) ++data_xz_count;
            if (sample.ready_bp_conflict) ++ready_bp_conflict_count;
        }

        auto row_json=[&](const StreamSample& sample, size_t beat_index) {
            Json row{{"cycle",sample.cycle},{"time",wf->format_time(sample.time,unit)},
                {"vld",sample.vld},{"rdy",sample.rdy},{"bp",sample.bp},
                {"sop",sample.sop},{"eop",sample.eop},{"transfer",sample.transfer},
                {"stall",sample.stall},{"beat_index",beat_index},{"fields",sample.fields}};
            if (!sample.channel_id.is_null())
                row["channel_id"]=sample.channel_id;
            if (sample.packet_index>=0) {
                row["packet_index"]=sample.packet_index;
                if (!sample.packet_stable_fields.empty())
                    row["packet_stable_fields"]=sample.packet_stable_fields;
            }
            return row;
        };
        Json stalls=Json::array();
        for (size_t pos=0;pos<samples.size();) {
            if (!samples[pos].stall) { ++pos; continue; }
            const size_t first=pos;
            while (pos+1<samples.size()&&samples[pos+1].stall) ++pos;
            const size_t end_boundary=std::min(pos+1,samples.size()-1);
            stalls.push_back({{"start_cycle",samples[first].cycle},
                {"end_cycle",samples[end_boundary].cycle},
                {"start_time",wf->format_time(samples[first].time,unit)},
                {"end_time",wf->format_time(samples[end_boundary].time,unit)},
                {"cycles",pos-first+1},{"reason",
                    cfg.backpressure.empty()?"rdy_low":"bp_high"}});
            ++pos;
        }

        Json packets=Json::array();
        struct PacketState {
            size_t packet_index=0;
            bool partial_begin=false;
            std::vector<StreamSample*> beats;
            Json channel_id;
            Json stable_fields=Json::object();
            Json stable_mismatches=Json::array();
        };
        std::map<std::string,PacketState> active_packets;
        std::vector<std::pair<size_t,Json>> completed_packet_json;
        size_t next_packet_index=0,complete_packets=0,partial_packets=0,
            packet_stable_mismatch_count=0;
        auto append_packet=[&](const std::string& key) {
            auto found=active_packets.find(key);
            if (found==active_packets.end()||found->second.beats.empty()) return;
            PacketState state=std::move(found->second);
            active_packets.erase(found);
            const bool partial_begin=state.partial_begin,
                partial_end=!state.beats.back()->eop;
            Json head=Json::array(),tail=Json::array();
            for (size_t index=0;index<state.beats.size();++index) {
                state.beats[index]->packet_index=static_cast<int>(state.packet_index);
                state.beats[index]->beat_index=index;
                Json beat{{"cycle",state.beats[index]->cycle},
                    {"time",wf->format_time(state.beats[index]->time,unit)},
                    {"beat_index",index},{"fields",state.beats[index]->fields}};
                if (index<5) head.push_back(beat);
                if (state.beats.size()>5 &&
                    index>=std::max<size_t>(5,state.beats.size()>10?
                        state.beats.size()-5:5)) tail.push_back(beat);
            }
            Json preview{{"head",head},{"tail",tail},{"scan_complete",true},
                {"analysis_complete",fields_complete},{"response_truncated",false},
                {"total_count",state.beats.size()},
                {"returned_count",head.size()+tail.size()},
                {"truncation_scopes",Json::array()}};
            Json packet{{"packet_index",state.packet_index},
                {"start_cycle",state.beats.front()->cycle},
                {"end_cycle",state.beats.back()->cycle},
                {"start_time",wf->format_time(state.beats.front()->time,unit)},
                {"end_time",wf->format_time(state.beats.back()->time,unit)},
                {"beat_count",state.beats.size()},{"partial_begin",partial_begin},
                {"partial_end",partial_end},
                {"packet_stable_fields",state.stable_fields},
                {"packet_stable_mismatches",state.stable_mismatches},
                {"beat_fields_preview",preview},
                {"first_fields",state.beats.front()->fields},
                {"last_fields",state.beats.back()->fields}};
            if (!state.channel_id.is_null())
                packet["channel_id"]=state.channel_id;
            completed_packet_json.emplace_back(
                state.packet_index,std::move(packet));
            packet_stable_mismatch_count+=state.stable_mismatches.size();
            partial_begin||partial_end?++partial_packets:++complete_packets;
        };
        if (packet_enabled) {
            for (auto* transfer : transfers) {
                const std::string key=cfg.allow_interleaving&&
                    transfer->channel_id.is_object()
                    ? transfer->channel_id.value("bits",std::string())
                    : std::string();
                if (transfer->sop && active_packets.count(key))
                    append_packet(key);
                if (!active_packets.count(key)) {
                    PacketState state;
                    state.packet_index=next_packet_index++;
                    state.partial_begin=!transfer->sop;
                    state.channel_id=transfer->channel_id;
                    state.stable_fields=transfer->packet_stable_fields;
                    active_packets.emplace(key,std::move(state));
                }
                PacketState& state=active_packets.at(key);
                if (!state.beats.empty()) {
                    for (const auto& item : state.stable_fields.items()) {
                        if (!transfer->packet_stable_fields.contains(item.key())||
                            transfer->packet_stable_fields.at(item.key())!=item.value()) {
                            state.stable_mismatches.push_back({
                                {"field",item.key()},
                                {"cycle",transfer->cycle},
                                {"time",wf->format_time(transfer->time,unit)},
                                {"expected",item.value()},
                                {"actual",transfer->packet_stable_fields.value(
                                    item.key(),Json())},
                            });
                        }
                    }
                }
                state.beats.push_back(transfer);
                if (transfer->eop) append_packet(key);
            }
            std::vector<std::string> remaining;
            for (const auto& item : active_packets) remaining.push_back(item.first);
            for (const std::string& key : remaining) append_packet(key);
            std::sort(completed_packet_json.begin(),completed_packet_json.end(),
                [](const auto& lhs,const auto& rhs){return lhs.first<rhs.first;});
            for (auto& item : completed_packet_json)
                packets.push_back(std::move(item.second));
        }

        const bool filter_applied=args.contains("filter");
        const bool channel_applied=args.contains("channel");
        uint64_t requested_channel=0;
        const bool channel_valid=!channel_applied||
            literal_u64(args.at("channel"),requested_channel);
        auto channel_matches=[&](const Json& channel_id) {
            if (!channel_applied) return true;
            uint64_t actual_channel=0;
            return channel_valid&&known_u64(channel_id,actual_channel)&&
                actual_channel==requested_channel;
        };

        std::vector<StreamSample*> selected_transfers;
        for (auto* transfer : transfers)
            if (channel_matches(transfer->channel_id))
                selected_transfers.push_back(transfer);

        size_t unresolved_filter_count=0;
        std::vector<StreamSample*> filtered_transfers;
        Json filtered_packets=Json::array();
        Json normalized_filter=Json::object();
        if (filter_applied)
            normalized_filter=args.at("filter");

        if (filter_applied&&!packet_enabled) {
            for (auto* transfer : selected_transfers) {
                bool no_match=false,unresolved=false;
                for (const auto& criterion : normalized_filter.at("fields").items()) {
                    const StreamFilterResult result=transfer->fields.contains(
                        criterion.key())
                        ? stream_field_filter_result(
                            transfer->fields.at(criterion.key()),criterion.value())
                        : StreamFilterResult::Unresolved;
                    if (result==StreamFilterResult::NoMatch) {
                        no_match=true;
                        break;
                    }
                    unresolved|=result==StreamFilterResult::Unresolved;
                }
                if (no_match) continue;
                if (unresolved) ++unresolved_filter_count;
                else filtered_transfers.push_back(transfer);
            }
        } else {
            filtered_transfers=selected_transfers;
        }

        Json candidate_packets=Json::array();
        for (const Json& packet : packets)
            if (!channel_applied||
                (packet.contains("channel_id")&&channel_matches(packet.at("channel_id"))))
                candidate_packets.push_back(packet);
        if (filter_applied&&packet_enabled) {
            if (!normalized_filter.contains("position"))
                normalized_filter["position"]="sop";
            const bool at_eop=normalized_filter.at("position")=="eop";
            for (const Json& packet : candidate_packets) {
                if ((at_eop&&packet.value("partial_end",false))||
                    (!at_eop&&packet.value("partial_begin",false))) {
                    ++unresolved_filter_count;
                    continue;
                }
                Json fields=at_eop?packet.at("last_fields"):packet.at("first_fields");
                for (const auto& stable : packet.at("packet_stable_fields").items())
                    fields[stable.key()]=stable.value();
                bool no_match=false,unresolved=false;
                for (const auto& criterion : normalized_filter.at("fields").items()) {
                    const StreamFilterResult result=fields.contains(criterion.key())
                        ? stream_field_filter_result(
                            fields.at(criterion.key()),criterion.value())
                        : StreamFilterResult::Unresolved;
                    if (result==StreamFilterResult::NoMatch) {
                        no_match=true;
                        break;
                    }
                    unresolved|=result==StreamFilterResult::Unresolved;
                }
                if (no_match) continue;
                if (unresolved) ++unresolved_filter_count;
                else filtered_packets.push_back(packet);
            }
        } else filtered_packets=candidate_packets;

        Json all_rows=Json::array();
        for (const auto* transfer : filtered_transfers)
            all_rows.push_back(row_json(*transfer,transfer->beat_index));

        size_t total_count=0,returned_count=0;
        bool truncated=false;
        Json data=Json::object();
        if (query=="summary") total_count=packet_enabled
            ? filtered_packets.size()
            : (filter_applied?filtered_transfers.size():transfers.size());
        else if (query=="first_transfer"||query=="last_transfer") {
            total_count=filter_applied?filtered_transfers.size():transfers.size();
            returned_count=filtered_transfers.empty()?0:1;
            if (!filtered_transfers.empty()) {
                const StreamSample* selected=query=="first_transfer"
                    ? filtered_transfers.front():filtered_transfers.back();
                data["row"]=row_json(*selected,selected->beat_index);
            }
        } else if (query=="transfer_window") {
            total_count=filter_applied?filtered_transfers.size():transfers.size();
            returned_count=std::min<size_t>(max_rows,all_rows.size());
            Json rows=Json::array();
            for (size_t index=0;index<returned_count;++index) rows.push_back(all_rows[index]);
            data["rows"]=rows; truncated=returned_count<total_count;
            if (truncated) data["hint"]=filter_applied
                ? "narrow filter/time_range or increase line_limit"
                : "use stream.export for large result";
        } else if (query=="first_stall"||query=="last_stall") {
            total_count=stalls.size(); returned_count=stalls.empty()?0:1;
            if (!stalls.empty()) data["stall"]=query=="first_stall"?stalls.front():stalls.back();
        } else if (query=="stall_window") {
            total_count=stalls.size(); returned_count=std::min<size_t>(max_rows,total_count);
            Json selected=Json::array();
            for (size_t index=0;index<returned_count;++index) selected.push_back(stalls[index]);
            data["stalls"]=selected; truncated=returned_count<total_count;
            if (truncated) data["hint"]="use stream.export for large result";
        } else if (query=="first_packet"||query=="last_packet"||query=="packet_at") {
            total_count=filtered_packets.size(); size_t index=0;
            if (query=="last_packet"&&!filtered_packets.empty()) index=filtered_packets.size()-1;
            if (query=="packet_at") index=args.at("packet_index");
            const bool found=index<filtered_packets.size(); returned_count=found?1:0;
            if (query=="packet_at") total_count=returned_count;
            data["found"]=found;
            data["packet"]=found?filtered_packets[index]:Json(nullptr);
        } else {
            total_count=filtered_packets.size(); returned_count=std::min<size_t>(max_rows,total_count);
            Json selected=Json::array();
            for (size_t index=0;index<returned_count;++index) selected.push_back(filtered_packets[index]);
            data["packets"]=selected; truncated=returned_count<total_count;
            if (truncated) data["hint"]="use stream.export for large result";
        }
        if (filter_applied) {
            data["filter"]=normalized_filter;
            data["notes"]={{"unresolved_filter_count",
                "因所选 SOP/EOP 边界未出现在查询窗口内，或被引用字段的有效比较位含 X/Z，导致无法判断是否匹配的 transfer/packet 数；mask 为 0 的位不影响判断。"}};
        }

        Json summary{{"stream",name},{"query",query},{"sampling_mode","clock_edge"},
            {"clock",cfg.source.at("clock")},{"edge",cfg.edge},
            {"sample_time_semantics","time is sample_time"},{"handshake",stream_handshake(cfg)},
            {"packet_enabled",packet_enabled},{"clock_edges",samples.size()},
            {"vld_cycles",vld_cycles},{"transfer_count",transfers.size()},
            {"stall_cycles",stall_cycles},{"stall_windows",stalls.size()},
            {"complete_packet_count",complete_packets},{"partial_packet_count",partial_packets},
            {"packet_count_status",packet_enabled?
                (partial_packets?"ambiguous":"exact"):"not_configured"},
            {"control_xz_count",control_xz_count},{"data_xz_count",data_xz_count},
            {"ready_bp_conflict_count",ready_bp_conflict_count},
            {"packet_stable_mismatch_count",packet_stable_mismatch_count},
            {"requested_range",{{"begin",wf->format_time(begin_time,unit)},
                {"end",wf->format_time(end_time,unit)}}},
            {"scanned_range",{{"begin",wf->format_time(
                    samples.empty()?begin_time:samples.front().time,unit)},
                {"end",wf->format_time(
                    samples.empty()?end_time:samples.back().time,unit)}}},
            {"filter_applied",filter_applied},
            {"scan_complete",control_xz_count==0},
            {"analysis_complete",control_xz_count==0&&data_xz_count==0&&
                fields_complete},
            {"response_truncated",truncated},{"total_count",total_count},
            {"returned_count",returned_count},
            {"truncation_scopes",[&]() {
                Json scopes=Json::array();
                if (control_xz_count||data_xz_count||!fields_complete)
                    scopes.push_back("analysis_samples");
                if (truncated) {
                    if (query=="transfer_window")
                        scopes.push_back("response_transfers");
                    else if (query=="stall_window")
                        scopes.push_back("response_stalls");
                    else
                        scopes.push_back("response_packets");
                }
                return scopes;
            }()}};
        if (filter_applied) {
            summary["unresolved_filter_count"]=unresolved_filter_count;
            if (packet_enabled) {
                summary["matched_packet_count"]=filtered_packets.size();
                summary["retained_packet_count"]=filtered_packets.size();
            } else {
                summary["matched_transfer_count"]=filtered_transfers.size();
            }
        }
        if (cfg.edge != "negedge") summary["sample_point"] = cfg.sample_point;
        if (!(filter_applied&&packet_enabled)&&!filtered_transfers.empty()) {
            if (query!="first_transfer")
                summary["first_transfer_time"] = wf->format_time(
                    filtered_transfers.front()->time,unit);
            if (query!="last_transfer")
                summary["last_transfer_time"] = wf->format_time(
                    filtered_transfers.back()->time,unit);
        }
        if (!stalls.empty()) {
            summary["first_stall_time"]=stalls.front().at("start_time");
            summary["last_stall_time"]=stalls.back().at("start_time");
        }
        if (filter_applied||(returned_count>0 && query!="summary" &&
            query!="first_stall" && query!="last_stall" &&
            query!="stall_window")) {
            summary["value_width_complete"]=true;
            summary["width_diagnostics"]=Json::array();
        }
        return {{"ok",true},{"summary",summary},{"data",data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_stream_xout(action_name(), response);
    }
};

// ═══════════════════════════════════════════════════════════════
// 6. stream.export
// ═══════════════════════════════════════════════════════════════

struct StreamExportHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.export"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        const bool written=args.contains("output")&&args.at("output").contains("path");
        const std::string kind=args.value("kind","transfer");
        const size_t line_limit=args.value("line_limit",16u);
        const size_t query_limit=written
            ? static_cast<size_t>(std::numeric_limits<int>::max())
            : line_limit;
        Json query_args=args;
        query_args.erase("kind"); query_args.erase("output");
        query_args["line_limit"]=query_limit;
        query_args["query"]=kind=="packet"
            ? "packet_window":"transfer_window";
        StreamQueryHandler query_handler;
        Json analyzed=query_handler.run({{"args",query_args}});
        if (!analyzed.value("ok",false)) return analyzed;

        Json preview=kind=="packet"
            ? analyzed.at("data").at("packets")
            : analyzed.at("data").at("rows");
        Json meta_analyzed=analyzed;
        if (written&&kind=="packet") {
            Json meta_args=query_args;
            meta_args["query"]="transfer_window";
            meta_analyzed=query_handler.run({{"args",meta_args}});
            if (!meta_analyzed.value("ok",false)) return meta_analyzed;
        }

        std::vector<std::string> beat_fields;
        if (cfg.source.contains("data")) beat_fields.push_back("data");
        for (const auto& item : cfg.beat_fields.items())
            if (std::find(beat_fields.begin(),beat_fields.end(),item.key())==
                beat_fields.end())
                beat_fields.push_back(item.key());
        std::vector<std::string> stable_fields;
        for (const auto& item : cfg.packet_stable_fields.items())
            stable_fields.push_back(item.key());
        auto value_text=[](const Json& owner,const char* member,
                           const std::string& field) {
            if (!owner.contains(member)||!owner.at(member).contains(field))
                return std::string();
            return owner.at(member).at(field).value("value",std::string());
        };

        Json output_summary;
        if (written) {
            const Json output=args.at("output");
            const std::string path=output.at("path"),
                file_format=output.value("file_format","tsv"),meta_path=path+".meta.json";
            std::ofstream rows_file(path),meta_file(meta_path);
            if (!rows_file||!meta_file)
                return action_error("OUTPUT_WRITE_FAILED","cannot open stream export output");
            const char separator=file_format=="csv"?',':'\t';
            if (kind=="transfer") {
                rows_file << "cycle" << separator << "time" << separator
                          << "transfer" << separator << "stall" << separator
                          << "vld" << separator << "rdy" << separator << "bp"
                          << separator << "sop" << separator << "eop";
                if (!cfg.channel_id.empty()) rows_file << separator << "channel_id";
                for (const std::string& field : beat_fields)
                    rows_file << separator << field;
                rows_file << '\n';
                for (const Json& row : preview) {
                    rows_file << row.at("cycle") << separator
                              << row.at("time").get<std::string>()
                              << separator << (row.at("transfer").get<bool>()?1:0)
                              << separator << (row.at("stall").get<bool>()?1:0)
                              << separator << (row.at("vld").get<bool>()?1:0)
                              << separator << (row.at("rdy").get<bool>()?1:0)
                              << separator << (row.at("bp").get<bool>()?1:0)
                              << separator << (row.at("sop").get<bool>()?1:0)
                              << separator << (row.at("eop").get<bool>()?1:0);
                    if (!cfg.channel_id.empty())
                        rows_file << separator << (row.contains("channel_id")
                            ? row.at("channel_id").value("value",std::string())
                            : std::string());
                    for (const std::string& field : beat_fields)
                        rows_file << separator << value_text(row,"fields",field);
                    rows_file << '\n';
                }
            } else if (kind=="packet") {
                rows_file << "packet_index" << separator << "channel_id"
                          << separator << "start_time" << separator << "end_time"
                          << separator << "start_cycle" << separator << "end_cycle"
                          << separator << "beat_count" << separator << "partial"
                          << separator << "packet_stable_mismatch_count";
                for (const std::string& field : stable_fields)
                    rows_file << separator << "packet_stable_" << field;
                for (const std::string& field : beat_fields)
                    rows_file << separator << "first_" << field
                              << separator << "last_" << field;
                rows_file << '\n';
                for (const Json& packet : preview) {
                    const bool partial=packet.at("partial_begin").get<bool>()||
                        packet.at("partial_end").get<bool>();
                    rows_file << packet.at("packet_index") << separator
                              << (packet.contains("channel_id")
                                  ? packet.at("channel_id").value(
                                      "value",std::string()) : std::string())
                              << separator << packet.at("start_time").get<std::string>()
                              << separator << packet.at("end_time").get<std::string>()
                              << separator << packet.at("start_cycle")
                              << separator << packet.at("end_cycle")
                              << separator << packet.at("beat_count")
                              << separator << (partial?"true":"false")
                              << separator
                              << packet.at("packet_stable_mismatches").size();
                    for (const std::string& field : stable_fields)
                        rows_file << separator << value_text(
                            packet,"packet_stable_fields",field);
                    for (const std::string& field : beat_fields)
                        rows_file << separator << value_text(
                            packet,"first_fields",field)
                                  << separator << value_text(
                            packet,"last_fields",field);
                    rows_file << '\n';
                }
            } else {
                rows_file << "packet_index" << separator << "channel_id"
                          << separator << "beat_index" << separator << "cycle"
                          << separator << "time";
                for (const std::string& field : beat_fields)
                    rows_file << separator << field;
                for (const std::string& field : stable_fields)
                    rows_file << separator << "packet_stable_" << field;
                rows_file << '\n';
                for (const Json& row : preview) {
                    rows_file << row.at("packet_index") << separator
                              << (row.contains("channel_id")
                                  ? row.at("channel_id").value(
                                      "value",std::string()) : std::string())
                              << separator << row.at("beat_index")
                              << separator << row.at("cycle")
                              << separator << row.at("time").get<std::string>();
                    for (const std::string& field : beat_fields)
                        rows_file << separator << value_text(row,"fields",field);
                    for (const std::string& field : stable_fields)
                        rows_file << separator << value_text(
                            row,"packet_stable_fields",field);
                    rows_file << '\n';
                }
            }

            using OrderedJson=nlohmann::ordered_json;
            auto ordered_summary=[](const Json& source) {
                OrderedJson result=OrderedJson::object();
                for (const char* key : {
                    "stream","sampling_mode","clock","edge","sample_point",
                    "sample_time_semantics","handshake","packet_enabled",
                    "clock_edges","vld_cycles","transfer_count","stall_cycles",
                    "stall_windows","complete_packet_count","partial_packet_count",
                    "packet_count_status","control_xz_count","data_xz_count",
                    "ready_bp_conflict_count","packet_stable_mismatch_count",
                    "scan_complete","analysis_complete","response_truncated",
                    "total_count","returned_count","truncation_scopes"})
                    if (source.contains(key))
                        result[key]=OrderedJson::parse(source.at(key).dump());
                for (const char* key : {"requested_range","scanned_range"}) {
                    if (!source.contains(key)) continue;
                    OrderedJson range=OrderedJson::object();
                    range["begin"]=source.at(key).at("begin");
                    range["end"]=source.at(key).at("end");
                    result[key]=std::move(range);
                }
                for (const char* key : {"first_transfer_time","last_transfer_time",
                     "first_stall_time","last_stall_time"})
                    if (source.contains(key)) result[key]=source.at(key);
                return result;
            };
            const Json& meta_base=meta_analyzed.at("summary");
            OrderedJson meta=OrderedJson::object();
            meta["stream"]=name;
            meta["kind"]=kind;
            for (const char* key : {"sampling_mode","clock","edge","sample_point",
                 "sample_time_semantics","handshake","packet_enabled"})
                if (meta_base.contains(key)) meta[key]=meta_base.at(key);
            meta["row_count"]=preview.size();
            meta["summary"]=ordered_summary(meta_base);
            OrderedJson field_rows=OrderedJson::array();
            if (cfg.source.contains("data")) {
                field_rows.push_back({
                    {"name","data"},{"expr",cfg.source.at("data")},{"scope","beat"},
                });
            }
            for (const auto& item : cfg.beat_fields.items())
                field_rows.push_back({
                    {"name",item.key()},{"expr",item.value()},{"scope","beat"},
                });
            for (const auto& item : cfg.packet_stable_fields.items())
                field_rows.push_back({
                    {"name",item.key()},{"expr",item.value()},
                    {"scope","packet_stable"},
                });
            meta["fields"]=std::move(field_rows);
            meta_file << meta.dump(2) << '\n';
            output_summary={{"path",path},{"meta_path",meta_path},{"file_format",file_format}};
        }

        const Json& base=analyzed.at("summary");
        Json summary;
        for (const char* key : {"stream","sampling_mode","clock","edge","sample_point",
             "sample_time_semantics","handshake","packet_enabled","clock_edges","vld_cycles",
             "transfer_count","stall_cycles","stall_windows","complete_packet_count",
             "partial_packet_count","packet_count_status","control_xz_count","data_xz_count",
             "ready_bp_conflict_count","packet_stable_mismatch_count","requested_range",
             "scanned_range","first_transfer_time","last_transfer_time","first_stall_time",
             "last_stall_time","scan_complete","analysis_complete"})
            if (base.contains(key)) summary[key]=base.at(key);
        const size_t row_count=base.at("total_count");
        const size_t returned_count=preview.size();
        const bool response_truncated=!written&&returned_count<row_count;
        summary["response_truncated"]=response_truncated;
        summary["total_count"]=row_count;
        summary["returned_count"]=returned_count;
        summary["truncation_scopes"]=response_truncated
            ? Json::array({"response_preview"}) : Json::array();
        summary["status"]=written?"written":"preview";
        summary["output_written"]=written;
        summary["row_count"]=row_count;
        summary["line_limit"]=line_limit;
        summary["kind"]=kind;
        if (written) summary["output"]=output_summary;
        else {
            if (base.contains("value_width_complete"))
                summary["value_width_complete"]=base.at("value_width_complete");
            if (base.contains("width_diagnostics"))
                summary["width_diagnostics"]=base.at("width_diagnostics");
        }
        Json data=written?Json::object():Json{{"preview",preview}};
        return {{"ok",true},{"summary",summary},{"data",data}};
    }

    std::string render_xout(const Json& response) const override {
        return render_stream_xout(action_name(), response);
    }
};

// ═══════════════════════════════════════════════════════════════
// 7. stream.validate
// ═══════════════════════════════════════════════════════════════

struct StreamValidateHandler : public EngineActionHandler {
    const char* action_name() const override { return "stream.validate"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& req) override {
        Json err = check_waveform(action_name());
        if (!err.empty()) return err;

        auto args = req.value("args", Json::object());
        std::string name = args.at("stream");

        StreamConfig cfg;
        Json cerr;
        if (!get_config(name, cfg, cerr)) return cerr;

        auto* wf = engine_globals().waveform.get();

        Json validation = validate_stream(wf,cfg,false);
        const bool static_ok = validation.at("status") == "ok";
        Json issues = Json::array();
        if (!static_ok) issues.push_back({{"code","signal_not_found"},
            {"severity","ERROR"},{"message","stream contains unresolved signals"}});
        if (cfg.signals.find(cfg.source.at("clock").get<std::string>())==
            cfg.signals.end()) {
            issues.push_back({
                {"code","CLOCK_COMPLEX"},
                {"message","clock expression is not a plain signal; edge detection uses expression dependency changes"},
                {"severity","WARNING"},
            });
        }
        const bool dynamic_requested = args.value("dynamic",true);
        Json dynamic = Json::object();
        bool scan_complete = static_ok, analysis_complete = static_ok;
        if (dynamic_requested && static_ok) {
            Json query_args=args;
            query_args.erase("dynamic");
            query_args["query"]="summary";
            StreamQueryHandler query_handler;
            Json analyzed=query_handler.run({{"args",query_args}});
            if (!analyzed.value("ok",false)) return analyzed;
            const Json& base=analyzed.at("summary");
            for (const char* key : {"stream","sampling_mode","clock","edge","sample_point",
                 "sample_time_semantics","handshake","packet_enabled","clock_edges","vld_cycles",
                 "transfer_count","stall_cycles","stall_windows","complete_packet_count",
                 "partial_packet_count","packet_count_status","control_xz_count","data_xz_count",
                 "ready_bp_conflict_count","packet_stable_mismatch_count","requested_range",
                 "scanned_range","first_transfer_time","last_transfer_time","first_stall_time",
                 "last_stall_time"})
                if (base.contains(key)) dynamic[key]=base.at(key);
            scan_complete=base.at("scan_complete");
            analysis_complete=base.at("analysis_complete");
            auto add_dynamic_issue=[&](const char* counter, const char* code,
                                       const char* message) {
                if (base.value(counter,0u)>0) issues.push_back({{"code",code},
                    {"severity","WARNING"},{"message",message}});
            };
            add_dynamic_issue("control_xz_count","control_xz",
                "stream control signals contain X/Z at sampled clock edges");
            add_dynamic_issue("data_xz_count","data_xz",
                "stream transfer data contains X/Z");
            add_dynamic_issue("ready_bp_conflict_count","READY_BP_CONFLICT",
                "observed vld=1,rdy=1,bp=1");
            add_dynamic_issue("packet_stable_mismatch_count",
                "PACKET_STABLE_FIELD_MISMATCH",
                "observed packet_stable_fields changing within packet");
        }
        const size_t total=issues.size();
        const size_t limit=args.value("line_limit",1000u);
        Json returned_issues=Json::array();
        for (size_t index=0;index<std::min(total,limit);++index)
            returned_issues.push_back(issues[index]);
        const bool response_truncated=returned_issues.size()<total;
        Json scopes=Json::array();
        if (dynamic_requested&&(!scan_complete||!analysis_complete))
            scopes.push_back("analysis_samples");
        if (response_truncated) scopes.push_back("response_issues");
        const bool ok=static_ok;
        return {{"ok",true},{"summary",{{"stream",name},{"ok",ok},
            {"static_validation_complete",true},{"dynamic_requested",dynamic_requested},
            {"scan_complete",scan_complete},{"analysis_complete",analysis_complete},
            {"response_truncated",response_truncated},{"total_count",total},
            {"returned_count",returned_issues.size()},{"truncation_scopes",scopes}}},
            {"data",{{"issues",returned_issues},{"dynamic",dynamic}}}};
    }
};

// ── Factory functions ──

std::unique_ptr<EngineActionHandler> make_stream_config_list_handler() {
    return std::make_unique<StreamConfigListHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_config_get_handler() {
    return std::make_unique<StreamConfigGetHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_config_load_handler() {
    return std::make_unique<StreamConfigLoadHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_describe_handler() {
    return std::make_unique<StreamDescribeHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_query_handler() {
    return std::make_unique<StreamQueryHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_export_handler() {
    return std::make_unique<StreamExportHandler>();
}
std::unique_ptr<EngineActionHandler> make_stream_validate_handler() {
    return std::make_unique<StreamValidateHandler>();
}

} // namespace xdebug_fst
