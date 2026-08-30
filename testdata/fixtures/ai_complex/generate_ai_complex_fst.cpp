// Deterministic direct-FST generator for the ai_complex semantic fixture.
// It intentionally preserves four-state values and same-time clock/NBA facts;
// no VCD/JSON conversion or waveform export is involved.
// BSD-3-Clause License

#include "fstcpp/fstcpp_writer.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Handle = fst::Handle;
using Change = std::pair<Handle, std::string>;
using Timeline = std::map<std::uint64_t, std::vector<Change>>;

Handle make_logic(fst::Writer& writer, const char* name,
                  std::uint32_t width) {
    return writer.createVar(
        fst::Hierarchy::VarType::SV_LOGIC,
        fst::Hierarchy::VarDirection::IMPLICIT,
        width,
        name,
        0);
}

void emit_logic(fst::Writer& writer, Handle handle,
                const std::string& bits) {
    if (bits.empty() || bits.size() > 64) {
        throw std::runtime_error("logic value width must be in [1, 64]");
    }
    std::uint64_t planes[2] = {0, 0};
    bool has_four_state_digit = false;
    for (std::size_t index = 0; index < bits.size(); ++index) {
        const char digit = bits[index];
        const std::uint64_t bit = std::uint64_t{1}
                                  << (bits.size() - index - 1);
        switch (digit) {
        case '0':
            break;
        case '1':
            planes[0] |= bit;
            break;
        case 'x':
        case 'X':
            planes[1] |= bit;
            has_four_state_digit = true;
            break;
        case 'z':
        case 'Z':
            planes[0] |= bit;
            planes[1] |= bit;
            has_four_state_digit = true;
            break;
        default:
            throw std::runtime_error("invalid four-state digit");
        }
    }
    if (has_four_state_digit) {
        writer.emitValueChange(handle, planes, fst::EncodingType::VERILOG);
    } else {
        writer.emitValueChange(handle, planes[0]);
    }
}

void add(Timeline& timeline, std::uint64_t time_ps, Handle handle,
         const char* bits) {
    timeline[time_ps].emplace_back(handle, bits);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: generate-ai-complex-fst OUTPUT.fst\n";
        return 2;
    }

    fst::Writer writer(argv[1]);
    writer.setWriterPackType(fst::WriterPackType::LZ4);
    writer.setTimecale(-12);  // one emitted tick is 1ps
    writer.setWriter("xdebug-fst deterministic ai_complex fixture v1");
    writer.setDate("Sun Aug 30 00:00:00 2026\n");
    writer.setScope(fst::Hierarchy::ScopeType::VCD_MODULE,
                    "ai_complex_top", "ai_complex_top");

    const Handle clk = make_logic(writer, "clk", 1);
    const Handle rst_n = make_logic(writer, "rst_n", 1);
    const Handle sig_a = make_logic(writer, "sig_a", 8);
    const Handle sig_b = make_logic(writer, "sig_b", 8);
    const Handle xz_bus = make_logic(writer, "xz_bus", 8);
    const Handle mixed_xz_bus = make_logic(writer, "mixed_xz_bus", 8);
    const Handle stable_sig = make_logic(writer, "stable_sig", 1);
    const Handle stuck_sig = make_logic(writer, "stuck_sig", 1);
    const Handle glitch_sig = make_logic(writer, "glitch_sig", 1);
    const Handle counter_inc = make_logic(writer, "counter_inc", 8);
    const Handle counter_nonmono = make_logic(writer, "counter_nonmono", 8);
    const Handle hs_valid = make_logic(writer, "hs_valid", 1);
    const Handle hs_ready = make_logic(writer, "hs_ready", 1);
    const Handle hs_data = make_logic(writer, "hs_data", 8);
    const Handle event_vld = make_logic(writer, "event_vld", 1);
    const Handle event_rdy = make_logic(writer, "event_rdy", 1);
    const Handle event_race = make_logic(writer, "event_race", 1);
    const Handle event_payload = make_logic(writer, "event_payload", 8);
    const Handle paddr = make_logic(writer, "paddr", 16);
    const Handle pwdata = make_logic(writer, "pwdata", 32);
    const Handle prdata = make_logic(writer, "prdata", 32);
    const Handle pwrite = make_logic(writer, "pwrite", 1);
    const Handle penable = make_logic(writer, "penable", 1);
    const Handle psel = make_logic(writer, "psel", 1);
    writer.upscope();

    Timeline timeline;
    add(timeline, 0, clk, "0");
    add(timeline, 0, rst_n, "0");
    add(timeline, 0, sig_a, "00000000");
    add(timeline, 0, sig_b, "00000000");
    add(timeline, 0, xz_bus, "00000000");
    add(timeline, 0, mixed_xz_bus, "00000000");
    add(timeline, 0, stable_sig, "1");
    add(timeline, 0, stuck_sig, "1");
    add(timeline, 0, glitch_sig, "0");
    add(timeline, 0, counter_inc, "00000000");
    add(timeline, 0, counter_nonmono, "00000101");
    add(timeline, 0, hs_valid, "0");
    add(timeline, 0, hs_ready, "0");
    add(timeline, 0, hs_data, "00000000");
    add(timeline, 0, event_vld, "0");
    add(timeline, 0, event_rdy, "0");
    add(timeline, 0, event_race, "0");
    add(timeline, 0, event_payload, "00000000");
    add(timeline, 0, paddr, "0000000000000000");
    add(timeline, 0, pwdata, "00000000000000000000000000000000");
    add(timeline, 0, prdata, "00000000000000000000000000000000");
    add(timeline, 0, pwrite, "0");
    add(timeline, 0, penable, "0");
    add(timeline, 0, psel, "0");

    bool clock_value = false;
    for (std::uint64_t time_ps = 5000; time_ps <= 485000;
         time_ps += 5000) {
        clock_value = !clock_value;
        add(timeline, time_ps, clk, clock_value ? "1" : "0");
    }

    add(timeline, 35000, rst_n, "1");
    add(timeline, 55000, sig_a, "00010001");
    add(timeline, 55000, sig_b, "00010001");
    add(timeline, 55000, counter_inc, "00000001");
    add(timeline, 55000, counter_nonmono, "00000110");
    add(timeline, 65000, sig_a, "00100010");
    add(timeline, 65000, counter_inc, "00000010");
    add(timeline, 65000, counter_nonmono, "00000111");
    add(timeline, 75000, sig_b, "00110011");
    add(timeline, 75000, counter_inc, "00000011");
    add(timeline, 75000, counter_nonmono, "00000100");
    add(timeline, 85000, xz_bus, "xxxxxxxx");
    add(timeline, 85000, mixed_xz_bus, "10xxzz00");
    add(timeline, 85000, counter_inc, "00000100");
    add(timeline, 85000, counter_nonmono, "00001000");
    add(timeline, 95000, xz_bus, "zzzzzzzz");
    add(timeline, 95000, mixed_xz_bus, "zz10xx01");
    add(timeline, 95000, counter_inc, "00000101");
    add(timeline, 96000, glitch_sig, "1");
    add(timeline, 96200, glitch_sig, "0");
    add(timeline, 105000, event_vld, "1");
    add(timeline, 105000, event_race, "1");
    add(timeline, 105000, event_payload, "01011010");
    add(timeline, 115000, event_payload, "00111100");
    add(timeline, 125000, event_vld, "0");
    add(timeline, 125000, event_rdy, "1");
    add(timeline, 125000, hs_valid, "1");
    add(timeline, 125000, hs_ready, "1");
    add(timeline, 125000, hs_data, "00010000");
    add(timeline, 135000, hs_data, "00010001");
    add(timeline, 145000, hs_ready, "0");
    add(timeline, 145000, hs_data, "00100010");
    add(timeline, 155000, hs_data, "00100011");
    add(timeline, 165000, hs_data, "00100100");
    add(timeline, 175000, hs_data, "00100101");
    add(timeline, 185000, hs_ready, "1");
    add(timeline, 185000, hs_data, "00110000");
    add(timeline, 195000, hs_valid, "0");

    add(timeline, 205000, paddr, "0000000100000000");
    add(timeline, 205000, pwdata, "11011110101011011011111011101111");
    add(timeline, 205000, pwrite, "1");
    add(timeline, 205000, psel, "1");
    add(timeline, 215000, penable, "1");
    add(timeline, 225000, pwrite, "0");
    add(timeline, 225000, psel, "0");
    add(timeline, 225000, penable, "0");
    add(timeline, 235000, prdata, "11001010111111101111000000001101");
    add(timeline, 235000, psel, "1");
    add(timeline, 245000, penable, "1");
    add(timeline, 255000, psel, "0");
    add(timeline, 255000, penable, "0");
    add(timeline, 265000, paddr, "0000001000000000");
    add(timeline, 265000, pwdata, "00010010001101000101011001111000");
    add(timeline, 265000, pwrite, "1");
    add(timeline, 265000, psel, "1");
    add(timeline, 275000, penable, "1");
    add(timeline, 285000, pwrite, "0");
    add(timeline, 285000, psel, "0");
    add(timeline, 285000, penable, "0");

    for (const auto& [time_ps, changes] : timeline) {
        writer.emitTimeChange(time_ps);
        for (const auto& [handle, bits] : changes) {
            emit_logic(writer, handle, bits);
        }
    }
    writer.close();
    return EXIT_SUCCESS;
}
