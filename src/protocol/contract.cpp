#include "protocol/contract.h"

#include "core/diagnostic_error.h"
#include "core/schema/runtime_schema_validator.h"
#include "protocol/public_action_registry.h"

#include <algorithm>
#include <string>
#include <set>
#include <utility>
#include <vector>

namespace xdebug_fst {
namespace {

size_t edit_distance(const std::string& lhs, const std::string& rhs) {
    std::vector<size_t> previous(rhs.size() + 1);
    std::vector<size_t> current(rhs.size() + 1);
    for (size_t column = 0; column <= rhs.size(); ++column) previous[column] = column;
    for (size_t row = 1; row <= lhs.size(); ++row) {
        current[0] = row;
        for (size_t column = 1; column <= rhs.size(); ++column) {
            const size_t substitution = previous[column - 1] +
                (lhs[row - 1] == rhs[column - 1] ? 0 : 1);
            current[column] = std::min(
                std::min(previous[column] + 1, current[column - 1] + 1), substitution);
        }
        previous.swap(current);
    }
    return previous[rhs.size()];
}

Json action_suggestions(const std::string& action) {
    std::vector<std::pair<size_t, std::string>> ranked;
    for (const auto& spec : PublicActionRegistry::instance().list()) {
        size_t score = edit_distance(action, spec.name);
        const size_t dot = action.find('.');
        if (dot != std::string::npos &&
            spec.name.compare(0, dot + 1, action, 0, dot + 1) == 0) {
            score = score > 2 ? score - 2 : 0;
        }
        ranked.emplace_back(score, spec.name);
    }
    std::sort(ranked.begin(), ranked.end());
    Json result = Json::array();
    for (size_t index = 0; index < ranked.size() && index < 3; ++index) {
        result.push_back(ranked[index].second);
    }
    return result;
}

std::string public_type_name(const Json& value) {
    if (value.is_number()) return "number";
    return value.type_name();
}

}  // namespace

ContractResult validate_public_request(const Json& request) {
    ContractResult result;
    if (!request.is_object()) {
        result.ok = false;
        result.error = xdebug_core::DiagnosticErrorBuilder::internal(
            "INVALID_REQUEST", "request must be a JSON object").to_json();
        return result;
    }
    if (!request.contains("action") || !request["action"].is_string() ||
        request["action"].get<std::string>().empty()) {
        result.ok = false;
        result.error = xdebug_core::DiagnosticErrorBuilder::internal(
            "INVALID_REQUEST", "action is required").to_json();
        return result;
    }
    const std::string action = request["action"].get<std::string>();
    if (!request.contains("api_version") || !request["api_version"].is_string() ||
        request["api_version"].get<std::string>() != "xdebug.v1") {
        result.ok = false;
        result.error = xdebug_core::DiagnosticErrorBuilder::internal(
            "UNSUPPORTED_API_VERSION", "expected xdebug.v1").to_json();
        return result;
    }
    if (!PublicActionRegistry::instance().find(action)) {
        result.ok = false;
        result.error = xdebug_core::DiagnosticErrorBuilder::handler(
            "UNKNOWN_ACTION", "unknown action: " + action)
            .invalid_arg("action")
            .received(action)
            .available_values(action_suggestions(action))
            .to_json();
        return result;
    }
    static const std::set<std::string> allowed_fields = {
        "api_version", "request_id", "action", "target", "args", "limits"
    };
    for (auto field = request.begin(); field != request.end(); ++field) {
        if (allowed_fields.count(field.key()) != 0) continue;
        result.ok = false;
        result.error = xdebug_core::DiagnosticErrorBuilder::schema(
            "INVALID_REQUEST", "public request contains unknown field: " + field.key())
            .invalid_arg(field.key())
            .expected("one of api_version, request_id, action, target, args, limits")
            .received(field.value())
            .received_type(public_type_name(field.value()))
            .to_json();
        return result;
    }

    const auto validation = xdebug_core::RuntimeSchemaValidator().validate_request(
        action, nlohmann::ordered_json(request));
    if (!validation.ok) {
        result.ok = false;
        result.error = validation.error;
    }
    return result;
}

ContractResult validate_public_response(const std::string& action,
                                        const Json& response) {
    ContractResult result;
    xdebug_core::RuntimeSchemaValidator validator;
    const auto validation = action == "batch"
        ? validator.validate_batch_response(nlohmann::ordered_json(response))
        : validator.validate_response(action, nlohmann::ordered_json(response));
    if (!validation.ok) {
        result.ok = false;
        result.error = validation.error;
    }
    return result;
}

}  // namespace xdebug_fst
