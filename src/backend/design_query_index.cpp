// design_query_index.cpp — Session-local reverse indexes for trace queries
// BSD-3-Clause License

#include "design_query_index.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <set>

namespace xdebug_fst {
namespace {

std::string scope_of(const std::string& signal) {
    const size_t dot = signal.rfind('.');
    return dot == std::string::npos ? std::string() : signal.substr(0, dot);
}

bool final_numeric_selector(const std::string& signal, std::string& base,
                            int64_t& value) {
    if (signal.empty() || signal.back() != ']') return false;
    const size_t open = signal.rfind('[');
    if (open == std::string::npos || open + 1 >= signal.size() - 1) return false;
    const std::string text = signal.substr(open + 1, signal.size() - open - 2);
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<int64_t>::max()))
        return false;
    base = signal.substr(0, open);
    value = static_cast<int64_t>(parsed);
    return !base.empty();
}

const std::vector<int> kEmpty;

}  // namespace

DesignQueryIndex::DesignQueryIndex(IDesignBackend& design) {
    const int count = design.signal_count();
    signals_scanned_ = count > 0 ? static_cast<uint64_t>(count) : 0;
    std::vector<IDesignBackend::PortConnection> connections;
    for (int index = 0; index < count; ++index) {
        const char* raw_name = design.signal_name(index);
        const std::string name = raw_name ? raw_name : "";
        const int direction = design.signal_direction(index);
        if (direction > 0 && direction < 4) {
            auto& ports = scope_ports_[scope_of(name)][direction];
            ports.push_back(index);
            estimated_bytes_ += sizeof(int);
        }
        std::string base;
        int64_t selector = 0;
        if (final_numeric_selector(name, base, selector)) {
            const auto found = last_selectors_.find(base);
            if (found == last_selectors_.end() || selector > found->second)
                last_selectors_[base] = selector;
        }
        if (direction <= 0 || direction >= 4) continue;
        design.port_connections(index, connections);
        port_records_scanned_ += connections.size();
        std::set<int> connected_once;
        for (const auto& connection : connections) {
            const int other = connection.port_signal == index
                ? connection.connected_signal : connection.port_signal;
            if (other >= 0 && connected_once.insert(other).second) {
                connected_ports_[other][direction].push_back(index);
                estimated_bytes_ += sizeof(int);
            }
            if (connection.port_signal >= 0 && connection.connected_signal >= 0) {
                connected_signals_[connection.port_signal].push_back(
                    connection.connected_signal);
                connected_signals_[connection.connected_signal].push_back(
                    connection.port_signal);
            }
        }
    }
    for (auto& [signal, neighbors] : connected_signals_) {
        (void)signal;
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
        estimated_bytes_ += neighbors.capacity() * sizeof(int);
    }
    for (const auto& [key, value] : scope_ports_)
        estimated_bytes_ += key.capacity() + sizeof(value);
    for (const auto& [key, value] : last_selectors_) {
        (void)value;
        estimated_bytes_ += key.capacity() + sizeof(int64_t);
    }
    estimated_bytes_ += connected_ports_.size() * sizeof(Directions);
}

const std::vector<int>& DesignQueryIndex::connected_signals(int signal) const {
    const auto found = connected_signals_.find(signal);
    return found == connected_signals_.end() ? kEmpty : found->second;
}

const std::vector<int>& DesignQueryIndex::ports_connected_to(
    int signal, int direction) const {
    const auto found = connected_ports_.find(signal);
    return found == connected_ports_.end() || direction < 0 || direction >= 4
        ? kEmpty : found->second[direction];
}

const std::vector<int>& DesignQueryIndex::ports_in_scope(
    const std::string& scope, int direction) const {
    const auto found = scope_ports_.find(scope);
    return found == scope_ports_.end() || direction < 0 || direction >= 4
        ? kEmpty : found->second[direction];
}

bool DesignQueryIndex::last_materialized_selector(const std::string& base,
                                                  int64_t& last) const {
    const auto found = last_selectors_.find(base);
    if (found == last_selectors_.end()) return false;
    last = found->second;
    return true;
}

}  // namespace xdebug_fst
