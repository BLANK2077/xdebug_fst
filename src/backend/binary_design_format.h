// binary_design_format.h — Versioned compact DesignDB on-disk layout
// BSD-3-Clause License
#pragma once

#include <cstdint>

namespace xdebug_fst::binary_design {

constexpr char kMagic[8] = {'X', 'D', 'D', 'B', 'I', 'N', '1', '\0'};
constexpr uint32_t kMajor = 1;
constexpr uint32_t kMinor = 0;
constexpr uint32_t kEndianTag = UINT32_C(0x01020304);

struct Section {
    uint64_t offset;
    uint64_t count;
};

enum SectionIndex : uint32_t {
    Signals,
    NameIndex,
    Drivers,
    DriverStarts,
    Loads,
    LoadStarts,
    Ports,
    PortStarts,
    Strings,
    SectionCount,
};

struct Header {
    char magic[8];
    uint32_t major;
    uint32_t minor;
    uint32_t endian_tag;
    uint32_t header_size;
    uint64_t file_size;
    Section sections[SectionCount];
};

#pragma pack(push, 1)
struct SignalRecord {
    uint32_t name;
    uint32_t type;
    uint32_t file;
    int32_t width;
    int32_t line;
    int32_t direction;
};

struct NameIndexRecord {
    uint32_t name;
    int32_t signal;
};

struct DriverRecord {
    int32_t target;
    int32_t source;
    uint32_t kind;
    uint32_t role;
    uint32_t predicate;
    uint32_t file;
    int32_t line;
};

struct LoadRecord {
    int32_t source;
    int32_t consumer;
    uint32_t kind;
    uint32_t file;
    int32_t line;
};

struct PortRecord {
    int32_t signal;
    int32_t connected;
    uint32_t kind;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 176, "binary DesignDB header layout changed");
static_assert(sizeof(SignalRecord) == 24, "binary signal layout changed");
static_assert(sizeof(NameIndexRecord) == 8, "binary name index layout changed");
static_assert(sizeof(DriverRecord) == 28, "binary driver layout changed");
static_assert(sizeof(LoadRecord) == 20, "binary load layout changed");
static_assert(sizeof(PortRecord) == 12, "binary port layout changed");

}  // namespace xdebug_fst::binary_design
