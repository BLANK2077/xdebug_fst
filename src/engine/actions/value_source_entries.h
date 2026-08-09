// value_source_entries.h — shared value.at configuration projection (BSD-3-Clause)
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace xdebug_fst {

using ValueSourceEntry = std::pair<std::string, std::string>;  // key, signal path

bool apb_value_source_entries(const std::string& name,
                              std::vector<ValueSourceEntry>& out);
bool axi_value_source_entries(const std::string& name,
                              std::vector<ValueSourceEntry>& out);
bool stream_value_source_entries(const std::string& name,
                                 std::vector<ValueSourceEntry>& out);

}  // namespace xdebug_fst
