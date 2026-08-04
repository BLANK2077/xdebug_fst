// clock_sampling.cpp — Clock-edge sampling utilities (BSD-3-Clause)

#include "clock_sampling.h"

#include <algorithm>

namespace xdebug_fst {

PointValues read_point_values(const IWaveformBackend& wf, uint32_t signal_ref,
                              uint32_t time_idx) {
    PointValues out;
    IWaveformBackend::SignalOffset off;
    if (!wf.signal_offset_at(signal_ref, time_idx, off)) return out;

    out.middle = wf.signal_value_str(signal_ref, off.start, 0);
    out.middle_match = off.time_match;

    // before: value at the previous data entry (if any)
    if (off.start > 0) {
        out.before = wf.signal_value_str(signal_ref, off.start - 1, 0);
        out.has_before = true;
    }
    // after: value at the next change (off.next_idx) if present
    if (off.has_next && off.next_idx < wf.time_count()) {
        IWaveformBackend::SignalOffset next;
        if (wf.signal_offset_at(signal_ref, off.next_idx, next)) {
            out.after = wf.signal_value_str(signal_ref, next.start, 0);
            out.has_after = true;
        }
    }
    return out;
}

ClockSampleScanner::ClockSampleScanner(const IWaveformBackend& wf,
                                       uint32_t clk_ref, uint32_t target_ref,
                                       bool rising, bool falling)
    : wf_(wf), clk_ref_(clk_ref), target_ref_(target_ref),
      rising_(rising), falling_(falling) {
    // Enumerate clock edges from the clock's change-time indices
    std::vector<uint32_t> idxs = wf_.time_indices_of(clk_ref_);
    std::string prev;
    for (uint32_t ti : idxs) {
        IWaveformBackend::SignalOffset off;
        if (!wf_.signal_offset_at(clk_ref_, ti, off)) continue;
        std::string cur = wf_.signal_value_str(clk_ref_, off.start, 0);
        if (!prev.empty()) {
            if ((rising_ && is_rising_edge(prev, cur)) ||
                (falling_ && is_falling_edge(prev, cur))) {
                edge_indices_.push_back(ti);
            }
        }
        prev = cur;
    }
}

size_t ClockSampleScanner::scan(uint32_t begin_time_idx, uint32_t end_time_idx,
                                std::vector<ClockSample>& out) const {
    size_t before = out.size();
    for (uint32_t ti : edge_indices_) {
        if (ti < begin_time_idx || ti > end_time_idx) continue;
        ClockSample s;
        s.time_idx = ti;
        s.time = wf_.time_at(ti);

        IWaveformBackend::SignalOffset off;
        if (!wf_.signal_offset_at(target_ref_, ti, off)) continue;
        s.middle = wf_.signal_value_str(target_ref_, off.start, 0);
        s.changed_at_edge = off.time_match;
        if (off.start > 0) {
            s.before = wf_.signal_value_str(target_ref_, off.start - 1, 0);
        }
        if (off.has_next && off.next_idx < wf_.time_count()) {
            IWaveformBackend::SignalOffset next;
            if (wf_.signal_offset_at(target_ref_, off.next_idx, next)) {
                s.after = wf_.signal_value_str(target_ref_, next.start, 0);
            }
        }
        out.push_back(std::move(s));
    }
    return out.size() - before;
}

} // namespace xdebug_fst
