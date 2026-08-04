// design_actions.cpp — signal.canonicalize, expr.normalize (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"
#include "core/value/logic_value.h"
#include "waveform/expr/expr_eval.h"

#include <memory>
#include <string>
#include <cstdlib>

namespace xdebug_fst {

// ── signal.canonicalize ──

struct SignalCanonicalizeHandler : public EngineActionHandler {
    const char* action_name() const override { return "signal.canonicalize"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_design || !g.design) {
            return Json{{"ok", false},
                        {"error", {{"code", "DESIGN_NOT_LOADED"},
                                   {"message", "action requires design database: signal.canonicalize"}}}};
        }
        auto args = req.value("args", Json::object());
        std::string sig = args.value("signal", "");
        if (sig.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.signal is required"}}}};
        }

        int idx = g.design->resolve(sig.c_str());
        Json data;
        data["input"] = sig;

        if (idx >= 0) {
            data["canonical"] = g.design->signal_name(idx) ? g.design->signal_name(idx) : sig;
            data["index"] = idx;
            const char* ty = g.design->signal_type(idx);
            data["type"] = ty ? ty : "";
            data["width"] = g.design->signal_width(idx);
            data["resolved"] = true;
        } else {
            data["canonical"] = sig;
            data["resolved"] = false;
        }

        return Json{{"ok", true}, {"data", data}};
    }
};

// ── expr.normalize helper ──

static std::string serialize_expr(const ExprNode* n) {
    if (!n) return "";
    switch (n->kind) {
    case ExprNode::Kind::Signal:
        return n->signal;
    case ExprNode::Kind::Slice:
        return n->signal + "[" + std::to_string(n->msb) + ":" + std::to_string(n->lsb) + "]";
    case ExprNode::Kind::Const:
        return sv_literal(n->value, 'h');
    case ExprNode::Kind::Unary: {
        std::string operand = serialize_expr(n->left);
        // For unary operators that are alphanumeric (like "not"), add a space
        bool need_space = !n->op.empty() && std::isalpha(static_cast<unsigned char>(n->op[0]));
        return n->op + (need_space ? " " : "") + operand;
    }
    case ExprNode::Kind::Binary: {
        std::string left = serialize_expr(n->left);
        std::string right = serialize_expr(n->right);
        return "(" + left + " " + n->op + " " + right + ")";
    }
    }
    return "";
}

// ── expr.normalize ──

struct ExprNormalizeHandler : public EngineActionHandler {
    const char* action_name() const override { return "expr.normalize"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto args = req.value("args", Json::object());
        std::string expr = args.value("expression", "");
        if (expr.empty()) {
            return Json{{"ok", false},
                        {"error", {{"code", "MISSING_FIELD"},
                                   {"message", "args.expression is required"}}}};
        }

        std::string parse_err;
        ExprNode* root = parse_expression(expr, parse_err);
        if (!root) {
            return Json{{"ok", false},
                        {"error", {{"code", "PARSE_ERROR"},
                                   {"message", parse_err.empty()
                                        ? "failed to parse expression: " + expr
                                        : parse_err}}}};
        }

        std::string normalized = serialize_expr(root);
        delete root;

        return Json{{"ok", true},
                    {"data", {{"input", expr}, {"normalized", normalized}}}};
    }
};

std::unique_ptr<EngineActionHandler> make_signal_canonicalize_handler() {
    return std::make_unique<SignalCanonicalizeHandler>();
}
std::unique_ptr<EngineActionHandler> make_expr_normalize_handler() {
    return std::make_unique<ExprNormalizeHandler>();
}

} // namespace xdebug_fst
