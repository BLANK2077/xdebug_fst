#pragma once

#include "api/json_types.h"

namespace xdebug_fst {

struct ContractResult {
    bool ok = true;
    Json error = nullptr;
};

ContractResult validate_public_request(const Json& request);
ContractResult validate_public_response(const std::string& action,
                                        const Json& response);

}  // namespace xdebug_fst
