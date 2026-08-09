#include "backend/xdd_design_backend.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 3, "expected ABI-v2 and legacy bundle paths");

    xdebug_fst::XddDesignBackend legacy;
    require(!legacy.open(argv[2]) && !legacy.is_open(),
            "legacy XDD bundle fails closed without ABI fallback");

    xdebug_fst::XddDesignBackend design;
    require(design.open(argv[1]), "ABI-v2 XDD bundle opens");
    const int top_clk = design.resolve("top.clk");
    const int nested_clk = design.resolve("top.counter_top.clk");
    require(top_clk >= 0 && nested_clk >= 0,
            "version-1 signal resolution remains compatible");
    require(design.signal_direction(top_clk) == 1 &&
                design.signal_direction(design.resolve("top.count")) == 2,
            "native declared input/output directions are preserved");

    std::vector<xdebug_fst::IDesignBackend::PortConnection> connections;
    require(design.port_conn_count(top_clk) == 1 &&
                design.port_connections(top_clk, connections) == 1 &&
                connections[0].port_signal == top_clk &&
                connections[0].connected_signal == nested_clk &&
                connections[0].kind == "port_boundary",
            "native port connection avoids full-table inference");
    design.close();
    require(!design.is_open(), "ABI-v2 XDD bundle closes cleanly");

    std::cout << "XDD ABI/direction/connection tests passed\n";
    return 0;
}
