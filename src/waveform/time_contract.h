#pragma once

#include <cstdint>
#include <string>

namespace xdebug_fst {

struct WaveformTimeScale {
    uint32_t factor = 0;
    int32_t exponent = 0;

    bool valid() const { return factor != 0; }
};

enum class TimeRenderUnit {
    Ns,
    Ps,
    Us,
    Auto,
};

bool parse_time_render_unit(const std::string& text,
                            TimeRenderUnit& unit,
                            std::string& error);

bool parse_waveform_time(const std::string& text,
                         const WaveformTimeScale& scale,
                         uint64_t waveform_max,
                         bool allow_max,
                         uint64_t& ticks,
                         std::string& error,
                         const std::string& default_unit = "ns");

std::string format_waveform_time(uint64_t ticks,
                                 const WaveformTimeScale& scale,
                                 TimeRenderUnit unit);

}  // namespace xdebug_fst
