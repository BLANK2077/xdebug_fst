// logic_value.h — 4-state logic value rendering (BSD-3-Clause)
#pragma once

#include "nlohmann/json.hpp"
#include <string>

namespace xdebug_fst {

using Json = nlohmann::json;

enum class ValueRenderFormat { Hex, Bin, Dec };

/// A width-carrying 4-state logic value stored as an MSB-first ASCII bit
/// string ('0','1','x','z'; 'h','u','w','l','-' are preserved on input but
/// treated as unknown for arithmetic).
struct LogicValue {
    std::string bits;      // MSB-first ASCII, e.g. "1010x"
    int width = 0;         // 0 = unknown width
    bool known = true;     // false if any bit is X/Z/unknown
    bool has_x = false;
    bool has_z = false;

    bool ok() const { return known && !bits.empty(); }
};

/// Build a LogicValue from a wellen ASCII bit string ('0','1','x','z',...).
LogicValue logic_value_from_bits(const std::string& bits, int width = 0);

/// Build a LogicValue from an unsigned integer (2-state).
LogicValue logic_value_from_u64(uint64_t value, int width);

/// Render as SV literal: "<width>'h<hex>" / "'b<bits>" / "'d<dec>".
std::string sv_literal(const LogicValue& v, char radix);

/// Render with format (default Hex).
std::string render_logic_value(const LogicValue& v,
                               ValueRenderFormat fmt = ValueRenderFormat::Hex);

/// Canonical value JSON object (same shape as xdebug logic_value_json):
/// {"value": "8'h05", "known": true, "width": 8, "bits": "00000101"}
/// plus has_x/has_z when unknown.
Json logic_value_json(const LogicValue& v,
                      ValueRenderFormat fmt = ValueRenderFormat::Hex);

/// Parse a "hex" / "bin" / "dec" render-format request string.
bool parse_value_render_format(const std::string& text, ValueRenderFormat& out);

} // namespace xdebug_fst
