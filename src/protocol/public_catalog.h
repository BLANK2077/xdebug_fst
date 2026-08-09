#pragma once

#include "api/json_types.h"

namespace xdebug_fst {

struct CatalogResult {
    bool ok = true;
    Json summary = Json::object();
    Json data = Json::object();
    Json error = nullptr;
};

CatalogResult build_actions_catalog(const Json& args);
CatalogResult build_schema_catalog(const Json& args);

}  // namespace xdebug_fst
