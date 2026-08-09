#include "backend/waveform_backend.h"
#include "backend/wellen_fst_backend.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 6,
            "expected 1ns, 1ps, string, real, and event fixtures");

    xdebug_fst::WellenFstBackend backend;
    require(backend.open(argv[1]), "fixture opens through WellenFstBackend");
    require(backend.root_scope_count() == 1,
            "root scope enumeration excludes descendants");
    require(backend.scope_count() == 2,
            "all-scope enumeration includes the nested scope");

    const uint32_t root = backend.root_scope_at(0);
    require(root != 0, "root scope handle is valid");
    require(std::string(backend.scope_full_name(root)) == "clkdiv2n_tb",
            "root full name is stable");
    require(backend.scope_child_count(root) == 1,
            "root exposes one direct child");
    const uint32_t child = backend.scope_child_at(root, 0);
    require(child != 0, "child scope handle is valid");
    require(std::string(backend.scope_full_name(child)) ==
                "clkdiv2n_tb.t1",
            "nested full name preserves arbitrary-depth lookup components");

    require(xdebug_fst::IWaveformBackend::kInvalidSignalRef == 0,
            "zero is the single invalid C signal sentinel");
    require(backend.find_signal("clkdiv2n_tb.clk") == 1,
            "native Wellen SignalRef(0) remains C reference 1");
    require(backend.find_signal("clkdiv2n_tb.t1.clk") == 6,
            "nested signal is indexed through all-scope traversal");
    require(backend.find_signal("clk") == 0,
            "ambiguous local signal names fail closed");
    require(backend.find_signal("clk_out") == 3,
            "same-signal aliases retain an unambiguous local lookup");
    require(backend.find_signal("clkdiv2n_tb.clk_out") == 3 &&
                backend.find_signal("clkdiv2n_tb.t1.clk_out") == 3,
            "all full alias paths resolve to the shared signal");
    require(backend.find_signal("missing") == 0,
            "missing signals return the unified sentinel");

    xdebug_fst::WaveformTimeScale scale;
    require(backend.time_scale(scale) && scale.factor == 1 &&
                scale.exponent == -9,
            "FST header publishes a 1ns waveform tick");
    uint64_t ticks = 0;
    std::string error;
    require(backend.parse_time("1ns", ticks, error) && ticks == 1,
            "1ns maps to one tick in a 1ns waveform");
    require(backend.parse_time("1us", ticks, error) && ticks == 1000,
            "1us maps to one thousand ticks and cannot collapse to 1ns");
    require(backend.parse_time("5", ticks, error) && ticks == 5,
            "unitless time uses the frozen ns default");
    require(!backend.parse_time("1ps", ticks, error) &&
                error.find("not representable") != std::string::npos,
            "sub-tick physical time fails closed");
    require(!backend.parse_time("1ns trailing", ticks, error),
            "unknown time suffix is rejected");
    require(backend.parse_time("max", ticks, error, true) &&
                ticks == backend.max_time(),
            "max resolves to the waveform maximum only when enabled");
    require(backend.format_time(1000, xdebug_fst::TimeRenderUnit::Auto) ==
                "1us",
            "auto rendering selects the largest integral public unit");
    require(backend.format_time(1000, xdebug_fst::TimeRenderUnit::Ns) ==
                "1000ns",
            "explicit ns rendering is preserved");
    require(backend.format_time(1, xdebug_fst::TimeRenderUnit::Ps) ==
                "1000ps",
            "explicit ps rendering uses the real FST timescale");

    require(backend.load_signals({1}) == 1,
            "the first native signal loads through its 1-based handle");
    xdebug_fst::IWaveformBackend::SignalInfo info;
    require(backend.signal_info(1, info) && info.width == 1 &&
                info.num_changes > 0,
            "signal zero retains real width and change metadata");
    backend.unload_signals({1});
    require(!backend.is_loaded(1), "batch unload releases the signal cache");
    backend.close();
    require(!backend.is_open(), "close releases the waveform handle");

    xdebug_fst::WellenFstBackend fine_backend;
    require(fine_backend.open(argv[2]), "1ps fixture opens");
    require(fine_backend.time_scale(scale) && scale.factor == 1 &&
                scale.exponent == -12,
            "second FST header publishes a 1ps waveform tick");
    require(fine_backend.parse_time("0.5ns", ticks, error) && ticks == 500,
            "non-integral ns remains exact on a finer waveform scale");
    require(fine_backend.format_time(
                500, xdebug_fst::TimeRenderUnit::Auto) == "500ps",
            "auto rendering preserves non-integral-ns precision");
    require(fine_backend.format_time(
                500, xdebug_fst::TimeRenderUnit::Ns) == "0.5ns",
            "explicit ns rendering retains an exact decimal");

    const uint32_t x_signal = fine_backend.find_signal("top.xprop_top.a");
    require(x_signal != 0 && fine_backend.load_signals({x_signal}) == 1,
            "four-state fixture signal loads");
    xdebug_fst::IWaveformBackend::SignalOffset offset;
    require(fine_backend.signal_offset_at(x_signal, 0, offset),
            "four-state value has an initial offset");
    xdebug_fst::IWaveformBackend::WaveformValue typed;
    require(fine_backend.signal_typed_value_at(
                x_signal, offset.start, 0, typed) &&
                typed.kind == xdebug_fst::IWaveformBackend::ValueKind::BitVector &&
                typed.text == "xxxxxxxx",
            "four-state bit-vector preserves every X and its real width: '" +
                typed.text + "' kind=" +
                std::to_string(static_cast<int>(typed.kind)));

    xdebug_fst::WellenFstBackend string_backend;
    require(string_backend.open(argv[3]), "UTF-8 string fixture opens");
    const uint32_t string_signal =
        string_backend.find_signal("string_test.test_string.[1:50]");
    require(string_signal != 0 &&
                string_backend.load_signals({string_signal}) == 1,
            "string signal resolves and loads");
    require(string_backend.signal_offset_at(string_signal, 0, offset) &&
                offset.elements >= 2,
            "same-time string elements remain addressable");
    require(string_backend.signal_typed_value_at(
                string_signal, offset.start, 1, typed) &&
                typed.kind == xdebug_fst::IWaveformBackend::ValueKind::String &&
                typed.text.find("En lång röd räv") == 0,
            "UTF-8 string payload is not collapsed into raw bytes");

    xdebug_fst::WellenFstBackend real_backend;
    require(real_backend.open(argv[4]), "real-value fixture opens");
    const uint32_t real_signal = real_backend.find_signal("real_r");
    require(real_signal != 0 && real_backend.load_signals({real_signal}) == 1,
            "real signal resolves and loads");
    require(real_backend.signal_offset_at(real_signal, 0, offset) &&
                real_backend.signal_typed_value_at(
                    real_signal, offset.start, 0, typed) &&
                typed.kind == xdebug_fst::IWaveformBackend::ValueKind::Real,
            "real payload remains a typed f64");

    xdebug_fst::WellenFstBackend event_backend;
    require(event_backend.open(argv[5]), "event fixture opens");
    const uint32_t event_signal = event_backend.find_signal("event_example.event1");
    require(event_signal != 0 && event_backend.load_signals({event_signal}) == 1,
            "event signal resolves and loads");
    require(event_backend.signal_offset_at(event_signal, 0, offset) &&
                event_backend.signal_typed_value_at(
                    event_signal, offset.start, 0, typed) &&
                typed.kind == xdebug_fst::IWaveformBackend::ValueKind::Event &&
                typed.text.empty(),
            "event remains distinct from a missing or X value");

    std::cout << "WellenFstBackend hierarchy/sentinel tests passed\n";
    return 0;
}
