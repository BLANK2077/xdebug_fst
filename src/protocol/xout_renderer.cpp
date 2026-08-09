#include "protocol/xout_renderer.h"

#include "protocol/text_response_builder.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {

namespace {

bool has_scalar(const Json& object, const std::string& key) {
    return object.is_object() && object.contains(key) &&
           xdebug_fst::is_xout_scalar_json(object[key]);
}

bool should_emit_scalar_key(const std::string& key, const Json& value) {
    if (key == "known" && value.is_boolean() && value.get<bool>()) return false;
    return xdebug_fst::is_xout_scalar_json(value);
}

std::string schema_type_text(const Json& property) {
    if (!property.is_object() || !property.contains("type")) return "any";
    const Json& type = property["type"];
    if (type.is_string()) return type.get<std::string>();
    if (!type.is_array()) return "any";
    std::string result;
    for (const auto& item : type) {
        if (!item.is_string()) continue;
        if (!result.empty()) result += "|";
        result += item.get<std::string>();
    }
    return result.empty() ? "any" : result;
}

std::string schema_property_notes(const Json& property) {
    if (!property.is_object()) return std::string();
    std::string notes = property.value("description", std::string());
    if (property.contains("enum") && property["enum"].is_array()) {
        std::string values;
        for (const auto& item : property["enum"]) {
            if (!item.is_string()) continue;
            if (!values.empty()) values += "|";
            values += item.get<std::string>();
        }
        if (!values.empty()) {
            if (!notes.empty()) notes += " ";
            notes += "values=" + values;
        }
    }
    if (property.contains("default") && property["default"].is_primitive()) {
        if (!notes.empty()) notes += " ";
        notes += "default=" + json_to_xout_value(property["default"]);
    }
    return notes;
}

void emit_schema_properties(TextResponseBuilder& out,
                            const std::string& section,
                            const Json& object_schema) {
    if (!object_schema.is_object()) return;
    const Json properties = object_schema.value("properties", Json::object());
    if (!properties.is_object() || properties.empty()) return;
    std::set<std::string> required;
    const Json required_json = object_schema.value("required", Json::array());
    if (required_json.is_array()) {
        for (const auto& item : required_json) {
            if (item.is_string()) required.insert(item.get<std::string>());
        }
    }
    std::vector<std::vector<std::string>> rows;
    for (auto field = properties.begin(); field != properties.end(); ++field) {
        rows.push_back({field.key(), schema_type_text(field.value()),
                        required.count(field.key()) ? "yes" : "no",
                        schema_property_notes(field.value())});
    }
    out.emit_section(section);
    out.emit_table({"name", "type", "required", "description"}, rows);
}

std::string render_schema_response(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("schema");
    const Json summary = response.value("summary", Json::object());
    const Json data = response.value("data", Json::object());
    const Json schema = data.value("schema", Json::object());
    out.emit_section("summary");
    if (summary.contains("action")) out.emit_kv("action", summary["action"]);
    if (summary.contains("kind")) out.emit_kv("kind", summary["kind"]);
    if (data.contains("schema_path")) out.emit_kv("schema_path", data["schema_path"]);
    for (const char* key : {"x-purpose", "x-how_it_works", "x-when_to_use"}) {
        if (schema.contains(key) && schema[key].is_string()) out.emit_kv(key, schema[key]);
    }
    const Json top_properties = schema.value("properties", Json::object());
    if (top_properties.is_object() && top_properties.contains("args")) {
        emit_schema_properties(out, "arguments", top_properties["args"]);
    } else {
        emit_schema_properties(out, "fields", schema);
    }
    if (top_properties.is_object() && top_properties.contains("limits")) {
        emit_schema_properties(out, "limits", top_properties["limits"]);
    }
    const Json constraints = data.value("constraints", Json::array());
    if (constraints.is_array() && !constraints.empty()) {
        out.emit_section("constraints");
        for (const auto& item : constraints) {
            if (item.is_string()) out.emit_row({item.get<std::string>()});
        }
    }
    const Json examples = data.value("examples", Json::array());
    if (examples.is_array() && !examples.empty()) {
        out.emit_section("examples");
        for (const auto& example : examples) {
            if (example.is_object() && example.contains("path") &&
                example["path"].is_string()) {
                out.emit_row({example["path"].get<std::string>()});
            }
        }
    }
    return out.str();
}

std::string render_actions_response(const Json& response) {
    TextResponseBuilder out("xdebug");
    out.emit_header("actions");
    const Json summary = response.value("summary", Json::object());
    const Json data = response.value("data", Json::object());
    out.emit_section("summary");
    for (const char* key : {"action_count", "total_action_count", "verbose", "filtered"}) {
        if (summary.contains(key)) out.emit_kv(key, summary[key]);
    }
    const bool verbose = summary.value("verbose", false);
    const Json actions = data.value("actions", Json::array());
    if (verbose && actions.is_array()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& action : actions) {
            if (!action.is_object()) continue;
            rows.push_back({action.value("name", std::string()),
                            action.value("category", std::string()),
                            action.value("requires", std::string()),
                            action.value("description_zh", std::string())});
        }
        out.emit_section("actions");
        out.emit_table({"name", "category", "requires", "description"}, rows);
    } else {
        const Json modes = data.value("modes", Json::object());
        for (const char* category : {"builtin", "session", "design", "waveform", "combined"}) {
            if (!modes.contains(category) || !modes[category].is_array() ||
                modes[category].empty()) continue;
            out.emit_section(category);
            for (const auto& action : modes[category]) {
                if (action.is_string()) out.emit_row({action.get<std::string>()});
            }
        }
    }
    return out.str();
}

std::string scalar_text(const Json& object, const std::string& key) {
    if (!has_scalar(object, key)) return std::string();
    return json_to_xout_value(object[key]);
}

void emit_summary(TextResponseBuilder& out, const Json& response) {
    if (!response.contains("summary") || !response["summary"].is_object()) return;
    out.emit_section("summary");
    for (auto it = response["summary"].begin(); it != response["summary"].end(); ++it) {
        if (should_emit_scalar_key(it.key(), it.value())) out.emit_kv(it.key(), it.value());
    }
}

void emit_warnings(TextResponseBuilder& out, const Json& response) {
    if (!response.contains("warnings") || !response["warnings"].is_array()) return;
    std::vector<std::vector<std::string>> rows;
    for (const auto& warning : response["warnings"]) {
        if (warning.is_string()) {
            rows.push_back({"warning", warning.get<std::string>()});
        } else if (warning.is_object()) {
            rows.push_back({
                scalar_text(warning, "code").empty() ? "warning" : scalar_text(warning, "code"),
                scalar_text(warning, "message").empty() ? warning.dump() : scalar_text(warning, "message")
            });
        }
    }
    if (!rows.empty()) {
        out.emit_section("warnings");
        out.emit_table({"code", "message"}, rows);
    }
}

void emit_suggestions(TextResponseBuilder& out, const Json& response) {
    if (!response.contains("suggested_next_actions") ||
        !response["suggested_next_actions"].is_array()) return;
    out.emit_section("next");
    for (const auto& item : response["suggested_next_actions"]) {
        if (item.is_string()) {
            out.emit_row({item.get<std::string>()});
        } else if (item.is_object()) {
            std::string action = scalar_text(item, "action");
            std::string reason = scalar_text(item, "reason");
            if (action.empty()) action = item.dump();
            out.emit_row({action, reason});
        }
    }
}

void emit_common_blocks(TextResponseBuilder& out, const Json& response) {
    const Json data = response.value("data", Json::object());
    if (!data.is_object() || !data.contains("common_blocks") ||
        !data["common_blocks"].is_array() || data["common_blocks"].empty()) {
        return;
    }
    out.emit_section("common_blocks");
    for (const auto& item : data["common_blocks"]) {
        if (!item.is_object()) continue;
        std::string message = scalar_text(item, "message");
        std::string file = scalar_text(item, "file");
        std::string card = scalar_text(item, "card");
        if (!message.empty()) out.emit_row({message});
        if (!file.empty()) out.emit_kv("file", file);
        if (!card.empty()) out.emit_kv("card", card);
    }
}

void render_data_value(TextResponseBuilder& out, const std::string& key,
                       const Json& value) {
    if (should_emit_scalar_key(key, value)) {
        out.emit_kv(key, value);
    } else if (value.is_array() && value.empty()) {
        out.emit_kv(key, "[empty]");
    } else if (value.is_array() && !value.empty() &&
               xdebug_fst::is_xout_scalar_json(value[0])) {
        out.emit_section(key);
        int n = std::min(20, static_cast<int>(value.size()));
        for (int i = 0; i < n; ++i) out.emit_row({json_to_xout_value(value[i])});
        if (static_cast<int>(value.size()) > n) {
            out.emit_kv("(+ " + std::to_string(value.size() - n) + " more)", "");
        }
    } else if (value.is_array() && !value.empty() && value[0].is_object()) {
        int count = static_cast<int>(value.size());
        out.emit_section(key);
        int n = std::min(20, count);
        out.emit_json_table(value, n);
        if (count > n) out.emit_kv("(+ " + std::to_string(count - n) + " more)", "");
    } else if (value.is_object()) {
        bool has_direct_fields = false;
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (should_emit_scalar_key(it.key(), it.value()) ||
                (it.value().is_array() && it.value().empty())) {
                if (!has_direct_fields) out.emit_section(key);
                if (should_emit_scalar_key(it.key(), it.value()))
                    out.emit_kv(it.key(), it.value());
                else
                    out.emit_kv(it.key(), "[empty]");
                has_direct_fields = true;
            }
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (should_emit_scalar_key(it.key(), it.value()) ||
                (it.value().is_array() && it.value().empty())) continue;
            render_data_value(out, key + "." + it.key(), it.value());
        }
    }
}

void render_generic(TextResponseBuilder& out, const Json& response) {
    emit_summary(out, response);
    const Json data = response.value("data", Json::object());
    if (data.is_object() && !data.empty()) {
        out.emit_section("data");
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it.key() == "common_blocks") continue;
            render_data_value(out, it.key(), it.value());
        }
    }
    if (response.contains("findings") && response["findings"].is_array() &&
        !response["findings"].empty()) {
        render_data_value(out, "findings", response["findings"]);
    }
}

} // namespace

std::string render_xout_response(const Json& response,
                                 const std::string& handler_xout) {
    if (response.value("ok", false) && !handler_xout.empty()) {
        std::string text = handler_xout;
        while (!text.empty() && text.back() == '\n') text.pop_back();
        text.push_back('\n');
        return text;
    }
    if (response.value("ok", false) && response.contains("text") &&
        response["text"].is_string()) {
        std::string text = response["text"].get<std::string>();
        while (!text.empty() && text.back() == '\n') text.pop_back();
        text.push_back('\n');
        return text;
    }
    if (response.value("ok", false) && response.value("action", "") == "actions") {
        return render_actions_response(response);
    }
    if (response.value("ok", false) && response.value("action", "") == "schema") {
        return render_schema_response(response);
    }

    const bool ok = response.value("ok", false);
    const std::string action =
        ok ? response.value("action", std::string("unknown")) : std::string("error");
    TextResponseBuilder out("xdebug");
    out.emit_header(action);

    if (!ok) {
        if (response.contains("action")) out.emit_kv("action", response["action"]);
        if (response.contains("error")) out.emit_error(response["error"]);
        if (response.contains("summary") && response["summary"].is_object() &&
            !response["summary"].empty()) {
            Json details = Json::object();
            const Json error = response.value("error", Json::object());
            const Json data = response.value("data", Json::object());
            for (auto it = response["summary"].begin(); it != response["summary"].end(); ++it) {
                if (it.key() == "status" || it.key() == "error_code") continue;
                if (error.is_object() && error.contains(it.key()) && error[it.key()] == it.value()) continue;
                if (data.is_object() && data.contains(it.key()) && data[it.key()] == it.value()) continue;
                details[it.key()] = it.value();
            }
            if (!details.empty()) {
                out.emit_section("failure_summary");
                for (auto it = details.begin(); it != details.end(); ++it) {
                    render_data_value(out, it.key(), it.value());
                }
            }
        }
        if (response.contains("error") && response["error"].is_object()) {
            const Json& error = response["error"];
            if (error.contains("candidates") && error["candidates"].is_array()) {
                out.emit_section("candidates");
                for (const auto& item : error["candidates"])
                    out.emit_row({json_to_xout_value(item)});
            }
            if (error.contains("suggested_actions") && error["suggested_actions"].is_array()) {
                out.emit_section("next");
                for (const auto& item : error["suggested_actions"])
                    out.emit_row({json_to_xout_value(item)});
            }
        }
        return out.str();
    }

    render_generic(out, response);
    emit_warnings(out, response);
    emit_suggestions(out, response);
    emit_common_blocks(out, response);
    return out.str();
}

std::string render_xout_response(const Json& response) {
    return render_xout_response(response, std::string());
}

std::string render_xout_transport_payload(const Json& response,
                                          const std::string& handler_xout) {
    return render_xout_response(response, handler_xout);
}

std::string render_xout_transport_payload(const Json& response) {
    return render_xout_response(response);
}

} // namespace xdebug_fst
