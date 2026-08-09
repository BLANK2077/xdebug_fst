// clock_sampling.cpp — Clock-edge sampling utilities (BSD-3-Clause)

#include "clock_sampling.h"

#include <algorithm>

namespace xdebug_fst {

PointValues read_point_values(const IWaveformBackend& wf, uint32_t signal_ref,
                              uint32_t time_idx) {
    PointValues out;
    IWaveformBackend::SampledValue sampled;
    if (!wf.sampled_value_at(signal_ref, time_idx,
                             IWaveformBackend::ObservationPoint::Raw,
                             sampled)) return out;

    auto render = [](const IWaveformBackend::WaveformValue& value) {
        if (value.kind == IWaveformBackend::ValueKind::Real) {
            return std::to_string(value.real);
        }
        if (value.kind == IWaveformBackend::ValueKind::Event) {
            return std::string("event");
        }
        return value.text;
    };
    out.middle = render(sampled.value);
    out.middle_match = sampled.time_match;

    if (wf.sampled_value_at(signal_ref, time_idx,
                            IWaveformBackend::ObservationPoint::Before,
                            sampled)) {
        out.before = render(sampled.value);
        out.has_before = true;
    }
    if (wf.sampled_value_at(signal_ref, time_idx,
                            IWaveformBackend::ObservationPoint::After,
                            sampled)) {
        out.after = render(sampled.value);
        out.has_after = true;
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
    uint32_t previous_ti = UINT32_MAX;
    for (uint32_t ti : idxs) {
        if (ti == previous_ti) continue;
        previous_ti = ti;
        IWaveformBackend::SampledValue sampled;
        if (!wf_.sampled_value_at(
                clk_ref_, ti, IWaveformBackend::ObservationPoint::Raw,
                sampled)) continue;
        std::string cur = sampled.value.text;
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

        PointValues point = read_point_values(wf_, target_ref_, ti);
        if (point.middle.empty()) continue;
        s.before = point.before;
        s.middle = point.middle;
        s.after = point.after;
        s.changed_at_edge = point.middle_match &&
                            point.has_before && point.before != point.after;
        out.push_back(std::move(s));
    }
    return out.size() - before;
}

} // namespace xdebug_fst
