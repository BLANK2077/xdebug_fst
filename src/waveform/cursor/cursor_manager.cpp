// cursor_manager.cpp — Waveform cursors (BSD-3-Clause)

#include "cursor_manager.h"

namespace xdebug_fst {

CursorManager& CursorManager::instance() {
    static CursorManager mgr;
    return mgr;
}

bool CursorManager::set(const std::string& name, uint64_t time) {
    if (name.empty()) return false;
    const uint64_t revision = ++revision_;
    auto it = cursors_.find(name);
    if (it == cursors_.end()) {
        WaveformCursor cursor;
        cursor.name = name;
        cursor.time = time;
        cursor.created_at = revision;
        cursor.updated_at = revision;
        cursors_[name] = std::move(cursor);
    } else {
        it->second.time = time;
        it->second.updated_at = revision;
    }
    return true;
}

bool CursorManager::get(const std::string& name, WaveformCursor& out) const {
    auto it = cursors_.find(name);
    if (it == cursors_.end()) return false;
    out = it->second;
    return true;
}

bool CursorManager::remove(const std::string& name) {
    const bool removed = cursors_.erase(name) > 0;
    if (removed && active_name_ == name) active_name_.clear();
    return removed;
}

bool CursorManager::use(const std::string& name, WaveformCursor& out) {
    if (!get(name, out)) return false;
    active_name_ = name;
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
    active_name_.clear();
    revision_ = 0;
}

std::string CursorManager::active_name() const { return active_name_; }

} // namespace xdebug_fst
