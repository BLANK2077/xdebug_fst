// combined_actions.cpp — trace.active_driver, trace.active_driver_chain,
// trace.x_origin (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/expr/expr_eval.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace xdebug_fst {

namespace {

Json action_error(const std::string& code, const std::string& message) {
    return {{"ok",false},{"error",{{"code",code},{"message",message}}}};
}

bool has_x(const std::string& bits) {
    return bits.find_first_of("xXhHuwUW-") != std::string::npos;
}

std::string x_mask(const std::string& bits) {
    std::string mask="'b";
    for (char bit : bits)
        mask += (bit=='x'||bit=='X'||bit=='h'||bit=='H'||bit=='u'||bit=='U'||
                 bit=='w'||bit=='W'||bit=='-')?'1':'0';
    return mask;
}

struct Sample {
    bool ok=false;
    uint32_t ref=0,time_idx=0,width=0;
    uint64_t query_time=0,time=0,active_time=0;
    std::string bits;
};

uint64_t previous_change_time(IWaveformBackend& waveform, uint32_t ref,
                              uint32_t query_index, uint64_t fallback) {
    const auto indices=waveform.time_indices_of(ref);
    for (auto it=indices.rbegin();it!=indices.rend();++it) {
        if (*it<=query_index) return waveform.time_at(*it);
    }
    return fallback;
}

Sample sample_at(IWaveformBackend& waveform, const std::string& signal,
                 uint64_t time) {
    Sample sample;
    sample.query_time=time;
    sample.ref=waveform.find_signal(signal);
    if (sample.ref==IWaveformBackend::kInvalidSignalRef) return sample;
    if (!waveform.is_loaded(sample.ref)) waveform.load_signals({sample.ref});
    sample.time_idx=waveform.time_idx_of(time);
    IWaveformBackend::SignalOffset offset;
    if (!waveform.signal_offset_at(sample.ref,sample.time_idx,offset)) return sample;
    IWaveformBackend::SignalInfo info;
    if (waveform.signal_info(sample.ref,info)) sample.width=info.width;
    sample.time=waveform.time_at(sample.time_idx);
    sample.active_time=previous_change_time(
        waveform,sample.ref,sample.time_idx,sample.time);
    sample.bits=waveform.signal_value_str(sample.ref,offset.start,0);
    sample.ok=!sample.bits.empty();
    return sample;
}

uint64_t x_onset_time(IWaveformBackend& waveform, const Sample& query) {
    uint64_t onset=query.time;
    const auto indices=waveform.time_indices_of(query.ref);
    for (auto it=indices.rbegin();it!=indices.rend();++it) {
        if (*it>query.time_idx) continue;
        IWaveformBackend::SignalOffset offset;
        if (!waveform.signal_offset_at(query.ref,*it,offset)) continue;
        const std::string bits=waveform.signal_value_str(query.ref,offset.start,0);
        if (!has_x(bits)) break;
        onset=waveform.time_at(*it);
    }
    return onset;
}

bool parse_common(const Json& request, IWaveformBackend& waveform,
                  std::string& signal, uint64_t& time, TimeRenderUnit& unit,
                  ValueRenderFormat& format, Json& error) {
    const Json args=request.value("args",Json::object());
    signal=args.at("signal");
    std::string message;
    if (!waveform.parse_time(args.at("time"),time,message)) {
        error=action_error("INVALID_TIME",message); return false;
    }
    if (!parse_time_render_unit(args.value("render_time_unit","ns"),unit,message)) {
        error=action_error("INVALID_FIELD",message); return false;
    }
    if (!parse_value_render_format(args.value("value_format","hex"),format)) {
        error=action_error("INVALID_FIELD","unsupported value_format"); return false;
    }
    return true;
}

std::vector<IDesignBackend::DriverRecord> drivers_for(IDesignBackend& design,
                                                       int signal_index) {
    std::vector<IDesignBackend::DriverRecord> drivers;
    if (signal_index>=0) design.trace_driver(signal_index,drivers);
    std::stable_sort(drivers.begin(),drivers.end(),[](const auto& left,const auto& right) {
        const auto rank=[](const std::string& role) {
            if (role=="rhs") return 0;
            if (role=="control") return 1;
            return 2;
        };
        return rank(left.dependency_role)<rank(right.dependency_role);
    });
    return drivers;
}

std::string signal_name(IDesignBackend& design, int index) {
    const char* name=index>=0?design.signal_name(index):nullptr;
    return name?std::string(name):std::string();
}

Json logic_json(const Sample& sample, ValueRenderFormat format) {
    return logic_value_json(logic_value_from_bits(sample.bits,sample.width),format);
}

std::string logic_string(const Sample& sample, ValueRenderFormat format) {
    return render_logic_value(logic_value_from_bits(sample.bits,sample.width),format);
}

Json source_path(const IDesignBackend::DriverRecord& driver,
                 const std::string& source, const std::string& target) {
    Json path=Json::array();
    if (!source.empty()&&source!=target) path.push_back(source);
    path.push_back(target);
    return {{"file",driver.file.empty()?"<unknown>":driver.file},
        {"line",std::max(1,driver.line)},{"source_context",Json::array()},
        {"signal_path",path}};
}

Json trace_hop(size_t index, const std::string& signal, const Sample& sample,
               const std::string& relation,
               const IDesignBackend::DriverRecord* driver,
               IDesignBackend& design, int signal_index,
               IWaveformBackend& waveform, TimeRenderUnit unit,
               ValueRenderFormat format) {
    std::string file=driver&&!driver->file.empty()?driver->file:
        (signal_index>=0&&design.signal_file(signal_index)?design.signal_file(signal_index):"<unknown>");
    int line=driver&&driver->line>0?driver->line:
        (signal_index>=0?design.signal_line(signal_index):1);
    return {{"index",index},{"chain_id","c0"},{"signal",signal},
        {"time",waveform.format_time(sample.query_time,unit)},
        {"active_time",waveform.format_time(sample.active_time,unit)},
        {"value",logic_string(sample,format)},{"relation",relation},
        {"file",file.empty()?"<unknown>":file},{"line",std::max(1,line)},
        {"source_context",Json::array()},{"signal_path",Json::array({signal})}};
}

struct StatementGroup {
    std::string kind,file,predicate;
    int line=0;
    std::vector<IDesignBackend::DriverRecord> records;
    std::vector<IDesignBackend::DriverRecord> rhs;
};

std::vector<StatementGroup> statement_groups(
    const std::vector<IDesignBackend::DriverRecord>& drivers) {
    std::map<std::tuple<std::string,int,std::string>,StatementGroup> grouped;
    for (const auto& driver : drivers) {
        if (driver.file.empty()||driver.line<=0) continue;
        const auto key=std::make_tuple(driver.file,driver.line,driver.kind);
        auto& statement=grouped[key];
        statement.kind=driver.kind;
        statement.file=driver.file;
        statement.line=driver.line;
        if (statement.predicate.empty())
            statement.predicate=driver.activation_predicate;
        statement.records.push_back(driver);
        if (driver.dependency_role=="rhs"&&driver.src_signal>=0&&
            std::none_of(statement.rhs.begin(),statement.rhs.end(),
                [&](const auto& item){return item.src_signal==driver.src_signal;})) {
            statement.rhs.push_back(driver);
        }
    }
    std::vector<StatementGroup> statements;
    for (auto& [key,statement] : grouped) {
        (void)key;
        std::sort(statement.rhs.begin(),statement.rhs.end(),
            [](const auto& left,const auto& right){
                return left.src_signal<right.src_signal;
            });
        statements.push_back(std::move(statement));
    }
    return statements;
}

enum class PredicateState { Active, Inactive, Unresolved };

PredicateState evaluate_predicate(const StatementGroup& statement,
                                  IWaveformBackend& waveform,
                                  uint64_t active_time) {
    if (statement.predicate.empty()) return PredicateState::Unresolved;
    std::string error;
    std::unique_ptr<ExprNode> expression(
        parse_expression(statement.predicate,error));
    if (!expression) return PredicateState::Unresolved;
    std::vector<uint32_t> refs;
    for (const std::string& signal : expression_signals(expression.get())) {
        const uint32_t ref=waveform.find_signal(signal);
        if (ref==IWaveformBackend::kInvalidSignalRef)
            return PredicateState::Unresolved;
        refs.push_back(ref);
    }
    if (!refs.empty()&&static_cast<size_t>(waveform.load_signals(refs))!=refs.size())
        return PredicateState::Unresolved;
    const LogicValue value=eval_expression(
        expression.get(),waveform,waveform.time_idx_of(active_time));
    if (!value.known||value.bits.empty()) return PredicateState::Unresolved;
    return value.bits.find('1')==std::string::npos
        ?PredicateState::Inactive:PredicateState::Active;
}

struct EvaluatedStatements {
    std::vector<StatementGroup> active;
    std::vector<StatementGroup> unresolved;
};

EvaluatedStatements active_statement_groups(
    const std::vector<IDesignBackend::DriverRecord>& drivers,
    IWaveformBackend& waveform,uint64_t active_time) {
    EvaluatedStatements result;
    for (auto& statement : statement_groups(drivers)) {
        const PredicateState state=evaluate_predicate(
            statement,waveform,active_time);
        if (state==PredicateState::Active)
            result.active.push_back(std::move(statement));
        else if (state==PredicateState::Unresolved)
            result.unresolved.push_back(std::move(statement));
    }
    return result;
}

const IDesignBackend::DriverRecord* representative_driver(
    const StatementGroup& statement) {
    if (!statement.rhs.empty()) return &statement.rhs.front();
    const auto control=std::find_if(statement.records.begin(),statement.records.end(),
        [](const auto& driver){return driver.dependency_role=="control";});
    return control==statement.records.end()
        ?(statement.records.empty()?nullptr:&statement.records.front()):&*control;
}

Sample sample_before(IWaveformBackend& waveform,const std::string& signal,
                     uint64_t time) {
    const uint32_t ref=waveform.find_signal(signal);
    if (ref==IWaveformBackend::kInvalidSignalRef) return {};
    if (!waveform.is_loaded(ref)) waveform.load_signals({ref});
    const uint32_t query_index=waveform.time_idx_of(time);
    const auto indices=waveform.time_indices_of(ref);
    for (auto it=indices.rbegin();it!=indices.rend();++it) {
        const uint64_t candidate_time=waveform.time_at(*it);
        if (*it<query_index&&candidate_time<time)
            return sample_at(waveform,signal,candidate_time);
    }
    return {};
}

bool known_bits(const std::string& bits) {
    return bits.find_first_of("xXzZhHlLuUwW-")==std::string::npos;
}

Json ambiguity_value(const Sample& sample,IWaveformBackend& waveform,
                     TimeRenderUnit unit,ValueRenderFormat format) {
    if (!sample.ok) return {{"status","missing_value"},{"value",nullptr},
        {"known",nullptr},{"value_time",nullptr}};
    return {{"status","ok"},{"value",logic_string(sample,format)},
        {"known",known_bits(sample.bits)},
        {"value_time",waveform.format_time(sample.active_time,unit)}};
}

Json ambiguity_evidence(const std::string& kind,const std::string& signal,
                        uint64_t active_time,size_t hop_index,
                        const std::vector<StatementGroup>& groups,
                        size_t max_trace_signals,IDesignBackend& design,
                        IWaveformBackend& waveform,TimeRenderUnit unit,
                        ValueRenderFormat format) {
    Json statements=Json::array();
    size_t rhs_count=0,returned=0;
    for (const auto& group : groups) {
        Json samples=Json::array();
        for (const auto& driver : group.rhs) {
            ++rhs_count;
            if (returned>=max_trace_signals) continue;
            const std::string source=signal_name(design,driver.src_signal);
            const Sample before=sample_before(waveform,source,active_time);
            const Sample after=sample_at(waveform,source,active_time);
            Json before_json=ambiguity_value(before,waveform,unit,format);
            Json after_json=ambiguity_value(after,waveform,unit,format);
            Json changed=nullptr;
            if (before.ok&&after.ok) changed=before.bits!=after.bits;
            samples.push_back({{"signal",source},{"before",before_json},
                {"after",after_json},{"changed",changed}});
            ++returned;
        }
        statements.push_back({{"kind",group.kind.empty()?"assignment":group.kind},
            {"driver",group.kind},{"file",group.file},{"line",std::max(0,group.line)},
            {"rhs_signal_count",group.rhs.size()},
            {"returned_rhs_signal_count",samples.size()},
            {"complete",samples.size()==group.rhs.size()},{"rhs_samples",samples}});
    }
    const size_t omitted=rhs_count-returned;
    return {{"kind",kind},{"signal",signal},
        {"active_time",waveform.format_time(active_time,unit)},
        {"hop_index",hop_index},{"statement_count",groups.size()},
        {"rhs_signal_count",rhs_count},{"returned_rhs_signal_count",returned},
        {"omitted_rhs_signal_count",omitted},{"analysis_complete",omitted==0},
        {"truncation_scopes",omitted?Json::array({"ambiguity_rhs_samples"}):Json::array()},
        {"statements",statements}};
}

Json x_hop(size_t index, const std::string& chain_id,
           const std::string& signal, const Sample& sample, uint64_t onset,
           const std::string& relation,
           const IDesignBackend::DriverRecord* driver,
           IDesignBackend& design, int signal_index,
           IWaveformBackend& waveform, TimeRenderUnit unit,
           ValueRenderFormat format) {
    std::string file=driver&&!driver->file.empty()?driver->file:
        (signal_index>=0&&design.signal_file(signal_index)?design.signal_file(signal_index):"");
    int line=driver&&driver->line>0?driver->line:
        (signal_index>=0?design.signal_line(signal_index):0);
    return {{"index",index},{"chain_id",chain_id},{"signal",signal},
        {"x_onset_time",waveform.format_time(onset,unit)},
        {"active_time",waveform.format_time(sample.active_time,unit)},
        {"value",logic_json(sample,format)},{"x_mask",x_mask(sample.bits)},
        {"relation",relation},{"file",file},{"line",std::max(0,line)},
        {"signal_path",Json::array({signal})}};
}

} // namespace

struct TraceActiveDriverHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.active_driver"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& request) override {
        auto& globals=engine_globals();
        auto& waveform=*globals.waveform; auto& design=*globals.design;
        std::string signal; uint64_t time=0; TimeRenderUnit unit;
        ValueRenderFormat format; Json error;
        if (!parse_common(request,waveform,signal,time,unit,format,error)) return error;
        const int index=design.resolve(signal.c_str());
        if (index<0) return action_error("SIGNAL_NOT_FOUND","signal not found in design: "+signal);
        const Sample target=sample_at(waveform,signal,time);
        if (!target.ok) return action_error("VALUE_NOT_AVAILABLE","signal value not available in FST");
        const Json limits=request.value("limits",Json::object());
        const size_t max_results=limits.value("max_results",10u);
        Json paths=Json::array();
        const auto evaluated=active_statement_groups(
            drivers_for(design,index),waveform,target.active_time);
        std::vector<IDesignBackend::DriverRecord> active_drivers;
        for (const auto& statement : evaluated.active) {
            const auto* driver=representative_driver(statement);
            if (driver&&driver->line>0&&!driver->file.empty())
                active_drivers.push_back(*driver);
        }
        for (const auto& driver : active_drivers) {
            if (paths.size()>=max_results) break;
            const std::string source=signal_name(design,driver.src_signal);
            paths.push_back(source_path(driver,source,signal));
        }
        const size_t total=active_drivers.size();
        const bool truncated=paths.size()<total;
        const std::string rendered_time=waveform.format_time(time,unit);
        const std::string active_time=waveform.format_time(target.active_time,unit);
        Json summary{{"signal",signal},{"time",rendered_time},{"active_time",active_time},
            {"termination",!evaluated.unresolved.empty()?"unresolved":
                (paths.empty()?"no_driver":"assignment")},
            {"termination_detail",!evaluated.unresolved.empty()?"predicate_unresolved":
                (paths.empty()?"no_driver":"assignment")},
            {"scan_complete",evaluated.unresolved.empty()},
            {"analysis_complete",evaluated.unresolved.empty()},
            {"response_truncated",truncated},{"total_count",total},
            {"returned_count",paths.size()},
            {"truncation_scopes",truncated?Json::array({"response_paths"}):Json::array()}};
        return {{"ok",true},{"summary",summary},{"data",{{"paths",paths}}}};
    }
};

struct TraceActiveDriverChainHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.active_driver_chain"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& request) override {
        auto& globals=engine_globals();
        auto& waveform=*globals.waveform; auto& design=*globals.design;
        std::string root; uint64_t time=0; TimeRenderUnit unit;
        ValueRenderFormat format; Json error;
        if (!parse_common(request,waveform,root,time,unit,format,error)) return error;
        const Json limits=request.value("limits",Json::object());
        const size_t max_depth=limits.value("max_depth",8u);
        const size_t max_nodes=limits.value("max_nodes",50u);
        const size_t max_trace_signals=limits.value("max_trace_signals",64u);
        Json hops=Json::array();
        std::set<std::string> visited;
        std::string current=root,termination="no_driver",detail="no_driver";
        uint64_t current_time=time;
        bool limited=false,ambiguity_limited=false,ambiguity_incomplete=false;
        std::string frontier_signal; Sample frontier_sample;
        Json ambiguity=nullptr;
        for (size_t depth=0;;++depth) {
            const std::string visit_key=current+"\x1f"+std::to_string(current_time);
            if (!visited.insert(visit_key).second) {
                termination="loop_detected"; detail="loop_detected"; break;
            }
            const int index=design.resolve(current.c_str());
            if (index<0) return action_error("SIGNAL_NOT_FOUND","signal not found in design: "+current);
            const Sample sample=sample_at(waveform,current,current_time);
            if (!sample.ok) return action_error("VALUE_NOT_AVAILABLE","signal value not available in FST: "+current);
            auto drivers=drivers_for(design,index);
            const auto evaluated=active_statement_groups(
                drivers,waveform,sample.active_time);
            std::vector<StatementGroup> groups;
            for (const auto& statement : evaluated.active) {
                if (!statement.rhs.empty()) groups.push_back(statement);
            }
            const IDesignBackend::DriverRecord* selected=nullptr;
            std::string upstream;
            std::string ambiguity_kind;
            if (!evaluated.unresolved.empty()) ambiguity_kind="predicate_unresolved";
            else if (groups.size()>1) ambiguity_kind="multiple_active_candidates";
            else if (groups.size()==1&&groups[0].rhs.size()>1)
                ambiguity_kind="multiple_rhs_sources";
            if (!groups.empty()&&!groups[0].rhs.empty())
                selected=&groups[0].rhs[0];
            if (ambiguity_kind.empty()&&selected) {
                const std::string candidate=signal_name(design,selected->src_signal);
                if (!candidate.empty()&&candidate!=current&&
                    sample_at(waveform,candidate,sample.active_time).ok)
                    upstream=candidate;
            }
            hops.push_back(trace_hop(depth,current,sample,depth==0?"root":"driver",
                selected,design,index,waveform,unit,format));

            if (!ambiguity_kind.empty()) {
                const auto& evidence_groups=ambiguity_kind=="predicate_unresolved"
                    ?evaluated.unresolved:groups;
                ambiguity=ambiguity_evidence(ambiguity_kind,current,
                    sample.active_time,hops.size()-1,evidence_groups,max_trace_signals,
                    design,waveform,unit,format);
                if (ambiguity_kind=="predicate_unresolved") {
                    ambiguity["analysis_complete"]=false;
                    ambiguity_incomplete=true;
                } else {
                    ambiguity_limited=!ambiguity["analysis_complete"].get<bool>();
                }
                termination="ambiguous"; detail=ambiguity_kind; break;
            }

            if (upstream.empty()) {
                if (drivers.empty()&&(design.signal_direction(index)==1||
                                      design.signal_direction(index)==3)) {
                    std::vector<IDesignBackend::PortConnection> connections;
                    design.port_connections(index,connections);
                    for (const auto& connection : connections) {
                        const int other=connection.port_signal==index
                            ?connection.connected_signal:connection.port_signal;
                        const std::string candidate=signal_name(design,other);
                        if (!candidate.empty()&&candidate!=current&&
                            sample_at(waveform,candidate,sample.active_time).ok) {
                            upstream=candidate;
                            break;
                        }
                    }
                    if (upstream.empty()) termination="primary_input";
                }
                if (upstream.empty()&&termination!="primary_input") {
                    const bool has_control=std::any_of(drivers.begin(),drivers.end(),
                        [](const auto& driver){return driver.dependency_role=="control";});
                    termination=drivers.empty()?"no_driver":
                        (has_control?"control_only":"assignment");
                }
            }
            if (upstream.empty()) {
                detail=termination; break;
            }

            const uint64_t next_time=sample.active_time;
            frontier_signal=upstream;
            frontier_sample=sample_at(waveform,upstream,next_time);
            if (hops.size()>=max_nodes) {
                limited=true; termination="limit"; detail="max_nodes"; break;
            }
            if (depth>=max_depth) {
                limited=true; termination="limit"; detail="max_depth";
                break;
            }
            current=upstream;
            current_time=next_time;
        }
        Json data{{"hops",hops}};
        Json truncation=Json::array();
        if (limited) {
            truncation.push_back("analysis_trace");
            if (detail=="max_depth"&&frontier_sample.ok) {
                const std::string frontier_time=
                    waveform.format_time(frontier_sample.query_time,unit);
                data["depth_frontiers"]=Json::array({{{"chain_id","c0"},
                    {"signal",frontier_signal},{"time",frontier_time},
                    {"value",logic_string(frontier_sample,format)},
                    {"stopped_after_depth",max_depth}}});
                data["suggested_next_actions"]=Json::array({
                    {{"action","trace.active_driver_chain"},
                     {"reason","continue_from_depth_frontier"},{"chain_id","c0"},
                     {"args",{{"signal",frontier_signal},{"time",frontier_time}}},
                     {"limits",{{"max_depth",max_depth}}}},
                    {{"action","trace.active_driver_chain"},
                     {"reason","rerun_from_root_with_higher_depth"},
                     {"args",{{"signal",root},{"time",waveform.format_time(time,unit)}}},
                     {"limits",{{"max_depth",max_depth*2}}}}
                });
            }
        }
        if (!ambiguity.is_null()) data["ambiguity_evidence"]=ambiguity;
        if (ambiguity_limited) truncation.push_back("ambiguity_rhs_samples");
        const bool complete=!limited&&!ambiguity_limited&&!ambiguity_incomplete;
        Json summary{{"signal",root},{"time",waveform.format_time(time,unit)},
            {"termination",termination},{"termination_detail",detail},
            {"scan_complete",complete},{"analysis_complete",complete},
            {"response_truncated",false},{"total_count",hops.size()},
            {"returned_count",hops.size()},{"truncation_scopes",truncation}};
        return {{"ok",true},{"summary",summary},{"data",data}};
    }
};

struct TraceXOriginHandler : public EngineActionHandler {
    const char* action_name() const override { return "trace.x_origin"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& request) override {
        auto& globals=engine_globals();
        auto& waveform=*globals.waveform; auto& design=*globals.design;
        std::string root; uint64_t time=0; TimeRenderUnit unit;
        ValueRenderFormat format; Json error;
        if (!parse_common(request,waveform,root,time,unit,format,error)) return error;
        const int root_index=design.resolve(root.c_str());
        if (root_index<0) return action_error("SIGNAL_NOT_FOUND","signal not found in design: "+root);
        const Sample query_sample=sample_at(waveform,root,time);
        if (!query_sample.ok) return action_error("VALUE_NOT_AVAILABLE","signal value not available in FST");
        const std::string query_time=waveform.format_time(time,unit);
        Json query{{"signal",root},{"query_time",query_time},
            {"value",logic_json(query_sample,format)},{"x_mask",x_mask(query_sample.bits)}};
        if (!has_x(query_sample.bits)) {
            Json summary{{"signal",root},{"query_time",query_time},
                {"termination","not_x_at_query_time"},{"evidence_status","proven"},
                {"chain_count",0},{"completed_chain_count",0},{"limited_chain_count",0},
                {"hop_count",0},{"origin_count",0},{"scan_complete",true},
                {"analysis_complete",true},{"response_truncated",false},
                {"total_count",0},{"returned_count",0},{"truncation_scopes",Json::array()}};
            return {{"ok",true},{"summary",summary},
                {"data",{{"query",query},{"chains",Json::array()},{"limitations",Json::array()}}}};
        }

        const Json limits=request.value("limits",Json::object());
        const size_t max_depth=limits.value("max_depth",8u);
        const size_t max_chains=limits.value("max_chains",8u);
        const size_t max_nodes=limits.value("max_nodes",50u);
        const size_t max_time_steps=limits.value("max_time_steps",128u);
        const size_t max_trace_signals=limits.value("max_trace_signals",64u);
        struct State {
            std::string signal,chain_id,relation;
            std::set<std::string> visited;
            Json hops=Json::array();
            Json pending_dependencies=Json::array();
            Json branch_events=Json::array();
            size_t depth=0;
            uint64_t time=0;
            bool complete=true;
            IDesignBackend::DriverRecord incoming;
            bool has_incoming=false;
        };
        auto visit_key=[](const std::string& signal,uint64_t onset) {
            return signal+"\x1f"+std::to_string(onset);
        };
        auto current_json=[&](const std::string& signal,const Sample& sample,
                              uint64_t onset) {
            return Json{{"signal",signal},
                {"x_onset_time",waveform.format_time(onset,unit)},
                {"value",logic_json(sample,format)},{"x_mask",x_mask(sample.bits)}};
        };
        auto finish_chain=[&](const State& state,const Sample& sample,uint64_t onset,
                              const std::string& status,const std::string& detail,
                              bool complete,bool with_origin) {
            Json chain{{"chain_id",state.chain_id},{"status",status},
                {"termination_detail",detail},{"complete",complete},
                {"current",current_json(state.signal,sample,onset)},
                {"hops",state.hops}};
            if (!state.pending_dependencies.empty())
                chain["pending_x_dependencies"]=state.pending_dependencies;
            if (!state.branch_events.empty()) chain["branch_events"]=state.branch_events;
            if (with_origin) {
                const int index=design.resolve(state.signal.c_str());
                const char* source_file=index>=0?design.signal_file(index):nullptr;
                chain["origin"]={{"signal",state.signal},
                    {"x_onset_time",waveform.format_time(onset,unit)},
                    {"kind","assignment"},{"reason",detail},
                    {"evidence_status","best_effort"},
                    {"file",source_file?source_file:""},
                    {"line",std::max(0,index>=0?design.signal_line(index):0)}};
            }
            return chain;
        };

        State initial;
        initial.signal=root;
        initial.chain_id="c0";
        initial.relation="root";
        initial.time=x_onset_time(waveform,query_sample);
        initial.visited.insert(visit_key(root,initial.time));
        std::vector<State> pending{initial};
        Json chains=Json::array(),limitations=Json::array();
        std::set<uint64_t> visited_times;
        size_t nodes=0,chain_serial=1;
        bool global_node_limit=false,global_time_limit=false;
        while (!pending.empty()) {
            State state=std::move(pending.back());
            pending.pop_back();
            const int index=design.resolve(state.signal.c_str());
            const Sample sample=sample_at(waveform,state.signal,state.time);
            if (index<0||!sample.ok) continue;
            const uint64_t onset=x_onset_time(waveform,sample);

            if (state.depth>max_depth||global_node_limit||global_time_limit) {
                const std::string detail=state.depth>max_depth?"max_depth":
                    (global_node_limit?"max_nodes":"max_time_steps");
                chains.push_back(finish_chain(
                    state,sample,onset,"limit",detail,false,false));
                continue;
            }
            if (nodes>=max_nodes) {
                global_node_limit=true;
                limitations.push_back("trace truncated by limits.max_nodes");
                chains.push_back(finish_chain(
                    state,sample,onset,"limit","max_nodes",false,false));
                continue;
            }
            if (visited_times.insert(state.time).second&&
                visited_times.size()>max_time_steps) {
                global_time_limit=true;
                limitations.push_back("trace truncated by limits.max_time_steps");
                chains.push_back(finish_chain(
                    state,sample,onset,"limit","max_time_steps",false,false));
                continue;
            }

            state.hops.push_back(x_hop(state.hops.size(),state.chain_id,
                state.signal,sample,onset,state.relation,
                state.has_incoming?&state.incoming:nullptr,
                design,index,waveform,unit,format));
            ++nodes;

            const auto all_drivers=drivers_for(design,index);
            const auto evaluated=active_statement_groups(
                all_drivers,waveform,sample.active_time);
            if (!evaluated.unresolved.empty()) {
                limitations.push_back(
                    "activation predicate unresolved at "+state.signal);
                chains.push_back(finish_chain(state,sample,onset,"unresolved",
                    "predicate_unresolved",false,false));
                continue;
            }
            std::vector<IDesignBackend::DriverRecord> active_drivers;
            for (const auto& statement : evaluated.active) {
                active_drivers.insert(active_drivers.end(),
                    statement.records.begin(),statement.records.end());
            }
            if (!all_drivers.empty()&&active_drivers.empty()) {
                limitations.push_back(
                    "no active statement matched waveform controls at "+state.signal);
                chains.push_back(finish_chain(state,sample,onset,"unresolved",
                    "no_active_statement",false,false));
                continue;
            }

            std::vector<IDesignBackend::DriverRecord> dependencies;
            std::set<std::pair<int,std::string>> dependency_keys;
            for (const auto& driver : active_drivers) {
                if ((driver.dependency_role!="rhs"&&
                     driver.dependency_role!="control")||driver.src_signal<0)
                    continue;
                if (dependency_keys.insert(
                        {driver.src_signal,driver.dependency_role}).second)
                    dependencies.push_back(driver);
            }
            if (dependencies.size()>max_trace_signals) {
                for (size_t offset=max_trace_signals;offset<dependencies.size();++offset) {
                    state.pending_dependencies.push_back({
                        {"signal",signal_name(design,dependencies[offset].src_signal)},
                        {"relation",dependencies[offset].dependency_role},
                        {"reason","max_trace_signals"}});
                }
                dependencies.resize(max_trace_signals);
                state.complete=false;
                limitations.push_back(
                    "dependency enumeration truncated by limits.max_trace_signals at "+
                    state.signal);
            }

            struct Source {
                std::string signal,relation;
                uint64_t onset=0;
                IDesignBackend::DriverRecord driver;
            };
            std::vector<Source> sources;
            for (const auto& driver : dependencies) {
                const std::string candidate=signal_name(design,driver.src_signal);
                if (candidate.empty()) continue;
                const Sample upstream=sample_at(waveform,candidate,state.time);
                if (!upstream.ok||!has_x(upstream.bits)) continue;
                const uint64_t upstream_onset=x_onset_time(waveform,upstream);
                if (state.visited.count(visit_key(candidate,upstream_onset))) continue;
                sources.push_back({candidate,driver.dependency_role,
                                   upstream_onset,driver});
            }
            if (dependencies.empty()) {
                std::vector<IDesignBackend::PortConnection> ports;
                design.port_connections(index,ports);
                for (const auto& port : ports) {
                    const int other=port.port_signal==index
                        ?port.connected_signal:port.port_signal;
                    const std::string candidate=signal_name(design,other);
                    if (candidate.empty()) continue;
                    const Sample upstream=sample_at(waveform,candidate,state.time);
                    if (!upstream.ok||!has_x(upstream.bits)) continue;
                    const uint64_t upstream_onset=x_onset_time(waveform,upstream);
                    if (state.visited.count(visit_key(candidate,upstream_onset))) continue;
                    IDesignBackend::DriverRecord relation;
                    relation.src_signal=other;
                    relation.kind=port.kind;
                    relation.dependency_role="port";
                    sources.push_back({candidate,"port",upstream_onset,relation});
                }
            }

            if (sources.empty()) {
                chains.push_back(finish_chain(state,sample,onset,"origin_found",
                    "candidate_x_source",state.complete,true));
                continue;
            }

            std::vector<State> children;
            children.reserve(sources.size());
            for (size_t source_index=0;source_index<sources.size();++source_index) {
                State child=state;
                child.signal=sources[source_index].signal;
                child.relation=sources[source_index].relation;
                child.incoming=sources[source_index].driver;
                child.has_incoming=true;
                child.depth=state.depth+1;
                child.time=sources[source_index].onset;
                child.visited.insert(visit_key(child.signal,child.time));
                if (source_index>0) {
                    child.chain_id="c"+std::to_string(chain_serial++);
                    for (auto& hop : child.hops) hop["chain_id"]=child.chain_id;
                    child.pending_dependencies=Json::array();
                    child.branch_events=Json::array();
                    child.complete=true;
                }
                children.push_back(std::move(child));
            }
            for (auto it=children.rbegin();it!=children.rend();++it)
                pending.push_back(std::move(*it));
        }

        if (chains.size()>max_chains) {
            Json& retained=chains[0];
            Json omitted=Json::array();
            for (size_t index=max_chains;index<chains.size();++index) {
                const Json& current=chains[index]["current"];
                const Json& hops=chains[index]["hops"];
                const std::string relation=hops.empty()?"rhs":
                    hops.back().value("relation","rhs");
                omitted.push_back({{"signal",current["signal"]},
                    {"relation",relation},
                    {"x_onset_time",current["x_onset_time"]}});
            }
            Json retained_pending=retained.value(
                "pending_x_dependencies",Json::array());
            for (const auto& item : omitted) retained_pending.push_back(item);
            retained["pending_x_dependencies"]=retained_pending;
            Json events=retained.value("branch_events",Json::array());
            events.push_back({{"hop_index",0},{"reason","max_chains"},
                {"x_dependency_count",chains.size()},
                {"returned_x_dependency_count",max_chains},
                {"omitted_x_dependency_count",chains.size()-max_chains},
                {"pending_x_dependencies",omitted}});
            retained["branch_events"]=events;
            retained["complete"]=false;
            limitations.push_back("X semantic branches truncated by limits.max_chains");
            while (chains.size()>max_chains) chains.erase(chains.end()-1);
        }

        Json depth_frontiers=Json::array();
        Json suggested=Json::array();
        size_t completed_count=0,limited_count=0,unresolved_count=0;
        size_t hop_count=0,origin_count=0;
        for (size_t index=0;index<chains.size();++index) {
            Json& chain=chains[index];
            const std::string chain_id="c"+std::to_string(index);
            chain["chain_id"]=chain_id;
            for (auto& hop : chain["hops"]) hop["chain_id"]=chain_id;
            hop_count+=chain["hops"].size();
            if (chain.contains("origin")) ++origin_count;
            const bool chain_limited=chain.value("status","")=="limit"||
                !chain.value("complete",true);
            if (chain.value("status","")=="unresolved") ++unresolved_count;
            if (chain_limited) ++limited_count; else ++completed_count;
            if (chain.value("status","")=="limit"&&
                chain.value("termination_detail","")=="max_depth") {
                const Json& current=chain["current"];
                depth_frontiers.push_back({{"signal",current["signal"]},
                    {"continue_time",current["x_onset_time"]},
                    {"value",current["value"]},{"x_mask",current["x_mask"]},
                    {"chain_id",chain_id},{"stopped_after_depth",max_depth}});
                Json continued_limits=limits;
                continued_limits["max_depth"]=max_depth;
                continued_limits["max_chains"]=max_chains;
                suggested.push_back({{"action","trace.x_origin"},
                    {"reason","continue_from_depth_frontier"},{"chain_id",chain_id},
                    {"args",{{"signal",current["signal"]},
                             {"time",current["x_onset_time"]},
                             {"value_format",request["args"].value("value_format","hex")}}},
                    {"limits",continued_limits}});
            }
        }
        if (!depth_frontiers.empty()) {
            Json deeper_limits=limits;
            deeper_limits["max_depth"]=max_depth*2;
            deeper_limits["max_chains"]=max_chains;
            suggested.push_back({{"action","trace.x_origin"},
                {"reason","rerun_from_root_with_higher_depth"},
                {"args",{{"signal",root},{"time",query_time},
                         {"value_format",request["args"].value("value_format","hex")}}},
                {"limits",deeper_limits}});
        }

        const bool complete=limited_count==0;
        const std::string termination=unresolved_count
            ?(completed_count?"partial":"unresolved")
            :limited_count?(completed_count?"partial":"limit")
            :(origin_count?"origin_found":"x_not_observable_upstream");
        Json data{{"query",query},{"chains",chains},{"limitations",limitations}};
        if (!depth_frontiers.empty()) data["depth_frontiers"]=depth_frontiers;
        if (!suggested.empty()) data["suggested_next_actions"]=suggested;
        Json summary{{"signal",root},{"query_time",query_time},
            {"termination",termination},
            {"evidence_status",origin_count?"best_effort":"unresolved"},
            {"chain_count",chains.size()},{"completed_chain_count",completed_count},
            {"limited_chain_count",limited_count},{"hop_count",hop_count},
            {"origin_count",origin_count},{"scan_complete",complete},
            {"analysis_complete",complete},{"response_truncated",false},
            {"total_count",chains.size()},{"returned_count",chains.size()},
            {"truncation_scopes",complete?Json::array():Json::array({"analysis_trace"})}};
        return {{"ok",true},{"summary",summary},{"data",data}};
    }
};

std::unique_ptr<EngineActionHandler> make_trace_active_driver_handler() {
    return std::make_unique<TraceActiveDriverHandler>();
}
std::unique_ptr<EngineActionHandler> make_trace_active_driver_chain_handler() {
    return std::make_unique<TraceActiveDriverChainHandler>();
}
std::unique_ptr<EngineActionHandler> make_trace_x_origin_handler() {
    return std::make_unique<TraceXOriginHandler>();
}

} // namespace xdebug_fst
