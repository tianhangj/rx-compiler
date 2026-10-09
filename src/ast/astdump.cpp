#include "node.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

namespace rx {
namespace {

void print(int indent, std::string_view value) {
    std::cout << std::string(std::max(0, indent), ' ') << value << '\n';
}

std::string quote(const std::string& value) {
    std::ostringstream output;
    output << std::quoted(value);
    return output.str();
}

void stringField(int indent, std::string_view name, const std::string& value) {
    print(indent, std::string(name) + ": " + quote(value));
}

void flag(int indent, std::string_view name, bool value) {
    print(indent, std::string(name) + ": " + (value ? "true" : "false"));
}

void list(int indent, std::string_view name, std::size_t size) {
    print(indent, std::string(name) + ": [" + std::to_string(size) + "]");
}

template<class T>
void child(int indent, std::string_view name, const std::unique_ptr<T>& node) {
    if (!node) {
        print(indent, std::string(name) + ": <none>");
        return;
    }
    print(indent, std::string(name) + ":");
    node->dump(indent + 2);
}

template<class T>
void children(int indent, std::string_view name,
              const std::vector<std::unique_ptr<T>>& nodes) {
    list(indent, name, nodes.size());
    for (const auto& node : nodes) {
        if (node) node->dump(indent + 2);
        else print(indent + 2, "<none>");
    }
}

void strings(int indent, std::string_view name, const std::vector<std::string>& values) {
    list(indent, name, values.size());
    for (const auto& value : values) print(indent + 2, quote(value));
}

void dumpPathSegment(int indent, const PathSegment& segment) {
    print(indent, "PathSegment");
    stringField(indent + 2, "name", segment.name);
    children(indent + 2, "generic_arguments", segment.generic_arguments);
}

void dumpPath(int indent, const Path& path) {
    print(indent, "path:");
    list(indent + 2, "segments", path.size());
    for (const auto& segment : path) dumpPathSegment(indent + 4, segment);
}

void dumpBinding(int indent, const Binding& binding) {
    print(indent, "binding:");
    stringField(indent + 2, "name", binding.name);
    flag(indent + 2, "is_mutable", binding.is_mutable);
}

} // namespace

void PathTypeNode::dump(int indent) {
    print(indent, "PathTypeNode");
    dumpPath(indent + 2, path);
}

void ReferenceTypeNode::dump(int indent) {
    print(indent, "ReferenceTypeNode");
    flag(indent + 2, "is_mutable", is_mutable);
    child(indent + 2, "referenced_type", referenced_type);
}

void ArrayTypeNode::dump(int indent) {
    print(indent, "ArrayTypeNode");
    child(indent + 2, "element_type", element_type);
    child(indent + 2, "length", length);
}

void UnitTypeNode::dump(int indent) {
    print(indent, "UnitTypeNode");
}

void LetStmtNode::dump(int indent) {
    print(indent, "LetStmtNode");
    dumpBinding(indent + 2, binding);
    child(indent + 2, "type", type);
    child(indent + 2, "initializer", initializer);
}

void ExprStmtNode::dump(int indent) {
    print(indent, "ExprStmtNode");
    flag(indent + 2, "requires_unit", requires_unit);
    child(indent + 2, "expression", expression);
}

void IntegerLiteralExprNode::dump(int indent) {
    print(indent, "IntegerLiteralExprNode");
    stringField(indent + 2, "text", text);
}

void BoolLiteralExprNode::dump(int indent) {
    print(indent, "BoolLiteralExprNode");
    flag(indent + 2, "value", value);
}

void PathExprNode::dump(int indent) {
    print(indent, "PathExprNode");
    dumpPath(indent + 2, path);
}

void UnitExprNode::dump(int indent) {
    print(indent, "UnitExprNode");
}

void UnaryExprNode::dump(int indent) {
    print(indent, "UnaryExprNode");
    stringField(indent + 2, "op", op);
    child(indent + 2, "operand", operand);
}

void BinaryExprNode::dump(int indent) {
    print(indent, "BinaryExprNode");
    stringField(indent + 2, "op", op);
    child(indent + 2, "left", left);
    child(indent + 2, "right", right);
}

void AssignExprNode::dump(int indent) {
    print(indent, "AssignExprNode");
    stringField(indent + 2, "op", op);
    child(indent + 2, "target", target);
    child(indent + 2, "value", value);
}

void CastExprNode::dump(int indent) {
    print(indent, "CastExprNode");
    child(indent + 2, "expression", expression);
    child(indent + 2, "type", type);
}

void CallExprNode::dump(int indent) {
    print(indent, "CallExprNode");
    child(indent + 2, "callee", callee);
    children(indent + 2, "arguments", arguments);
}

void MethodCallExprNode::dump(int indent) {
    print(indent, "MethodCallExprNode");
    child(indent + 2, "receiver", receiver);
    print(indent + 2, "method:");
    dumpPathSegment(indent + 4, method);
    children(indent + 2, "arguments", arguments);
}

void FieldExprNode::dump(int indent) {
    print(indent, "FieldExprNode");
    child(indent + 2, "object", object);
    stringField(indent + 2, "field", field);
}

void IndexExprNode::dump(int indent) {
    print(indent, "IndexExprNode");
    child(indent + 2, "array", array);
    child(indent + 2, "index", index);
}

void StructExprNode::dump(int indent) {
    print(indent, "StructExprNode");
    dumpPath(indent + 2, path);
    list(indent + 2, "fields", fields.size());
    for (const auto& field : fields) {
        print(indent + 4, "StructFieldInitializer");
        stringField(indent + 6, "name", field.name);
        child(indent + 6, "value", field.value);
    }
}

void ArrayExprNode::dump(int indent) {
    print(indent, "ArrayExprNode");
    children(indent + 2, "elements", elements);
}

void RepeatArrayExprNode::dump(int indent) {
    print(indent, "RepeatArrayExprNode");
    child(indent + 2, "value", value);
    child(indent + 2, "count", count);
}

void BlockExprNode::dump(int indent) {
    print(indent, "BlockExprNode");
    children(indent + 2, "statements", statements);
    child(indent + 2, "return_value", return_value);
}

void IfExprNode::dump(int indent) {
    print(indent, "IfExprNode");
    child(indent + 2, "condition", condition);
    child(indent + 2, "then_branch", then_branch);
    child(indent + 2, "else_branch", else_branch);
}

void LoopExprNode::dump(int indent) {
    print(indent, "LoopExprNode");
    child(indent + 2, "condition", condition);
    child(indent + 2, "body", body);
}

void BreakExprNode::dump(int indent) {
    print(indent, "BreakExprNode");
    child(indent + 2, "value", value);
}

void ReturnExprNode::dump(int indent) {
    print(indent, "ReturnExprNode");
    child(indent + 2, "value", value);
}

void ContinueExprNode::dump(int indent) {
    print(indent, "ContinueExprNode");
}

void FunctionDeclNode::dump(int indent) {
    print(indent, "FunctionDeclNode");
    stringField(indent + 2, "name", name);
    if (self_parameter) {
        print(indent + 2, "self_parameter:");
        flag(indent + 4, "by_reference", self_parameter->by_reference);
        flag(indent + 4, "is_mutable", self_parameter->is_mutable);
    } else {
        print(indent + 2, "self_parameter: <none>");
    }
    list(indent + 2, "parameters", parameters.size());
    for (const auto& parameter : parameters) {
        print(indent + 4, "FunctionParameter");
        dumpBinding(indent + 6, parameter.binding);
        child(indent + 6, "type", parameter.type);
    }
    child(indent + 2, "return_type", return_type);
    child(indent + 2, "body", body);
}

void StructDeclNode::dump(int indent) {
    print(indent, "StructDeclNode");
    stringField(indent + 2, "name", name);
    strings(indent + 2, "derives", derives);
    list(indent + 2, "fields", fields.size());
    for (const auto& field : fields) {
        print(indent + 4, "StructField");
        stringField(indent + 6, "name", field.name);
        child(indent + 6, "type", field.type);
    }
}

void ConstDeclNode::dump(int indent) {
    print(indent, "ConstDeclNode");
    stringField(indent + 2, "name", name);
    child(indent + 2, "type", type);
    child(indent + 2, "value", value);
}

void ImplDeclNode::dump(int indent) {
    print(indent, "ImplDeclNode");
    child(indent + 2, "type", type);
    children(indent + 2, "items", items);
}

void ProgramNode::dump(int indent) {
    print(indent, "ProgramNode");
    children(indent + 2, "declarations", declarations);
}

} // namespace rx
