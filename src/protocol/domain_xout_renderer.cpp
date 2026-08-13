#include "protocol/domain_xout_renderer.h"

#include "common/env_config.h"
#include "engine/trace_source_context.h"
#include "protocol/text_response_builder.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <set>
#include <vector>

namespace xdebug_fst {
namespace {

void emit_summary(TextResponseBuilder& out, const Json& response) {
    const Json summary = response.value("summary", Json::object());
    if (!summary.is_object() || summary.empty()) return;
    out.emit_section("summary");
    for (auto item = summary.begin(); item != summary.end(); ++item) {
        if (is_xout_scalar_json(item.value())) {
            out.emit_kv(item.key(), item.value());
        } else if (item.value().is_object()) {
            for (auto field = item.value().begin();
                 field != item.value().end(); ++field) {
                if (is_xout_scalar_json(field.value()))
                    out.emit_kv(item.key() + "." + field.key(), field.value());
            }
        } else if (item.value().is_array() && !item.value().empty()) {
            out.emit_kv(item.key(), json_to_xout_value(item.value()));
        }
    }
}

std::string joined_path(const Json& path) {
    if (!path.is_array()) return json_to_xout_value(path);
    std::ostringstream text;
    for (size_t index = 0; index < path.size(); ++index) {
        if (index != 0) text << " -> ";
        text << json_to_xout_value(path[index]);
    }
    return text.str();
}

std::string sample_value(const Json& sample) {
    if (!sample.is_object()) return json_to_xout_value(sample);
    if (sample.contains("value") && !sample["value"].is_null())
        return json_to_xout_value(sample["value"]);
    return sample.value("status", std::string("unavailable"));
}

std::vector<std::string> hop_row(const Json& hop) {
    return {
        json_to_xout_value(hop.value("index", Json())),
        hop.value("chain_id", std::string()),
        hop.value("relation", std::string()),
        hop.value("signal", std::string()),
        hop.value("active_time", std::string()),
        hop.value("x_onset_time", std::string()),
        json_to_xout_value(hop.value("value", Json())),
        hop.value("x_mask", std::string()),
        hop.value("file", std::string()),
        json_to_xout_value(hop.value("line", Json())),
        joined_path(hop.value("signal_path", Json::array())),
    };
}

void emit_hops(TextResponseBuilder& out, const Json& hops) {
    if (!hops.is_array() || hops.empty()) return;
    std::vector<std::vector<std::string>> rows;
    for (const auto& hop : hops) rows.push_back(hop_row(hop));
    out.emit_section("hops");
    out.emit_table({"index", "chain_id", "relation", "signal", "active_time",
                    "x_onset_time", "value", "x_mask", "file", "line",
                    "signal_path"}, rows);
}

enum class SourceEvidenceKind { path, active_chain, x_origin };

struct SourceEvidenceGroup {
    std::string file;
    int first_line = 0;
    int last_line = 0;
    int last_seen_line = 0;
    std::set<int> active_lines;
    std::vector<Json> items;
};

std::vector<SourceEvidenceGroup> source_evidence_groups(const Json& items) {
    std::vector<SourceEvidenceGroup> groups;
    const int threshold =
        xdebug_core::xdebug_trace_source_merge_threshold_lines();
    for (const auto& item : items) {
        const std::string file = item.value("file", std::string());
        const int line = item.value("line", 0);
        if (file.empty() || file == "<unknown>" || line <= 0) continue;
        const bool merge = !groups.empty() && groups.back().file == file &&
            std::abs(line - groups.back().last_seen_line) < threshold;
        if (!merge) {
            groups.push_back({file, line, line, line, {line}, {item}});
            continue;
        }
        auto& group = groups.back();
        group.first_line = std::min(group.first_line, line);
        group.last_line = std::max(group.last_line, line);
        group.last_seen_line = line;
        group.active_lines.insert(line);
        group.items.push_back(item);
    }
    return groups;
}

std::vector<std::string> source_evidence_row(const Json& item,
                                             SourceEvidenceKind kind) {
    const std::string path = joined_path(
        item.value("signal_path", Json::array()));
    if (kind == SourceEvidenceKind::path) {
        return {json_to_xout_value(item.value("line", Json())), path};
    }
    if (kind == SourceEvidenceKind::active_chain) {
        return {item.value("chain_id", std::string()),
                json_to_xout_value(item.value("index", Json())),
                item.value("time", std::string()),
                item.value("active_time", std::string()),
                item.value("relation", std::string()),
                json_to_xout_value(item.value("line", Json())), path};
    }
    return {item.value("chain_id", std::string()),
            json_to_xout_value(item.value("index", Json())),
            item.value("x_onset_time", std::string()),
            item.value("active_time", std::string()),
            item.value("relation", std::string()),
            json_to_xout_value(item.value("line", Json())), path};
}

size_t emit_source_evidence(TextResponseBuilder& out, const Json& items,
                            SourceEvidenceKind kind) {
    size_t rendered_count = 0;
    for (const auto& group : source_evidence_groups(items)) {
        Json context = trace_source_context(
            group.file, group.first_line, group.last_line);
        if (context.empty()) continue;
        rendered_count += group.items.size();
        const int begin = context.front().value("line", group.first_line);
        const int end = context.back().value("line", group.last_line);
        std::ostringstream source;
        source << "\nsource: " << group.file << ":" << begin << "-" << end
               << "\n";
        for (const auto& row : context) {
            const int line = row.value("line", 0);
            source << (group.active_lines.count(line) ? '>' : ' ')
                   << std::setw(5) << line << " | "
                   << row.value("text", std::string()) << "\n";
        }
        out.emit_raw(source.str());

        std::vector<std::vector<std::string>> rows;
        for (const auto& item : group.items)
            rows.push_back(source_evidence_row(item, kind));
        out.emit_section("active_signals");
        if (kind == SourceEvidenceKind::path) {
            out.emit_table({"line", "signal_path"}, rows);
        } else if (kind == SourceEvidenceKind::active_chain) {
            out.emit_table({"chain", "hop", "time", "active_time",
                            "relation", "line", "signal_path"}, rows);
        } else {
            out.emit_table({"chain", "hop", "x_onset_time", "active_time",
                            "relation", "line", "signal_path"}, rows);
        }
    }
    return rendered_count;
}

void emit_ambiguity(TextResponseBuilder& out, const Json& ambiguity) {
    if (!ambiguity.is_object()) return;
    out.emit_section("ambiguity");
    for (auto item = ambiguity.begin(); item != ambiguity.end(); ++item) {
        if (is_xout_scalar_json(item.value())) out.emit_kv(item.key(), item.value());
    }
    const Json statements = ambiguity.value("statements", Json::array());
    if (!statements.empty()) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::vector<std::string>> samples;
        for (size_t statement_index = 0;
             statement_index < statements.size(); ++statement_index) {
            const Json& statement = statements[statement_index];
            rows.push_back({
                std::to_string(statement_index),
                statement.value("kind", std::string()),
                statement.value("driver", std::string()),
                statement.value("file", std::string()),
                json_to_xout_value(statement.value("line", Json())),
                json_to_xout_value(statement.value("complete", Json())),
                json_to_xout_value(statement.value("rhs_signal_count", Json())),
                json_to_xout_value(
                    statement.value("returned_rhs_signal_count", Json())),
            });
            for (const auto& sample :
                 statement.value("rhs_samples", Json::array())) {
                samples.push_back({
                    std::to_string(statement_index),
                    sample.value("signal", std::string()),
                    sample_value(sample.value("before", Json::object())),
                    sample_value(sample.value("after", Json::object())),
                    json_to_xout_value(sample.value("changed", Json())),
                });
            }
        }
        out.emit_section("statements");
        out.emit_table({"statement", "kind", "driver", "file", "line",
                        "complete", "rhs_total", "rhs_returned"}, rows);
        if (!samples.empty()) {
            out.emit_section("rhs_samples");
            out.emit_table({"statement", "signal", "before", "after",
                            "changed"}, samples);
        }
    }
}

std::set<std::string> field_names(const Json& rows,
                                  const std::string& member) {
    std::set<std::string> names;
    if (!rows.is_array()) return names;
    for (const auto& row : rows) {
        const Json fields = row.value(member, Json::object());
        if (!fields.is_object()) continue;
        for (auto item = fields.begin(); item != fields.end(); ++item)
            names.insert(item.key());
    }
    return names;
}

std::string field_value(const Json& object, const std::string& member,
                        const std::string& field) {
    const Json values = object.value(member, Json::object());
    if (!values.is_object() || !values.contains(field)) return std::string();
    return json_to_xout_value(values[field]);
}

void emit_stream_records(TextResponseBuilder& out, const std::string& section,
                         const Json& records) {
    if (!records.is_array() || records.empty()) return;
    const bool packets = records[0].contains("packet_index");
    if (!packets) {
        const auto fields = field_names(records, "fields");
        std::vector<std::string> columns{
            "cycle", "time", "transfer", "stall", "vld", "rdy", "bp",
            "sop", "eop", "beat_index"};
        for (const auto& field : fields) columns.push_back(field);
        std::vector<std::vector<std::string>> rows;
        for (const auto& record : records) {
            std::vector<std::string> row;
            for (const char* key : {"cycle", "time", "transfer", "stall",
                                    "vld", "rdy", "bp", "sop", "eop",
                                    "beat_index"})
                row.push_back(json_to_xout_value(record.value(key, Json())));
            for (const auto& field : fields)
                row.push_back(field_value(record, "fields", field));
            rows.push_back(std::move(row));
        }
        out.emit_section(section);
        out.emit_table(columns, rows);
        return;
    }

    const auto first_fields = field_names(records, "first_fields");
    const auto last_fields = field_names(records, "last_fields");
    std::vector<std::string> columns{
        "packet_index", "start_cycle", "end_cycle", "start_time", "end_time",
        "beat_count", "partial_begin", "partial_end", "preview_total",
        "preview_returned", "preview_truncated"};
    for (const auto& field : first_fields) columns.push_back("first." + field);
    for (const auto& field : last_fields) columns.push_back("last." + field);
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<std::string>> beat_rows;
    std::vector<Json> beat_payloads;
    std::set<std::string> beat_fields;
    for (const auto& packet : records) {
        const Json preview = packet.value("beat_fields_preview", Json::object());
        std::vector<std::string> row;
        for (const char* key : {"packet_index", "start_cycle", "end_cycle",
                                "start_time", "end_time", "beat_count",
                                "partial_begin", "partial_end"})
            row.push_back(json_to_xout_value(packet.value(key, Json())));
        row.push_back(json_to_xout_value(preview.value("total_count", Json())));
        row.push_back(json_to_xout_value(preview.value("returned_count", Json())));
        row.push_back(json_to_xout_value(
            preview.value("response_truncated", Json())));
        for (const auto& field : first_fields)
            row.push_back(field_value(packet, "first_fields", field));
        for (const auto& field : last_fields)
            row.push_back(field_value(packet, "last_fields", field));
        rows.push_back(std::move(row));
        // A one-beat packet already has its complete boundary values in the
        // packet row.  Repeating the same beat as both head and tail adds no
        // information; reserve packet_beats for actual multi-beat previews.
        if (packet.value("beat_count", 0u) <= 1) continue;
        for (const char* role : {"head", "tail"}) {
            for (const auto& beat : preview.value(role, Json::array())) {
                const Json fields = beat.value("fields", Json::object());
                if (fields.is_object())
                    for (auto item = fields.begin(); item != fields.end(); ++item)
                        beat_fields.insert(item.key());
                beat_rows.push_back({
                    json_to_xout_value(packet.value("packet_index", Json())),
                    role,
                    json_to_xout_value(beat.value("beat_index", Json())),
                    json_to_xout_value(beat.value("cycle", Json())),
                    beat.value("time", std::string()),
                    json_to_xout_value(fields),
                });
                beat_payloads.push_back(beat);
            }
        }
    }
    out.emit_section(section);
    out.emit_table(columns, rows);
    if (!beat_rows.empty()) {
        std::vector<std::string> beat_columns{
            "packet_index", "preview_role", "beat_index", "cycle", "time"};
        for (const auto& field : beat_fields) beat_columns.push_back(field);
        for (size_t row_index = 0; row_index < beat_rows.size(); ++row_index) {
            beat_rows[row_index].resize(5);
            for (const auto& field : beat_fields)
                beat_rows[row_index].push_back(
                    field_value(beat_payloads[row_index], "fields", field));
        }
        out.emit_section("packet_beats");
        out.emit_table(beat_columns, beat_rows);
    }
}

}  // namespace

std::string render_source_paths_xout(const std::string& action,
                                     const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header(action);
    emit_summary(out, response);
    const Json paths = response.value("data", Json::object())
                           .value("paths", Json::array());
    if (!paths.empty() &&
        emit_source_evidence(out, paths, SourceEvidenceKind::path) <
            paths.size()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& path : paths) {
            rows.push_back({joined_path(path.value("signal_path", Json::array())),
                            path.value("file", std::string()),
                            json_to_xout_value(path.value("line", Json()))});
        }
        out.emit_section("paths");
        out.emit_table({"signal_path", "file", "line"}, rows);
    }
    return out.str();
}

std::string render_active_driver_chain_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("trace.active_driver_chain");
    emit_summary(out, response);
    const Json data = response.value("data", Json::object());
    const Json hops = data.value("hops", Json::array());
    if (emit_source_evidence(out, hops, SourceEvidenceKind::active_chain) <
        hops.size())
        emit_hops(out, hops);
    emit_ambiguity(out, data.value("ambiguity_evidence", Json()));
    return out.str();
}

std::string render_x_origin_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("trace.x_origin");
    emit_summary(out, response);
    const Json data = response.value("data", Json::object());
    const Json query = data.value("query", Json::object());
    if (!query.empty()) {
        out.emit_section("query_evidence");
        for (auto item = query.begin(); item != query.end(); ++item) {
            // signal/query_time are already authoritative in summary.  Keep
            // only the value evidence here to avoid repeating the query.
            if (item.key() != "signal" && item.key() != "query_time")
                out.emit_kv(item.key(), item.value());
        }
    }
    const Json chains = data.value("chains", Json::array());
    if (!chains.empty()) {
        std::vector<std::vector<std::string>> chain_rows;
        std::vector<std::vector<std::string>> origin_rows;
        Json all_hops = Json::array();
        for (const auto& chain : chains) {
            const Json current = chain.value("current", Json::object());
            chain_rows.push_back({
                chain.value("chain_id", std::string()),
                chain.value("status", std::string()),
                chain.value("termination_detail", std::string()),
                json_to_xout_value(chain.value("complete", Json())),
                current.value("signal", std::string()),
                json_to_xout_value(current.value("value", Json())),
                current.value("x_mask", std::string()),
                current.value("x_onset_time", std::string()),
            });
            for (const auto& hop : chain.value("hops", Json::array()))
                all_hops.push_back(hop);
            const Json origin = chain.value("origin", Json());
            if (origin.is_object()) {
                origin_rows.push_back({
                    chain.value("chain_id", std::string()),
                    origin.value("signal", std::string()),
                    origin.value("kind", std::string()),
                    origin.value("reason", std::string()),
                    origin.value("evidence_status", std::string()),
                    origin.value("x_onset_time", std::string()),
                    origin.value("file", std::string()),
                    json_to_xout_value(origin.value("line", Json())),
                });
            }
        }
        out.emit_section("chains");
        out.emit_table({"chain_id", "status", "termination", "complete",
                        "current_signal", "current_value", "current_x_mask",
                        "current_x_onset"}, chain_rows);
        if (emit_source_evidence(
                out, all_hops, SourceEvidenceKind::x_origin) < all_hops.size())
            emit_hops(out, all_hops);
        if (!origin_rows.empty()) {
            out.emit_section("origins");
            out.emit_table({"chain_id", "signal", "kind", "reason",
                            "evidence", "x_onset_time", "file", "line"},
                           origin_rows);
        }
    }
    const Json limitations = data.value("limitations", Json::array());
    if (!limitations.empty()) {
        out.emit_section("limitations");
        for (const auto& limitation : limitations)
            out.emit_row({json_to_xout_value(limitation)});
    }
    return out.str();
}

std::string render_stream_xout(const std::string& action,
                               const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header(action);
    emit_summary(out, response);
    const Json data = response.value("data", Json::object());
    for (const char* key : {"rows", "transfers", "stalls", "packets",
                            "preview"}) {
        const Json records = data.value(key, Json::array());
        if (!records.empty()) emit_stream_records(out, key, records);
    }
    for (const char* key : {"row", "transfer", "stall", "packet"}) {
        const Json record = data.value(key, Json());
        if (record.is_object())
            emit_stream_records(out, key, Json::array({record}));
    }
    return out.str();
}

std::string render_scope_roots_xout(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("scope.roots");
    emit_summary(out, response);
    const Json data = response.value("data", Json::object());
    const Json roots = data.value("roots", Json::array());
    if (!roots.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& root : roots) {
            const Json design = root.contains("design") &&
                    root["design"].is_object()
                ? root["design"] : Json::object();
            const Json wave = root.contains("wave") && root["wave"].is_object()
                ? root["wave"] : Json::object();
            rows.push_back({
                root.value("path", std::string()),
                root.value("status", std::string()),
                json_to_xout_value(root.value("sources", Json::array())),
                design.value("kind", std::string()),
                design.value("def_name", std::string()),
                json_to_xout_value(design.value("traceable", Json())),
                json_to_xout_value(wave.value("type", Json())),
                json_to_xout_value(wave.value("queryable", Json())),
            });
        }
        out.emit_section("roots");
        out.emit_table({"path", "status", "sources", "design_kind",
                        "design_def", "traceable", "wave_type", "queryable"},
                       rows);
    }
    const Json limitations = data.value("limitations", Json::array());
    if (!limitations.empty()) {
        out.emit_section("limitations");
        for (const auto& limitation : limitations)
            out.emit_row({json_to_xout_value(limitation)});
    }
    return out.str();
}

}  // namespace xdebug_fst
