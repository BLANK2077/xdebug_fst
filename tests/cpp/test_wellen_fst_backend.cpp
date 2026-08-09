#include "backend/waveform_backend.h"
#include "backend/wellen_fst_backend.h"
#include "waveform/clock_sampling.h"

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
            "expected 1ns, 1ps/XZ, string/delta, real, and event FST fixtures");
    for (int index = 1; index < argc; ++index) {
        const std::string path = argv[index];
        require(path.size() >= 4 && path.substr(path.size() - 4) == ".fst",
                "FST-only test gate rejects every non-.fst waveform input");
    }

    xdebug_fst::WellenFstBackend rejected_backend;
    require(!rejected_backend.open("forbidden-waveform.vcd"),
            "production backend rejects VCD without attempting fallback");

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
    const uint32_t sampled_target = backend.find_signal("clk_out");
    require(sampled_target != 0 && backend.load_signals({sampled_target}) == 1,
            "clock-sampled target loads from the FST fixture");
    xdebug_fst::ClockSampleScanner scanner(
        backend, 1, sampled_target, true, false);
    std::vector<xdebug_fst::ClockSample> samples;
    require(scanner.edge_count() > 0 &&
                scanner.scan(0, backend.time_count() - 1, samples) > 0 &&
                samples.front().middle == samples.front().after,
            "clock sampler reads settled FST values at real rising edges");
    backend.unload_signals({1, sampled_target});
    require(!backend.is_loaded(1) && !backend.is_loaded(sampled_target),
            "batch unload releases the signal cache");
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

    const uint32_t x_signal = fine_backend.find_signal(
        "AXI_top_tb_from_compiled.dut.bram_r");
    const uint32_t z_signal = fine_backend.find_signal(
        "AXI_top_tb_from_compiled.dut.a_regex_coprocessor.genblk1."
        "a_topology.genblk1[0].genblk1[0].engine_and_station_i."
        "ch_x_out.channel_input_ready");
    const uint32_t deep_leaf = fine_backend.find_signal(
        "AXI_top_tb_from_compiled.dut.a_regex_coprocessor.genblk1."
        "a_topology.genblk1[0].genblk1[0].engine_and_station_i.anEngine."
        "anEngine.genblk3.a_cache.data_from_memory");
    require(x_signal != 0 && z_signal != 0 && deep_leaf != 0 &&
                fine_backend.load_signals({x_signal, z_signal, deep_leaf}) == 3,
            "FST wide/X/Z signals and deeply nested array leaves load");
    xdebug_fst::IWaveformBackend::SignalOffset offset;
    xdebug_fst::IWaveformBackend::WaveformValue typed;
    std::vector<xdebug_fst::IWaveformBackend::SignalChange> state_changes;
    xdebug_fst::IWaveformBackend::ScanDiagnostics state_diagnostics;
    require(fine_backend.scan_changes(
                x_signal, 0, fine_backend.time_count() - 1, 0,
                state_changes, state_diagnostics) &&
                state_diagnostics.width == 64 &&
                state_diagnostics.scan_complete,
            "wide FST signal reports complete 64-bit scan diagnostics");
    bool saw_x = false;
    for (const auto& change : state_changes) {
        saw_x = saw_x || change.value.text.find('x') != std::string::npos;
    }
    require(saw_x, "FST bit-vector preserves X across its real 64-bit width");
    require(fine_backend.signal_offset_at(z_signal, 0, offset) &&
                fine_backend.signal_typed_value_at(
                    z_signal, offset.start, 0, typed) && typed.text == "z",
            "FST bit-vector preserves a high-impedance Z state");

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

    std::vector<xdebug_fst::IWaveformBackend::WaveformValue> deltas;
    bool exact = false;
    require(string_backend.delta_values_at(
                string_signal, 0, deltas, exact) && exact &&
                deltas.size() == 2 &&
                deltas[0].text == std::string(50, ' ') &&
                deltas[1].text.find("En lång röd räv") == 0,
            "FST same-time delta elements preserve source order");
    xdebug_fst::IWaveformBackend::SampledValue sampled;
    require(string_backend.sampled_value_at(
                string_signal, 0,
                xdebug_fst::IWaveformBackend::ObservationPoint::Raw,
                sampled) && sampled.element == 1 &&
                sampled.elements_at_time == 2 &&
                sampled.value.text.find("En lång röd räv") == 0,
            "raw observation selects the settled last FST delta");
    require(!string_backend.sampled_value_at(
                string_signal, 0,
                xdebug_fst::IWaveformBackend::ObservationPoint::Before,
                sampled),
            "before observation fails closed when no prior FST value exists");
    require(string_backend.sampled_value_at(
                string_signal, 1,
                xdebug_fst::IWaveformBackend::ObservationPoint::Before,
                sampled) && sampled.value.text.find("En lång röd räv") == 0,
            "before observation selects the settled preceding timestamp");
    require(string_backend.sampled_value_at(
                string_signal, 1,
                xdebug_fst::IWaveformBackend::ObservationPoint::After,
                sampled) && sampled.value.text.find("Viel \"spaß\"") == 0,
            "after observation selects the settled current timestamp");

    std::vector<xdebug_fst::IWaveformBackend::SampledValue> batch_values;
    std::vector<bool> batch_found;
    string_backend.typed_values_at(
        {string_signal, 0}, 1,
        xdebug_fst::IWaveformBackend::ObservationPoint::After,
        batch_values, batch_found);
    require(batch_found.size() == 2 && batch_found[0] && !batch_found[1] &&
                batch_values[0].value.text.find("Viel \"spaß\"") == 0,
            "typed FST batch sampling preserves order and per-signal status");

    xdebug_fst::IWaveformBackend::SignalChange change;
    require(string_backend.signal_change_at(string_signal, 1, change) &&
                change.time_idx == 0 && change.delta == 1 &&
                change.value.text.find("En lång röd räv") == 0,
            "FST change cursor exposes ordered same-time delta ordinals");
    std::vector<xdebug_fst::IWaveformBackend::SignalChange> changes;
    xdebug_fst::IWaveformBackend::ScanDiagnostics diagnostics;
    require(string_backend.scan_changes(
                string_signal, 0, string_backend.time_count() - 1, 2,
                changes, diagnostics) && changes.size() == 2 &&
                diagnostics.total_count == 4 &&
                diagnostics.returned_count == 2 && diagnostics.truncated &&
                diagnostics.scan_complete && diagnostics.analysis_complete &&
                diagnostics.encoding ==
                    xdebug_fst::IWaveformBackend::ValueKind::String,
            "limited FST response preserves exact totals and complete analysis");
    require(string_backend.scan_changes(
                string_signal, 0, string_backend.time_count() - 1, 0,
                changes, diagnostics) && changes.size() == 4 &&
                diagnostics.scan_complete && diagnostics.analysis_complete &&
                !diagnostics.truncated,
            "unlimited FST scan proves complete analysis");

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
