#include "protocol/public_catalog.h"

#include "protocol/public_action_registry.h"
#include "core/common/data_path.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

std::string lower_ascii(std::string value) {
    for (char& ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 128) ch = static_cast<char>(std::tolower(byte));
    }
    return value;
}

bool string_array_contains(const Json& values, const std::string& candidate) {
    if (!values.is_array()) return true;
    for (const auto& value : values) {
        if (value.is_string() && value.get<std::string>() == candidate) return true;
    }
    return false;
}

bool purpose_matches(const Json& requested, const std::vector<std::string>& actual) {
    if (!requested.is_array()) return true;
    for (const auto& value : requested) {
        if (!value.is_string()) continue;
        if (std::find(actual.begin(), actual.end(), value.get<std::string>()) != actual.end()) {
            return true;
        }
    }
    return false;
}

bool matches(const ActionSpec& spec, const Json& filter) {
    if (!filter.is_object() || filter.empty()) return true;
    if (filter.contains("category") &&
        !string_array_contains(filter["category"], spec.category)) return false;
    if (filter.contains("requires") &&
        !string_array_contains(filter["requires"], to_string(spec.resource))) return false;
    if (filter.contains("purposes") &&
        !purpose_matches(filter["purposes"], spec.purposes)) return false;
    if (filter.contains("keyword")) {
        const std::string keyword = lower_ascii(filter.value("keyword", std::string()));
        if (lower_ascii(spec.name).find(keyword) == std::string::npos &&
            lower_ascii(spec.description_en).find(keyword) == std::string::npos &&
            lower_ascii(spec.description_zh).find(keyword) == std::string::npos) return false;
    }
    return true;
}

std::string data_path(const std::string& relative) {
    return xdebug_core::installed_data_path(relative);
}

bool read_json(const std::string& relative, Json& value) {
    std::ifstream input(data_path(relative));
    if (!input.good()) return false;
    try {
        input >> value;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

CatalogResult build_actions_catalog(const Json& args) {
    const Json filter = args.value("filter", Json::object());
    const bool verbose = args.value("output", Json::object()).value("verbose", false);
    const auto all_specs = PublicActionRegistry::instance().list();

    Json actions = Json::array();
    Json modes = {{"design", Json::array()}, {"waveform", Json::array()},
                  {"combined", Json::array()}, {"builtin", Json::array()},
                  {"session", Json::array()}};
    for (const auto& spec : all_specs) {
        if (!matches(spec, filter)) continue;
        actions.push_back(verbose ? action_spec_descriptor(spec) : Json(spec.name));
        modes[spec.category].push_back(spec.name);
    }

    CatalogResult result;
    result.summary = {
        {"action_count", actions.size()},
        {"total_action_count", all_specs.size()},
        {"verbose", verbose},
        {"filtered", !filter.empty()}
    };
    result.data = {
        {"actions", actions},
        {"modes", modes},
        {"filters", filter}
    };
    return result;
}

CatalogResult build_schema_catalog(const Json& args) {
    CatalogResult result;
    const std::string action = args.value("action", std::string());
    const std::string kind = args.value("kind", std::string("request"));
    const ActionSpec* spec = PublicActionRegistry::instance().find(action);
    if (!spec) {
        result.ok = false;
        result.error = {
            {"code", "UNKNOWN_ACTION"},
            {"message", "unknown action: " + action},
            {"recoverable", true},
            {"error_layer", "handler"},
            {"invalid_arg", "args.action"},
            {"received", action}
        };
        return result;
    }
    if (kind != "request" && kind != "response") {
        result.ok = false;
        result.error = {
            {"code", "INVALID_ENUM"},
            {"message", "schema args.kind must be request or response"},
            {"recoverable", true},
            {"error_layer", "handler"},
            {"invalid_arg", "args.kind"},
            {"expected", "one of request, response"},
            {"received", kind},
            {"available_values", Json::array({"request", "response"})}
        };
        return result;
    }

    const std::string schema_path =
        kind == "request" ? spec->request_schema : spec->response_schema;
    Json schema;
    if (!read_json(schema_path, schema)) {
        result.ok = false;
        result.error = {
            {"code", "ACTION_SCHEMA_NOT_FOUND"},
            {"message", "schema not found for " + action + " " + kind},
            {"recoverable", true},
            {"error_layer", "handler"}
        };
        return result;
    }

    Json examples = Json::array();
    const auto& paths = kind == "request" ? spec->request_examples : spec->response_examples;
    for (const auto& path : paths) {
        Json value;
        if (read_json(path, value)) examples.push_back({{"path", path}, {"value", value}});
    }
    result.summary = {{"action", action}, {"kind", kind}};
    result.data = {
        {"schema", schema},
        {"schema_path", schema_path},
        {"examples", examples},
        {"constraints", schema.value("x-agent", Json::object()).value(
            "constraints", Json::array())}
    };
    return result;
}

}  // namespace xdebug_fst
