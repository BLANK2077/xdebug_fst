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
};

/// Session-scoped cursor registry for waveform.cursor.* actions.
class CursorManager {
public:
    static CursorManager& instance();

    bool set(const std::string& name, uint64_t time);
    bool get(const std::string& name, WaveformCursor& out) const;
    bool remove(const std::string& name);
    bool use(const std::string& name, uint64_t& time) const;  // alias of get
    std::vector<WaveformCursor> all() const;
    void clear();

private:
    CursorManager() = default;
    std::map<std::string, WaveformCursor> cursors_;
};

} // namespace xdebug_fst
