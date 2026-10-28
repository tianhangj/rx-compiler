#include "model.hpp"

#include <stdexcept>
#include <utility>

namespace rx {

TypeId SemanticModel::internType(SemanticType value) {
    if (value.element != InvalidTypeId) {
        type(value.element);
    }
    TypeKey key{value.kind, value.name, value.element, value.length, value.is_mutable};
    if (auto it = type_ids_.find(key); it != type_ids_.end()) {
        return it->second;
    }
    auto id = static_cast<TypeId>(type_table_.size() + 1);
    type_table_.push_back(std::move(value));
    type_ids_.emplace(std::move(key), id);
    return id;
}

TypeId SemanticModel::internType(std::string name, TypeId element,
                                 std::uint64_t length, bool is_mutable) {
    static const std::map<std::string, TypeKind> kinds = {
        {"i32", TypeKind::I32}, {"u32", TypeKind::U32},
        {"isize", TypeKind::Isize}, {"usize", TypeKind::Usize},
        {"bool", TypeKind::Bool}, {"()", TypeKind::Unit}, {"!", TypeKind::Never},
        {"&", TypeKind::Reference}, {"[]", TypeKind::Array},
        {"Box", TypeKind::Box}, {"Vec", TypeKind::Vec}
    };
    auto it = kinds.find(name);
    auto kind = it == kinds.end() ? TypeKind::Struct : it->second;
    return internType(SemanticType{kind, std::move(name), element, length, is_mutable});
}

const SemanticType& SemanticModel::type(TypeId id) const {
    if (id == InvalidTypeId) {
        throw std::out_of_range("invalid semantic type ID");
    }
    return type_table_.at(static_cast<std::size_t>(id) - 1);
}

std::string SemanticModel::typeName(TypeId id) const {
    if (id == InvalidTypeId) return "<unknown>";
    const auto& value = type(id);
    if (value.kind == TypeKind::Reference) {
        return std::string(value.is_mutable ? "&mut " : "&") + typeName(value.element);
    }
    if (value.kind == TypeKind::Array) {
        return "[" + typeName(value.element) + "; " + std::to_string(value.length) + "]";
    }
    return value.name + (value.element != InvalidTypeId ? "<" + typeName(value.element) + ">" : "");
}

bool SemanticModel::is(TypeId id, TypeKind kind) const {
    return id != InvalidTypeId && type(id).kind == kind;
}

bool SemanticModel::isInteger(TypeId id) const {
    return is(id, TypeKind::I32) || is(id, TypeKind::U32) ||
        is(id, TypeKind::Isize) || is(id, TypeKind::Usize);
}

bool SemanticModel::isSignedInteger(TypeId id) const {
    return is(id, TypeKind::I32) || is(id, TypeKind::Isize);
}

bool SemanticModel::canCoerce(TypeId source, TypeId target) const {
    if (source == target || is(source, TypeKind::Never)) {
        return true;
    }
    if (!is(source, TypeKind::Reference) || !is(target, TypeKind::Reference)) {
        return false;
    }
    const auto& target_type = type(target);
    bool writable = type(source).is_mutable;
    auto inner = type(source).element;
    for (;;) {
        if ((!target_type.is_mutable || writable) && inner == target_type.element) {
            return true;
        }
        if (is(inner, TypeKind::Box)) {
            inner = type(inner).element;
        } else if (is(inner, TypeKind::Reference)) {
            writable = writable && type(inner).is_mutable;
            inner = type(inner).element;
        } else {
            return false;
        }
    }
}

bool SemanticModel::hasTrait(TypeId id, const std::string& name) const {
    return hasTrait(id, name, {});
}

bool SemanticModel::hasTrait(TypeId id, const std::string& name, std::set<TypeId> visiting) const {
    if (isInteger(id) || is(id, TypeKind::Bool) || is(id, TypeKind::Unit)) {
        return true;
    }
    const auto& value = type(id);
    if (value.kind == TypeKind::Reference) {
        if (name == "Copy" || name == "Clone") {
            return !value.is_mutable;
        }
        return hasTrait(value.element, name, std::move(visiting));
    }
    if (value.kind == TypeKind::Box || value.kind == TypeKind::Vec || value.kind == TypeKind::Array) {
        if (name == "Copy" && value.kind != TypeKind::Array) {
            return false;
        }
        return hasTrait(value.element, name, std::move(visiting));
    }
    auto it = structures.find(value.name);
    if (it == structures.end() || !it->second.derives.count(name)) {
        return false;
    }
    if (!visiting.insert(id).second) {
        return true;
    }
    for (const auto& field : it->second.fields) {
        if (!hasTrait(field.type, name, visiting)) return false;
    }
    return true;
}

} // namespace rx
