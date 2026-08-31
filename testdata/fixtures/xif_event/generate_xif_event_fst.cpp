// Deterministic direct-FST generator for the open-source XIF event mirror.
// It preserves packed-struct display text, member widths, clock sampling and X.
// BSD-3-Clause License

#include "fstcpp/fstcpp_writer.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Handle = fst::Handle;

struct Change {
    Handle handle;
    std::string value;
    bool string_value;
};

using Timeline = std::map<std::uint64_t, std::vector<Change>>;

struct InterfaceHandles {
    Handle vld;
    Handle rdy;
    Handle bp;
    Handle pd;
    Handle opcode;
    Handle channel;
    Handle id;
    Handle data;
};

Handle logic(fst::Writer& writer, const char* name, std::uint32_t width) {
    return writer.createVar(
        fst::Hierarchy::VarType::SV_LOGIC,
        fst::Hierarchy::VarDirection::IMPLICIT,
        width,
        name,
        0);
}

InterfaceHandles make_interface(fst::Writer& writer, const char* name) {
    writer.setScope(fst::Hierarchy::ScopeType::VCD_INTERFACE, name, name);
    InterfaceHandles handles{
        logic(writer, "vld", 1),
        logic(writer, "rdy", 1),
        logic(writer, "bp", 1),
        logic(writer, "pd", 32),
        0, 0, 0, 0,
    };
    writer.setScope(fst::Hierarchy::ScopeType::VCD_STRUCT, "pd", "pd");
    handles.opcode = logic(writer, "opcode", 8);
    handles.channel = logic(writer, "channel", 4);
    handles.id = logic(writer, "id", 4);
    handles.data = logic(writer, "data", 16);
    writer.upscope();
    writer.upscope();
    return handles;
}

std::string bits(std::uint64_t value, unsigned width) {
    std::string result(width, '0');
    for (unsigned index = 0; index < width; ++index) {
        if ((value >> index) & 1u) result[width - index - 1] = '1';
    }
    return result;
}

void emit_logic(fst::Writer& writer, Handle handle, const std::string& value) {
    if (value.empty() || value.size() > 64) {
        throw std::runtime_error("logic value width must be in [1,64]");
    }
    std::uint64_t planes[2] = {0, 0};
    bool four_state = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::uint64_t mask = std::uint64_t{1} << (value.size() - index - 1);
        switch (value[index]) {
        case '0': break;
        case '1': planes[0] |= mask; break;
        case 'x': case 'X': planes[1] |= mask; four_state = true; break;
        case 'z': case 'Z': planes[0] |= mask; planes[1] |= mask; four_state = true; break;
        default: throw std::runtime_error("invalid four-state digit");
        }
    }
    if (four_state) {
        writer.emitValueChange(handle, planes, fst::EncodingType::VERILOG);
    } else {
        writer.emitValueChange(handle, planes[0]);
    }
}

void add_logic(Timeline& timeline, std::uint64_t time_ps,
               Handle handle, std::string value) {
    timeline[time_ps].push_back({handle, std::move(value), false});
}

void add_pd(Timeline& timeline, std::uint64_t time_ps,
            const InterfaceHandles& handles, std::uint8_t opcode,
            std::uint8_t channel, std::uint8_t id, std::uint16_t data) {
    const std::string opcode_bits = bits(opcode, 8);
    const std::string channel_bits = bits(channel, 4);
    const std::string id_bits = bits(id, 4);
    const std::string data_bits = bits(data, 16);
    add_logic(
        timeline,
        time_ps,
        handles.pd,
        opcode_bits + channel_bits + id_bits + data_bits);
    add_logic(timeline, time_ps, handles.opcode, opcode_bits);
    add_logic(timeline, time_ps, handles.channel, channel_bits);
    add_logic(timeline, time_ps, handles.id, id_bits);
    add_logic(timeline, time_ps, handles.data, data_bits);
}

void initialize(Timeline& timeline, const InterfaceHandles& handles,
                bool rdy, bool bp) {
    add_logic(timeline, 0, handles.vld, "0");
    add_logic(timeline, 0, handles.rdy, rdy ? "1" : "0");
    add_logic(timeline, 0, handles.bp, bp ? "1" : "0");
    add_pd(timeline, 0, handles, 0, 0, 0, 0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: generate-xif-event-fst OUTPUT.fst\n";
        return 2;
    }

    fst::Writer writer(argv[1]);
    writer.setWriterPackType(fst::WriterPackType::LZ4);
    writer.setTimecale(-12);
    writer.setWriter("xdebug-fst deterministic xif_event fixture v1");
    writer.setDate("Mon Aug 31 00:00:00 2026\n");
    writer.setScope(fst::Hierarchy::ScopeType::VCD_MODULE,
                    "xif_event_top", "xif_event_top");
    const Handle clk = logic(writer, "clk", 1);
    const Handle rst_n = logic(writer, "rst_n", 1);
    const Handle xz_vld = logic(writer, "xz_vld", 1);
    const Handle xz_data = logic(writer, "xz_data", 16);
    const InterfaceHandles rdy = make_interface(writer, "if_rdy");
    const InterfaceHandles bp = make_interface(writer, "if_bp");
    const InterfaceHandles none = make_interface(writer, "if_none");
    const InterfaceHandles pair_master = make_interface(writer, "if_pair_master");
    const InterfaceHandles pair_slave = make_interface(writer, "if_pair_slave");
    writer.upscope();

    Timeline timeline;
    add_logic(timeline, 0, clk, "0");
    for (std::uint64_t time = 5000; time <= 195000; time += 5000) {
        add_logic(timeline, time, clk, ((time / 5000) & 1u) ? "1" : "0");
    }
    add_logic(timeline, 0, rst_n, "0");
    add_logic(timeline, 45000, rst_n, "1");
    add_logic(timeline, 0, xz_vld, "0");
    add_logic(timeline, 0, xz_data, bits(0, 16));
    add_logic(timeline, 65000, xz_vld, "1");
    add_logic(timeline, 65000, xz_data, "xxxxxxxxxxxxxxxx");
    add_logic(timeline, 75000, xz_vld, "0");
    add_logic(timeline, 75000, xz_data, bits(0, 16));

    initialize(timeline, rdy, true, false);
    add_logic(timeline, 84000, rdy.vld, "1");
    add_pd(timeline, 84000, rdy, 0x5a, 3, 2, 0xa55a);
    add_pd(timeline, 104000, rdy, 0x10, 1, 1, 0x1000);
    add_logic(timeline, 106000, rdy.vld, "0");
    add_logic(timeline, 124000, rdy.vld, "1");
    add_pd(timeline, 124000, rdy, 0x11, 2, 0, 0x1001);
    add_pd(timeline, 134000, rdy, 0x12, 3, 2, 0x1002);
    add_logic(timeline, 136000, rdy.vld, "0");

    initialize(timeline, bp, false, true);
    add_logic(timeline, 64000, bp.vld, "1");
    add_pd(timeline, 64000, bp, 0xb0, 0, 0, 0x2000);
    add_pd(timeline, 74000, bp, 0xb1, 1, 1, 0x2001);
    add_logic(timeline, 76000, bp.vld, "0");
    add_logic(timeline, 84000, bp.bp, "0");
    add_logic(timeline, 94000, bp.vld, "1");
    add_pd(timeline, 94000, bp, 0xb2, 2, 2, 0x2002);
    add_pd(timeline, 104000, bp, 0xb3, 3, 3, 0x2003);
    add_logic(timeline, 106000, bp.vld, "0");

    initialize(timeline, none, false, false);
    add_logic(timeline, 64000, none.vld, "1");
    add_pd(timeline, 64000, none, 0xc0, 0, 1, 0x3000);
    add_logic(timeline, 66000, none.vld, "0");
    add_logic(timeline, 104000, none.vld, "1");
    add_pd(timeline, 104000, none, 0xc1, 1, 2, 0x3001);
    add_pd(timeline, 114000, none, 0xc2, 2, 3, 0x3002);
    add_logic(timeline, 116000, none.vld, "0");

    for (const InterfaceHandles* handles : {&pair_master, &pair_slave}) {
        initialize(timeline, *handles, false, false);
        add_logic(timeline, 64000, handles->vld, "1");
        add_logic(timeline, 64000, handles->rdy, "1");
        add_pd(timeline, 64000, *handles, 0xd0, 0, 0, 0x4000);
        add_pd(timeline, 74000, *handles, 0xd1, 1, 1, 0x4001);
        add_logic(timeline, 76000, handles->rdy, "0");
        add_pd(timeline, 84000, *handles, 0xd2, 2, 2, 0x4002);
        add_logic(timeline, 92000, handles->rdy, "1");
        add_pd(timeline, 104000, *handles, 0xd3, 3, 3, 0x4003);
        add_logic(timeline, 106000, handles->vld, "0");
    }

    for (const auto& [time, changes] : timeline) {
        writer.emitTimeChange(time);
        for (const Change& change : changes) {
            if (change.string_value) {
                writer.emitValueChange(change.handle, change.value.c_str());
            } else {
                emit_logic(writer, change.handle, change.value);
            }
        }
    }
    writer.close();
    return EXIT_SUCCESS;
}
