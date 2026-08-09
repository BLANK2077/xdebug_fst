#include "common/tool_config.h"

namespace xdebug_core {

ToolConfig make_tool_config(const std::string& tool_name,
                            const std::string& home_dir_name,
                            const std::string& executable_name,
                            const std::string& protocol_version) {
    ToolConfig config;
    config.tool_name = tool_name;
    config.home_dir_name = home_dir_name;
    config.executable_name = executable_name;
    config.protocol_version = protocol_version;
    return config;
}

} // namespace xdebug_core
