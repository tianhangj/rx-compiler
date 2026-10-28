#pragma once

#include "ast/node.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rx {

enum class TypeKind {
    I32, U32, Isize, Usize, Bool, Unit, Never,
    Reference, Array, Struct, Box, Vec, Function
};

using TypeId = std::uint32_t;
inline constexpr TypeId InvalidTypeId = 0;

struct SemanticType {
    TypeKind kind;
    std::string name;
    TypeId element = InvalidTypeId;
    std::uint64_t length = 0;
    bool is_mutable = false;
};

enum class AdjustmentKind { Dereference, Borrow, NeverToAny };
struct Adjustment {
    AdjustmentKind kind;
    TypeId type = InvalidTypeId;
};

struct ValueInfo {
    TypeId type = InvalidTypeId;
    bool is_place = false;
    bool is_writable = true;
    bool is_restricted = false;
    bool diverges = false;
};

struct ExpressionInfo : ValueInfo {
    TypeId adjusted_type = InvalidTypeId;
    std::vector<Adjustment> adjustments;
};

using BindingId = std::size_t;
struct BindingInfo {
    const FunctionDeclNode* function;
    const Binding* declaration;
    std::string name;
    TypeId type = InvalidTypeId;
    bool is_mutable = false;
    bool is_parameter = false;
};

enum class Builtin {
    None, GetI32, PrintI32, PrintlnI32, BoxNew, VecNew, Clone,
    ArrayLen, VecLen, VecIsEmpty, VecPush, VecRemove
};

struct FunctionInfo {
    const FunctionDeclNode* declaration = nullptr;
    TypeId owner = InvalidTypeId;
    std::vector<TypeId> parameters;
    TypeId result = InvalidTypeId;
    Builtin builtin = Builtin::None;
    std::vector<BindingId> parameter_bindings = {};
    std::vector<BindingId> local_bindings = {};
    TypeId type = InvalidTypeId;
};

struct ConstantInfo {
    const ConstDeclNode* declaration;
    TypeId owner = InvalidTypeId;
    TypeId type = InvalidTypeId;
    std::int64_t value = 0;
};

struct FieldInfo {
    std::string name;
    TypeId type = InvalidTypeId;
};
struct StructInfo {
    const StructDeclNode* declaration = nullptr;
    std::vector<FieldInfo> fields;
    std::set<std::string> derives;
    std::map<std::string, std::size_t> field_indices;
    TypeId type = InvalidTypeId;
};

struct CallInfo {
    const FunctionDeclNode* declaration = nullptr;
    std::string name;
    Builtin builtin = Builtin::None;
    TypeId target_type = InvalidTypeId;
    TypeId receiver_type = InvalidTypeId;
};

struct FieldAccessInfo {
    const StructDeclNode* declaration;
    std::size_t index;
};

struct LoopInfo {
    const FunctionDeclNode* function = nullptr;
    bool is_while = false;
    TypeId result_type = InvalidTypeId;
    std::vector<const BreakExprNode*> breaks = {};
    std::vector<const ContinueExprNode*> continues = {};
};

struct SemanticModel {
    TypeId internType(SemanticType type);
    TypeId internType(std::string name, TypeId element = InvalidTypeId, std::uint64_t length = 0, bool is_mutable = false);
    const SemanticType& type(TypeId id) const;
    std::string typeName(TypeId id) const;
    bool is(TypeId id, TypeKind kind) const;
    bool isInteger(TypeId id) const;
    bool isSignedInteger(TypeId id) const;
    bool canCoerce(TypeId source, TypeId target) const;
    bool hasTrait(TypeId id, const std::string& name) const;

    const ProgramNode* program = nullptr;
    const FunctionDeclNode* entry_point = nullptr;
    std::map<std::string, StructInfo> structures;
    std::map<std::string, FunctionInfo> functions;
    std::map<std::string, ConstantInfo> constants;
    std::map<const TypeNode*, TypeId> types;
    std::map<const ExprNode*, ExpressionInfo> expressions;
    std::vector<BindingInfo> bindings;
    std::map<const Binding*, BindingId> binding_declarations;
    std::map<const PathExprNode*, BindingId> local_references;
    std::map<const PathExprNode*, std::string> constant_references;

    std::map<const ExprNode*, std::int64_t> constant_values;
    std::map<const ExprNode*, CallInfo> calls;
    std::map<const FieldExprNode*, FieldAccessInfo> field_accesses;

    std::map<const StructFieldInitializer*, FieldAccessInfo> field_initializers;
    std::map<const LoopExprNode*, LoopInfo> loops;
    std::map<const ExprNode*, const LoopExprNode*> loop_targets;
    std::map<const ReturnExprNode*, const FunctionDeclNode*> return_targets;

private:
    bool hasTrait(TypeId id, const std::string& name, std::set<TypeId> visiting) const;
    using TypeKey = std::tuple<TypeKind, std::string, TypeId, std::uint64_t, bool>;

    std::vector<SemanticType> type_table_;
    std::map<TypeKey, TypeId> type_ids_;
};

} // namespace rx
