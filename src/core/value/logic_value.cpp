// logic_value.cpp — 4-state logic value rendering (BSD-3-Clause)

#include "logic_value.h"

#include <cctype>
#include <cstdint>
#include <sstream>

namespace xdebug_fst {

// internal helpers (defined below)
std::string nibble_to_hex(const std::string& nib);
std::string bits_to_decimal(const std::string& bits);

LogicValue logic_value_from_bits(const std::string& bits, int width) {
    LogicValue v;
    v.width = width;
    v.bits = bits;
    for (char c : bits) {
        if (c == 'x' || c == 'X' || c == 'h' || c == 'u' || c == 'w' || c == '-') {
            v.known = false;
            v.has_x = true;
        } else if (c == 'z' || c == 'Z' || c == 'l') {
            v.known = false;
            v.has_z = true;
        } else if (c != '0' && c != '1') {
            v.known = false;
            v.has_x = true;
        }
    }
    if (v.width == 0 && !bits.empty()) v.width = static_cast<int>(bits.size());
    return v;
}

LogicValue logic_value_from_u64(uint64_t value, int width) {
    LogicValue v;
    v.width = width;
    std::string bits;
    int w = width > 0 ? width : 64;
    for (int i = w - 1; i >= 0; --i) {
        bits.push_back(((value >> i) & 1ULL) ? '1' : '0');
    }
    if (width > 0 && w > 0) {
        // keep exactly `width` bits
    }
    v.bits = bits;
    return v;
}

std::string sv_literal(const LogicValue& v, char radix) {
    std::string r;
    if (v.width > 0) r += std::to_string(v.width);
    r += '\'';
    r += radix;
    if (radix == 'h' || radix == 'H') {
        // Hex: group 4 bits from the MSB; X/Z in a nibble forces 'x'/'z' hex digit
        int w = v.width > 0 ? v.width : static_cast<int>(v.bits.size());
        std::string padded = v.bits;
        while (static_cast<int>(padded.size()) < w) padded.insert(padded.begin(), '0');
        if (static_cast<int>(padded.size()) > w) padded = padded.substr(padded.size() - w);
        int first = w % 4 == 0 ? 4 : w % 4;
        size_t i = 0;
        bool first_nibble = true;
        while (i < padded.size()) {
            int nib = first_nibble ? first : 4;
            first_nibble = false;
            std::string chunk = padded.substr(i, nib);
            i += nib;
            r += nibble_to_hex(chunk);
        }
    } else if (radix == 'b' || radix == 'B') {
        r += v.bits;
    } else { // dec
        if (v.known && !v.bits.empty()) {
            r += bits_to_decimal(v.bits);
        } else {
            r = v.width > 0 ? std::to_string(v.width) : std::string();
            r += "'bx";
        }
    }
    return r;
}

std::string render_logic_value(const LogicValue& v, ValueRenderFormat fmt) {
    switch (fmt) {
        case ValueRenderFormat::Bin: return sv_literal(v, 'b');
        case ValueRenderFormat::Dec:
            if (v.known && !v.bits.empty()) return sv_literal(v, 'd');
            return sv_literal(v, 'b');
        case ValueRenderFormat::Hex:
        default: return sv_literal(v, 'h');
    }
}

Json logic_value_json(const LogicValue& v, ValueRenderFormat fmt) {
    Json out;
    if (fmt == ValueRenderFormat::Bin) {
        out["value"] = sv_literal(v, 'b');
    } else if (fmt == ValueRenderFormat::Dec && v.known && !v.bits.empty()) {
        out["value"] = sv_literal(v, 'd');
    } else {
        out["value"] = sv_literal(v, 'h');
        if (fmt == ValueRenderFormat::Dec && !v.known) {
            out["requested_value_format"] = "dec";
            out["effective_value_format"] = "bin";
            out["value_format_reason"] = "decimal cannot preserve per-bit X/Z";
            out["value"] = sv_literal(v, 'b');
        }
    }
    out["known"] = v.known;
    if (v.width > 0) out["width"] = v.width;
    if (!v.bits.empty()) out["bits"] = v.bits;
    if (!v.known) {
        out["has_x"] = v.has_x;
        out["has_z"] = v.has_z;
    }
    return out;
}

bool parse_value_render_format(const std::string& text, ValueRenderFormat& out) {
    std::string t;
    for (char c : text) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (t == "hex" || t == "h") { out = ValueRenderFormat::Hex; return true; }
    if (t == "bin" || t == "b") { out = ValueRenderFormat::Bin; return true; }
    if (t == "dec" || t == "d") { out = ValueRenderFormat::Dec; return true; }
    return false;
}

std::string nibble_to_hex(const std::string& nib) {
    bool has_x = false, has_z = false;
    for (char c : nib) {
        if (c == 'x' || c == 'h' || c == 'u' || c == 'w' || c == '-') has_x = true;
        else if (c == 'z' || c == 'l') has_z = true;
    }
    if (has_x) return "x";
    if (has_z) return "z";
    unsigned v = 0;
    for (char c : nib) v = (v << 1) | (c == '1' ? 1u : 0u);
    const char* hex = "0123456789abcdef";
    return std::string(1, hex[v & 0xF]);
}

std::string bits_to_decimal(const std::string& bits) {
    // unsigned interpretation, MSB first
    unsigned __int128 acc = 0;
    for (char c : bits) {
        acc = acc * 2 + (c == '1' ? 1 : 0);
    }
    if (acc == 0) return "0";
    std::string s;
    while (acc > 0) {
        s.insert(s.begin(), static_cast<char>('0' + (acc % 10)));
        acc /= 10;
    }
    return s;
}


bool parse_sv_literal(const std::string& text, LogicValue& out) {
    std::string t;
    for (char c : text) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // strip underscores
    std::string clean;
    for (char c : t) if (c != '_') clean.push_back(c);
    if (clean.empty()) return false;

    // <width>'<base><digits> or '<base><digits>
    size_t tick = clean.find('\'');
    if (tick != std::string::npos) {
        int width = 0;
        std::string wstr = clean.substr(0, tick);
        if (!wstr.empty()) {
            for (char c : wstr) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            width = atoi(wstr.c_str());
        }
        if (tick + 1 >= clean.size()) return false;
        char base = clean[tick + 1];
        std::string digits = clean.substr(tick + 2);
        if (digits.empty()) return false;
        if (base != 'h' && base != 'b' && base != 'd' && base != 'o') return false;
        // validate digits
        for (char c : digits) {
            bool ok = (base == 'b') ? (c == '0' || c == '1' || c == 'x' || c == 'z')
                    : (base == 'h') ? (std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f') || c == 'x' || c == 'z')
                    : (base == 'o') ? (c >= '0' && c <= '7')
                    : std::isdigit(static_cast<unsigned char>(c));
            if (!ok) return false;
        }
        // build bit string
        std::string bits;
        bool unknown = false;
        for (char c : digits) if (c == 'x' || c == 'z') unknown = true;
        if (base == 'b') {
            bits = digits;
        } else if (base == 'h') {
            for (char c : digits) {
                if (c == 'x' || c == 'z') { bits += std::string(4, c); unknown = true; }
                else {
                    int nib = (c >= 'a') ? (c - 'a' + 10) : (c - '0');
                    for (int i = 3; i >= 0; --i) bits.push_back(((nib >> i) & 1) ? '1' : '0');
                }
            }
        } else if (base == 'o') {
            for (char c : digits) {
                int oct = c - '0';
                for (int i = 2; i >= 0; --i) bits.push_back(((oct >> i) & 1) ? '1' : '0');
            }
        } else {  // dec
            uint64_t v = strtoull(digits.c_str(), nullptr, 10);
            out = logic_value_from_u64(v, width);
            return true;
        }
        if (width > 0) {
            if ((int)bits.size() > width) bits = bits.substr(bits.size() - width);
            else if ((int)bits.size() < width) bits.insert(bits.begin(), width - bits.size(), unknown ? 'x' : '0');
        }
        out = logic_value_from_bits(bits, width);
        return true;
    }
    // plain decimal or hex-with-0x or bare bit string
    if (clean.size() > 2 && clean[0] == '0' && clean[1] == 'x') {
        uint64_t v = strtoull(clean.c_str() + 2, nullptr, 16);
        out = logic_value_from_u64(v, 0);
        return true;
    }
    bool all_digits = true;
    for (char c : clean) if (!std::isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; }
    if (all_digits) {
        uint64_t v = strtoull(clean.c_str(), nullptr, 10);
        out = logic_value_from_u64(v, 0);
        return true;
    }
    // bare bit string of 0/1/x/z
    for (char c : clean) {
        if (c != '0' && c != '1' && c != 'x' && c != 'z') return false;
    }
    out = logic_value_from_bits(clean, static_cast<int>(clean.size()));
    return true;
}
} // namespace xdebug_fst
