// list_manager.cpp — Session signal lists (BSD-3-Clause)

#include "list_manager.h"

#include <fstream>
#include <sstream>

namespace xdebug_fst {

ListManager& ListManager::instance() {
    static ListManager mgr;
    return mgr;
}

bool ListManager::create(const std::string& name, std::string& error) {
    if (name.empty()) {
        error = "list name is required";
        return false;
    }
    if (lists_.count(name)) {
        error = "list already exists: " + name;
        return false;
    }
    SignalList list;
    list.name = name;
    list.created_from = "user";
    lists_[name] = std::move(list);
    return true;
}

bool ListManager::add(const std::string& name,
                      const std::vector<std::string>& signals,
                      std::string& error) {
    auto it = lists_.find(name);
    if (it == lists_.end()) {
        error = "list not found: " + name;
        return false;
    }
    for (const auto& s : signals) {
        if (s.empty()) continue;
        bool dup = false;
        for (const auto& existing : it->second.signals) {
            if (existing == s) { dup = true; break; }
        }
        if (!dup) it->second.signals.push_back(s);
    }
    return true;
}

bool ListManager::remove(const std::string& name,
                         const std::vector<std::string>& signals,
                         std::string& error) {
    auto it = lists_.find(name);
    if (it == lists_.end()) {
        error = "list not found: " + name;
        return false;
    }
    std::vector<std::string> kept;
    for (const auto& s : it->second.signals) {
        bool drop = false;
        for (const auto& r : signals) {
            if (s == r) { drop = true; break; }
        }
        if (!drop) kept.push_back(s);
    }
    it->second.signals = std::move(kept);
    return true;
}

bool ListManager::load(const std::string& name, const std::string& file_path,
                       std::string& error) {
    std::ifstream in(file_path);
    if (!in) {
        error = "cannot open file: " + file_path;
        return false;
    }
    std::vector<std::string> signals;
    std::string line;
    while (std::getline(in, line)) {
        // strip comments and whitespace
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream ss(line);
        std::string token;
        while (ss >> token) signals.push_back(token);
    }
    if (lists_.count(name)) {
        lists_[name].signals.clear();
    } else {
        SignalList list;
        list.name = name;
        lists_[name] = std::move(list);
    }
    lists_[name].created_from = "file:" + file_path;
    lists_[name].signals = std::move(signals);
    return true;
}

bool ListManager::validate(const std::string& name, std::string& error) {
    auto it = lists_.find(name);
    if (it == lists_.end()) {
        error = "list not found: " + name;
        return false;
    }
    // Existence of each signal is checked against the waveform backend by
    // the action handler; here we only ensure non-empty names.
    for (const auto& s : it->second.signals) {
        if (s.empty()) {
            error = "list contains an empty signal name";
            return false;
        }
    }
    return true;
}

const SignalList* ListManager::get(const std::string& name) const {
    auto it = lists_.find(name);
    return it == lists_.end() ? nullptr : &it->second;
}

bool ListManager::exists(const std::string& name) const {
    return lists_.count(name) > 0;
}

std::vector<std::string> ListManager::names() const {
    std::vector<std::string> out;
    out.reserve(lists_.size());
    for (const auto& kv : lists_) out.push_back(kv.first);
    return out;
}

void ListManager::clear() {
    lists_.clear();
}

} // namespace xdebug_fst
