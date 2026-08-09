#include "core/schema/runtime_schema_validator.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>

namespace {

using Json = nlohmann::ordered_json;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "runtime schema validator test failed: " << message << '\n';
        std::exit(1);
    }
}

Json read_json(const std::string& path) {
    std::ifstream input(path);
    require(input.good(), "cannot open " + path);
    Json value;
    input >> value;
    return value;
}

}  // namespace

int main() {
    xdebug_core::RuntimeSchemaValidator validator;

    Json valid = {
        {"api_version", "xdebug.v1"},
        {"action", "actions"},
        {"args", {{"output", {{"verbose", false}}}}}
    };
    require(validator.validate_request("actions", valid).ok,
            "valid actions request was rejected");

    Json unknown = valid;
    unknown["unexpected"] = true;
    auto unknown_result = validator.validate_request("actions", unknown);
    require(!unknown_result.ok, "unknown top-level field was accepted");
    require(unknown_result.error.value("error_layer", "") == "schema",
            "unknown field error did not identify schema layer");

    Json wrong_type = valid;
    wrong_type["args"]["output"]["verbose"] = "yes";
    require(!validator.validate_request("actions", wrong_type).ok,
            "wrong field type was accepted");

    Json missing = {{"action", "actions"}};
    require(!validator.validate_request("actions", missing).ok,
            "missing api_version was accepted");

    const std::string baseline =
        std::string(XDEBUG_FST_SOURCE_DIR) +
        "/compat/xdebug-v1/catalog.response.json";
    require(validator.validate_response("actions", read_json(baseline)).ok,
            "frozen original actions response did not validate");

    const std::filesystem::path examples =
        std::filesystem::path(XDEBUG_FST_SOURCE_DIR) /
        "compat/xdebug-v1/examples";
    size_t request_count = 0;
    size_t response_count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(examples)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        const Json example = read_json(entry.path().string());
        require(example.is_object() && example.contains("action") &&
                    example["action"].is_string(),
                "example lacks action: " + entry.path().string());
        const std::string action = example["action"].get<std::string>();
        if (entry.path().parent_path().filename() == "requests") {
            const auto validation = validator.validate_request(action, example);
            require(validation.ok,
                    "request example failed validation: " + entry.path().string() +
                    ": " + validation.message);
            ++request_count;
        } else if (entry.path().parent_path().filename() == "responses") {
            const auto validation = validator.validate_response(action, example);
            require(validation.ok,
                    "response example failed validation: " + entry.path().string() +
                    ": " + validation.message);
            ++response_count;
        }
    }
    require(request_count == 105, "expected 105 frozen request examples");
    require(response_count == 110, "expected 110 frozen response examples");
    return 0;
}
