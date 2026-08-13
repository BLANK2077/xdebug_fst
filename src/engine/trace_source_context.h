#pragma once

#include "api/json_types.h"

#include <string>

namespace xdebug_fst {

// Read a deterministic source window for a location reported by the design
// database.  The response keeps the database's original file spelling; path
// resolution is used only to read source text from the current session.
Json trace_source_context(const std::string& file, int line);
Json trace_source_context(const std::string& file, int first_line,
                          int last_line);

}  // namespace xdebug_fst
