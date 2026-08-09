// design_actions.cpp — signal.canonicalize, expr.normalize (BSD-3-Clause)
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "core/value/logic_value.h"
#include "waveform/expr/expr_eval.h"

#include <memory>
#include <string>
#include <vector>

namespace xdebug_fst {
namespace {

Json failure(const char* code, const std::string& message) {
    return Json{{"ok", false},
                {"error", {{"code", code}, {"message", message}}}};
}

std::pair<std::string, std::string> split_signal(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return {"", path};
    return {path.substr(0, dot), path.substr(dot + 1)};
}

struct SignalCanonicalizeHandler final : EngineActionHandler {
    const char* action_name() const override { return "signal.canonicalize"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        auto& design = *engine_globals().design;
        const std::string query = req.at("args").at("signal").get<std::string>();
        const int index = design.resolve(query.c_str());
        if (index < 0) {
            return failure("SIGNAL_NOT_FOUND", "signal not found: " + query);
        }
        const std::string resolved = design.signal_name(index)
            ? design.signal_name(index) : query;
        const auto [scope, leaf] = split_signal(resolved);
        std::vector<IDesignBackend::PortConnection> connections;
        design.port_connections(index, connections);
        if (connections.size() > 1) {
            return failure("AMBIGUOUS_SIGNAL",
                           "signal has multiple static port connections: " + query);
        }

        Json data{{"resolved_path", resolved},
                  {"connected_path", nullptr},
                  {"canonical_path", resolved},
                  {"mapping_kind", "identity"},
                  {"selection_basis", "unique_exact_design_match"},
                  {"scope", scope},
                  {"leaf", leaf},
                  {"connection", nullptr}};
        if (connections.size() == 1) {
            const auto& connection = connections.front();
            const char* connected_name =
                design.signal_name(connection.connected_signal);
            if (connected_name && *connected_name) {
                const int direction = design.signal_direction(index);
                data["connected_path"] = connected_name;
                data["canonical_path"] = connected_name;
                data["mapping_kind"] = "static_port_connection";
                data["connection"] = {
                    {"instance", scope},
                    {"port", leaf},
                    {"direction", direction == 2 ? "output" : "input_or_inout"},
                    {"evidence", "npi_static_port_connection"}};
            }
        }
        Json summary{{"status", "found"},
                     {"query", query},
                     {"match_count", 1},
                     {"canonicalization_scope", "static_design_connectivity"}};
        return Json{{"ok", true}, {"summary", summary}, {"data", data}};
    }
};

std::string serialize_expr(const ExprNode* node) {
    if (!node) return "";
    switch (node->kind) {
    case ExprNode::Kind::Signal:
        return node->signal;
    case ExprNode::Kind::Slice:
        return node->signal + "[" + std::to_string(node->msb) + ":" +
               std::to_string(node->lsb) + "]";
    case ExprNode::Kind::Const:
        return sv_literal(node->value, 'h');
    case ExprNode::Kind::Unary:
        return node->op + serialize_expr(node->right ? node->right : node->left);
    case ExprNode::Kind::Binary:
        return "(" + serialize_expr(node->left) + " " + node->op + " " +
               serialize_expr(node->right) + ")";
    }
    return "";
}

std::string schema_operator(const std::string& op) {
    if (op == "||") return "or";
    if (op == "&&") return "and";
    if (op == "!=") return "neq";
    if (op == "==") return "eq";
    if (op == ">=") return "ge";
    if (op == "<=") return "le";
    if (op == ">") return "gt";
    if (op == "<") return "lt";
    if (op == "+") return "add";
    if (op == "-") return "sub";
    if (op == "*") return "mul";
    return "";
}

Json expr_ast(const ExprNode* node) {
    if (!node) return Json{{"type", "unknown"}, {"text", ""}};
    switch (node->kind) {
    case ExprNode::Kind::Signal:
        return Json{{"type", "signal"}, {"name", node->signal}};
    case ExprNode::Kind::Slice:
        return Json{{"type", "unknown"}, {"text", serialize_expr(node)}};
    case ExprNode::Kind::Const:
        return Json{{"type", "const"}, {"value", sv_literal(node->value, 'h')}};
    case ExprNode::Kind::Unary:
        if (node->op == "!" || node->op == "~") {
            return Json{{"op", "not"},
                        {"args", Json::array({expr_ast(
                            node->right ? node->right : node->left)})}};
        }
        return Json{{"type", "unknown"}, {"text", serialize_expr(node)}};
    case ExprNode::Kind::Binary: {
        const std::string op = schema_operator(node->op);
        if (op.empty())
            return Json{{"type", "unknown"}, {"text", serialize_expr(node)}};
        return Json{{"op", op},
                    {"args", Json::array({expr_ast(node->left),
                                          expr_ast(node->right)})}};
    }
    }
    return Json{{"type", "unknown"}, {"text", serialize_expr(node)}};
}

struct ExprNormalizeHandler final : EngineActionHandler {
    const char* action_name() const override { return "expr.normalize"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& req) override {
        const Json& args = req.value("args", Json::object());
        if (args.contains("signal")) {
            const std::string signal = args.at("signal").get<std::string>();
            auto& globals = engine_globals();
            if (!globals.has_design || !globals.design)
                return failure("DESIGN_NOT_LOADED",
                               "action requires design database: expr.normalize");
            if (globals.design->resolve(signal.c_str()) < 0)
                return failure("SIGNAL_NOT_FOUND", "signal not found: " + signal);
            return Json{{"ok", true},
                        {"summary", {{"signal", signal},
                                     {"source", "npi_trace_assignment"},
                                     {"confidence", "unknown"}}},
                        {"data", {{"expr", Json::object()},
                                  {"assignment", Json::object()},
                                  {"rhs_signals", Json::array()}}}};
        }

        const std::string expression = args.at("expr").get<std::string>();
        std::string parse_error;
        std::unique_ptr<ExprNode> root(parse_expression(expression, parse_error));
        if (!root) {
            return failure("PARSE_ERROR", parse_error.empty()
                ? "failed to parse expression: " + expression : parse_error);
        }
        return Json{{"ok", true},
                    {"summary", {{"status", "parsed"},
                                 {"source", "deterministic_syntax_parser"},
                                 {"confidence", "syntax_validated"}}},
                    {"data", {{"expr", expr_ast(root.get())},
                              {"parsed", true},
                              {"confidence_reason",
                               "expression syntax was validated and parsed without design-resource semantics"}}}};
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_signal_canonicalize_handler() {
    return std::make_unique<SignalCanonicalizeHandler>();
}
std::unique_ptr<EngineActionHandler> make_expr_normalize_handler() {
    return std::make_unique<ExprNormalizeHandler>();
}

}  // namespace xdebug_fst
