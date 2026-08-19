// design_query_index.h — Session-local reverse indexes for trace queries
// BSD-3-Clause License
#pragma once

#include "design_backend.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xdebug_fst {

class DesignQueryIndex {
public:
    explicit DesignQueryIndex(IDesignBackend& design);

    const std::vector<int>& ports_connected_to(int signal, int direction) const;
    const std::vector<int>& ports_in_scope(const std::string& scope,
                                           int direction) const;
    const std::vector<int>& connected_signals(int signal) const;
    bool last_materialized_selector(const std::string& base, int64_t& last) const;
    size_t estimated_bytes() const { return estimated_bytes_; }
    uint64_t signals_scanned() const { return signals_scanned_; }
    uint64_t port_records_scanned() const { return port_records_scanned_; }

private:
    using Directions = std::array<std::vector<int>, 4>;
    std::unordered_map<int, Directions> connected_ports_;
    std::unordered_map<int, std::vector<int>> connected_signals_;
    std::unordered_map<std::string, Directions> scope_ports_;
    std::unordered_map<std::string, int64_t> last_selectors_;
    size_t estimated_bytes_ = 0;
    uint64_t signals_scanned_ = 0;
    uint64_t port_records_scanned_ = 0;
};

}  // namespace xdebug_fst
