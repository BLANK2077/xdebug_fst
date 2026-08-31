#include "Vaxi_vip_fixture_top.h"
#include "Vaxi_vip_fixture_top___024root.h"
#include "Vaxi_vip_fixture_top_axi_master_mirror_if.h"
#include "Vaxi_vip_fixture_top_axi_vip_mirror_if.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Event {
    std::string channel;
    std::uint64_t time = 0;
    std::uint64_t valid_begin = 0;
    std::uint64_t id = 0;
    std::uint64_t addr = 0;
    std::uint64_t len = 0;
    std::uint64_t last = 0;
    std::uint64_t resp = 0;
    std::string data;
    std::string wstrb;
};

static std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = line.find('\t',begin);
        fields.push_back(line.substr(begin,end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    fields.resize(10);
    return fields;
}

static std::uint64_t number(const std::string& text) {
    return text.empty() ? 0 : std::stoull(text,nullptr,0);
}

static std::vector<Event> load_events(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open event TSV: " + path);
    std::string line;
    std::getline(input,line);
    std::vector<Event> events;
    while (std::getline(input,line)) {
        const auto field = split_tabs(line);
        events.push_back({field[0],number(field[1]),number(field[2]),
            number(field[3]),number(field[4]),number(field[5]),number(field[6]),
            number(field[7]),field[8],field[9]});
    }
    return events;
}

template <std::size_t Words>
static void assign_hex(VlWide<Words>& target, std::string value) {
    for (std::size_t word = 0; word < Words; ++word) target[word] = 0;
    for (std::size_t word = 0; word < Words && !value.empty(); ++word) {
        const std::size_t count = std::min<std::size_t>(8,value.size());
        const std::size_t begin = value.size() - count;
        target[word] = static_cast<std::uint32_t>(
            std::stoul(value.substr(begin,count),nullptr,16));
        value.resize(begin);
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: Vaxi_vip_fixture_top EVENTS.tsv OUTPUT.fst\n";
        return 2;
    }
    try {
        const auto events = load_events(argv[1]);
        if (events.empty()) throw std::runtime_error("empty event TSV");
        VerilatedContext context;
        context.commandArgs(argc,argv);
        context.traceEverOn(true);
        Vaxi_vip_fixture_top top{&context};
        auto* interface = top.rootp
            ->__PVT__axi_vip_fixture_top__DOT__axi_vip_if
            ->__PVT__master_if__BRA__0__KET__;
        VerilatedFstC trace;
        top.trace(&trace,4);
        trace.open(argv[2]);

        top.rootp->axi_vip_fixture_top__DOT__clk = 0;
        top.rootp->axi_vip_fixture_top__DOT__rst_n = 0;
        interface->awsize = 3; interface->awburst = 1;
        interface->arsize = 3; interface->arburst = 1;
        interface->bready = 0; interface->rready = 0;
        top.eval(); trace.dump(0);

        struct Change { std::uint64_t time; int kind; std::size_t event; };
        std::vector<Change> changes;
        changes.reserve(events.size() * 3 + 4);
        for (std::size_t index = 0; index < events.size(); ++index) {
            const auto& event = events[index];
            const std::uint64_t begin = event.valid_begin ? event.valid_begin : event.time;
            changes.push_back({begin - 1000,0,index});
            if (event.channel == "AW" || event.channel == "W" || event.channel == "AR")
                changes.push_back({event.time - 1000,1,index});
            changes.push_back({event.time + 1000,2,index});
        }
        const std::uint64_t finish = events.back().time + 10000000;
        for (std::uint64_t time = 5000; time <= finish; time += 5000)
            changes.push_back({time,3,0});
        changes.push_back({14000,4,0});
        changes.push_back({195000,5,0});
        std::stable_sort(changes.begin(),changes.end(),
            [](const Change& left, const Change& right) {
                return left.time < right.time;
            });

        for (std::size_t change_index = 0; change_index < changes.size();) {
            const std::uint64_t change_time = changes[change_index].time;
            context.time(change_time);
            do {
                const auto& change = changes[change_index];
            if (change.kind == 3) {
                top.rootp->axi_vip_fixture_top__DOT__clk ^= 1;
            } else if (change.kind == 4) {
                interface->bready = 1; interface->rready = 1;
            } else if (change.kind == 5) {
                top.rootp->axi_vip_fixture_top__DOT__rst_n = 1;
            } else {
                const auto& event = events[change.event];
                const bool assert_signal = change.kind != 2;
                if (event.channel == "AW") {
                    if (change.kind == 0) {
                        interface->awid = event.id; interface->awaddr = event.addr;
                        interface->awlen = event.len; interface->awvalid = 1;
                    } else if (change.kind == 1) interface->awready = 1;
                    else { interface->awvalid = 0; interface->awready = 0; }
                } else if (event.channel == "W") {
                    if (change.kind == 0) {
                        assign_hex(interface->wdata,event.data);
                        if (event.wstrb.empty()) assign_hex(interface->wstrb,"ff");
                        else assign_hex(interface->wstrb,event.wstrb);
                        interface->wlast = event.last; interface->wvalid = 1;
                    } else if (change.kind == 1) interface->wready = 1;
                    else { interface->wvalid = 0; interface->wready = 0; interface->wlast = 0; }
                } else if (event.channel == "B") {
                    interface->bid = event.id; interface->bresp = event.resp;
                    interface->bvalid = assert_signal;
                } else if (event.channel == "AR") {
                    if (change.kind == 0) {
                        interface->arid = event.id; interface->araddr = event.addr;
                        interface->arlen = event.len; interface->arvalid = 1;
                    } else if (change.kind == 1) interface->arready = 1;
                    else { interface->arvalid = 0; interface->arready = 0; }
                } else if (event.channel == "R") {
                    interface->rid = event.id; interface->rresp = event.resp;
                    interface->rlast = assert_signal ? event.last : 0;
                    interface->rvalid = assert_signal;
                }
            }
                ++change_index;
            } while (change_index < changes.size()
                     && changes[change_index].time == change_time);
            top.eval(); trace.dump(change_time);
        }
        top.final(); trace.close();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
