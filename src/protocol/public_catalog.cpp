#include "protocol/public_catalog.h"

#include "protocol/public_action_registry.h"

#include <algorithm>
#include <cctype>
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

}  // namespace xdebug_fst
