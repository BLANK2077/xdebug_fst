#pragma once

#include <string>

namespace xdebug_core {

struct ToolConfig {
    std::string tool_name;
    std::string home_dir_name;
    std::string executable_name;
    std::string protocol_version;
};

ToolConfig make_tool_config(const std::string& tool_name,
                            const std::string& home_dir_name,
                            const std::string& executable_name,
                            const std::string& protocol_version);

} // namespace xdebug_core
