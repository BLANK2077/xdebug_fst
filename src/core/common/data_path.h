#pragma once
#include "core/common/env_config.h"
#include <filesystem>
#include <string>

namespace xdebug_core {
// Explicit overrides are authoritative, including when files are missing.
inline std::string installed_data_path(const std::string& relative) {
    const auto root = env_raw_string("XDEBUG_FST_DATA_ROOT");
    if (!root.empty()) return root + "/" + relative;
#ifdef XDEBUG_FST_SOURCE_DIR
    // Only the standalone schema unit test defines this path.
    return std::string(XDEBUG_FST_SOURCE_DIR) + "/compat/xdebug-v1/" + relative;
#else
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return "/unavailable-xdebug-fst-data/" + relative;
    auto directory = executable.parent_path();
    if (directory.filename() == "bin") directory = directory.parent_path();
    return (directory / "share/xdebug-fst" / relative).string();
#endif
}
}
