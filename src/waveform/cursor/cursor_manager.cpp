// cursor_manager.cpp — Waveform cursors (BSD-3-Clause)

#include "cursor_manager.h"

namespace xdebug_fst {

CursorManager& CursorManager::instance() {
    static CursorManager mgr;
    return mgr;
}

bool CursorManager::set(const std::string& name, uint64_t time) {
    if (name.empty()) return false;
    cursors_[name] = WaveformCursor{name, time};
    return true;
}

bool CursorManager::get(const std::string& name, WaveformCursor& out) const {
    auto it = cursors_.find(name);
    if (it == cursors_.end()) return false;
    out = it->second;
    return true;
}

bool CursorManager::remove(const std::string& name) {
    return cursors_.erase(name) > 0;
}

bool CursorManager::use(const std::string& name, uint64_t& time) const {
    WaveformCursor c;
    if (!get(name, c)) return false;
    time = c.time;
    return true;
}

std::vector<WaveformCursor> CursorManager::all() const {
    std::vector<WaveformCursor> out;
    out.reserve(cursors_.size());
    for (const auto& kv : cursors_) out.push_back(kv.second);
    return out;
}

void CursorManager::clear() {
    cursors_.clear();
}

} // namespace xdebug_fst
