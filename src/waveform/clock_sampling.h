// clock_sampling.h — Clock-edge sampling utilities (BSD-3-Clause)
//
// Port of the xdebug ClockPointSampler / ClockSampleScanner concepts onto the
// IWaveformBackend abstraction. All times are integer time-table indices.
#pragma once

#include "backend/waveform_backend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xdebug_fst {

/// Value read at a clock-aligned sample point.
struct ClockSample {
    uint32_t time_idx = 0;    // time-table index of the sample
    uint64_t time = 0;        // absolute time
    std::string before;       // value before the clock edge
    std::string middle;       // value at the clock edge (time_match)
    std::string after;        // value at the next change after the edge
    bool changed_at_edge = false;  // middle != before
};

/// Sampler that walks the clock signal's rising/falling edges and reads
/// before/middle/after values of a target signal at each edge.
///
/// Semantics (matching xdebug ClockPointSampler):
///   * edges are the change points of the clock signal that go 0→1 (rising)
///     or 1→0 (falling) — X/Z transitions are skipped
///   * middle is the target value at the exact edge time index
///   * before is the target value just before the edge (previous change)
///   * after is the target value at its next change after the edge
class ClockSampleScanner {
public:
    ClockSampleScanner(const IWaveformBackend& wf, uint32_t clk_ref,
                       uint32_t target_ref, bool rising = true,
                       bool falling = false);

    /// Scan all edges in [begin_time_idx, end_time_idx] (inclusive).
    /// Appends samples to `out`. Returns the number of edges scanned.
    size_t scan(uint32_t begin_time_idx, uint32_t end_time_idx,
                std::vector<ClockSample>& out) const;

    /// Number of edges in the full waveform.
    size_t edge_count() const { return edge_indices_.size(); }

    /// Time indices of all clock edges (sorted).
    const std::vector<uint32_t>& edge_indices() const { return edge_indices_; }

private:
    const IWaveformBackend& wf_;
    uint32_t clk_ref_;
    uint32_t target_ref_;
    bool rising_;
    bool falling_;
    std::vector<uint32_t> edge_indices_;  // clock edge time indices
};

/// Read the target value at an exact time index (middle), the previous
/// value (before) and the next change value (after).
struct PointValues {
    std::string before;
    std::string middle;
    std::string after;
    bool middle_match = false;
    bool has_before = false;
    bool has_after = false;
};

/// Read before/middle/after for a signal at a given time index.
PointValues read_point_values(const IWaveformBackend& wf, uint32_t signal_ref,
                              uint32_t time_idx);

/// True if the bit string is a rising edge (0 → 1).
inline bool is_rising_edge(const std::string& prev, const std::string& cur) {
    return !prev.empty() && !cur.empty() && prev.back() == '0' && cur.back() == '1';
}

/// True if the bit string is a falling edge (1 → 0).
inline bool is_falling_edge(const std::string& prev, const std::string& cur) {
    return !prev.empty() && !cur.empty() && prev.back() == '1' && cur.back() == '0';
}

} // namespace xdebug_fst
