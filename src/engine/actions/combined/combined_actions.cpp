// combined_actions.cpp — trace.active_driver, trace.active_driver_chain,
// trace.x_origin (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"
#include "core/value/logic_value.h"
#include "waveform/clock_sampling.h"
#include "waveform/expr/expr_eval.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <iterator>
#include <limits>
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
    std::string mask=std::to_string(bits.size())+"'b";
    for (char bit : bits)
        mask += (bit=='x'||bit=='X'||bit=='h'||bit=='H'||bit=='u'||bit=='U'||
                 bit=='w'||bit=='W'||bit=='-')?'1':'0';
    return mask;
}

std::string x_origin_semantic_relation(const std::string& relation) {
    std::string semantic;
    size_t begin=0;
    while (begin<=relation.size()) {
        const size_t end=relation.find('+',begin);
        const std::string token=relation.substr(
            begin,end==std::string::npos?std::string::npos:end-begin);
        if (!token.empty()&&token!="port") {
            if (!semantic.empty()) semantic+='+';
            semantic+=token;
        }
        if (end==std::string::npos) break;
        begin=end+1;
    }
    return semantic;
}

void append_identity_field(std::string& key,const std::string& value) {
    key+=std::to_string(value.size());
    key+=':';
    key+=value;
}

std::string x_origin_semantic_chain_key(const Json& chain) {
    std::string key;
    for (const auto& hop : chain.value("hops",Json::array())) {
        const std::string relation=x_origin_semantic_relation(
            hop.value("relation",""));
        if (relation.empty()) continue;
        append_identity_field(key,relation);
        append_identity_field(key,hop.value("signal",""));
        append_identity_field(key,hop.value("x_onset_time",""));
    }
    if (chain.contains("_semantic_tail_relation")) {
        const std::string relation=x_origin_semantic_relation(
            chain.value("_semantic_tail_relation",""));
        if (!relation.empty()) {
            append_identity_field(key,relation);
            const Json& current=chain.at("current");
            append_identity_field(key,current.value("signal",""));
            append_identity_field(key,current.value("x_onset_time",""));
        }
    }
    // Port hops are physically transparent, but their terminal semantic
    // outcome is not.  A feedback loop and a real X source can share the same
    // non-port prefix and diverge only through ref/modport edges.  Preserve
    // that distinction while still coalescing physical alias variants that
    // reach the same terminal signal with the same outcome.
    append_identity_field(key,chain.value("status",""));
    if (chain.contains("current")&&chain.at("current").is_object()) {
        const Json& current=chain.at("current");
        append_identity_field(key,current.value("signal",""));
        append_identity_field(key,current.value("x_onset_time",""));
    }
    return key;
}

std::string x_origin_semantic_state_key(
    const Json& hops,const std::string& incoming_relation,
    const std::string& current_signal,uint64_t current_onset) {
    const Json prefix{{"hops",hops}};
    std::string key=x_origin_semantic_chain_key(prefix);
    const std::string relation=x_origin_semantic_relation(incoming_relation);
    if (!relation.empty()) append_identity_field(key,relation);
    append_identity_field(key,current_signal);
    append_identity_field(key,std::to_string(current_onset));
    return key;
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

bool final_numeric_selector(const std::string& signal,std::string& base,
                            int64_t& value) {
    if (signal.empty()||signal.back()!=']') return false;
    const size_t open=signal.rfind('[');
    if (open==std::string::npos||open==0||open+2>signal.size()) return false;
    const std::string selector=signal.substr(open+1,signal.size()-open-2);
    if (selector.empty()||selector.find(':')!=std::string::npos) return false;
    size_t offset=0;
    bool negative=false;
    if (selector.front()=='-'||selector.front()=='+') {
        negative=selector.front()=='-';
        offset=1;
    }
    if (offset==selector.size()) return false;
    uint64_t magnitude=0;
    const uint64_t limit=negative
        ?static_cast<uint64_t>(std::numeric_limits<int64_t>::max())+1
        :static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    for (;offset<selector.size();++offset) {
        const unsigned char ch=static_cast<unsigned char>(selector[offset]);
        if (!std::isdigit(ch)) return false;
        const uint64_t digit=static_cast<uint64_t>(selector[offset]-'0');
        if (magnitude>(limit-digit)/10) return false;
        magnitude=magnitude*10+digit;
    }
    if (negative) {
        value=magnitude==limit?std::numeric_limits<int64_t>::min()
                              :-static_cast<int64_t>(magnitude);
    } else {
        value=static_cast<int64_t>(magnitude);
    }
    base=signal.substr(0,open);
    return !base.empty();
}

struct SelectedDesignSignal {
    int index=-1;
    bool exact=false;
    bool has_numeric_selector=false;
    std::string base;
    int64_t selector=0;
};

SelectedDesignSignal resolve_selected_design_signal(
    IDesignBackend& design,const std::string& signal) {
    SelectedDesignSignal selected;
    selected.index=design.resolve(signal.c_str());
    selected.exact=selected.index>=0&&
        signal_name(design,selected.index)==signal;
    selected.has_numeric_selector=final_numeric_selector(
        signal,selected.base,selected.selector);
    if (selected.index<0&&selected.has_numeric_selector)
        selected.index=design.resolve(selected.base.c_str());
    return selected;
}

size_t hierarchy_depth(const std::string& signal) {
    return static_cast<size_t>(std::count(signal.begin(),signal.end(),'.'));
}

std::string signal_scope(const std::string& signal) {
    const size_t dot=signal.rfind('.');
    return dot==std::string::npos?std::string():signal.substr(0,dot);
}

bool is_scope_ancestor(const std::string& ancestor,
                       const std::string& descendant) {
    return !ancestor.empty()&&descendant.size()>ancestor.size()&&
        descendant.compare(0,ancestor.size(),ancestor)==0&&
        descendant[ancestor.size()]=='.';
}

std::vector<int> ports_connected_to(IDesignBackend& design,int connected,
                                    int direction) {
    std::vector<int> ports;
    for (int index=0;index<design.signal_count();++index) {
        if (design.signal_direction(index)!=direction) continue;
        std::vector<IDesignBackend::PortConnection> connections;
        design.port_connections(index,connections);
        const bool matches=std::any_of(connections.begin(),connections.end(),
            [&](const auto& connection) {
                const int other=connection.port_signal==index
                    ?connection.connected_signal:connection.port_signal;
                return other==connected;
            });
        if (matches) ports.push_back(index);
    }
    return ports;
}

void annotate_output_instance_identities(
    IDesignBackend& design,int target,
    std::vector<IDesignBackend::DriverRecord>& drivers) {
    const std::vector<int> output_ports=ports_connected_to(design,target,2);
    if (output_ports.size()<2) return;

    std::map<int,std::set<std::string>> identities_by_source;
    for (int output_port : output_ports) {
        const std::string instance_scope=signal_scope(
            signal_name(design,output_port));
        if (instance_scope.empty()) continue;
        for (int input_port=0;input_port<design.signal_count();++input_port) {
            if (design.signal_direction(input_port)!=1||
                signal_scope(signal_name(design,input_port))!=instance_scope) {
                continue;
            }
            std::vector<IDesignBackend::PortConnection> connections;
            design.port_connections(input_port,connections);
            for (const auto& connection : connections) {
                const int source=connection.port_signal==input_port
                    ?connection.connected_signal:connection.port_signal;
                if (source>=0) identities_by_source[source].insert(instance_scope);
            }
        }
    }

    using BaseKey=std::tuple<std::string,int,std::string,std::string>;
    std::map<BaseKey,std::vector<size_t>> records_by_statement;
    for (size_t index=0;index<drivers.size();++index) {
        const auto& driver=drivers[index];
        records_by_statement[{driver.file,driver.line,driver.kind,
                              driver.activation_predicate}].push_back(index);
    }
    for (const auto& [base,indices] : records_by_statement) {
        (void)base;
        std::set<std::string> statement_identities;
        bool complete=!indices.empty();
        for (size_t index : indices) {
            const auto found=identities_by_source.find(drivers[index].src_signal);
            if (found==identities_by_source.end()||found->second.size()!=1) {
                complete=false;
                break;
            }
            statement_identities.insert(*found->second.begin());
        }
        if (!complete||statement_identities.size()<2) continue;
        for (size_t index : indices) {
            drivers[index].statement_identity=
                *identities_by_source.at(drivers[index].src_signal).begin();
        }
    }
}

bool is_assignment_kind(const std::string& kind) {
    return kind=="nba"||kind=="proc_assign"||kind=="cont_assign";
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
    bool has_event_time=false;
    uint64_t event_time=0;
    bool predicate_waveform_unknown=false;
    std::map<int,std::pair<int64_t,int64_t>> target_loop_ranges;
    std::vector<IDesignBackend::DriverRecord> records;
    std::vector<IDesignBackend::DriverRecord> rhs;
};

std::vector<StatementGroup> statement_groups(
    const std::vector<IDesignBackend::DriverRecord>& drivers) {
    std::map<std::tuple<std::string,int,std::string,std::string,std::string>,
             StatementGroup> grouped;
    for (const auto& driver : drivers) {
        if (driver.file.empty()||driver.line<=0) continue;
        const auto key=std::make_tuple(driver.file,driver.line,driver.kind,
                                       driver.activation_predicate,
                                       driver.statement_identity);
        auto& statement=grouped[key];
        statement.kind=driver.kind;
        statement.file=driver.file;
        statement.line=driver.line;
        if (statement.predicate.empty())
            statement.predicate=driver.activation_predicate;
        statement.records.push_back(driver);
        if (driver.dependency_role=="target_loop_index"&&
            driver.src_signal>=0&&driver.has_target_loop_range) {
            statement.target_loop_ranges[driver.src_signal]={
                driver.target_loop_first,driver.target_loop_last};
        }
        if (driver.dependency_role=="rhs"&&driver.src_signal>=0&&
            std::none_of(statement.rhs.begin(),statement.rhs.end(),
                [&](const auto& item){return item.src_signal==driver.src_signal;})) {
            statement.rhs.push_back(driver);
        }
    }
    std::vector<StatementGroup> statements;
    for (auto& [key,statement] : grouped) {
        (void)key;
        std::set<int> target_loop_indices;
        for (const auto& record : statement.records) {
            if (record.dependency_role=="target_loop_index"&&
                record.src_signal>=0) {
                target_loop_indices.insert(record.src_signal);
            }
        }
        statement.rhs.erase(std::remove_if(
            statement.rhs.begin(),statement.rhs.end(),[&](const auto& driver) {
                return target_loop_indices.count(driver.src_signal)>0;
            }),statement.rhs.end());
        std::sort(statement.rhs.begin(),statement.rhs.end(),
            [](const auto& left,const auto& right){
                return left.src_signal<right.src_signal;
            });
        statements.push_back(std::move(statement));
    }
    return statements;
}

// Keep a four-state waveform result separate from missing static/runtime
// evidence.  X-origin may trace the former; every consumer must fail closed
// on the latter.
enum class PredicateState { Active, Inactive, WaveformUnknown, Unresolved };

bool event_edge_matches(const std::string& role,const std::string& before,
                        const std::string& after) {
    if (before.size()!=1||after.size()!=1) return false;
    const char left=static_cast<char>(std::tolower(
        static_cast<unsigned char>(before.front())));
    const char right=static_cast<char>(std::tolower(
        static_cast<unsigned char>(after.front())));
    const auto unknown=[](char bit) { return bit!='0'&&bit!='1'; };
    const bool posedge=(left=='0'&&(right=='1'||unknown(right)))||
        (unknown(left)&&right=='1');
    const bool negedge=(left=='1'&&(right=='0'||unknown(right)))||
        (unknown(left)&&right=='0');
    if (role=="event_posedge") return posedge;
    if (role=="event_negedge") return negedge;
    if (role=="event_bothedge") return posedge||negedge;
    return role=="event_changed"&&left!=right;
}

bool latest_statement_event_time(const StatementGroup& statement,
                                 IDesignBackend& design,
                                 IWaveformBackend& waveform,uint64_t horizon,
                                 uint64_t& event_time) {
    bool found=false;
    const uint32_t horizon_index=waveform.time_idx_of(horizon);
    for (const auto& record : statement.records) {
        if (record.src_signal<0||record.dependency_role.rfind("event_",0)!=0)
            continue;
        const std::string event_signal=signal_name(design,record.src_signal);
        const uint32_t ref=waveform.find_signal(event_signal);
        if (ref==IWaveformBackend::kInvalidSignalRef) continue;
        if (!waveform.is_loaded(ref)&&waveform.load_signals({ref})!=1) continue;
        const auto indices=waveform.time_indices_of(ref);
        for (auto it=indices.rbegin();it!=indices.rend();++it) {
            if (*it>horizon_index||waveform.time_at(*it)>horizon) continue;
            IWaveformBackend::SampledValue before,after;
            if (!waveform.sampled_value_at(
                    ref,*it,IWaveformBackend::ObservationPoint::Before,before)||
                !waveform.sampled_value_at(
                    ref,*it,IWaveformBackend::ObservationPoint::Raw,after)) continue;
            if (!event_edge_matches(record.dependency_role,
                                    before.value.text,after.value.text)) continue;
            const uint64_t candidate=waveform.time_at(*it);
            if (!found||candidate>event_time) event_time=candidate;
            found=true;
            break;
        }
    }
    return found;
}

std::string predicate_waveform_signal(const std::string& signal,
                                      IDesignBackend& design,
                                      IWaveformBackend& waveform) {
    if (waveform.find_signal(signal)!=IWaveformBackend::kInvalidSignalRef)
        return signal;
    const int index=design.resolve(signal.c_str());
    if (index<0) return {};
    const int width=design.signal_width(index);
    std::set<std::string> candidates;
    for (int owner=0;owner<design.signal_count();++owner) {
        std::vector<IDesignBackend::PortConnection> connections;
        design.port_connections(owner,connections);
        for (const auto& connection : connections) {
            int other=-1;
            if (connection.port_signal==index)
                other=connection.connected_signal;
            else if (connection.connected_signal==index)
                other=connection.port_signal;
            if (other<0||other==index||design.signal_width(other)!=width)
                continue;
            const std::string candidate=signal_name(design,other);
            if (candidate.empty()||waveform.find_signal(candidate)==
                    IWaveformBackend::kInvalidSignalRef) {
                continue;
            }
            candidates.insert(candidate);
        }
    }
    return candidates.size()==1?*candidates.begin():std::string();
}

void map_driver_waveform_aliases(
    IDesignBackend& design,IWaveformBackend& waveform,
    std::vector<IDesignBackend::DriverRecord>& drivers) {
    for (auto& driver : drivers) {
        if (driver.src_signal<0) continue;
        const std::string source=signal_name(design,driver.src_signal);
        if (source.empty()) continue;
        const std::string waveform_signal=predicate_waveform_signal(
            source,design,waveform);
        if (waveform_signal.empty()||waveform_signal==source) continue;
        const int waveform_index=design.resolve(waveform_signal.c_str());
        if (waveform_index>=0) driver.src_signal=waveform_index;
    }
}

bool select_flattened_output_driver_facts(
    IDesignBackend& design,IWaveformBackend& waveform,int waveform_index,
    bool allow_sampleable_flattened,
    std::vector<IDesignBackend::DriverRecord>& drivers,int& driver_index) {
    if (design.signal_direction(waveform_index)!=2) return false;
    const std::vector<StatementGroup> boundary=statement_groups(drivers);
    if (boundary.size()!=1||boundary.front().kind!="cont_assign"||
        boundary.front().rhs.size()!=1) {
        return false;
    }

    const int flattened_index=boundary.front().rhs.front().src_signal;
    const std::string flattened=signal_name(design,flattened_index);
    const std::string waveform_signal=signal_name(design,waveform_index);
    if (flattened.empty()||waveform_signal.empty()||
        design.signal_direction(flattened_index)!=2||
        hierarchy_depth(flattened)>=hierarchy_depth(waveform_signal)||
        (!allow_sampleable_flattened&&
         waveform.find_signal(flattened)!=IWaveformBackend::kInvalidSignalRef)) {
        return false;
    }

    const std::vector<int> connected_outputs=ports_connected_to(
        design,flattened_index,2);
    if (std::find(connected_outputs.begin(),connected_outputs.end(),
                  waveform_index)==connected_outputs.end()) {
        return false;
    }

    auto flattened_drivers=drivers_for(design,flattened_index);
    const bool has_procedural_facts=std::any_of(
        flattened_drivers.begin(),flattened_drivers.end(),[](const auto& driver) {
            return driver.kind!="cont_assign";
        });
    if (!has_procedural_facts) return false;

    map_driver_waveform_aliases(design,waveform,flattened_drivers);
    drivers=std::move(flattened_drivers);
    driver_index=flattened_index;
    return true;
}

bool is_verilator_expression_temporary(const std::string& signal) {
    const size_t dot=signal.rfind('.');
    const std::string leaf=dot==std::string::npos
        ?signal:signal.substr(dot+1);
    return leaf.rfind("__vlemcall_",0)==0;
}

bool is_transparent_expression_signal(
    IDesignBackend& design,IWaveformBackend& waveform,int source) {
    const std::string source_name=signal_name(design,source);
    if (is_verilator_expression_temporary(source_name)) return true;
    if (source_name.empty()||
        waveform.find_signal(source_name)!=IWaveformBackend::kInvalidSignalRef) {
        return false;
    }

    // Pattern-variable bindings and similar combinational lowering locals can
    // be present in DesignDB without being emitted into the FST.  They are
    // safe to expand only when every static record belongs to a combinational
    // assignment and at least one real RHS dependency exists.  Sequential,
    // force and stateful unobservable signals must remain hard boundaries.
    const auto source_drivers=drivers_for(design,source);
    bool has_rhs=false;
    for (const auto& driver : source_drivers) {
        if (driver.kind!="cont_assign"&&driver.kind!="proc_assign")
            return false;
        if (driver.dependency_role=="rhs") has_rhs=true;
        if (driver.dependency_role=="self_rhs"||
            driver.dependency_role.rfind("event_",0)==0) {
            return false;
        }
    }
    return has_rhs;
}

void collect_expression_sources(
    IDesignBackend& design,IWaveformBackend& waveform,int source,
    const std::set<int>& parent_controls,std::set<int>& visited,
    std::set<int>& sources) {
    const std::string source_name=signal_name(design,source);
    if (!is_transparent_expression_signal(design,waveform,source)) {
        if (source>=0) sources.insert(source);
        return;
    }
    if (!visited.insert(source).second) {
        sources.insert(source);
        return;
    }

    const auto temporary_drivers=drivers_for(design,source);
    std::set<int> target_loop_indices;
    for (const auto& driver : temporary_drivers) {
        if (driver.dependency_role=="target_loop_index"&&driver.src_signal>=0)
            target_loop_indices.insert(driver.src_signal);
    }
    bool found_dependency=false;
    for (const auto& driver : temporary_drivers) {
        if (driver.src_signal<0||
            (driver.dependency_role!="rhs"&&driver.dependency_role!="control")||
            target_loop_indices.count(driver.src_signal)>0||
            (driver.dependency_role=="control"&&
             parent_controls.count(driver.src_signal)>0)) {
            continue;
        }
        found_dependency=true;
        collect_expression_sources(design,waveform,driver.src_signal,
                                   parent_controls,visited,sources);
    }
    if (!found_dependency) sources.insert(source);
}

void expand_expression_temporaries(
    IDesignBackend& design,IWaveformBackend& waveform,
    std::vector<IDesignBackend::DriverRecord>& drivers) {
    std::set<int> parent_controls;
    for (const auto& driver : drivers) {
        if (driver.dependency_role=="control"&&driver.src_signal>=0)
            parent_controls.insert(driver.src_signal);
    }

    std::vector<IDesignBackend::DriverRecord> expanded;
    for (const auto& driver : drivers) {
        if (driver.dependency_role!="rhs"||
            !is_transparent_expression_signal(
                design,waveform,driver.src_signal)) {
            expanded.push_back(driver);
            continue;
        }
        std::set<int> visited;
        std::set<int> sources;
        collect_expression_sources(design,waveform,driver.src_signal,
                                   parent_controls,visited,sources);
        if (sources.size()==1&&sources.count(driver.src_signal)>0) {
            expanded.push_back(driver);
            continue;
        }
        for (int source_index : sources) {
            auto source_driver=driver;
            source_driver.src_signal=source_index;
            expanded.push_back(std::move(source_driver));
        }
    }
    map_driver_waveform_aliases(design,waveform,expanded);
    drivers=std::move(expanded);
}

void annotate_loop_selected_rhs(
    std::vector<IDesignBackend::DriverRecord>& drivers) {
    using StatementKey=
        std::tuple<std::string,int,std::string,std::string,std::string>;
    std::map<StatementKey,std::vector<size_t>> records_by_statement;
    for (size_t index=0;index<drivers.size();++index) {
        const auto& driver=drivers[index];
        records_by_statement[{driver.file,driver.line,driver.kind,
                              driver.activation_predicate,
                              driver.statement_identity}].push_back(index);
    }
    for (const auto& [key,indices] : records_by_statement) {
        (void)key;
        std::set<int> selected_sources;
        std::set<int> selector_sources;
        for (size_t index : indices) {
            const auto& driver=drivers[index];
            if (driver.src_signal<0) continue;
            if (driver.dependency_role=="rhs_loop_selected")
                selected_sources.insert(driver.src_signal);
            else if (driver.dependency_role=="rhs_loop_index")
                selector_sources.insert(driver.src_signal);
        }
        if (selector_sources.size()!=1) continue;
        const int selector=*selector_sources.begin();
        for (size_t index : indices) {
            auto& driver=drivers[index];
            if (driver.dependency_role=="rhs"&&
                selected_sources.count(driver.src_signal)>0) {
                driver.rhs_selector_signal=selector;
            }
        }
    }
}

void replace_expression_signal(ExprNode* node,const std::string& from,
                               const std::string& to) {
    if (!node) return;
    if ((node->kind==ExprNode::Kind::Signal||
         node->kind==ExprNode::Kind::Slice)&&node->signal==from) {
        node->signal=to;
    }
    replace_expression_signal(node->left,from,to);
    replace_expression_signal(node->right,from,to);
}

void bind_expression_signal(ExprNode* node,const std::string& signal,
                            int64_t value,int signal_width) {
    if (!node) return;
    if ((node->kind==ExprNode::Kind::Signal||
         node->kind==ExprNode::Kind::Slice)&&node->signal==signal) {
        uint64_t raw=static_cast<uint64_t>(value);
        int width=std::clamp(signal_width,1,64);
        if (node->kind==ExprNode::Kind::Slice) {
            width=std::clamp(node->msb-node->lsb+1,1,64);
            raw=node->lsb>=64?0:raw>>node->lsb;
            if (width<64) raw&=(UINT64_C(1)<<width)-1;
        }
        node->kind=ExprNode::Kind::Const;
        node->op.clear();
        node->signal.clear();
        node->msb=-1;
        node->lsb=-1;
        node->value=logic_value_from_u64(raw,width);
        return;
    }
    bind_expression_signal(node->left,signal,value,signal_width);
    bind_expression_signal(node->right,signal,value,signal_width);
}

bool last_materialized_selector(IDesignBackend& design,const std::string& base,
                                int64_t& last) {
    bool found=false;
    for (int index=0;index<design.signal_count();++index) {
        const std::string candidate=signal_name(design,index);
        std::string candidate_base;
        int64_t selector=0;
        if (!final_numeric_selector(candidate,candidate_base,selector)||
            candidate_base!=base) {
            continue;
        }
        if (!found||selector>last) last=selector;
        found=true;
    }
    return found;
}

bool bind_target_loop_indices(
    IDesignBackend& design,const SelectedDesignSignal& selected,
    std::vector<IDesignBackend::DriverRecord>& drivers) {
    if (!selected.has_numeric_selector||selected.index<0) return false;
    int64_t first=0;
    int64_t last=0;
    if (selected.exact) {
        // Original active-trace exposes one elaborated loop-body statement for
        // materialized unpacked elements and evaluates it with the final
        // elaborated selector, independently of the requested element.
        if (!last_materialized_selector(design,selected.base,last)) return false;
        first=last;
    } else {
        // A packed bit is a waveform view of one vector object.  The original
        // reports every statement whose loop predicate can be active for any
        // vector position, so retain the full selector domain here.
        const int width=design.signal_width(selected.index);
        if (width<=0) return false;
        last=static_cast<int64_t>(width)-1;
    }
    bool bound=false;
    for (auto& driver : drivers) {
        if (driver.dependency_role!="target_loop_index"||driver.src_signal<0)
            continue;
        driver.has_target_loop_range=true;
        driver.target_loop_first=first;
        driver.target_loop_last=last;
        bound=true;
    }
    return bound;
}

PredicateState evaluate_predicate_once(
    const StatementGroup& statement,const std::map<int,int64_t>& bindings,
    IDesignBackend& design,IWaveformBackend& waveform,uint64_t active_time) {
    if (statement.predicate.empty()) return PredicateState::Unresolved;
    std::string error;
    std::unique_ptr<ExprNode> expression(
        parse_expression(statement.predicate,error));
    if (!expression) return PredicateState::Unresolved;
    for (const auto& [index,value] : bindings) {
        const std::string bound_signal=signal_name(design,index);
        if (bound_signal.empty()) return PredicateState::Unresolved;
        bind_expression_signal(expression.get(),bound_signal,value,
                               design.signal_width(index));
    }
    std::vector<uint32_t> refs;
    for (const std::string& signal : expression_signals(expression.get())) {
        const std::string resolved=predicate_waveform_signal(
            signal,design,waveform);
        if (resolved.empty()) return PredicateState::Unresolved;
        const uint32_t ref=waveform.find_signal(resolved);
        if (ref==IWaveformBackend::kInvalidSignalRef)
            return PredicateState::Unresolved;
        refs.push_back(ref);
        if (resolved!=signal)
            replace_expression_signal(expression.get(),signal,resolved);
    }
    if (!refs.empty()&&static_cast<size_t>(waveform.load_signals(refs))!=refs.size())
        return PredicateState::Unresolved;
    const LogicValue value=eval_expression(
        expression.get(),waveform,waveform.time_idx_of(active_time));
    if (value.bits.empty()) return PredicateState::Unresolved;
    if (!value.known) return PredicateState::WaveformUnknown;
    return value.bits.find('1')==std::string::npos
        ?PredicateState::Inactive:PredicateState::Active;
}

PredicateState evaluate_predicate(const StatementGroup& statement,
                                  IDesignBackend& design,
                                  IWaveformBackend& waveform,
                                  uint64_t active_time) {
    if (statement.target_loop_ranges.empty()) {
        return evaluate_predicate_once(
            statement,{},design,waveform,active_time);
    }

    constexpr size_t kMaxLoopSelectorBindings=65536;
    std::vector<std::map<int,int64_t>> bindings(1);
    for (const auto& [index,range] : statement.target_loop_ranges) {
        const auto [first,last]=range;
        if (last<first) return PredicateState::Unresolved;
        const uint64_t span=static_cast<uint64_t>(last-first)+1;
        if (span>kMaxLoopSelectorBindings||
            bindings.size()>kMaxLoopSelectorBindings/span) {
            return PredicateState::Unresolved;
        }
        std::vector<std::map<int,int64_t>> expanded;
        expanded.reserve(bindings.size()*static_cast<size_t>(span));
        for (const auto& existing : bindings) {
            for (int64_t value=first;;++value) {
                auto current=existing;
                current[index]=value;
                expanded.push_back(std::move(current));
                if (value==last) break;
            }
        }
        bindings=std::move(expanded);
    }

    bool waveform_unknown=false;
    bool unresolved=false;
    for (const auto& binding : bindings) {
        const PredicateState state=evaluate_predicate_once(
            statement,binding,design,waveform,active_time);
        if (state==PredicateState::Active) return state;
        waveform_unknown|=state==PredicateState::WaveformUnknown;
        unresolved|=state==PredicateState::Unresolved;
    }
    if (unresolved) return PredicateState::Unresolved;
    return waveform_unknown
        ?PredicateState::WaveformUnknown:PredicateState::Inactive;
}

struct EvaluatedStatements {
    std::vector<StatementGroup> active;
    std::vector<StatementGroup> unresolved;
};

void apply_unique_nba_priority(EvaluatedStatements& statements) {
    const auto has_kind=[](const StatementGroup& statement,const char* kind) {
        return statement.kind==kind;
    };
    const bool has_force=std::any_of(
        statements.active.begin(),statements.active.end(),
        [&](const auto& statement){return has_kind(statement,"force");})||
        std::any_of(statements.unresolved.begin(),statements.unresolved.end(),
        [&](const auto& statement){return has_kind(statement,"force");});
    if (has_force) return;
    const size_t active_nba=static_cast<size_t>(std::count_if(
        statements.active.begin(),statements.active.end(),
        [&](const auto& statement){return has_kind(statement,"nba");}));
    const bool unresolved_nba=std::any_of(
        statements.unresolved.begin(),statements.unresolved.end(),
        [&](const auto& statement){return has_kind(statement,"nba");});
    if (active_nba!=1||unresolved_nba) return;
    statements.active.erase(std::remove_if(
        statements.active.begin(),statements.active.end(),
        [&](const auto& statement){return !has_kind(statement,"nba");}),
        statements.active.end());
    statements.unresolved.clear();
}

EvaluatedStatements active_statement_groups(
    const std::vector<IDesignBackend::DriverRecord>& drivers,
    IDesignBackend& design,IWaveformBackend& waveform,uint64_t active_time,
    uint64_t event_horizon=0) {
    EvaluatedStatements result;
    for (auto& statement : statement_groups(drivers)) {
        if (event_horizon>0) {
            statement.has_event_time=latest_statement_event_time(
                statement,design,waveform,event_horizon,statement.event_time);
        }
        const PredicateState state=evaluate_predicate(
            statement,design,waveform,statement.has_event_time
                ?statement.event_time:active_time);
        if (state==PredicateState::Active)
            result.active.push_back(std::move(statement));
        else if (state==PredicateState::WaveformUnknown) {
            statement.predicate_waveform_unknown=true;
            result.unresolved.push_back(std::move(statement));
        } else if (state==PredicateState::Unresolved)
            result.unresolved.push_back(std::move(statement));
    }
    return result;
}

bool is_pure_self_hold(const StatementGroup& statement,int target_signal) {
    if (statement.kind!="nba"||!statement.rhs.empty()) return false;
    return std::any_of(statement.records.begin(),statement.records.end(),
        [&](const auto& record) {
        return record.dependency_role=="self_rhs"&&
            record.src_signal==target_signal;
    });
}

EvaluatedStatements active_statement_groups_skipping_self_hold(
    const std::vector<IDesignBackend::DriverRecord>& drivers,
    IDesignBackend& design,IWaveformBackend& waveform,int target_signal,
    uint64_t active_time,uint64_t event_horizon,size_t max_backtracks,
    bool* backtrack_limited=nullptr) {
    if (backtrack_limited) *backtrack_limited=false;
    EvaluatedStatements original=active_statement_groups(
        drivers,design,waveform,active_time,event_horizon);
    apply_unique_nba_priority(original);
    EvaluatedStatements current=original;
    uint64_t horizon=event_horizon;
    for (size_t attempt=0;;++attempt) {
        if (!current.unresolved.empty()||current.active.empty()||
            !std::all_of(current.active.begin(),current.active.end(),
                [&](const auto& statement) {
                    return is_pure_self_hold(statement,target_signal);
                })) return current;
        if (attempt>=max_backtracks) {
            if (backtrack_limited) *backtrack_limited=true;
            return original;
        }
        uint64_t self_hold_time=0;
        bool has_event=false;
        for (const auto& statement : current.active) {
            if (!statement.has_event_time) return original;
            if (!has_event||statement.event_time>self_hold_time)
                self_hold_time=statement.event_time;
            has_event=true;
        }
        if (!has_event||self_hold_time==0) return original;
        horizon=self_hold_time-1;
        current=active_statement_groups(
            drivers,design,waveform,active_time,horizon);
        apply_unique_nba_priority(current);
    }
}

bool causal_event_time_through_unique_chain(
    IDesignBackend& design,IWaveformBackend& waveform,
    const std::string& signal,uint64_t horizon,size_t remaining,
    std::set<std::string>& visited,uint64_t& event_time) {
    if (remaining==0||!visited.insert(signal).second) return false;
    const int index=design.resolve(signal.c_str());
    if (index<0) return false;
    const Sample sample=sample_at(waveform,signal,horizon);
    if (!sample.ok) return false;
    auto drivers=drivers_for(design,index);
    annotate_output_instance_identities(design,index,drivers);
    auto evaluated=active_statement_groups_skipping_self_hold(
        drivers,design,waveform,index,sample.active_time,horizon,remaining);
    if (!evaluated.unresolved.empty()||evaluated.active.size()!=1) return false;
    const StatementGroup& statement=evaluated.active.front();
    if (statement.kind=="nba"&&statement.has_event_time) {
        event_time=statement.event_time;
        return true;
    }
    if (statement.kind!="cont_assign"||statement.rhs.size()!=1) return false;
    const std::string upstream=signal_name(
        design,statement.rhs.front().src_signal);
    if (upstream.empty()||upstream==signal) return false;
    return causal_event_time_through_unique_chain(
        design,waveform,upstream,horizon,remaining-1,visited,event_time);
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
                     TimeRenderUnit unit,ValueRenderFormat format,
                     const char* missing_status="missing_value") {
    if (!sample.ok) return {{"status",missing_status},{"value",nullptr},
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
            std::string source=signal_name(design,driver.src_signal);
            if (driver.rhs_selector_signal>=0) {
                const std::string selector=signal_name(
                    design,driver.rhs_selector_signal);
                const size_t dot=selector.rfind('.');
                const std::string leaf=dot==std::string::npos
                    ?selector:selector.substr(dot+1);
                if (!source.empty()&&!leaf.empty())
                    source+="["+leaf+"]";
            }
            const bool source_exists=waveform.find_signal(source)!=
                IWaveformBackend::kInvalidSignalRef;
            const char* missing_status=source_exists
                ?"missing_value":"signal_not_found";
            const Sample before=sample_before(waveform,source,active_time);
            const Sample after=sample_at(waveform,source,active_time);
            Json before_json=ambiguity_value(
                before,waveform,unit,format,missing_status);
            Json after_json=ambiguity_value(
                after,waveform,unit,format,missing_status);
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
        auto drivers=drivers_for(design,index);
        annotate_output_instance_identities(design,index,drivers);
        auto evaluated=active_statement_groups(
            drivers,design,waveform,target.active_time);
        apply_unique_nba_priority(evaluated);
        const bool has_active_force=std::any_of(
            evaluated.active.begin(),evaluated.active.end(),
            [](const auto& statement) { return statement.kind=="force"; });
        std::vector<IDesignBackend::DriverRecord> active_drivers;
        for (const auto& statement : evaluated.active) {
            if (has_active_force&&statement.kind!="force") continue;
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
            {"termination",has_active_force?"force":
                (!evaluated.unresolved.empty()?"unresolved":
                    (paths.empty()?"no_driver":"assignment"))},
            {"termination_detail",has_active_force?"force":
                (!evaluated.unresolved.empty()?"predicate_unresolved":
                    (paths.empty()?"no_driver":"assignment"))},
            {"scan_complete",has_active_force||evaluated.unresolved.empty()},
            {"analysis_complete",has_active_force||evaluated.unresolved.empty()},
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
        std::string current=root,termination="unresolved",detail="unresolved";
        std::string previous;
        SelectedDesignSignal loop_selection_context;
        bool has_loop_selection_context=false;
        uint64_t current_time=time;
        bool limited=false,ambiguity_limited=false,ambiguity_incomplete=false;
        std::string frontier_signal; Sample frontier_sample;
        Json ambiguity=nullptr;
        for (size_t depth=0;;++depth) {
            const std::string visit_key=current+"\x1f"+std::to_string(current_time);
            if (!visited.insert(visit_key).second) {
                termination="loop_detected"; detail="loop_detected"; break;
            }
            const SelectedDesignSignal selected_signal=
                resolve_selected_design_signal(design,current);
            if (selected_signal.has_numeric_selector) {
                loop_selection_context=selected_signal;
                has_loop_selection_context=true;
            }
            const int index=selected_signal.index;
            if (index<0) return action_error("SIGNAL_NOT_FOUND","signal not found in design: "+current);
            Sample sample=sample_at(waveform,current,current_time);
            if (!sample.ok) return action_error("VALUE_NOT_AVAILABLE","signal value not available in FST: "+current);
            auto drivers=drivers_for(design,index);
            int driver_index=index;
            select_flattened_output_driver_facts(
                design,waveform,index,
                selected_signal.has_numeric_selector&&!selected_signal.exact,
                drivers,driver_index);
            if (has_loop_selection_context&&bind_target_loop_indices(
                    design,loop_selection_context,drivers)) {
                has_loop_selection_context=false;
            }
            annotate_output_instance_identities(design,driver_index,drivers);
            expand_expression_temporaries(design,waveform,drivers);
            annotate_loop_selected_rhs(drivers);
            bool self_hold_backtrack_limited=false;
            auto evaluated=active_statement_groups_skipping_self_hold(
                drivers,design,waveform,driver_index,sample.active_time,current_time,
                max_nodes,&self_hold_backtrack_limited);
            if (self_hold_backtrack_limited) {
                hops.push_back(trace_hop(depth,current,sample,
                    depth==0?"root":"driver",nullptr,design,index,waveform,
                    unit,format));
                limited=true;
                termination="limit";
                detail="max_nodes";
                break;
            }
            std::vector<StatementGroup> groups;
            for (const auto& statement : evaluated.active) {
                const bool has_control=std::any_of(
                    statement.records.begin(),statement.records.end(),
                    [](const auto& driver) {
                        return driver.dependency_role=="control";
                    });
                if (!statement.rhs.empty()||statement.kind=="nba"||has_control)
                    groups.push_back(statement);
            }
            const bool has_active_force=std::any_of(
                groups.begin(),groups.end(),[](const auto& statement) {
                    return statement.kind=="force";
                });
            if (has_active_force) {
                groups.erase(std::remove_if(
                    groups.begin(),groups.end(),[](const auto& statement) {
                        return statement.kind!="force";
                    }),groups.end());
            }
            const IDesignBackend::DriverRecord* selected=nullptr;
            IDesignBackend::DriverRecord mapped_driver;
            std::string upstream;
            std::string ambiguity_kind;
            if (!has_active_force&&!evaluated.unresolved.empty())
                ambiguity_kind="predicate_unresolved";
            else if (groups.size()>1) ambiguity_kind="multiple_active_candidates";
            else if (groups.size()==1&&groups[0].rhs.size()>1)
                ambiguity_kind="multiple_rhs_sources";
            if (!groups.empty()) selected=representative_driver(groups[0]);
            if (ambiguity_kind.empty()&&selected&&groups[0].kind!="force"&&
                selected->dependency_role=="rhs") {
                const std::string candidate=signal_name(design,selected->src_signal);
                if (!candidate.empty()&&candidate!=current&&
                    sample_at(waveform,candidate,sample.active_time).ok)
                    upstream=candidate;
            }

            const int direction=design.signal_direction(index);
            // A top/parent output may be a flattened alias of one child output.
            // Cross only a unique deepest continuous boundary; procedural/NBA
            // assignments retain their own frozen termination semantics.
            if (direction==2&&previous.empty()&&evaluated.unresolved.empty()&&
                groups.size()==1&&groups[0].kind=="cont_assign") {
                std::vector<int> child_outputs=ports_connected_to(design,index,2);
                child_outputs.erase(std::remove_if(
                    child_outputs.begin(),child_outputs.end(),[&](int port) {
                        const std::string candidate=signal_name(design,port);
                        return port==index||
                            hierarchy_depth(candidate)<=hierarchy_depth(current);
                    }),child_outputs.end());
                if (!child_outputs.empty()) {
                    const size_t deepest=hierarchy_depth(signal_name(
                        design,*std::max_element(child_outputs.begin(),child_outputs.end(),
                            [&](int left,int right) {
                                return hierarchy_depth(signal_name(design,left))<
                                    hierarchy_depth(signal_name(design,right));
                            })));
                    std::vector<int> deepest_outputs;
                    std::copy_if(child_outputs.begin(),child_outputs.end(),
                        std::back_inserter(deepest_outputs),[&](int port) {
                            return hierarchy_depth(signal_name(design,port))==deepest;
                        });
                    if (deepest_outputs.size()==1) {
                        const std::string candidate=signal_name(
                            design,deepest_outputs.front());
                        if (sample_at(waveform,candidate,sample.active_time).ok) {
                            ambiguity_kind.clear();
                            upstream=candidate;
                            selected=nullptr;
                        }
                    }
                }
            }
            const int previous_index=previous.empty()
                ?-1:design.resolve(previous.c_str());
            // Returning from a ref/inout port already crossed the child
            // boundary.  Follow the parent driver's RHS instead of reflecting
            // through another child output connected to the same alias net.
            const bool arrived_from_ref=previous_index>=0&&
                design.signal_direction(previous_index)==3;
            if (direction==0&&!arrived_from_ref&&
                evaluated.unresolved.empty()&&groups.size()==1) {
                std::vector<int> output_ports=ports_connected_to(design,index,2);
                output_ports.erase(std::remove_if(output_ports.begin(),output_ports.end(),
                    [&](int port) {
                        const std::string candidate=signal_name(design,port);
                        return hierarchy_depth(candidate)<=hierarchy_depth(current);
                    }),output_ports.end());
                const std::string output_candidate=output_ports.size()==1
                    ?signal_name(design,output_ports.front()):std::string();
                if (!output_candidate.empty()&&
                    sample_at(waveform,output_candidate,sample.active_time).ok) {
                    ambiguity_kind.clear();
                    upstream=output_candidate;
                    selected=nullptr;
                }
            }

            if (ambiguity_kind.empty()&&direction==2&&!previous.empty()&&
                upstream==previous) {
                const int parent_index=design.resolve(previous.c_str());
                auto parent_evaluated=active_statement_groups(
                    drivers_for(design,parent_index),design,waveform,
                    sample.active_time,current_time);
                apply_unique_nba_priority(parent_evaluated);
                if (parent_evaluated.unresolved.empty()&&
                    parent_evaluated.active.size()==1) {
                    const std::string instance_scope=signal_scope(current);
                    StatementGroup mapped_group=parent_evaluated.active[0];
                    if (mapped_group.rhs.empty()) {
                        groups={std::move(mapped_group)};
                        selected=representative_driver(groups[0]);
                        upstream.clear();
                    } else {
                        std::map<int,int> mapped_sources;
                        bool mapping_complete=true;
                        for (const auto& flattened : mapped_group.rhs) {
                            std::vector<int> input_ports=ports_connected_to(
                                design,flattened.src_signal,1);
                            const std::vector<int> ref_ports=ports_connected_to(
                                design,flattened.src_signal,3);
                            input_ports.insert(input_ports.end(),
                                ref_ports.begin(),ref_ports.end());
                            input_ports.erase(std::remove_if(
                                input_ports.begin(),input_ports.end(),[&](int port) {
                                    const std::string port_scope=signal_scope(
                                        signal_name(design,port));
                                    // A modport member is nested below its
                                    // instance as <instance>.<port>.<member>.
                                    return port_scope!=instance_scope&&
                                        !is_scope_ancestor(instance_scope,port_scope);
                                }),input_ports.end());
                            if (input_ports.size()!=1) {
                                mapping_complete=false;
                                break;
                            }
                            mapped_sources[flattened.src_signal]=input_ports.front();
                        }
                        if (mapping_complete) {
                            for (auto& record : mapped_group.records) {
                                if (record.dependency_role!="rhs") continue;
                                const auto found=mapped_sources.find(record.src_signal);
                                if (found!=mapped_sources.end())
                                    record.src_signal=found->second;
                            }
                            for (auto& rhs : mapped_group.rhs)
                                rhs.src_signal=mapped_sources.at(rhs.src_signal);
                            groups={std::move(mapped_group)};
                            selected=representative_driver(groups[0]);
                            if (groups[0].rhs.size()>1) {
                                upstream.clear();
                                ambiguity_kind="multiple_rhs_sources";
                            } else {
                                upstream=signal_name(
                                    design,groups[0].rhs[0].src_signal);
                            }
                        }
                    }
                }
            }

            if (ambiguity_kind.empty()&&direction==1&&selected) {
                std::vector<int> ancestor_ports=ports_connected_to(
                    design,selected->src_signal,1);
                const std::string current_scope=signal_scope(current);
                ancestor_ports.erase(std::remove_if(
                    ancestor_ports.begin(),ancestor_ports.end(),[&](int port) {
                        if (port==index) return true;
                        const std::string candidate=signal_name(design,port);
                        return !is_scope_ancestor(signal_scope(candidate),current_scope);
                    }),ancestor_ports.end());
                if (!ancestor_ports.empty()) {
                    const size_t nearest_depth=hierarchy_depth(signal_name(design,
                        *std::max_element(ancestor_ports.begin(),ancestor_ports.end(),
                            [&](int left,int right) {
                                return hierarchy_depth(signal_name(design,left))<
                                    hierarchy_depth(signal_name(design,right));
                            })));
                    std::vector<int> nearest_ports;
                    std::copy_if(ancestor_ports.begin(),ancestor_ports.end(),
                        std::back_inserter(nearest_ports),[&](int port) {
                            return hierarchy_depth(signal_name(design,port))==nearest_depth;
                        });
                    if (nearest_ports.size()==1) {
                        const std::string candidate=signal_name(
                            design,nearest_ports.front());
                        if (sample_at(waveform,candidate,sample.active_time).ok) {
                            mapped_driver=*selected;
                            mapped_driver.src_signal=nearest_ports.front();
                            selected=&mapped_driver;
                            upstream=candidate;
                        }
                    }
                }
            }

            // FST records value changes, while an NBA executes on every
            // matching sensitivity event.  Refine a sequential hop from its
            // DesignDB event dependency, and propagate that causal time across
            // one direct continuous alias so the downstream hop reports the
            // same assignment event as the original active-trace semantics.
            if (ambiguity_kind.empty()&&groups.size()==1&&
                groups[0].kind=="nba"&&groups[0].has_event_time) {
                sample.query_time=groups[0].event_time;
                sample.active_time=groups[0].event_time;
            } else if (ambiguity_kind.empty()&&groups.size()==1&&
                       groups[0].kind=="cont_assign"&&!upstream.empty()) {
                std::set<std::string> causal_visited;
                uint64_t causal_event_time=0;
                if (causal_event_time_through_unique_chain(
                        design,waveform,upstream,current_time,max_nodes,
                        causal_visited,causal_event_time)) {
                    sample.active_time=causal_event_time;
                }
            }
            const bool stops_before_current_hop=
                ambiguity_kind=="multiple_active_candidates";
            // The original resolver checks assignment-handle multiplicity
            // before constructing a node.  Other ambiguity kinds are found
            // only after the current node exists and therefore keep the hop.
            if (!stops_before_current_hop) {
                hops.push_back(trace_hop(depth,current,sample,
                    depth==0?"root":"driver",selected,design,index,waveform,
                    unit,format));
            }

            if (!ambiguity_kind.empty()) {
                const auto& evidence_groups=ambiguity_kind=="predicate_unresolved"
                    ?evaluated.unresolved:groups;
                const size_t ambiguity_hop_index=stops_before_current_hop
                    ?hops.size():hops.size()-1;
                ambiguity=ambiguity_evidence(ambiguity_kind,current,
                    sample.active_time,ambiguity_hop_index,evidence_groups,
                    max_trace_signals,design,waveform,unit,format);
                if (ambiguity_kind=="predicate_unresolved") {
                    ambiguity["analysis_complete"]=false;
                    ambiguity_incomplete=true;
                } else {
                    ambiguity_limited=!ambiguity["analysis_complete"].get<bool>();
                }
                termination="ambiguous"; detail=ambiguity_kind; break;
            }

            if (groups.size()==1&&groups[0].kind=="force") {
                termination="force"; detail="force"; break;
            }

            if (upstream.empty()) {
                if (drivers.empty()&&(design.signal_direction(index)==1||
                                      design.signal_direction(index)==3)) {
                    std::vector<IDesignBackend::PortConnection> connections;
                    design.port_connections(index,connections);
                    const int direction=design.signal_direction(index);
                    const size_t current_depth=hierarchy_depth(current);
                    for (const auto& connection : connections) {
                        const int other=connection.port_signal==index
                            ?connection.connected_signal:connection.port_signal;
                        const std::string candidate=signal_name(design,other);
                        if (direction==1&&hierarchy_depth(candidate)>=current_depth)
                            continue;
                        if (!candidate.empty()&&candidate!=current&&
                            sample_at(waveform,candidate,sample.active_time).ok) {
                            upstream=candidate;
                            break;
                        }
                    }
                    if (upstream.empty()) termination="primary_input";
                }
                if (upstream.empty()&&termination!="primary_input") {
                    const bool has_assignment=std::any_of(
                        evaluated.active.begin(),evaluated.active.end(),
                        [](const auto& statement){
                            return is_assignment_kind(statement.kind);
                        });
                    const bool has_control=std::any_of(drivers.begin(),drivers.end(),
                        [](const auto& driver){return driver.dependency_role=="control";});
                    termination=drivers.empty()?"unresolved":
                        (has_assignment?"assignment":
                            (has_control?"control_only":"assignment"));
                }
            }
            if (upstream.empty()) {
                detail=termination=="assignment"
                    ?"constant_or_no_rhs_signal":termination;
                break;
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
            previous=current;
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
        std::set<std::string> explored_states;
        size_t nodes=0,chain_serial=1;
        bool global_node_limit=false,global_time_limit=false;
        while (!pending.empty()) {
            State state=std::move(pending.back());
            pending.pop_back();
            const int index=design.resolve(state.signal.c_str());
            const Sample sample=sample_at(waveform,state.signal,state.time);
            if (index<0||!sample.ok) continue;
            const uint64_t onset=x_onset_time(waveform,sample);

            if (state.depth>max_depth) {
                chains.push_back(finish_chain(
                    state,sample,onset,"limit","max_depth",false,false));
                continue;
            }
            if (!explored_states.insert(x_origin_semantic_state_key(
                    state.hops,state.relation,state.signal,onset)).second) {
                continue;
            }
            if (global_node_limit||global_time_limit) {
                const std::string detail=global_node_limit
                    ?"max_nodes":"max_time_steps";
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

            auto all_drivers=drivers_for(design,index);
            annotate_output_instance_identities(design,index,all_drivers);
            expand_expression_temporaries(design,waveform,all_drivers);
            auto evaluated=active_statement_groups(
                all_drivers,design,waveform,sample.active_time);
            apply_unique_nba_priority(evaluated);
            const auto force_statement=std::find_if(
                evaluated.active.begin(),evaluated.active.end(),
                [](const auto& statement) { return statement.kind=="force"; });
            if (force_statement!=evaluated.active.end()) {
                const auto* driver=representative_driver(*force_statement);
                if (driver&&!state.hops.empty()) {
                    state.hops.back()["file"]=driver->file;
                    state.hops.back()["line"]=std::max(0,driver->line);
                }
                Json chain=finish_chain(state,sample,onset,"origin_found",
                    "force_x",state.complete,false);
                chain["origin"]={{"signal",state.signal},
                    {"x_onset_time",waveform.format_time(onset,unit)},
                    {"kind","force"},{"reason","force_x"},
                    {"evidence_status","proven"},
                    {"file",driver?driver->file:""},
                    {"line",driver?std::max(0,driver->line):0}};
                chains.push_back(std::move(chain));
                continue;
            }
            const bool opaque_unresolved=std::any_of(
                evaluated.unresolved.begin(),evaluated.unresolved.end(),
                [](const auto& statement) {
                    return !statement.predicate_waveform_unknown;
                });
            if (opaque_unresolved) {
                limitations.push_back(
                    "activation predicate unresolved at "+state.signal);
                chains.push_back(finish_chain(state,sample,onset,"unresolved",
                    "predicate_unresolved",false,false));
                continue;
            }
            // A predicate that was fully resolved from the current FST but
            // evaluated to X/Z is itself causal evidence for X-origin.  Add
            // only those statically published statement dependencies; an
            // opaque unresolved predicate was rejected above.
            std::vector<IDesignBackend::DriverRecord> active_drivers;
            for (const auto& statement : evaluated.active) {
                active_drivers.insert(active_drivers.end(),
                    statement.records.begin(),statement.records.end());
            }
            for (const auto& statement : evaluated.unresolved) {
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
            std::vector<Source> sources,loop_sources;
            for (const auto& driver : dependencies) {
                const std::string candidate=signal_name(design,driver.src_signal);
                if (candidate.empty()) continue;
                const Sample upstream=sample_at(waveform,candidate,state.time);
                if (!upstream.ok||!has_x(upstream.bits)) continue;
                const uint64_t upstream_onset=x_onset_time(waveform,upstream);
                Source source{candidate,driver.dependency_role,
                              upstream_onset,driver};
                if (state.visited.count(visit_key(candidate,upstream_onset)))
                    loop_sources.push_back(std::move(source));
                else
                    sources.push_back(std::move(source));
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
                    IDesignBackend::DriverRecord relation;
                    relation.src_signal=other;
                    relation.kind=port.kind;
                    relation.dependency_role="port";
                    Source source{candidate,"port",upstream_onset,relation};
                    const bool returns_to_parent=state.hops.size()>=2&&
                        state.hops[state.hops.size()-2].value(
                            "signal",std::string())==candidate;
                    if (state.visited.count(
                            visit_key(candidate,upstream_onset))&&
                        !returns_to_parent) {
                        loop_sources.push_back(std::move(source));
                    } else if (!returns_to_parent) {
                        sources.push_back(std::move(source));
                    }
                }
            }

            for (const auto& source : loop_sources) {
                State loop=state;
                loop.signal=source.signal;
                loop.relation=source.relation;
                loop.incoming=source.driver;
                loop.has_incoming=true;
                loop.depth=state.depth+1;
                loop.time=source.onset;
                const Sample upstream=sample_at(
                    waveform,source.signal,state.time);
                Json chain=finish_chain(loop,upstream,source.onset,
                    "loop_detected","loop_detected",state.complete,false);
                chain["_semantic_tail_relation"]=source.relation;
                chains.push_back(std::move(chain));
            }

            if (sources.empty()) {
                if (!loop_sources.empty()) continue;
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

        // Module/interface port hops are observable evidence but transparent
        // to semantic branch identity.  Coalesce their physical variants
        // before applying max_chains so aliases cannot consume branch budget.
        Json semantic_chains=Json::array();
        std::map<std::string,size_t> semantic_indices;
        for (const auto& chain : chains) {
            const std::string key=x_origin_semantic_chain_key(chain);
            const auto [found,inserted]=semantic_indices.emplace(
                key,semantic_chains.size());
            if (inserted) {
                semantic_chains.push_back(chain);
            } else if (!semantic_chains[found->second].value("complete",false)&&
                       chain.value("complete",false)) {
                semantic_chains[found->second]=chain;
            }
        }
        chains=std::move(semantic_chains);

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

        for (auto& chain : chains) chain.erase("_semantic_tail_relation");

        Json depth_frontiers=Json::array();
        Json suggested=Json::array();
        size_t completed_count=0,limited_count=0,unresolved_count=0;
        size_t loop_count=0;
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
            if (chain.value("status","")=="loop_detected") ++loop_count;
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
            ?(completed_count?"partial":"pending")
            :limited_count?(completed_count?"partial":"limit")
            :origin_count?"origin_found"
            :loop_count?"loop_detected":"x_not_observable_upstream";
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
