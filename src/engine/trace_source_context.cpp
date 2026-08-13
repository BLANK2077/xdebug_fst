#include "engine/trace_source_context.h"

#include "common/env_config.h"
#include "engine/engine_globals.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

namespace fs = std::filesystem;

bool usable_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

void add_session_candidates(std::vector<fs::path>& candidates,
                            const std::string& session_path,
                            const fs::path& reported) {
    if (session_path.empty()) return;
    fs::path base(session_path);
    std::error_code error;
    if (!fs::is_directory(base, error)) base = base.parent_path();
    if (base.empty()) return;
    candidates.push_back(base / reported);
    if (!base.parent_path().empty())
        candidates.push_back(base.parent_path() / reported);
}

std::optional<fs::path> resolve_source_file(const std::string& file) {
    if (file.empty() || file == "<unknown>") return std::nullopt;
    const fs::path reported(file);
    std::vector<fs::path> candidates;
    if (reported.is_absolute()) {
        candidates.push_back(reported);
    } else {
        std::error_code error;
        candidates.push_back(fs::current_path(error) / reported);
        const auto& globals = engine_globals();
        add_session_candidates(candidates, globals.design_path, reported);
        add_session_candidates(candidates, globals.waveform_path, reported);
    }

    std::set<std::string> visited;
    for (const auto& candidate : candidates) {
        const std::string key = candidate.lexically_normal().string();
        if (!visited.insert(key).second) continue;
        if (usable_file(candidate)) return candidate;
    }
    return std::nullopt;
}

Json read_source_context(const std::string& file, int first_line,
                         int last_line) {
    if (first_line <= 0 || last_line <= 0) return Json::array();
    if (first_line > last_line) std::swap(first_line, last_line);
    const auto resolved = resolve_source_file(file);
    if (!resolved) return Json::array();

    std::ifstream input(*resolved);
    if (!input) return Json::array();
    std::vector<std::string> lines;
    std::string text;
    while (std::getline(input, text)) lines.push_back(text);
    if (lines.empty() || first_line > static_cast<int>(lines.size()))
        return Json::array();

    const int context = xdebug_core::xdebug_trace_source_context_lines();
    const int begin = std::max(1, first_line - context);
    const int end = std::min(static_cast<int>(lines.size()), last_line + context);
    Json rows = Json::array();
    for (int line = begin; line <= end; ++line) {
        rows.push_back({{"line", line}, {"text", lines[line - 1]},
                        {"active", line >= first_line && line <= last_line}});
    }
    return rows;
}

}  // namespace

Json trace_source_context(const std::string& file, int line) {
    return read_source_context(file, line, line);
}

Json trace_source_context(const std::string& file, int first_line,
                          int last_line) {
    return read_source_context(file, first_line, last_line);
}

}  // namespace xdebug_fst
