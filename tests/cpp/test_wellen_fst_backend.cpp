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
    require(argc == 2, "expected one waveform fixture path");

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

    std::cout << "WellenFstBackend hierarchy/sentinel tests passed\n";
    return 0;
}
