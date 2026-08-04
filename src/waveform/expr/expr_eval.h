// expr_eval.h — 4-state expression evaluation over waveform signals (BSD-3-Clause)
#pragma once

#include "backend/waveform_backend.h"
#include "core/value/logic_value.h"

#include <map>
#include <string>
#include <vector>

namespace xdebug_fst {

/// A parsed expression tree node.
struct ExprNode {
    enum class Kind { Signal, Slice, Const, Unary, Binary };
    Kind kind = Kind::Const;
    std::string op;              // unary/binary operator
    std::string signal;          // signal path for Signal/Slice
    int msb = -1, lsb = -1;      // Slice bounds (msb>=lsb when present)
    LogicValue value;            // Const
    ExprNode* left = nullptr;
    ExprNode* right = nullptr;
    ~ExprNode() { delete left; delete right; }
};

/// Parse an expression like "a[7:0] == 8'h05 && b != 0".
/// Returns nullptr on parse error (with message in `error`).
ExprNode* parse_expression(const std::string& text, std::string& error);

/// Evaluate an expression at a time index against the waveform backend.
/// `samples` may be used to memoize signal values (optional; pass nullptr).
LogicValue eval_expression(const ExprNode* root, const IWaveformBackend& wf,
                           uint32_t time_idx,
                           std::map<std::string, LogicValue>* samples = nullptr);

/// Convenience: parse + evaluate at a time index. Returns false on error.
bool expr_eval_at(const std::string& text, const IWaveformBackend& wf,
                  uint32_t time_idx, LogicValue& out, std::string& error);

/// All signal names referenced by an expression (for pre-loading).
std::vector<std::string> expression_signals(const ExprNode* root);

} // namespace xdebug_fst
