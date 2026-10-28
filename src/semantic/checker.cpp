#include "checker.hpp"

#include <map>
#include <set>
#include <string>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <functional>

namespace rx {
namespace {

[[noreturn]] void fail(int line, const char* condition) {
    throw std::runtime_error("semantic check fail at line " + std::to_string(line) + ": " + condition);
}

#define require(condition) ((condition) ? static_cast<void>(0) : fail(__LINE__, #condition))

using Value = ValueInfo;

enum class ConstantState {
    Evaluating, Evaluated
};
struct LoopContext {
    const LoopExprNode* declaration;
    TypeId expected = InvalidTypeId;
};

class Checker {
public:
    SemanticModel run(const ProgramNode* program);
private:
    SemanticModel model;
    std::vector<std::map<std::string, BindingId>> scopes;
    std::map<const ConstDeclNode*, ConstantState> constant_states;
    std::vector<LoopContext> loop_stack;
    TypeId owner = InvalidTypeId;
    FunctionInfo* current_function = nullptr;

    TypeId merge(TypeId a, TypeId b) const;
    TypeId resolve(const TypeNode* node);
    TypeId resolveType(const TypeNode* node);
    TypeId resolveSegment(const PathSegment& segment);
    std::string valueName(const Path& path);
    void add(const DeclNode* node, TypeId enclosing);
    std::pair<TypeId, std::int64_t> constantExpr(const ExprNode* node, TypeId expected = {});
    std::pair<TypeId, std::int64_t> inferConstant(const ExprNode* node, TypeId expected);
    ConstantInfo& constant(const std::string& name);
    std::uint64_t length(const ExprNode* node);
    std::pair<TypeId, std::int64_t> literal(const IntegerLiteralExprNode* node, TypeId expected, bool negative = false);
    void layout(TypeId ty, std::set<std::string> visiting);
    Value expression(const ExprNode* node, TypeId expected = {});
    void adjust(const ExprNode* node, AdjustmentKind kind, TypeId target);
    void coerce(const ExprNode* node, TypeId target);
    void coerceBreaks(const LoopExprNode* node, TypeId target);
    Value infer(const ExprNode* node, TypeId expected);
    Value block(const BlockExprNode* node, TypeId expected);
    Value dereference(Value value);
    Value autoDeref(Value value, const ExprNode* node);
    Value binary(const std::string& op, Value left, Value right);
    Value call(const CallExprNode* node);
    Value method(const MethodCallExprNode* node);
    Value invoke(const FunctionInfo& fn, const std::vector<std::unique_ptr<ExprNode>>& args, std::size_t offset = 0);
    Value builtin(TypeId target, const std::string& name,
                  const std::vector<std::unique_ptr<ExprNode>>& args, bool method_call);
    void recordBuiltin(const ExprNode* node, TypeId target, const std::string& name, TypeId receiver = {});
    void expect(const Value& value, TypeId expected);
    BindingId binding(const Binding& binding, TypeId ty, bool parameter = false, bool self = false);
};

TypeId Checker::merge(TypeId a, TypeId b) const {
    if (!a) return b;
    return model.canCoerce(b, a) ? a : b;
}

TypeId Checker::resolveSegment(const PathSegment& segment) {
    std::string name = segment.name;
    if (name == "Self") {
        require(owner != InvalidTypeId);
        require(segment.generic_arguments.empty());
        return owner;
    } else if (name == "Box" || name == "Vec") {
        require(segment.generic_arguments.size() == 1);
        return model.internType(name, resolve(segment.generic_arguments[0].get()));
    } else {
        require(segment.generic_arguments.empty());
        TypeId t = model.internType(name);
        require(model.isInteger(t) || model.is(t, TypeKind::Bool) || model.structures.count(name));
        return t;
    }
}
TypeId Checker::resolve(const TypeNode* node) {
    TypeId result = resolveType(node);
    if (node) {
        model.types[node] = result;
    }
    return result;
}
TypeId Checker::resolveType(const TypeNode* node) {
    if (!node || dynamic_cast<const UnitTypeNode*>(node)) {
        return model.internType("()");
    } else if (auto* p = dynamic_cast<const PathTypeNode*>(node)) {
        require(p->path.size() == 1);
        return resolveSegment(p->path[0]);
    } else if (auto* r = dynamic_cast<const ReferenceTypeNode*>(node)) {
        return model.internType("&", resolve(r->referenced_type.get()), 0, r->is_mutable);
    } else if (auto* a = dynamic_cast<const ArrayTypeNode*>(node)) {
        TypeId element = resolve(a->element_type.get());
        return model.internType("[]", element, length(a->length.get()));
    }
    require(false);
}
std::string Checker::valueName(const Path& path) {
    require(path.size() <= 2);
    require(path.back().generic_arguments.empty());
    if (path.size() == 1) {
        return path[0].name;
    }
    return model.typeName(resolveSegment(path[0])) + "::" + path[1].name;
}
void Checker::add(const DeclNode* node, TypeId enclosing) {
    std::string prefix = (enclosing == InvalidTypeId) ? "" : model.typeName(enclosing) + "::";
    if (auto* fn = dynamic_cast<const FunctionDeclNode*>(node)) {
        std::string name = prefix + fn->name;
        require(!model.functions.count(name) && !model.constants.count(name));
        require(!fn->self_parameter || enclosing != InvalidTypeId);
        FunctionInfo& info = model.functions.emplace(name, FunctionInfo{fn, enclosing, {}, {}}).first->second;
        info.type = model.internType(SemanticType{TypeKind::Function, name});
    } else if (auto* c = dynamic_cast<const ConstDeclNode*>(node)) {
        std::string name = prefix + c->name;
        require(!model.functions.count(name) && !model.constants.count(name));
        model.constants.emplace(name, ConstantInfo{c, enclosing});
    }
}
std::pair<TypeId, std::int64_t> Checker::literal(const IntegerLiteralExprNode* node, TypeId expected, bool negative) {
    std::string text = node->text;
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    TypeId t = InvalidTypeId;
    for (const char* suffix : {"isize", "usize", "i32", "u32"}) {
        if (text.ends_with(suffix)) {
            t = model.internType(suffix);
            text.resize(text.size() - std::string(suffix).size());
            break;
        }
    }
    if (!t) {
        t = model.isInteger(expected) ? expected : model.internType("i32");
    }
    require(!negative || model.isSignedInteger(t));
    int base = 10;
    if (text.starts_with("0x")) { base = 16; text.erase(0, 2); }
    else if (text.starts_with("0o")) { base = 8; text.erase(0, 2); }
    else if (text.starts_with("0b")) { base = 2; text.erase(0, 2); }
    int64_t result = static_cast<std::int64_t>(std::stoull(text, nullptr, base));
    model.expressions[node] = ExpressionInfo{{t}, t, {}};
    model.constant_values[node] = result;
    return {t, negative ? -result : result};
}
ConstantInfo& Checker::constant(const std::string& name) {
    auto it = model.constants.find(name);
    require(it != model.constants.end());
    ConstantInfo& c = it->second;
    auto state = constant_states.find(c.declaration);
    require(state == constant_states.end() || state->second != ConstantState::Evaluating);
    if (state == constant_states.end()) {
        constant_states.emplace(c.declaration, ConstantState::Evaluating);
        TypeId saved_owner = owner;
        owner = c.owner;
        c.type = resolve(c.declaration->type.get());
        require(model.isInteger(c.type) || model.is(c.type, TypeKind::Bool));
        auto [t, value] = constantExpr(c.declaration->value.get(), c.type);
        require(t == c.type);
        c.value = value;
        constant_states.at(c.declaration) = ConstantState::Evaluated;
        owner = saved_owner;
    }
    return c;
}
std::pair<TypeId, std::int64_t> Checker::constantExpr(const ExprNode* node, TypeId expected) {
    auto result = inferConstant(node, expected);
    model.expressions[node] = ExpressionInfo{{result.first}, result.first, {}};
    model.constant_values[node] = result.second;
    return result;
}
std::pair<TypeId, std::int64_t> Checker::inferConstant(const ExprNode* node, TypeId expected) {
    if (auto* n = dynamic_cast<const IntegerLiteralExprNode*>(node)) {
        return literal(n, expected);
    } else if (auto* n = dynamic_cast<const BoolLiteralExprNode*>(node)) {
        return {model.internType("bool"), n->value};
    } else if (auto* n = dynamic_cast<const PathExprNode*>(node)) {
        std::string name = valueName(n->path);
        ConstantInfo& c = constant(name);
        model.constant_references[n] = name;
        return {c.type, c.value};
    } else if (auto* n = dynamic_cast<const UnaryExprNode*>(node); n && n->op == "-") {
        if (auto* lit = dynamic_cast<const IntegerLiteralExprNode*>(n->operand.get())) {
            return literal(lit, {}, true);
        }
        auto [t, value] = constantExpr(n->operand.get());
        require(model.isSignedInteger(t));
        require(value != -2147483648LL);
        return {t, -value};
    }
    require(false);
}
std::uint64_t Checker::length(const ExprNode* node) {
    auto [t, value] = constantExpr(node, model.internType("usize"));
    require(model.is(t, TypeKind::Usize));
    return static_cast<std::uint64_t>(value);
}

void Checker::layout(TypeId t, std::set<std::string> visiting) {
    if (model.is(t, TypeKind::Array)) {
        layout(model.type(t).element, std::move(visiting));
        return;
    }
    auto it = model.structures.find(model.type(t).name);
    if (it == model.structures.end()) {
        return;
    }
    require(visiting.insert(model.type(t).name).second);
    for (const FieldInfo& field : it->second.fields) {
        layout(field.type, visiting);
    }
}
SemanticModel Checker::run(const ProgramNode* program) {
    model.program = program;
    // builtin functions
    for (auto* name : {"get_i32", "print_i32", "println_i32"}) {
        std::vector<TypeId> params;
        if (std::string(name) != "get_i32") {
            params.push_back(model.internType("i32"));
        }
        auto builtin = std::string(name) == "get_i32" ? Builtin::GetI32
            : std::string(name) == "print_i32" ? Builtin::PrintI32 : Builtin::PrintlnI32;
        auto& info = model.functions.emplace(name, FunctionInfo{nullptr, {}, params,
            model.internType(std::string(name) == "get_i32" ? "i32" : "()"), builtin}).first->second;
        info.type = model.internType(SemanticType{TypeKind::Function, name});
    }
    // struct collect
    for (auto& declaration : program->declarations) {
        if (auto* s = dynamic_cast<const StructDeclNode*>(declaration.get())) {
            require(s->name != "i32");
            StructInfo info;
            info.declaration = s;
            info.type = model.internType(s->name);
            require(model.structures.emplace(s->name, std::move(info)).second);
        } else add(declaration.get(), InvalidTypeId);
    }
    // function collect
    for (auto& declaration : program->declarations) {
        if (auto* impl = dynamic_cast<const ImplDeclNode*>(declaration.get())) {
            TypeId target = resolve(impl->type.get());
            require(model.structures.count(model.type(target).name));
            for (auto& item : impl->items) {
                add(item.get(), target);
            }
        }
    }
    // struct check
    for (auto& [name, s] : model.structures) {
        owner = s.type;
        for (auto& field : s.declaration->fields) {
            TypeId field_type = resolve(field.type.get());
            require(s.field_indices.emplace(field.name, s.fields.size()).second);
            s.fields.push_back(FieldInfo{field.name, field_type});
        }
        for (const std::string& derive : s.declaration->derives) {
            require(s.derives.insert(derive).second);
        }
        require(!s.derives.count("Copy") || s.derives.count("Clone"));
        require(!s.derives.count("Eq") || s.derives.count("PartialEq"));
    }
    owner = InvalidTypeId;
    for (auto& [name, _] : model.constants) {
        constant(name);
    }
    for (auto& [name, s] : model.structures) {
        layout(s.type, {});
        for (auto& derive : s.derives) {
            require(model.hasTrait(s.type, derive));
        }
    }
    // function check
    for (auto& [name, fn] : model.functions) {
        if (!fn.declaration) {
            continue;
        }
        owner = fn.owner;
        auto& declaration = *fn.declaration;
        if (declaration.self_parameter) {
            TypeId self = fn.owner;
            auto receiver = *declaration.self_parameter;
            if (receiver.by_reference) {
                self = model.internType("&", self, 0, receiver.is_mutable);
            }
            fn.parameters.push_back(self);
        }
        std::set<std::string> names;
        for (auto& param : declaration.parameters) {
            require(names.insert(param.binding.name).second);
            fn.parameters.push_back(resolve(param.type.get()));
        }
        fn.result = resolve(declaration.return_type.get());
    }

    auto main = model.functions.find("main");
    require(main != model.functions.end() && main->second.declaration);
    require(main->second.parameters.empty() && model.is(main->second.result, TypeKind::Unit) &&
            !main->second.declaration->has_generic_parameters);
    model.entry_point = main->second.declaration;
    
    for (auto& [name, fn] : model.functions) {
        if (!fn.declaration) {
            continue;
        }
        owner = fn.owner;
        current_function = &fn;
        scopes = {{}};
        std::size_t index = 0;
        if (fn.declaration->self_parameter) {
            auto receiver = *fn.declaration->self_parameter;
            fn.parameter_bindings.push_back(binding(
                Binding{"self", !receiver.by_reference && receiver.is_mutable}, fn.parameters[index++], true, true));
        }
        for (auto& param : fn.declaration->parameters)
            fn.parameter_bindings.push_back(binding(param.binding, fn.parameters[index++], true));
        expression(fn.declaration->body.get(), fn.result);
    }
    return std::move(model);
}
BindingId Checker::binding(const Binding& b, TypeId t, bool parameter, bool self) {
    size_t id = model.bindings.size();
    model.bindings.push_back(BindingInfo{current_function->declaration, self ? nullptr : &b,
        b.name, t, b.is_mutable, parameter});
    if (!parameter) {
        current_function->local_bindings.push_back(id);
    }
    if (!self) {
        model.binding_declarations[&b] = id;
    }
    scopes.back()[b.name] = id;
    return id;
}
void Checker::expect(const Value& value, TypeId expected) {
    if (!expected) {
        return;
    }
    require(model.canCoerce(value.type, expected));
    if (model.is(expected, TypeKind::Reference) && model.type(expected).is_mutable) {
        require(!value.is_restricted);
    }
}
Value Checker::expression(const ExprNode* node, TypeId expected) {
    Value value = infer(node, expected);
    value.diverges = value.diverges || model.is(value.type, TypeKind::Never);
    expect(value, expected);
    model.expressions[node] = ExpressionInfo{value, value.type, {}};
    if (expected) {
        coerce(node, expected);
    }
    return value;
}
void Checker::adjust(const ExprNode* node, AdjustmentKind kind, TypeId target) {
    auto& info = model.expressions.at(node);
    info.adjustments.push_back(Adjustment{kind, target});
    info.adjusted_type = target;
}
void Checker::coerceBreaks(const LoopExprNode* node, TypeId target) {
    auto& info = model.loops.at(node);
    info.result_type = target;
    if (model.is(target, TypeKind::Never)) {
        return;
    }
    for (auto* jump : info.breaks) {
        if (jump->value) {
            coerce(jump->value.get(), target);
        }
    }
}
void Checker::coerce(const ExprNode* node, TypeId target) {
    TypeId source = model.expressions.at(node).adjusted_type;
    if (source == target) {
        return;
    }
    if (model.is(source, TypeKind::Never)) {
        adjust(node, AdjustmentKind::NeverToAny, target);
        return;
    }
    if (auto* block = dynamic_cast<const BlockExprNode*>(node); block && block->return_value) {
        coerce(block->return_value.get(), target);
        model.expressions.at(node).adjusted_type = target;
        return;
    }
    if (auto* branch = dynamic_cast<const IfExprNode*>(node); branch && branch->else_branch) {
        coerce(branch->then_branch.get(), target);
        coerce(branch->else_branch.get(), target);
        model.expressions.at(node).adjusted_type = target;
        return;
    }
    if (auto* loop = dynamic_cast<const LoopExprNode*>(node)) {
        coerceBreaks(loop, target);
        model.expressions.at(node).adjusted_type = target;
        return;
    }
    TypeId inner = model.type(source).element;
    adjust(node, AdjustmentKind::Dereference, inner);
    while (inner != model.type(target).element) {
        inner = model.type(inner).element;
        adjust(node, AdjustmentKind::Dereference, inner);
    }
    adjust(node, AdjustmentKind::Borrow, target);
}
Value Checker::dereference(Value value) {
    require(model.is(value.type, TypeKind::Reference) || model.is(value.type, TypeKind::Box));
    if (model.is(value.type, TypeKind::Reference)) {
        value.is_restricted = value.is_restricted || !model.type(value.type).is_mutable;
        value.is_writable = model.type(value.type).is_mutable && !value.is_restricted;
    }
    value.type = model.type(value.type).element;
    value.is_place = true;
    return value;
}
Value Checker::autoDeref(Value value, const ExprNode* node) {
    while (model.is(value.type, TypeKind::Reference) || model.is(value.type, TypeKind::Box)) {
        value = dereference(value);
        adjust(node, AdjustmentKind::Dereference, value.type);
    }
    return value;
}
Value Checker::block(const BlockExprNode* node, TypeId expected) {
    scopes.emplace_back();
    bool diverges = false;
    for (auto& statement : node->statements) {
        if (auto* let = dynamic_cast<const LetStmtNode*>(statement.get())) {
            TypeId target = let->type ? resolve(let->type.get()) : InvalidTypeId;
            Value init = expression(let->initializer.get(), target);
            binding(let->binding, target ? target : init.type);
            diverges = diverges || init.diverges;
        } else if (auto* expr = dynamic_cast<const ExprStmtNode*>(statement.get())) {
            Value v = expression(expr->expression.get(), expr->requires_unit ? model.internType("()") : InvalidTypeId);
            diverges = diverges || v.diverges;
        }
    }
    Value value{model.internType(diverges ? "!" : "()")};
    if (node->return_value) {
        value = expression(node->return_value.get(), expected);
    }
    scopes.pop_back();
    return {.type = value.type, .diverges = diverges || value.diverges};
}

Value Checker::binary(const std::string& op, Value left, Value right) {
    if (op == "&&" || op == "||") {
        expect(left, model.internType("bool"));
        expect(right, model.internType("bool"));
        return {model.internType("bool")};
    } else if (op == "==" || op == "!=") {
        require(model.hasTrait(left.type, "PartialEq"));
        return {model.internType("bool")};
    } else if (op == "<" || op == ">" || op == "<=" || op == ">=") {
        bool shared_right = model.is(left.type, TypeKind::Reference) && !model.type(left.type).is_mutable &&
            model.is(right.type, TypeKind::Reference) && model.type(right.type).is_mutable && model.type(left.type).element == model.type(right.type).element;
        require(left.type == right.type || shared_right);
        TypeId base = left.type;
        while (model.is(base, TypeKind::Reference)) {
            base = model.type(base).element;
        }
        require(model.isInteger(base) || model.is(base, TypeKind::Bool));
        return {model.internType("bool")};
    }
    std::function<TypeId(TypeId)> scalar = [this](TypeId t) { return model.is(t, TypeKind::Reference) && !model.type(t).is_mutable ? model.type(t).element : t; };
    TypeId l = scalar(left.type), r = scalar(right.type);
    if (op == "<<" || op == ">>") {
        require(model.isInteger(l) && model.isInteger(r));
        return {l};
    }
    require(l == r);
    require(model.isInteger(l) || ((op == "&" || op == "|" || op == "^") && model.is(l, TypeKind::Bool)));
    return {l};
}

Value Checker::invoke(const FunctionInfo& fn, const std::vector<std::unique_ptr<ExprNode>>& args, std::size_t offset) {
    require(args.size() + offset == fn.parameters.size());
    bool diverges = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        Value argument = expression(args[i].get(), fn.parameters[i + offset]);
        diverges = diverges || argument.diverges;
    }
    return {.type = fn.result, .diverges = diverges};
}
Value Checker::builtin(TypeId target, const std::string& name, const std::vector<std::unique_ptr<ExprNode>>& args, bool method_call) {
    std::size_t offset = method_call ? 0 : 1;
    bool diverges = false;
    const std::function<void(size_t,TypeId)> checkArgument = [&](std::size_t index, TypeId expected) {
        Value value = expression(args[index].get(), expected);
        diverges = diverges || value.diverges;
    };
    std::function<Value(TypeId)> result = [&](TypeId type) -> Value { return {.type = type, .diverges = diverges}; };
    if (name == "new" && (model.is(target, TypeKind::Box) || model.is(target, TypeKind::Vec))) {
        if (model.is(target, TypeKind::Box)) {
            require(args.size() == 1);
        } else {
            require(args.size() == 0);
        }
        if (model.is(target, TypeKind::Box)) {
            checkArgument(0, model.type(target).element);
        }
        return result(target);
    }
    if (name == "clone") {
        require(args.size() == offset);
        if (!method_call) {
            require(model.hasTrait(target, "Clone"));
            checkArgument(0, model.internType("&", target));
        }
        return result(target);
    }
    require(model.is(target, TypeKind::Vec));
    bool mutable_receiver = name == "push" || name == "remove";
    require(name == "push" || name == "remove" || name == "len" || name == "is_empty");
    if (name == "push" || name == "remove") {
        require(args.size() == offset + 1);
    } else {
        require(args.size() == offset);
    }
    if (!method_call) {
        checkArgument(0, model.internType("&", target, 0, mutable_receiver));
    }
    if (name == "push") {
        checkArgument(offset, model.type(target).element);
        return result(model.internType("()"));
    } else if (name == "remove") {
        checkArgument(offset, model.internType("usize"));
    } else if (name == "len") {
        return result(model.internType("usize"));
    } else if (name == "is_empty") {
        return result(model.internType("bool"));
    }
    return result(model.type(target).element);
}
void Checker::recordBuiltin(const ExprNode* node, TypeId target, const std::string& name, TypeId receiver) {
    Builtin builtin;
    if (name == "new") {
        builtin = model.is(target, TypeKind::Box) ? Builtin::BoxNew : Builtin::VecNew;
    } else if (name == "clone") {
        builtin = Builtin::Clone;
    } else if (name == "len") {
        builtin = model.is(target, TypeKind::Array) ? Builtin::ArrayLen : Builtin::VecLen;
    } else if (name == "is_empty") {
        builtin = Builtin::VecIsEmpty;
    } else if (name == "push") {
        builtin = Builtin::VecPush;
    } else {
        builtin = Builtin::VecRemove;
    }
    model.calls[node] = CallInfo{nullptr, model.typeName(target) + "::" + name, builtin, target, receiver};
}
Value Checker::call(const CallExprNode* node) {
    auto* path = dynamic_cast<const PathExprNode*>(node->callee.get());
    if (!path) expression(node->callee.get());
    require(path);
    std::string name = valueName(path->path);
    if (path->path.size() == 1) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            require(!scope->count(name));
        }
    }
    auto fn = model.functions.find(name);
    Value result;
    if (fn != model.functions.end()) {
        result = invoke(fn->second, node->arguments);
        model.calls[node] = CallInfo{fn->second.declaration, name, fn->second.builtin, fn->second.owner, InvalidTypeId};
    } else {
        require(path->path.size() == 2);
        TypeId target = resolveSegment(path->path[0]);
        result = builtin(target, path->path[1].name, node->arguments, false);
        recordBuiltin(node, target, path->path[1].name);
    }
    TypeId callee_type = fn != model.functions.end() ? fn->second.type : model.internType(SemanticType{TypeKind::Function, name});
    model.expressions[path] = ExpressionInfo{{callee_type}, callee_type, {}};
    return result;
}
Value Checker::method(const MethodCallExprNode* node) {
    require(node->method.generic_arguments.empty());
    Value receiver = expression(node->receiver.get());
    std::function<Value(Value)> result = [&](Value value) {
        value.diverges = value.diverges || receiver.diverges;
        return value;
    };
    const std::string& name = node->method.name;
    for (;;) {
        for (int adjustment = 0; adjustment < 3; ++adjustment) {
            TypeId candidate = adjustment == 0 ? receiver.type
                : model.internType("&", receiver.type, 0, adjustment == 2);
            TypeId target = model.is(candidate, TypeKind::Reference) ? model.type(candidate).element : candidate;
            auto fn = model.functions.find(model.typeName(target) + "::" + name);
            if (fn != model.functions.end() && fn->second.declaration &&
                fn->second.declaration->self_parameter &&
                fn->second.parameters[0] == candidate) {
                if (adjustment == 2) {
                    require(receiver.is_writable);
                }
                if (adjustment == 0) {
                    expect(receiver, candidate);
                } else {
                    adjust(node->receiver.get(), AdjustmentKind::Borrow, candidate);
                }
                model.calls[node] = CallInfo{fn->second.declaration, model.typeName(target) + "::" + name, Builtin::None, target, candidate};
                return result(invoke(fn->second, node->arguments, 1));
            }
            if (!model.is(candidate, TypeKind::Reference)) {
                continue;
            }
            bool clone = name == "clone" && !model.type(candidate).is_mutable && model.hasTrait(target, "Clone");
            bool array_len = name == "len" && !model.type(candidate).is_mutable && model.is(target, TypeKind::Array);
            bool vector_method = model.is(target, TypeKind::Vec) &&
                (model.type(candidate).is_mutable ? (name == "push" || name == "remove")
                                : (name == "len" || name == "is_empty"));
            if (clone || array_len || vector_method) {
                if (adjustment == 2) {
                    require(receiver.is_writable);
                }
                if (adjustment == 0) {
                    expect(receiver, candidate);
                } else {
                    adjust(node->receiver.get(), AdjustmentKind::Borrow, candidate);
                }
                recordBuiltin(node, target, name, candidate);
                if (array_len) {
                    require(node->arguments.empty());
                    return result(Value{model.internType("usize")});
                }
                return result(builtin(target, name, node->arguments, true));
            }
        }
        receiver = dereference(receiver);
        adjust(node->receiver.get(), AdjustmentKind::Dereference, receiver.type);
    }
}

Value Checker::infer(const ExprNode* node, TypeId expected) {
    if (auto* n = dynamic_cast<const IntegerLiteralExprNode*>(node)) {
        return {literal(n, expected).first};
    } else if (auto* n = dynamic_cast<const BoolLiteralExprNode*>(node)) {
        model.constant_values[node] = n->value;
        return {model.internType("bool")};
    } else if (dynamic_cast<const UnitExprNode*>(node)) {
        return {model.internType("()")};
    } else if (auto* n = dynamic_cast<const PathExprNode*>(node)) {
        std::string name = valueName(n->path);
        if (n->path.size() == 1) {
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                auto it = scope->find(name);
                if (it != scope->end()) {
                    const auto& binding = model.bindings.at(it->second);
                    model.local_references[n] = it->second;
                    return {binding.type, true, binding.is_mutable, false};
                }
            }
        }
        auto& c = constant(name);
        model.constant_references[n] = name;
        model.constant_values[n] = c.value;
        return {c.type};
    } else if (auto* n = dynamic_cast<const BlockExprNode*>(node)) {
        return block(n, expected);
    } else if (auto* n = dynamic_cast<const UnaryExprNode*>(node)) {
        if (n->op == "-") {
            if (auto* lit = dynamic_cast<const IntegerLiteralExprNode*>(n->operand.get())) {
                auto [t, value] = literal(lit, {}, true);
                model.constant_values[n] = value;
                return {t};
            }
        }
        Value operand = expression(n->operand.get());
        if (n->op == "*") {
            return dereference(operand);
        }
        if (n->op == "&" || n->op == "&mut") {
            bool mut = n->op == "&mut";
            require(!mut || operand.is_writable);
            return {.type = model.internType("&", operand.type, 0, mut), .diverges = operand.diverges};
        }
        TypeId t = operand.type;
        if (model.is(t, TypeKind::Reference) && !model.type(t).is_mutable) {
            t = model.type(t).element;
            adjust(n->operand.get(), AdjustmentKind::Dereference, t);
        }
        require(n->op == "-" ? model.isSignedInteger(t) : model.isInteger(t) || model.is(t, TypeKind::Bool));
        return {.type = t, .diverges = operand.diverges};
    } else if (auto* n = dynamic_cast<const BinaryExprNode*>(node)) {
        Value left = expression(n->left.get());
        Value right = expression(n->right.get());
        Value result = binary(n->op, left, right);
        result.diverges = left.diverges || (n->op != "&&" && n->op != "||" && right.diverges);
        if (model.isInteger(result.type) || n->op == "&" || n->op == "|" || n->op == "^") {
            if (model.is(left.type, TypeKind::Reference)) {
                adjust(n->left.get(), AdjustmentKind::Dereference, model.type(left.type).element);
            }
            if (model.is(right.type, TypeKind::Reference)) {
                adjust(n->right.get(), AdjustmentKind::Dereference, model.type(right.type).element);
            }
        }
        if ((n->op == "<" || n->op == ">" || n->op == "<=" || n->op == ">=") && left.type != right.type) {
            coerce(n->right.get(), left.type);
        }
        return result;
    } else if (auto* n = dynamic_cast<const AssignExprNode*>(node)) {
        Value target = expression(n->target.get());
        require(target.is_place && target.is_writable);
        Value value = expression(n->value.get(), n->op == "=" ? target.type : InvalidTypeId);
        if (n->op != "=") {
            require(model.isInteger(target.type) || model.is(target.type, TypeKind::Bool));
            Value result = binary(n->op.substr(0, n->op.size() - 1), target, value);
            expect(result, target.type);
            if (model.is(value.type, TypeKind::Reference)) {
                adjust(n->value.get(), AdjustmentKind::Dereference, model.type(value.type).element);
            }
        }
        return {.type = model.internType("()"), .diverges = target.diverges || value.diverges};
    } else if (auto* n = dynamic_cast<const CastExprNode*>(node)) {
        Value source = expression(n->expression.get());
        TypeId target = resolve(n->type.get());
        require(model.isInteger(target) && (model.isInteger(source.type) || model.is(source.type, TypeKind::Bool)));
        return {.type = target, .diverges = source.diverges};
    } else if (auto* n = dynamic_cast<const CallExprNode*>(node)) {
        return call(n);
    } else if (auto* n = dynamic_cast<const MethodCallExprNode*>(node)) {
        return method(n);
    } else if (auto* n = dynamic_cast<const FieldExprNode*>(node)) {
        Value object = autoDeref(expression(n->object.get()), n->object.get());
        auto s = model.structures.find(model.type(object.type).name);
        require(s != model.structures.end() && s->second.field_indices.count(n->field));
        size_t index = s->second.field_indices.at(n->field);
        model.field_accesses[n] = FieldAccessInfo{s->second.declaration, index};
        object.type = s->second.fields.at(index).type;
        object.is_place = true;
        return object;
    } else if (auto* n = dynamic_cast<const IndexExprNode*>(node)) {
        Value array = autoDeref(expression(n->array.get()), n->array.get());
        require(model.is(array.type, TypeKind::Array) || model.is(array.type, TypeKind::Vec));
        Value index = expression(n->index.get(), model.internType("usize"));
        array.diverges = array.diverges || index.diverges;
        if (model.is(array.type, TypeKind::Vec) && !array.is_writable) {
            array.is_restricted = true;
        }
        array.type = model.type(array.type).element;
        array.is_place = true;
        return array;
    } else if (auto* n = dynamic_cast<const StructExprNode*>(node)) {
        require(n->path.size() == 1);
        TypeId t = resolveSegment(n->path[0]);
        auto it = model.structures.find(model.type(t).name);
        require(it != model.structures.end());
        std::set<std::string> seen;
        bool diverges = false;
        for (auto& field : n->fields) {
            require(seen.insert(field.name).second);
            auto ft = it->second.field_indices.find(field.name);
            require(ft != it->second.field_indices.end());
            model.field_initializers[&field] = FieldAccessInfo{it->second.declaration, ft->second};
            Value value = expression(field.value.get(), it->second.fields.at(ft->second).type);
            diverges = diverges || value.diverges;
        }
        require(seen.size() == it->second.fields.size());
        return {.type = t, .diverges = diverges};
    } else if (auto* n = dynamic_cast<const ArrayExprNode*>(node)) {
        TypeId element = model.is(expected, TypeKind::Array) ? model.type(expected).element : InvalidTypeId;
        bool diverges = false;
        for (auto& entry : n->elements) {
            Value value = expression(entry.get(), model.is(expected, TypeKind::Array) ? element : InvalidTypeId);
            element = model.is(expected, TypeKind::Array) ? element : merge(element, value.type);
            diverges = diverges || value.diverges;
        }
        require(element != InvalidTypeId);
        for (auto& entry : n->elements) {
            coerce(entry.get(), element);
        }
        return {.type = model.internType("[]", element, n->elements.size()), .diverges = diverges};
    } else if (auto* n = dynamic_cast<const RepeatArrayExprNode*>(node)) {
        size_t count = length(n->count.get());
        Value element = expression(n->value.get(), model.is(expected, TypeKind::Array) ? model.type(expected).element : InvalidTypeId);
        require(count <= 1 || model.hasTrait(element.type, "Copy"));
        return {.type = model.internType("[]", model.is(expected, TypeKind::Array) ? model.type(expected).element : element.type, count), .diverges = element.diverges};
    } else if (auto* n = dynamic_cast<const IfExprNode*>(node)) {
        Value condition = expression(n->condition.get(), model.internType("bool"));
        Value yes = expression(n->then_branch.get(), n->else_branch ? expected : model.internType("()"));
        if (!n->else_branch) {
            return {.type = model.internType("()"), .diverges = condition.diverges};
        }
        Value no = expression(n->else_branch.get(), expected);
        TypeId result = expected ? expected : merge(yes.type, no.type);
        if (!model.is(result, TypeKind::Never)) {
            coerce(n->then_branch.get(), result);
            coerce(n->else_branch.get(), result);
        }
        return {.type = result, .diverges = condition.diverges || (yes.diverges && no.diverges)};
    } else if (auto* n = dynamic_cast<const LoopExprNode*>(node)) {
        bool condition_diverges = false;
        if (n->condition) {
            auto saved = std::move(loop_stack);
            loop_stack.clear();
            condition_diverges = expression(n->condition.get(), model.internType("bool")).diverges;
            loop_stack = std::move(saved);
        }
        auto& info = model.loops.emplace(n, LoopInfo{current_function->declaration, n->condition != nullptr}).first->second;
        loop_stack.push_back(LoopContext{n, expected});
        expression(n->body.get(), model.internType("()"));
        loop_stack.pop_back();
        TypeId result_type = info.is_while ? model.internType("()") : info.result_type ? info.result_type : model.internType("!");
        coerceBreaks(n, result_type);
        bool diverges = info.is_while ? condition_diverges
            : std::all_of(info.breaks.begin(), info.breaks.end(), [&](const BreakExprNode* jump) {
                return jump->value && model.expressions.at(jump->value.get()).diverges;
            });
        return {.type = result_type, .diverges = diverges};
    } else if (auto* n = dynamic_cast<const BreakExprNode*>(node)) {
        require(!loop_stack.empty());
        auto frame = loop_stack.back();
        auto& info = model.loops.at(frame.declaration);
        model.loop_targets[n] = frame.declaration;
        info.breaks.push_back(n);
        require(!info.is_while || !n->value);
        TypeId target = info.is_while ? model.internType("()") : frame.expected;
        Value value = n->value ? expression(n->value.get(), target) : Value{model.internType("()")};
        if (!n->value) {
            expect(value, target);
        }
        info.result_type = merge(info.result_type, model.is(value.type, TypeKind::Never) ? value.type : target ? target : value.type);
        return {model.internType("!")};
    } else if (auto* n = dynamic_cast<const ReturnExprNode*>(node)) {
        model.return_targets[n] = current_function->declaration;
        if (n->value) {
            expression(n->value.get(), current_function->result);
        } else {
            expect(Value{model.internType("()")}, current_function->result);
        }
        return {model.internType("!")};
    } else if (auto* n = dynamic_cast<const ContinueExprNode*>(node)) {
        require(!loop_stack.empty());
        auto target = loop_stack.back().declaration;
        model.loop_targets[n] = target;
        model.loops.at(target).continues.push_back(n);
        return {model.internType("!")};
    }
    require(false);
}

#undef require
}
SemanticModel checkSemantics(const ProgramNode* program) { return Checker{}.run(program); }
} // namespace rx
