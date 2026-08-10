// expr_eval.cpp — 4-state expression evaluation (BSD-3-Clause)

#include "expr_eval.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <set>

namespace xdebug_fst {

// ── Lexer / parser ──

namespace {

struct Parser {
    const std::string& text;
    size_t pos = 0;
    std::string error;

    explicit Parser(const std::string& t) : text(t) {}

    void skip_ws() {
        while (pos < text.size() &&
               std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    }

    bool at_end() {
        skip_ws();
        return pos >= text.size();
    }

    char peek() {
        skip_ws();
        return pos < text.size() ? text[pos] : '\0';
    }

    bool eat(char c) {
        skip_ws();
        if (pos < text.size() && text[pos] == c) { ++pos; return true; }
        return false;
    }

    // identifier: [A-Za-z_][A-Za-z0-9_$]* possibly dotted a.b.c
    std::string parse_identifier() {
        skip_ws();
        size_t start = pos;
        if (pos < text.size() &&
            (std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
            ++pos;
            while (pos < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[pos])) ||
                    text[pos] == '_' || text[pos] == '$')) ++pos;
            // allow dotted hierarchy
            while (pos + 1 < text.size() && text[pos] == '.' &&
                   (std::isalpha(static_cast<unsigned char>(text[pos + 1])) ||
                    text[pos + 1] == '_')) {
                ++pos;  // '.'
                while (pos < text.size() &&
                       (std::isalnum(static_cast<unsigned char>(text[pos])) ||
                        text[pos] == '_' || text[pos] == '$')) ++pos;
            }
        }
        return text.substr(start, pos - start);
    }

    // constant: <width>'<base><digits> | '<base><digits> | decimal
    LogicValue parse_const() {
        skip_ws();
        size_t save = pos;
        // width'digits form
        size_t tick = text.find('\'', pos);
        LogicValue v;
        if (tick != std::string::npos) {
            std::string width_str = text.substr(pos, tick - pos);
            bool all_digits = !width_str.empty();
            for (char c : width_str) {
                if (!std::isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; }
            }
            if (all_digits && width_str.size() <= 9) {
                int width = atoi(width_str.c_str());
                size_t base_pos=tick+1;
                if (base_pos<text.size()&&
                    std::tolower(static_cast<unsigned char>(text[base_pos]))=='s') {
                    ++base_pos;
                }
                if (base_pos>=text.size()) return v;
                char base=std::tolower(
                    static_cast<unsigned char>(text[base_pos]));
                if (base == 'h' || base == 'b' || base == 'd' || base == 'o') {
                    size_t digits_start=base_pos+1;
                    size_t d = digits_start;
                    while (d < text.size() &&
                           (std::isalnum(static_cast<unsigned char>(text[d])) ||
                            text[d] == '_' || text[d] == 'x' || text[d] == 'X' ||
                            text[d] == 'z' || text[d] == 'Z')) ++d;
                    std::string digits = text.substr(digits_start, d - digits_start);
                    std::string clean;
                    for (char c : digits) {
                        if (c != '_') clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    }
                    v = parse_radix(clean, base, width);
                    pos = d;
                    return v;
                }
            }
        }
        (void)save;
        // plain decimal
        std::string num;
        while (pos < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[pos]))) {
            num.push_back(text[pos++]);
        }
        if (num.empty()) {
            error = "expected expression, found: '" + text.substr(pos) + "'";
            return v;
        }
        uint64_t n = strtoull(num.c_str(), nullptr, 10);
        return logic_value_from_u64(n, 0);
    }

    static LogicValue parse_radix(const std::string& digits, char base, int width) {
        // digits already lowercased, no underscores
        bool unknown = false, has_x = false, has_z = false;
        for (char c : digits) {
            if (c == 'x' || c == 'z') { unknown = true; if (c == 'x') has_x = true; else has_z = true; }
        }
        LogicValue v;
        v.width = width;
        if (unknown) {
            // build bit string of the requested width with x/z
            std::string bits;
            if (base == 'b') {
                for (char c : digits) bits.push_back(c);
            } else if (base == 'h') {
                for (char c : digits) {
                    if (c == 'x' || c == 'z') { bits += std::string(4, c); }
                    else {
                        int nib = (c >= 'a') ? (c - 'a' + 10) : (c - '0');
                        for (int i = 3; i >= 0; --i) bits.push_back(((nib >> i) & 1) ? '1' : '0');
                    }
                }
            } else if (base == 'o') {
                for (char c : digits) {
                    if (c == 'x' || c == 'z') { bits += std::string(3, c); }
                    else {
                        int oct = c - '0';
                        for (int i = 2; i >= 0; --i) bits.push_back(((oct >> i) & 1) ? '1' : '0');
                    }
                }
            } else {
                bits = digits;
            }
            if (width > 0 && (int)bits.size() > width) bits = bits.substr(bits.size() - width);
            v.bits = bits;
            v.known = false;
            v.has_x = has_x;
            v.has_z = has_z;
            return v;
        }
        // known value
        uint64_t value = 0;
        if (base == 'b') {
            for (char c : digits) value = (value << 1) | (c == '1' ? 1 : 0);
        } else if (base == 'h') {
            for (char c : digits) {
                value <<= 4;
                value |= (c >= 'a') ? (c - 'a' + 10) : (c - '0');
            }
        } else if (base == 'o') {
            for (char c : digits) {
                value <<= 3;
                value |= (c - '0');
            }
        } else {
            value = strtoull(digits.c_str(), nullptr, 10);
        }
        return logic_value_from_u64(value, width);
    }

    ExprNode* parse_primary() {
        skip_ws();
        if (eat('(')) {
            ExprNode* inner = parse_logical();
            if (!eat(')')) { error = "missing ')'"; delete inner; return nullptr; }
            return inner;
        }
        if (peek() == '~' || peek() == '!' || peek() == '-') {
            char op = peek();
            ++pos;
            auto* n = new ExprNode;
            n->kind = ExprNode::Kind::Unary;
            n->op = std::string(1, op);
            n->right = parse_primary();
            if (!n->right) { delete n; return nullptr; }
            return n;
        }
        // signal or constant?
        size_t save = pos;
        std::string id = parse_identifier();
        if (!id.empty()) {
            // signal reference with optional slice
            auto* n = new ExprNode;
            n->kind = ExprNode::Kind::Signal;
            n->signal = id;
            if (peek() == '[') {
                ++pos;
                std::string lo, hi;
                std::string first;
                while (pos < text.size() && text[pos] != ':' && text[pos] != ']') {
                    first.push_back(text[pos++]);
                }
                if (peek() == ':') {
                    ++pos;
                    hi = first;
                    while (pos < text.size() && text[pos] != ']') lo.push_back(text[pos++]);
                } else {
                    lo = first;  // single bit
                }
                if (!eat(']')) { error = "missing ']'"; delete n; return nullptr; }
                int l = atoi(lo.c_str());
                int h = hi.empty() ? l : atoi(hi.c_str());
                n->kind = ExprNode::Kind::Slice;
                n->msb = h;
                n->lsb = l;
            }
            return n;
        }
        pos = save;
        LogicValue c = parse_const();
        if (c.bits.empty() && !error.empty()) return nullptr;
        auto* n = new ExprNode;
        n->kind = ExprNode::Kind::Const;
        n->value = c;
        return n;
    }

    ExprNode* parse_mul() {
        ExprNode* left = parse_primary();
        while (left) {
            char op = peek();
            if (op == '*' || op == '/' || op == '%') {
                ++pos;
                ExprNode* right = parse_primary();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary;
                n->op = std::string(1, op);
                n->left = left;
                n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse_add() {
        ExprNode* left = parse_mul();
        while (left) {
            char op = peek();
            if (op == '+' || op == '-') {
                ++pos;
                ExprNode* right = parse_mul();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary;
                n->op = std::string(1, op);
                n->left = left;
                n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse_shift() {
        ExprNode* left = parse_add();
        while (left) {
            skip_ws();
            if (pos + 1 < text.size() && text[pos] == '<' && text[pos + 1] == '<') {
                pos += 2;
                ExprNode* right = parse_add();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = "<<";
                n->left = left; n->right = right;
                left = n;
            } else if (pos + 1 < text.size() && text[pos] == '>' && text[pos + 1] == '>') {
                pos += 2;
                ExprNode* right = parse_add();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = ">>";
                n->left = left; n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse_cmp() {
        ExprNode* left = parse_shift();
        while (left) {
            skip_ws();
            std::string op;
            if (pos + 1 < text.size() && text[pos] == '<' && text[pos + 1] == '=') { op = "<="; pos += 2; }
            else if (pos + 1 < text.size() && text[pos] == '>' && text[pos + 1] == '=') { op = ">="; pos += 2; }
            else if (peek() == '<') { op = "<"; ++pos; }
            else if (peek() == '>') { op = ">"; ++pos; }
            else break;
            ExprNode* right = parse_shift();
            if (!right) { delete left; return nullptr; }
            auto* n = new ExprNode;
            n->kind = ExprNode::Kind::Binary; n->op = op;
            n->left = left; n->right = right;
            left = n;
        }
        return left;
    }

    ExprNode* parse_eq() {
        ExprNode* left = parse_cmp();
        while (left) {
            skip_ws();
            std::string op;
            if (pos + 3 < text.size() && text[pos] == '=' && text[pos + 1] == '=' &&
                text[pos + 2] == '?' &&
                (text[pos + 3] == 'z' || text[pos + 3] == 'x'
                 || text[pos + 3] == 'i')) {
                op = text.substr(pos, 4);
                pos += 4;
            } else if (pos + 1 < text.size() && text[pos] == '=' && text[pos + 1] == '=' &&
                pos + 2 < text.size() && text[pos + 2] == '=') { op = "==="; pos += 3; }
            else if (pos + 1 < text.size() && text[pos] == '!' && text[pos + 1] == '=' &&
                     pos + 2 < text.size() && text[pos + 2] == '=') { op = "!=="; pos += 3; }
            else if (pos + 1 < text.size() && text[pos] == '=' && text[pos + 1] == '=') { op = "=="; pos += 2; }
            else if (pos + 1 < text.size() && text[pos] == '!' && text[pos + 1] == '=') { op = "!="; pos += 2; }
            else break;
            ExprNode* right = parse_cmp();
            if (!right) { delete left; return nullptr; }
            auto* n = new ExprNode;
            n->kind = ExprNode::Kind::Binary; n->op = op;
            n->left = left; n->right = right;
            left = n;
        }
        return left;
    }

    ExprNode* parse_and() {
        ExprNode* left = parse_eq();
        while (left) {
            skip_ws();
            if (peek() == '&' && pos + 1 < text.size() && text[pos + 1] != '&') {
                ++pos;
                ExprNode* right = parse_eq();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = "&";
                n->left = left; n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse_xor() {
        ExprNode* left = parse_and();
        while (left) {
            skip_ws();
            if (peek() == '^') {
                ++pos;
                ExprNode* right = parse_and();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = "^";
                n->left = left; n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse_or() {
        ExprNode* left = parse_xor();
        while (left) {
            skip_ws();
            if (peek() == '|' && pos + 1 < text.size() && text[pos + 1] != '|') {
                ++pos;
                ExprNode* right = parse_xor();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = "|";
                n->left = left; n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse_logical() {
        ExprNode* left = parse_or();
        while (left) {
            skip_ws();
            if (pos + 1 < text.size() && text[pos] == '&' && text[pos + 1] == '&') {
                pos += 2;
                ExprNode* right = parse_or();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = "&&";
                n->left = left; n->right = right;
                left = n;
            } else if (pos + 1 < text.size() && text[pos] == '|' && text[pos + 1] == '|') {
                pos += 2;
                ExprNode* right = parse_or();
                if (!right) { delete left; return nullptr; }
                auto* n = new ExprNode;
                n->kind = ExprNode::Kind::Binary; n->op = "||";
                n->left = left; n->right = right;
                left = n;
            } else break;
        }
        return left;
    }

    ExprNode* parse() {
        ExprNode* root = parse_logical();
        if (!root) return nullptr;
        if (!at_end()) {
            error = "unexpected trailing input: '" + text.substr(pos) + "'";
            delete root;
            return nullptr;
        }
        return root;
    }
};

// ── Evaluation helpers ──

LogicValue bitwise(const LogicValue& a, const LogicValue& b, const char* op) {
    // widths: use the wider of the two (or the fixed width if present)
    int w = std::max(a.width > 0 ? a.width : static_cast<int>(a.bits.size()),
                     b.width > 0 ? b.width : static_cast<int>(b.bits.size()));
    std::string ba = a.bits, bb = b.bits;
    if ((int)ba.size() < w) ba.insert(ba.begin(), w - ba.size(), a.known ? '0' : 'x');
    if ((int)bb.size() < w) bb.insert(bb.begin(), w - bb.size(), b.known ? '0' : 'x');
    if ((int)ba.size() > w) ba = ba.substr(ba.size() - w);
    if ((int)bb.size() > w) bb = bb.substr(bb.size() - w);
    std::string out;
    out.reserve(w);
    bool known = true, has_x = false, has_z = false;
    for (int i = 0; i < w; ++i) {
        char ca = ba[i], cb = bb[i];
        bool ua = (ca != '0' && ca != '1'), ub = (cb != '0' && cb != '1');
        char r = '0';
        if (ua || ub) {
            r = 'x';
            known = false;
            if (ca == 'z' || cb == 'z') has_z = true; else has_x = true;
        } else {
            int va = ca - '0', vb = cb - '0';
            int v;
            if (std::string(op) == "&") v = va & vb;
            else if (std::string(op) == "|") v = va | vb;
            else v = va ^ vb;
            r = v ? '1' : '0';
        }
        out.push_back(r);
    }
    LogicValue v;
    v.bits = out;
    v.width = w;
    v.known = known;
    v.has_x = has_x;
    v.has_z = has_z;
    return v;
}

enum class LogicalTruth { False, True, Unknown };

LogicalTruth logical_truth(const LogicValue& value) {
    bool saw_unknown = false;
    for (const char bit : value.bits) {
        if (bit == '1') return LogicalTruth::True;
        if (bit != '0') saw_unknown = true;
    }
    return saw_unknown ? LogicalTruth::Unknown : LogicalTruth::False;
}

LogicValue unknown_bool() {
    LogicValue value;
    value.bits = "x";
    value.width = 1;
    value.known = false;
    value.has_x = true;
    return value;
}

LogicValue from_bool(bool b, int w = 1) {
    return logic_value_from_u64(b ? 1 : 0, w);
}

uint64_t to_u64(const LogicValue& v) {
    uint64_t acc = 0;
    for (char c : v.bits) acc = (acc << 1) | (c == '1' ? 1 : 0);
    return acc;
}

LogicValue arith(const LogicValue& a, const LogicValue& b, const std::string& op) {
    // Prefer declared widths; only fall back to bit length when neither side
    // carries a width (e.g. a bare decimal constant).
    int w = 0;
    if (a.width > 0 && b.width > 0) w = std::max(a.width, b.width);
    else if (a.width > 0) w = a.width;
    else if (b.width > 0) w = b.width;
    else w = std::max(static_cast<int>(a.bits.size()),
                      static_cast<int>(b.bits.size()));
    if (!a.known || !b.known) {
        LogicValue v;
        v.width = w;
        v.bits.assign(std::max(1, w), 'x');
        v.known = false;
        v.has_x = true;
        return v;
    }
    uint64_t va = to_u64(a), vb = to_u64(b);
    uint64_t r = 0;
    if (op == "+") r = va + vb;
    else if (op == "-") r = va - vb;
    else if (op == "*") r = va * vb;
    else if (op == "/") r = vb == 0 ? 0 : va / vb;
    else if (op == "%") r = vb == 0 ? 0 : va % vb;
    else if (op == "<<") r = va << (vb & 63);
    else if (op == ">>") r = va >> (vb & 63);
    return logic_value_from_u64(r, w);
}

LogicValue cmp(const LogicValue& a, const LogicValue& b, const std::string& op) {
    if (!a.known || !b.known) {
        LogicValue v;
        v.width = 1;
        v.bits = "x";
        v.known = false;
        v.has_x = true;
        return v;
    }
    uint64_t va = to_u64(a), vb = to_u64(b);
    bool r = false;
    if (op == "<") r = va < vb;
    else if (op == "<=") r = va <= vb;
    else if (op == ">") r = va > vb;
    else if (op == ">=") r = va >= vb;
    return from_bool(r);
}

LogicValue eq(const LogicValue& a, const LogicValue& b, const std::string& op) {
    bool strict = (op == "===" || op == "!==");
    bool equal = true;
    if (!a.known || !b.known) {
        if (strict) {
            equal = (a.bits == b.bits) && (a.known == b.known);
        } else {
            LogicValue v;
            v.width = 1;
            v.bits = "x";
            v.known = false;
            v.has_x = true;
            return v;
        }
    } else {
        equal = (a.bits == b.bits);
    }
    bool r = (op == "!=" || op == "!==") ? !equal : equal;
    return from_bool(r);
}

LogicValue wildcard_case_eq(const LogicValue& a, const LogicValue& b,
                            const std::string& op) {
    const int width = std::max(
        a.width > 0 ? a.width : static_cast<int>(a.bits.size()),
        b.width > 0 ? b.width : static_cast<int>(b.bits.size()));
    const auto extend = [width](const LogicValue& value) {
        std::string bits = value.bits;
        if (static_cast<int>(bits.size()) > width) {
            bits = bits.substr(bits.size() - width);
        } else if (static_cast<int>(bits.size()) < width) {
            char fill = '0';
            if (!value.known && !bits.empty()
                && (bits.front() == 'x' || bits.front() == 'z')) {
                fill = bits.front();
            }
            bits.insert(bits.begin(), width - bits.size(), fill);
        }
        return bits;
    };
    const std::string lhs = extend(a);
    const std::string rhs = extend(b);
    const bool casex = op == "==?x";
    const bool inside = op == "==?i";
    const auto wildcard = [casex](char bit) {
        return bit == 'z' || (casex && bit == 'x');
    };
    for (int i = 0; i < width; ++i) {
        if ((!inside && wildcard(lhs[i])) || wildcard(rhs[i])) continue;
        if (lhs[i] != rhs[i]) return from_bool(false);
    }
    return from_bool(true);
}

LogicValue reduce(const LogicValue& a, const std::string& op) {
    // unary bitwise reduction on the whole vector
    if (op == "!") {
        const LogicalTruth truth = logical_truth(a);
        if (truth == LogicalTruth::Unknown) return unknown_bool();
        return from_bool(truth == LogicalTruth::False);
    }
    if (!a.known) {
        LogicValue v;
        v.width = 1;
        v.bits = "x";
        v.known = false;
        v.has_x = true;
        return v;
    }
    int r = -1;
    for (char c : a.bits) {
        int b = (c == '1') ? 1 : 0;
        if (op == "~") { r = (r < 0) ? b : r ^ b; }
        else if (op == "&") { r = (r < 0) ? b : r & b; }
        else if (op == "|") { r = (r < 0) ? b : r | b; }
        else if (op == "^") { r = (r < 0) ? b : r ^ b; }
    }
    if (r < 0) r = 0;
    if (op == "~") {
        // unary ~ is bitwise NOT, not reduction
        std::string out;
        for (char c : a.bits) out.push_back(c == '0' ? '1' : '0');
        LogicValue v;
        v.bits = out;
        v.width = a.width;
        return v;
    }
    return from_bool(r != 0);
}

} // namespace

// ── Public API ──

ExprNode* parse_expression(const std::string& text, std::string& error) {
    Parser p(text);
    ExprNode* root = p.parse();
    error = p.error;
    return root;
}

LogicValue eval_expression(const ExprNode* root, const IWaveformBackend& wf,
                           uint32_t time_idx,
                           std::map<std::string, LogicValue>* samples,
                           IWaveformBackend::ObservationPoint point) {
    if (!root) return LogicValue{};
    switch (root->kind) {
        case ExprNode::Kind::Const:
            return root->value;
        case ExprNode::Kind::Signal:
        case ExprNode::Kind::Slice: {
            std::string key = root->signal;
            if (root->kind == ExprNode::Kind::Slice) {
                key += "[" + std::to_string(root->msb) + ":" + std::to_string(root->lsb) + "]";
            }
            if (samples) {
                auto it = samples->find(key);
                if (it != samples->end()) return it->second;
            }
            uint32_t ref = wf.find_signal(root->signal);
            LogicValue v;
            if (ref) {
                IWaveformBackend::SampledValue sampled;
                if (wf.sampled_value_at(ref, time_idx, point, sampled)) {
                    std::string bits = sampled.value.text;
                    IWaveformBackend::SignalInfo info;
                    wf.signal_info(ref, info);
                    v = logic_value_from_bits(bits, static_cast<int>(info.width));
                    if (root->kind == ExprNode::Kind::Slice) {
                        // extract [msb:lsb] — bits string is MSB-first
                        int w = static_cast<int>(bits.size());
                        int msb = root->msb, lsb = root->lsb;
                        if (msb < lsb) std::swap(msb, lsb);
                        if (msb >= w) msb = w - 1;
                        if (lsb < 0) lsb = 0;
                        std::string out;
                        for (int i = msb; i >= lsb; --i) {
                            int idx = w - 1 - i;
                            out.push_back(idx >= 0 && idx < w ? bits[idx] : '0');
                        }
                        v.bits = out;
                        v.width = msb - lsb + 1;
                    }
                } else {
                    v.bits = std::string(
                        root->kind == ExprNode::Kind::Slice ? (root->msb - root->lsb + 1) : 1, 'x');
                    v.known = false;
                    v.has_x = true;
                }
            } else {
                v.bits = "x";
                v.known = false;
                v.has_x = true;
            }
            if (samples) (*samples)[key] = v;
            return v;
        }
        case ExprNode::Kind::Unary: {
            LogicValue r = eval_expression(root->right, wf, time_idx, samples, point);
            if (root->op == "~") {
                if (!r.known) {
                    LogicValue v;
                    v.width = r.width;
                    v.bits.assign(std::max(1, (int)r.bits.size()), 'x');
                    v.known = false;
                    v.has_x = true;
                    return v;
                }
                std::string out;
                for (char c : r.bits) out.push_back(c == '0' ? '1' : '0');
                LogicValue v;
                v.bits = out;
                v.width = r.width;
                return v;
            }
            if (root->op == "-") {
                if (!r.known) return r;
                LogicValue neg = logic_value_from_u64(0, r.width);
                return arith(neg, r, "-");
            }
            return reduce(r, root->op);  // '!'
        }
        case ExprNode::Kind::Binary: {
            LogicValue l = eval_expression(root->left, wf, time_idx, samples, point);
            LogicValue r = eval_expression(root->right, wf, time_idx, samples, point);
            const std::string& op = root->op;
            if (op == "&" || op == "|" || op == "^") return bitwise(l, r, op.c_str());
            if (op == "&&") {
                const LogicalTruth left = logical_truth(l);
                const LogicalTruth right = logical_truth(r);
                if (left == LogicalTruth::False || right == LogicalTruth::False)
                    return from_bool(false);
                if (left == LogicalTruth::True && right == LogicalTruth::True)
                    return from_bool(true);
                return unknown_bool();
            }
            if (op == "||") {
                const LogicalTruth left = logical_truth(l);
                const LogicalTruth right = logical_truth(r);
                if (left == LogicalTruth::True || right == LogicalTruth::True)
                    return from_bool(true);
                if (left == LogicalTruth::False && right == LogicalTruth::False)
                    return from_bool(false);
                return unknown_bool();
            }
            if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
                op == "<<" || op == ">>") return arith(l, r, op);
            if (op == "<" || op == "<=" || op == ">" || op == ">=") return cmp(l, r, op);
            if (op == "==" || op == "!=" || op == "===" || op == "!==") return eq(l, r, op);
            if (op == "==?z" || op == "==?x" || op == "==?i")
                return wildcard_case_eq(l, r, op);
            return LogicValue{};
        }
    }
    return LogicValue{};
}

bool expr_eval_at(const std::string& text, const IWaveformBackend& wf,
                  uint32_t time_idx, LogicValue& out, std::string& error) {
    ExprNode* root = parse_expression(text, error);
    if (!root) return false;
    out = eval_expression(root, wf, time_idx);
    delete root;
    return true;
}

std::vector<std::string> expression_signals(const ExprNode* root) {
    std::vector<std::string> out;
    if (!root) return out;
    std::set<std::string> seen;
    std::function<void(const ExprNode*)> walk = [&](const ExprNode* n) {
        if (!n) return;
        if (n->kind == ExprNode::Kind::Signal || n->kind == ExprNode::Kind::Slice) {
            if (!seen.count(n->signal)) {
                seen.insert(n->signal);
                out.push_back(n->signal);
            }
        }
        walk(n->left);
        walk(n->right);
    };
    walk(root);
    return out;
}

} // namespace xdebug_fst
