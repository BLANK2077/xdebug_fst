// binary_design_backend.cpp — mmap-backed compact DesignDB reader
// BSD-3-Clause License

#include "binary_design_backend.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xdebug_fst {
namespace bd = binary_design;

namespace {

template <typename T>
bool valid_section(const bd::Header& header, bd::SectionIndex index,
                   uint64_t file_size) {
    const auto& section = header.sections[index];
    if (section.offset < sizeof(bd::Header) || section.offset > file_size)
        return false;
    if (section.count > std::numeric_limits<uint64_t>::max() / sizeof(T))
        return false;
    const uint64_t bytes = section.count * sizeof(T);
    return bytes <= file_size - section.offset;
}

}  // namespace

BinaryDesignBackend::~BinaryDesignBackend() { close(); }

bool BinaryDesignBackend::open(const std::string& path) {
    close();
    fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) {
        std::fprintf(stderr, "binary_design_backend: open failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    struct stat status {};
    if (fstat(fd_, &status) != 0 || status.st_size < 0 ||
        static_cast<uint64_t>(status.st_size) >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        std::fprintf(stderr, "binary_design_backend: invalid file size\n");
        close();
        return false;
    }
    size_ = static_cast<size_t>(status.st_size);
    if (size_ < sizeof(bd::Header)) {
        std::fprintf(stderr, "binary_design_backend: truncated header\n");
        close();
        return false;
    }
    mapping_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        std::fprintf(stderr, "binary_design_backend: mmap failed: %s\n",
                     std::strerror(errno));
        close();
        return false;
    }
    bytes_ = static_cast<const unsigned char*>(mapping_);
    header_ = reinterpret_cast<const bd::Header*>(bytes_);
    if (!validate()) {
        std::fprintf(stderr, "binary_design_backend: validation failed\n");
        close();
        return false;
    }
    return true;
}

void BinaryDesignBackend::close() {
    if (mapping_) munmap(mapping_, size_);
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    mapping_ = nullptr;
    bytes_ = nullptr;
    size_ = 0;
    header_ = nullptr;
}

const char* BinaryDesignBackend::string_at(uint32_t offset) const {
    if (!header_) return nullptr;
    const auto& strings = header_->sections[bd::Strings];
    if (offset >= strings.count) return nullptr;
    const char* value = reinterpret_cast<const char*>(bytes_ + strings.offset + offset);
    const size_t available = static_cast<size_t>(strings.count - offset);
    return std::memchr(value, '\0', available) ? value : nullptr;
}

bool BinaryDesignBackend::validate() {
    if (std::memcmp(header_->magic, bd::kMagic, sizeof(bd::kMagic)) != 0 ||
        header_->major != bd::kMajor || header_->minor != bd::kMinor ||
        header_->endian_tag != bd::kEndianTag ||
        header_->header_size != sizeof(bd::Header) ||
        header_->file_size != size_) return false;

    const bool sections_ok =
        valid_section<bd::SignalRecord>(*header_, bd::Signals, size_) &&
        valid_section<bd::NameIndexRecord>(*header_, bd::NameIndex, size_) &&
        valid_section<bd::DriverRecord>(*header_, bd::Drivers, size_) &&
        valid_section<uint64_t>(*header_, bd::DriverStarts, size_) &&
        valid_section<bd::LoadRecord>(*header_, bd::Loads, size_) &&
        valid_section<uint64_t>(*header_, bd::LoadStarts, size_) &&
        valid_section<bd::PortRecord>(*header_, bd::Ports, size_) &&
        valid_section<uint64_t>(*header_, bd::PortStarts, size_) &&
        valid_section<char>(*header_, bd::Strings, size_);
    if (!sections_ok || header_->sections[bd::Signals].count > INT32_MAX ||
        header_->sections[bd::Drivers].count > INT32_MAX ||
        header_->sections[bd::Loads].count > INT32_MAX ||
        header_->sections[bd::Ports].count > INT32_MAX ||
        header_->sections[bd::NameIndex].count != header_->sections[bd::Signals].count ||
        header_->sections[bd::DriverStarts].count != header_->sections[bd::Signals].count + 1 ||
        header_->sections[bd::LoadStarts].count != header_->sections[bd::Signals].count + 1 ||
        header_->sections[bd::PortStarts].count != header_->sections[bd::Signals].count + 1 ||
        header_->sections[bd::Strings].count == 0) return false;

    const auto section_end = [&](bd::SectionIndex index, uint64_t width) {
        return header_->sections[index].offset + header_->sections[index].count * width;
    };
    uint64_t previous = sizeof(bd::Header);
    const uint64_t widths[bd::SectionCount] = {
        sizeof(bd::SignalRecord), sizeof(bd::NameIndexRecord),
        sizeof(bd::DriverRecord), sizeof(uint64_t), sizeof(bd::LoadRecord),
        sizeof(uint64_t), sizeof(bd::PortRecord), sizeof(uint64_t), 1};
    for (uint32_t i = 0; i < bd::SectionCount; ++i) {
        const auto index = static_cast<bd::SectionIndex>(i);
        if (header_->sections[index].offset < previous ||
            header_->sections[index].offset % 8 != 0) return false;
        previous = section_end(index, widths[i]);
    }

    const uint64_t signals = header_->sections[bd::Signals].count;
    const auto* signal_records = section<bd::SignalRecord>(bd::Signals);
    for (uint64_t i = 0; i < signals; ++i) {
        const auto& record = signal_records[i];
        if (!string_at(record.name) || !string_at(record.type) ||
            !string_at(record.file) || record.direction < 0 || record.direction > 3)
            return false;
    }
    const auto* names = section<bd::NameIndexRecord>(bd::NameIndex);
    const char* prior = nullptr;
    std::vector<bool> indexed_signals(static_cast<size_t>(signals), false);
    for (uint64_t i = 0; i < signals; ++i) {
        const char* name = string_at(names[i].name);
        if (!name || names[i].signal < 0 ||
            static_cast<uint64_t>(names[i].signal) >= signals ||
            indexed_signals[static_cast<size_t>(names[i].signal)] ||
            (prior && std::strcmp(prior, name) >= 0)) return false;
        indexed_signals[static_cast<size_t>(names[i].signal)] = true;
        prior = name;
    }
    const auto validate_starts = [&](bd::SectionIndex index, uint64_t records) {
        const auto* starts = section<uint64_t>(index);
        if (starts[0] != 0 || starts[signals] != records) return false;
        for (uint64_t i = 1; i <= signals; ++i)
            if (starts[i] < starts[i - 1] || starts[i] > records) return false;
        return true;
    };
    if (!validate_starts(bd::DriverStarts, header_->sections[bd::Drivers].count) ||
        !validate_starts(bd::LoadStarts, header_->sections[bd::Loads].count) ||
        !validate_starts(bd::PortStarts, header_->sections[bd::Ports].count)) return false;

    const auto* drivers = section<bd::DriverRecord>(bd::Drivers);
    const auto* driver_starts = section<uint64_t>(bd::DriverStarts);
    uint64_t driver_owner = 0;
    for (uint64_t i = 0; i < header_->sections[bd::Drivers].count; ++i) {
        while (driver_owner < signals && i >= driver_starts[driver_owner + 1])
            ++driver_owner;
        const auto& r = drivers[i];
        if (driver_owner >= signals || r.target < 0 ||
            static_cast<uint64_t>(r.target) != driver_owner ||
            r.source < -1 || (r.source >= 0 && static_cast<uint64_t>(r.source) >= signals) ||
            !string_at(r.kind) || !string_at(r.role) ||
            !string_at(r.predicate) || !string_at(r.file)) return false;
    }
    const auto* loads = section<bd::LoadRecord>(bd::Loads);
    const auto* load_starts = section<uint64_t>(bd::LoadStarts);
    uint64_t load_owner = 0;
    for (uint64_t i = 0; i < header_->sections[bd::Loads].count; ++i) {
        while (load_owner < signals && i >= load_starts[load_owner + 1])
            ++load_owner;
        const auto& r = loads[i];
        if (load_owner >= signals || r.source < 0 || r.consumer < 0 ||
            static_cast<uint64_t>(r.source) != load_owner ||
            static_cast<uint64_t>(r.consumer) >= signals ||
            !string_at(r.kind) || !string_at(r.file)) return false;
    }
    const auto* ports = section<bd::PortRecord>(bd::Ports);
    const auto* port_starts = section<uint64_t>(bd::PortStarts);
    uint64_t port_owner = 0;
    for (uint64_t i = 0; i < header_->sections[bd::Ports].count; ++i) {
        while (port_owner < signals && i >= port_starts[port_owner + 1])
            ++port_owner;
        const auto& r = ports[i];
        if (port_owner >= signals || r.signal < 0 || r.connected < 0 ||
            static_cast<uint64_t>(r.signal) != port_owner ||
            static_cast<uint64_t>(r.connected) >= signals || !string_at(r.kind))
            return false;
    }
    return true;
}

bool BinaryDesignBackend::valid_signal(int index) const {
    return header_ && index >= 0 &&
        static_cast<uint64_t>(index) < header_->sections[bd::Signals].count;
}

int BinaryDesignBackend::signal_count() const {
    return header_ ? static_cast<int>(header_->sections[bd::Signals].count) : 0;
}

int BinaryDesignBackend::resolve(const char* name) const {
    if (!header_ || !name || !*name) return -1;
    const auto* records = section<bd::NameIndexRecord>(bd::NameIndex);
    const auto exact = [&](const std::string& query) {
        uint64_t low = 0, high = header_->sections[bd::NameIndex].count;
        while (low < high) {
            const uint64_t mid = low + (high - low) / 2;
            const int order = std::strcmp(
                query.c_str(), string_at(records[mid].name));
            if (order == 0) return records[mid].signal;
            if (order < 0) high = mid;
            else low = mid + 1;
        }
        return -1;
    };

    std::string query(name);
    int resolved = exact(query);
    if (resolved >= 0) return resolved;
    if (query.rfind("TOP.", 0) == 0) {
        query = "top." + query.substr(4);
        resolved = exact(query);
        if (resolved >= 0) return resolved;
    }
    if (query.rfind("top.", 0) != 0) {
        return exact("top." + query);
    }
    resolved = exact(query.substr(4));
    if (resolved >= 0) return resolved;

    // Verilator's model root is also named "top".  A user HDL top named
    // ``top`` is therefore stored as top.top.*, while its native/NPI-visible
    // path remains top.*.  Only try the duplicate-root spelling after every
    // ordinary exact/compatibility candidate has missed.
    resolved = exact("top." + query);
    if (resolved >= 0) return resolved;
    return -1;
}

const char* BinaryDesignBackend::signal_name(int idx) const {
    return valid_signal(idx) ? string_at(section<bd::SignalRecord>(bd::Signals)[idx].name) : nullptr;
}
const char* BinaryDesignBackend::signal_type(int idx) const {
    return valid_signal(idx) ? string_at(section<bd::SignalRecord>(bd::Signals)[idx].type) : nullptr;
}
int BinaryDesignBackend::signal_width(int idx) const {
    return valid_signal(idx) ? section<bd::SignalRecord>(bd::Signals)[idx].width : 0;
}
const char* BinaryDesignBackend::signal_file(int idx) const {
    return valid_signal(idx) ? string_at(section<bd::SignalRecord>(bd::Signals)[idx].file) : nullptr;
}
int BinaryDesignBackend::signal_line(int idx) const {
    return valid_signal(idx) ? section<bd::SignalRecord>(bd::Signals)[idx].line : 0;
}
int BinaryDesignBackend::signal_direction(int idx) const {
    return valid_signal(idx) ? section<bd::SignalRecord>(bd::Signals)[idx].direction : 0;
}

bool BinaryDesignBackend::grouped_range(bd::SectionIndex starts_index,
                                        bd::SectionIndex records_index,
                                        int signal, uint64_t& begin,
                                        uint64_t& end) const {
    if (!valid_signal(signal)) return false;
    const auto* starts = section<uint64_t>(starts_index);
    begin = starts[signal];
    end = starts[signal + 1];
    return end <= header_->sections[records_index].count;
}

int BinaryDesignBackend::trace_driver_count(int signal_idx) const {
    uint64_t begin = 0, end = 0;
    return grouped_range(bd::DriverStarts, bd::Drivers, signal_idx, begin, end)
        ? static_cast<int>(end - begin) : 0;
}

int BinaryDesignBackend::trace_driver(
    int signal_idx, std::vector<IDesignBackend::DriverRecord>& out) const {
    out.clear();
    uint64_t begin = 0, end = 0;
    if (!grouped_range(bd::DriverStarts, bd::Drivers, signal_idx, begin, end)) return 0;
    const auto* records = section<bd::DriverRecord>(bd::Drivers);
    out.reserve(static_cast<size_t>(end - begin));
    for (uint64_t i = begin; i < end; ++i) {
        const auto& source = records[i];
        IDesignBackend::DriverRecord record;
        record.src_signal = source.source;
        record.kind = string_at(source.kind);
        record.dependency_role = string_at(source.role);
        record.activation_predicate = string_at(source.predicate);
        record.file = string_at(source.file);
        record.line = source.line;
        out.push_back(std::move(record));
    }
    return static_cast<int>(out.size());
}

int BinaryDesignBackend::trace_load_count(int signal_idx) const {
    uint64_t begin = 0, end = 0;
    return grouped_range(bd::LoadStarts, bd::Loads, signal_idx, begin, end)
        ? static_cast<int>(end - begin) : 0;
}

int BinaryDesignBackend::trace_load(
    int signal_idx, std::vector<IDesignBackend::LoadRecord>& out) const {
    out.clear();
    uint64_t begin = 0, end = 0;
    if (!grouped_range(bd::LoadStarts, bd::Loads, signal_idx, begin, end)) return 0;
    const auto* records = section<bd::LoadRecord>(bd::Loads);
    out.reserve(static_cast<size_t>(end - begin));
    for (uint64_t i = begin; i < end; ++i) {
        const auto& source = records[i];
        out.push_back({source.consumer, string_at(source.kind),
                       string_at(source.file), source.line});
    }
    return static_cast<int>(out.size());
}

int BinaryDesignBackend::port_conn_count(int signal_idx) const {
    uint64_t begin = 0, end = 0;
    return grouped_range(bd::PortStarts, bd::Ports, signal_idx, begin, end)
        ? static_cast<int>(end - begin) : 0;
}

int BinaryDesignBackend::port_connections(
    int signal_idx, std::vector<IDesignBackend::PortConnection>& out) const {
    out.clear();
    uint64_t begin = 0, end = 0;
    if (!grouped_range(bd::PortStarts, bd::Ports, signal_idx, begin, end)) return 0;
    const auto* records = section<bd::PortRecord>(bd::Ports);
    out.reserve(static_cast<size_t>(end - begin));
    for (uint64_t i = begin; i < end; ++i) {
        const auto& source = records[i];
        out.push_back({source.signal, source.connected, string_at(source.kind)});
    }
    return static_cast<int>(out.size());
}

}  // namespace xdebug_fst
