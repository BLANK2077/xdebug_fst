#include "waveform/time_contract.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>

namespace xdebug_fst {
namespace {

std::string trim(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string lowercase(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string normalize_unit(const std::string& source) {
    std::string unit = lowercase(trim(source));
    if (unit == "m") return "ms";
    if (unit == "u") return "us";
    if (unit == "n") return "ns";
    if (unit == "p") return "ps";
    if (unit == "f") return "fs";
    return unit;
}

bool unit_exponent(const std::string& unit, int32_t& exponent) {
    if (unit == "s") exponent = 0;
    else if (unit == "ms") exponent = -3;
    else if (unit == "us") exponent = -6;
    else if (unit == "ns") exponent = -9;
    else if (unit == "ps") exponent = -12;
    else if (unit == "fs") exponent = -15;
    else return false;
    return true;
}

long double power_of_ten(int32_t exponent) {
    return std::pow(10.0L, static_cast<long double>(exponent));
}

bool approximately_integer(long double value, long double& rounded) {
    rounded = std::round(value);
    const long double tolerance =
        std::max(1.0L, std::fabs(value)) * 1.0e-15L;
    return std::fabs(value - rounded) <= tolerance;
}

std::string format_number(long double value) {
    long double rounded = 0.0L;
    if (approximately_integer(value, rounded) &&
        rounded <= static_cast<long double>(
            std::numeric_limits<unsigned long long>::max())) {
        return std::to_string(static_cast<unsigned long long>(rounded));
    }
    std::ostringstream stream;
    stream << std::setprecision(18) << value;
    return stream.str();
}

long double ticks_in_unit(uint64_t ticks,
                          const WaveformTimeScale& scale,
                          int32_t unit_power) {
    return static_cast<long double>(ticks) *
           static_cast<long double>(scale.factor) *
           power_of_ten(scale.exponent - unit_power);
}

std::string format_in_unit(uint64_t ticks,
                           const WaveformTimeScale& scale,
                           const char* unit,
                           int32_t unit_power) {
    return format_number(ticks_in_unit(ticks, scale, unit_power)) + unit;
}

}  // namespace

bool parse_time_render_unit(const std::string& text,
                            TimeRenderUnit& unit,
                            std::string& error) {
    if (text == "ns") unit = TimeRenderUnit::Ns;
    else if (text == "ps") unit = TimeRenderUnit::Ps;
    else if (text == "us") unit = TimeRenderUnit::Us;
    else if (text == "auto") unit = TimeRenderUnit::Auto;
    else {
        error = "TIME_UNIT_INVALID: args.render_time_unit must be ns, ps, us, or auto";
        return false;
    }
    return true;
}

bool parse_waveform_time(const std::string& text,
                         const WaveformTimeScale& scale,
                         uint64_t waveform_max,
                         bool allow_max,
                         uint64_t& ticks,
                         std::string& error,
                         const std::string& default_unit) {
    const std::string source = trim(text);
    if (source.empty()) {
        error = "Invalid time: empty";
        return false;
    }
    const std::string lowered = lowercase(source);
    if (allow_max && (lowered == "max" || lowered == "inf")) {
        ticks = waveform_max;
        return true;
    }
    if (!scale.valid()) {
        error = "Invalid time '" + source +
                "': waveform timescale is unavailable";
        return false;
    }
    if (source.front() == '-') {
        error = "Invalid time '" + source +
                "': negative time is not allowed";
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const long double value = std::strtold(source.c_str(), &end);
    if (errno != 0 || end == source.c_str() || !std::isfinite(value)) {
        error = "Invalid time '" + source + "'";
        return false;
    }
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    const std::string unit = normalize_unit(*end ? end : default_unit);
    int32_t unit_power = 0;
    if (!unit_exponent(unit, unit_power)) {
        error = "Invalid time '" + source +
                "': unsupported unit, expected ms/us/ns/ps/fs";
        return false;
    }
    const long double converted =
        value * power_of_ten(unit_power - scale.exponent) /
        static_cast<long double>(scale.factor);
    if (!std::isfinite(converted) || converted < 0.0L ||
        converted > static_cast<long double>(
            std::numeric_limits<uint64_t>::max())) {
        error = "Invalid time '" + source + "': converted time is out of range";
        return false;
    }
    long double rounded = 0.0L;
    if (!approximately_integer(converted, rounded)) {
        error = "Invalid time '" + source +
                "': time is not representable in the waveform timescale";
        return false;
    }
    ticks = static_cast<uint64_t>(rounded);
    return true;
}

std::string format_waveform_time(uint64_t ticks,
                                 const WaveformTimeScale& scale,
                                 TimeRenderUnit unit) {
    if (!scale.valid()) return std::to_string(ticks);
    if (unit == TimeRenderUnit::Us) {
        return format_in_unit(ticks, scale, "us", -6);
    }
    if (unit == TimeRenderUnit::Ns) {
        return format_in_unit(ticks, scale, "ns", -9);
    }
    if (unit == TimeRenderUnit::Ps) {
        return format_in_unit(ticks, scale, "ps", -12);
    }

    const long double us = ticks_in_unit(ticks, scale, -6);
    long double rounded = 0.0L;
    if (us >= 1.0L && approximately_integer(us, rounded)) {
        return format_number(us) + "us";
    }
    const long double ns = ticks_in_unit(ticks, scale, -9);
    if (ns >= 1.0L && approximately_integer(ns, rounded)) {
        return format_number(ns) + "ns";
    }
    return format_in_unit(ticks, scale, "ps", -12);
}

}  // namespace xdebug_fst
