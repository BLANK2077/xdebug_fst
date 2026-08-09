// cursor_manager.h — Waveform cursors (BSD-3-Clause)
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xdebug_fst {

/// One named waveform cursor (a time marker).
struct WaveformCursor {
    std::string name;
    uint64_t time = 0;
    std::string note;
    std::string origin = "user";
    std::string clock;
    uint64_t created_at = 0;
    uint64_t updated_at = 0;
};

/// Session-scoped cursor registry for waveform.cursor.* actions.
class CursorManager {
public:
    static CursorManager& instance();

    bool set(const std::string& name, uint64_t time);
    bool get(const std::string& name, WaveformCursor& out) const;
    bool remove(const std::string& name);
    bool use(const std::string& name, WaveformCursor& out);
    std::vector<WaveformCursor> all() const;
    std::string active_name() const;
    void clear();

private:
    CursorManager() = default;
    std::map<std::string, WaveformCursor> cursors_;
    std::string active_name_;
    uint64_t revision_ = 0;
};

} // namespace xdebug_fst
