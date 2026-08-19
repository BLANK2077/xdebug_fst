// binary_design_backend.h — mmap-backed compact DesignDB reader
// BSD-3-Clause License
#pragma once

#include "binary_design_format.h"
#include "design_backend.h"

#include <cstddef>
#include <string>

namespace xdebug_fst {

class BinaryDesignBackend final : public IDesignBackend {
public:
    BinaryDesignBackend() = default;
    ~BinaryDesignBackend() override;

    bool open(const std::string& path) override;
    void close() override;
    bool is_open() const override { return mapping_ != nullptr; }

    int signal_count() const override;
    int resolve(const char* name) const override;
    const char* signal_name(int idx) const override;
    const char* signal_type(int idx) const override;
    int signal_width(int idx) const override;
    const char* signal_file(int idx) const override;
    int signal_line(int idx) const override;
    int signal_direction(int idx) const override;
    int trace_driver_count(int signal_idx) const override;
    int trace_driver(int signal_idx, std::vector<IDesignBackend::DriverRecord>& out) const override;
    int trace_load_count(int signal_idx) const override;
    int trace_load(int signal_idx, std::vector<IDesignBackend::LoadRecord>& out) const override;
    int port_conn_count(int signal_idx) const override;
    int port_connections(int signal_idx,
                         std::vector<IDesignBackend::PortConnection>& out) const override;

private:
    template <typename T>
    const T* section(binary_design::SectionIndex index) const {
        return reinterpret_cast<const T*>(bytes_ + header_->sections[index].offset);
    }
    const char* string_at(uint32_t offset) const;
    bool validate();
    bool valid_signal(int index) const;
    bool grouped_range(binary_design::SectionIndex starts,
                       binary_design::SectionIndex records, int signal,
                       uint64_t& begin, uint64_t& end) const;

    int fd_ = -1;
    void* mapping_ = nullptr;
    const unsigned char* bytes_ = nullptr;
    size_t size_ = 0;
    const binary_design::Header* header_ = nullptr;
};

}  // namespace xdebug_fst
