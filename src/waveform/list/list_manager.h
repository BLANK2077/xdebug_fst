// list_manager.h — Session signal lists (BSD-3-Clause)
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xdebug_fst {

/// One named signal list, shared across list.* actions within a session.
struct SignalList {
    std::string name;
    std::vector<std::string> signals;  // hierarchical paths, in add order
    std::string created_from;          // "user" | "file:<path>"
};

/// Session-scoped signal list registry.
class ListManager {
public:
    static ListManager& instance();

    bool create(const std::string& name, std::string& error);
    bool add(const std::string& name, const std::vector<std::string>& signals,
             std::string& error);
    bool remove(const std::string& name, const std::vector<std::string>& signals,
                std::string& error);
    bool load(const std::string& name, const std::string& file_path,
              std::string& error);
    bool validate(const std::string& name, std::string& error);
    const SignalList* get(const std::string& name) const;
    bool exists(const std::string& name) const;
    std::vector<std::string> names() const;
    void clear();

private:
    ListManager() = default;
    std::map<std::string, SignalList> lists_;
};

} // namespace xdebug_fst
