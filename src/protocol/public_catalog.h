#pragma once

#include "api/json_types.h"

namespace xdebug_fst {

struct CatalogResult {
    Json summary = Json::object();
    Json data = Json::object();
};

CatalogResult build_actions_catalog(const Json& args);

}  // namespace xdebug_fst
