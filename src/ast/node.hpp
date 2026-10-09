#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rx {

struct AstNode {
    virtual ~AstNode() = default;
    virtual void dump(int indent = 0) = 0;
};

class ExprNode : public AstNode {};
class DeclNode : public AstNode {};
class StmtNode : public AstNode {};
class TypeNode : public AstNode {};

// 路径段 <name>[<generic_arguments>]
struct PathSegment {
    std::string name;
    std::vector<std::unique_ptr<TypeNode>> generic_arguments;
};

// 完整路径，由路径段以 :: 连接
using Path = std::vector<PathSegment>;

// 名称绑定 [mut] <name>
struct Binding {
    std::string name;
    bool is_mutable = false;
};

// 类型 -----------------------------------------------------------------------

// 路径类型 <path>
struct PathTypeNode : TypeNode {
    void dump(int indent = 0) override;
    Path path;
};

// 引用类型 &[mut] <referenced_type>
struct ReferenceTypeNode : TypeNode {
    void dump(int indent = 0) override;
    bool is_mutable = false;
    std::unique_ptr<TypeNode> referenced_type;
};

// 定长数组类型 [<element_type>; <length>]
struct ArrayTypeNode : TypeNode {
    void dump(int indent = 0) override;
    std::unique_ptr<TypeNode> element_type;
    std::unique_ptr<ExprNode> length;
};

// 单元类型 ()
struct UnitTypeNode : TypeNode {
    void dump(int indent = 0) override;
};

// 语句 -----------------------------------------------------------------------

// let 声明 let <binding>[: <type>] = <initializer>;
struct LetStmtNode : StmtNode {
    void dump(int indent = 0) override;
    Binding binding;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<ExprNode> initializer;
};

// 表达式语句 <expression> 或 <expression>;
struct ExprStmtNode : StmtNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> expression;
    // 无分号的非尾块表达式必须为 ()；带分号时允许丢弃任意值。
    bool requires_unit = false;
};

// 表达式 ---------------------------------------------------------------------

// 整数字面量 <text>
struct IntegerLiteralExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::string text;
};

// 布尔字面量 <value>，为 true 或 false
struct BoolLiteralExprNode : ExprNode {
    void dump(int indent = 0) override;
    bool value = false;
};

// 路径表达式 <path>，如 x 或 Self::new
struct PathExprNode : ExprNode {
    void dump(int indent = 0) override;
    Path path;
};

// 单元值表达式 ()
struct UnitExprNode : ExprNode {
    void dump(int indent = 0) override;
};

// 一元运算 <op><operand>
struct UnaryExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::string op; //- ! * & &mut
    std::unique_ptr<ExprNode> operand;
};

// 二元运算 <left> <op> <right>
struct BinaryExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::string op; //+ - * / % << >> & | ^ == != < <= > >= && ||
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;
};

// 赋值 <target> <op> <value>，普通赋值和复合赋值均保留，不展开目标表达式
struct AssignExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::string op = "="; // = += -= *= /= %= <<= >>= &= |= ^=
    std::unique_ptr<ExprNode> target;
    std::unique_ptr<ExprNode> value;
};

// 类型转换 <expression> as <type>
struct CastExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> expression;
    std::unique_ptr<TypeNode> type;
};

// 函数调用 <callee>(<arguments>)
struct CallExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> callee;
    std::vector<std::unique_ptr<ExprNode>> arguments;
};

// 方法调用 <receiver>.<method>(<arguments>)
struct MethodCallExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> receiver;
    PathSegment method;
    std::vector<std::unique_ptr<ExprNode>> arguments;
};

// 字段访问 <object>.<field>
struct FieldExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> object;
    std::string field;
};

// 下标访问 <array>[<index>]
struct IndexExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> array;
    std::unique_ptr<ExprNode> index;
};

// 结构体字段初始化 <name>: <value>
struct StructFieldInitializer {
    std::string name;
    std::unique_ptr<ExprNode> value;
};

// 结构体构造 <path> { <fields> }
struct StructExprNode : ExprNode {
    void dump(int indent = 0) override;
    Path path;
    std::vector<StructFieldInitializer> fields;
};

// 数组表达式 [<elements>]
struct ArrayExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::vector<std::unique_ptr<ExprNode>> elements;
};

// 数组重复表达式 [<value>; <count>]
struct RepeatArrayExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> value;
    std::unique_ptr<ExprNode> count;
};

// 块表达式 { <statements> <return_value> }
struct BlockExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::vector<std::unique_ptr<StmtNode>> statements;
    std::unique_ptr<ExprNode> return_value;
};

// 条件分支 if <condition> <then_branch> else <else_branch>
struct IfExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<BlockExprNode> then_branch;
    std::unique_ptr<ExprNode> else_branch;
};

// 循环 loop <body> / while <condition> <body>
struct LoopExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> condition; // nullptr 表示 loop；非空表示 while
    std::unique_ptr<BlockExprNode> body;
};

// 跳出循环 break <value>
struct BreakExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> value; // optional
};

// 函数返回 return <value>
struct ReturnExprNode : ExprNode {
    void dump(int indent = 0) override;
    std::unique_ptr<ExprNode> value; // optional
};

// 继续下次循环 continue
struct ContinueExprNode : ExprNode {
    void dump(int indent = 0) override;
};

// 声明 -----------------------------------------------------------------------

// 函数形参 <binding>: <type>
struct FunctionParameter {
    Binding binding;
    std::unique_ptr<TypeNode> type;
};

// self 参数
struct SelfParameter {
    bool by_reference = false;
    bool is_mutable = false;
};

// 函数定义 fn <name>(<parameters>) -> <return_type> <body>
struct FunctionDeclNode : DeclNode {
    void dump(int indent = 0) override;
    std::string name;
    std::optional<SelfParameter> self_parameter;
    std::vector<FunctionParameter> parameters;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<BlockExprNode> body;
};

// 结构体字段 <name>: <type>
struct StructField {
    std::string name;
    std::unique_ptr<TypeNode> type;
};

// 结构体定义 #[derive(<derives>)] struct <name> { <fields> }
struct StructDeclNode : DeclNode {
    void dump(int indent = 0) override;
    std::string name;
    std::vector<std::string> derives;
    std::vector<StructField> fields;
};

// 常量声明 const <name>: <type> = <value>;
struct ConstDeclNode : DeclNode {
    void dump(int indent = 0) override;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<ExprNode> value;
};

// 实现块 impl <type> { <items> }
struct ImplDeclNode : DeclNode {
    void dump(int indent = 0) override;
    std::unique_ptr<TypeNode> type;
    std::vector<std::unique_ptr<DeclNode>> items;
};

// 程序根节点 <declarations>
struct ProgramNode : AstNode {
    void dump(int indent = 0) override;
    std::vector<std::unique_ptr<DeclNode>> declarations;
};

}