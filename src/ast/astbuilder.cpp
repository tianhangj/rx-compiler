#include "astbuilder.hpp"

#include <stdexcept>
#include <utility>

namespace rx {
void requireContext(antlr4::ParserRuleContext* ctx) {
    if (!ctx) throw std::invalid_argument("AstBuilder: null context");
    std::vector<antlr4::tree::ParseTree*> pending{ctx};
    while (!pending.empty()) {
        auto* tree = pending.back();
        pending.pop_back();
        if (dynamic_cast<antlr4::tree::ErrorNode*>(tree)) {
            throw std::invalid_argument("AstBuilder: parse tree contains a syntax error");
        }
        if (auto* rule = dynamic_cast<antlr4::ParserRuleContext*>(tree)) {
            if (rule->exception) {
                throw std::invalid_argument("AstBuilder: parse tree contains a syntax error");
            }
            pending.insert(pending.end(), rule->children.begin(), rule->children.end());
        }
    }
}

std::unique_ptr<IntegerLiteralExprNode> makeInteger(const std::string& text) {
    auto node = std::make_unique<IntegerLiteralExprNode>();
    node->text = text;
    return node;
}

std::unique_ptr<BoolLiteralExprNode> makeBool(bool value) {
    auto node = std::make_unique<BoolLiteralExprNode>();
    node->value = value;
    return node;
}

std::unique_ptr<PathExprNode> makePath(Path path) {
    auto node = std::make_unique<PathExprNode>();
    node->path = std::move(path);
    return node;
}

std::unique_ptr<ReferenceTypeNode> makeReference(
    std::unique_ptr<TypeNode> type, bool is_mutable, bool doubled) {
    auto node = std::make_unique<ReferenceTypeNode>();
    node->referenced_type = std::move(type);
    node->is_mutable = is_mutable;
    if (!doubled) return node;
    auto outer = std::make_unique<ReferenceTypeNode>();
    outer->referenced_type = std::move(node);
    return outer;
}

std::unique_ptr<ExprNode> makeUnary(const std::string& op, std::unique_ptr<ExprNode> operand) {
    if (op.starts_with("&&")) {
        return makeUnary("&", makeUnary(op == "&&mut" ? "&mut" : "&", std::move(operand)));
    }
    auto node = std::make_unique<UnaryExprNode>();
    node->op = op;
    node->operand = std::move(operand);
    return node;
}

std::unique_ptr<ExprNode> makeAssignment(
    const std::string& op, std::unique_ptr<ExprNode> target, std::unique_ptr<ExprNode> value) {
    auto node = std::make_unique<AssignExprNode>();
    node->op = op;
    node->target = std::move(target);
    node->value = std::move(value);
    return node;
}

std::unique_ptr<ProgramNode> AstBuilder::visitCrate(
    Parser::CrateContext* ctx) {
    requireContext(ctx);
    auto node = std::make_unique<ProgramNode>();
    for (auto* item : ctx->item()) {
        if (auto declaration = visitItem(item)) {
            node->declarations.push_back(std::move(declaration));
        }
    }
    return node;
}

std::unique_ptr<DeclNode> AstBuilder::visitItem(
    Parser::ItemContext* ctx) {
    requireContext(ctx);
    if (ctx->useDeclaration()) return nullptr; // ignore use
    if (ctx->functionDefinition()) return visitFunctionDefinition(ctx->functionDefinition());
    if (ctx->structDefinition()) return visitStructDefinition(ctx->structDefinition());
    if (ctx->constantItem()) return visitConstantItem(ctx->constantItem());
    return visitInherentImpl(ctx->inherentImpl());
}


std::unique_ptr<FunctionDeclNode> AstBuilder::visitFunctionDefinition(
    Parser::FunctionDefinitionContext* ctx) {
    auto node = std::make_unique<FunctionDeclNode>();
    node->name = visitIdentifier(ctx->identifier());
    if (ctx->functionParameters()) visitFunctionParameters(ctx->functionParameters(), *node);
    if (ctx->typeRef()) node->return_type = visitTypeRef(ctx->typeRef());
    else node->return_type = std::make_unique<UnitTypeNode>();
    node->body = visitBlockExpression(ctx->blockExpression());
    return node;
}

void AstBuilder::visitFunctionParameters(
    Parser::FunctionParametersContext* ctx, FunctionDeclNode& function) {
    if (ctx->selfParam()) function.self_parameter = visitSelfParam(ctx->selfParam());
    for (auto* param : ctx->functionParam()) {
        function.parameters.push_back(visitFunctionParam(param));
    }
}

SelfParameter AstBuilder::visitSelfParam(
    Parser::SelfParamContext* ctx) {
    SelfParameter param;
    param.by_reference = ctx->AMP() != nullptr;
    param.is_mutable = ctx->MUT() != nullptr;
    return param;
}

FunctionParameter AstBuilder::visitFunctionParam(
    Parser::FunctionParamContext* ctx) {
    FunctionParameter param;
    param.binding = visitIdentifierBinding(ctx->identifierBinding());
    param.type = visitTypeRef(ctx->typeRef());
    return param;
}

std::unique_ptr<StructDeclNode> AstBuilder::visitStructDefinition(
    Parser::StructDefinitionContext* ctx) {
    auto node = std::make_unique<StructDeclNode>();
    node->name = visitIdentifier(ctx->identifier());
    for (auto* attribute : ctx->outerAttribute()) {
        for (auto* name : attribute->deriveName()) {
            node->derives.push_back(visitDeriveName(name));
        }
    }
    for (auto* field : ctx->structField()) {
        node->fields.push_back(visitStructField(field));
    }
    return node;
}

StructField AstBuilder::visitStructField(
    Parser::StructFieldContext* ctx) {
    StructField field;
    field.name = visitIdentifier(ctx->identifier());
    field.type = visitTypeRef(ctx->typeRef());
    return field;
}

std::string AstBuilder::visitDeriveName(
    Parser::DeriveNameContext* ctx) {
    return ctx->getText();
}

std::unique_ptr<ConstDeclNode> AstBuilder::visitConstantItem(
    Parser::ConstantItemContext* ctx) {
    auto node = std::make_unique<ConstDeclNode>();
    node->name = visitIdentifier(ctx->identifier());
    node->type = visitTypeRef(ctx->typeRef());
    node->value = visitConstValue(ctx->constValue());
    return node;
}

std::unique_ptr<ImplDeclNode> AstBuilder::visitInherentImpl(
    Parser::InherentImplContext* ctx) {
    auto node = std::make_unique<ImplDeclNode>();
    node->type = visitTypeRef(ctx->typeRef());
    for (auto* item : ctx->associatedItem()) {
        node->items.push_back(visitAssociatedItem(item));
    }
    return node;
}

std::unique_ptr<DeclNode> AstBuilder::visitAssociatedItem(
    Parser::AssociatedItemContext* ctx) {
    if (ctx->constantItem()) return visitConstantItem(ctx->constantItem());
    return visitFunctionDefinition(ctx->functionDefinition());
}


std::unique_ptr<TypeNode> AstBuilder::visitTypeRef(
    Parser::TypeRefContext* ctx) {
    requireContext(ctx);
    if (ctx->LPAREN()) {
        if (!ctx->typeRef()) return std::make_unique<UnitTypeNode>();
        return visitTypeRef(ctx->typeRef());
    }
    if (ctx->referenceType()) return visitReferenceType(ctx->referenceType());
    if (ctx->arrayType()) return visitArrayType(ctx->arrayType());
    auto node = std::make_unique<PathTypeNode>();
    node->path = visitTypePath(ctx->typePath());
    return node;
}

std::unique_ptr<ReferenceTypeNode> AstBuilder::visitReferenceType(
    Parser::ReferenceTypeContext* ctx) {
    return makeReference(visitTypeRef(ctx->typeRef()),
                         ctx->MUT() != nullptr, ctx->ANDAND() != nullptr);
}

std::unique_ptr<ArrayTypeNode> AstBuilder::visitArrayType(
    Parser::ArrayTypeContext* ctx) {
    auto node = std::make_unique<ArrayTypeNode>();
    node->element_type = visitTypeRef(ctx->typeRef());
    node->length = visitConstValue(ctx->constValue());
    return node;
}

Path AstBuilder::visitTypePath(
    Parser::TypePathContext* ctx) {
    Path path;
    for (auto* segment : ctx->typePathSegment()) {
        path.push_back(visitTypePathSegment(segment));
    }
    return path;
}

PathSegment AstBuilder::visitTypePathSegment(
    Parser::TypePathSegmentContext* ctx) {
    PathSegment segment;
    segment.name = visitPathIdentSegment(ctx->pathIdentSegment());
    if (ctx->genericArgs()) segment.generic_arguments = visitGenericArgs(ctx->genericArgs());
    return segment;
}

Path AstBuilder::visitPathInExpression(
    Parser::PathInExpressionContext* ctx) {
    Path path;
    for (auto* segment : ctx->pathExprSegment()) {
        path.push_back(visitPathExprSegment(segment));
    }
    return path;
}

PathSegment AstBuilder::visitPathExprSegment(
    Parser::PathExprSegmentContext* ctx) {
    PathSegment segment;
    segment.name = visitPathIdentSegment(ctx->pathIdentSegment());
    if (ctx->genericArgs()) segment.generic_arguments = visitGenericArgs(ctx->genericArgs());
    return segment;
}

std::string AstBuilder::visitPathIdentSegment(
    Parser::PathIdentSegmentContext* ctx) {
    return ctx->getText();
}

std::vector<std::unique_ptr<TypeNode>> AstBuilder::visitGenericArgs(
    Parser::GenericArgsContext* ctx) {
    std::vector<std::unique_ptr<TypeNode>> arguments;
    for (auto* argument : ctx->genericArg()) {
        if (argument->typeRef()) arguments.push_back(visitTypeRef(argument->typeRef()));
    }
    return arguments;
}

std::unique_ptr<TypeNode> AstBuilder::visitClosedCastType(
    Parser::ClosedCastTypeContext* ctx) {
    if (ctx->LPAREN()) {
        if (!ctx->typeRef()) return std::make_unique<UnitTypeNode>();
        return visitTypeRef(ctx->typeRef());
    }
    if (ctx->arrayType()) return visitArrayType(ctx->arrayType());
    if (ctx->closedCastType()) {
        return makeReference(visitClosedCastType(ctx->closedCastType()),
                             ctx->MUT() != nullptr, ctx->ANDAND() != nullptr);
    }
    auto node = std::make_unique<PathTypeNode>();
    for (auto* segment : ctx->typePathSegment()) {
        node->path.push_back(visitTypePathSegment(segment));
    }
    PathSegment last;
    last.name = visitPathIdentSegment(ctx->pathIdentSegment());
    last.generic_arguments = visitGenericArgs(ctx->genericArgs());
    node->path.push_back(std::move(last));
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitConstValue(
    Parser::ConstValueContext* ctx) {
    if (ctx->INTEGER_LITERAL()) return makeInteger(ctx->INTEGER_LITERAL()->getText());
    if (ctx->TRUE() || ctx->FALSE()) return makeBool(ctx->TRUE() != nullptr);
    if (ctx->pathInExpression()) return makePath(visitPathInExpression(ctx->pathInExpression()));
    if (ctx->MINUS()) return makeUnary("-", visitMagnitude(ctx->magnitude()));
    return visitConstValue(ctx->constValue());
}

std::unique_ptr<ExprNode> AstBuilder::visitMagnitude(
    Parser::MagnitudeContext* ctx) {
    if (ctx->INTEGER_LITERAL()) return makeInteger(ctx->INTEGER_LITERAL()->getText());
    if (ctx->pathInExpression()) return makePath(visitPathInExpression(ctx->pathInExpression()));
    return visitMagnitude(ctx->magnitude());
}

Binding AstBuilder::visitIdentifierBinding(
    Parser::IdentifierBindingContext* ctx) {
    Binding binding;
    binding.name = visitIdentifier(ctx->identifier());
    binding.is_mutable = ctx->MUT() != nullptr;
    return binding;
}

std::unique_ptr<LetStmtNode> AstBuilder::visitLetStatement(
    Parser::LetStatementContext* ctx) {
    requireContext(ctx);
    auto node = std::make_unique<LetStmtNode>();
    node->binding = visitIdentifierBinding(ctx->identifierBinding());
    if (ctx->typeRef()) node->type = visitTypeRef(ctx->typeRef());
    node->initializer = visitExpression(ctx->expression());
    return node;
}

std::unique_ptr<BlockExprNode> AstBuilder::visitBlockExpression(
    Parser::BlockExpressionContext* ctx) {
    auto node = std::make_unique<BlockExprNode>();
    const auto statements = ctx->statement();
    for (auto* statement : statements) {
        if (statement == statements.back() && !ctx->statementExpression() &&
            statement->expressionWithBlock() && !statement->SEMI()) {
            node->return_value = visitExpressionWithBlock(statement->expressionWithBlock());
        } else if (auto built = visitStatement(statement)) {
            node->statements.push_back(std::move(built));
        }
    }
    if (ctx->statementExpression()) {
        node->return_value = visitStatementExpression(ctx->statementExpression());
    }
    return node;
}

std::unique_ptr<StmtNode> AstBuilder::visitStatement(
    Parser::StatementContext* ctx) {
    if (ctx->letStatement()) return visitLetStatement(ctx->letStatement());
    if (!ctx->expressionWithBlock() && !ctx->statementExpression()) {
        return nullptr;
    }
    auto node = std::make_unique<ExprStmtNode>();
    node->requires_unit = ctx->expressionWithBlock() && !ctx->SEMI();
    node->expression = ctx->expressionWithBlock()
        ? visitExpressionWithBlock(ctx->expressionWithBlock())
        : visitStatementExpression(ctx->statementExpression());
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitExpressionWithBlock(
    Parser::ExpressionWithBlockContext* ctx) {
    if (ctx->ifExpression()) return visitIfExpression(ctx->ifExpression());
    if (ctx->LOOP() || ctx->WHILE()) {
        auto node = std::make_unique<LoopExprNode>();
        if (ctx->WHILE()) node->condition = visitConditionExpression(ctx->conditionExpression());
        node->body = visitBlockExpression(ctx->blockExpression());
        return node;
    }
    return visitBlockExpression(ctx->blockExpression());
}

std::unique_ptr<IfExprNode> AstBuilder::visitIfExpression(
    Parser::IfExpressionContext* ctx) {
    auto node = std::make_unique<IfExprNode>();
    node->condition = visitConditionExpression(ctx->conditionExpression());
    node->then_branch = visitBlockExpression(ctx->blockExpression(0));
    if (ctx->ifExpression()) node->else_branch = visitIfExpression(ctx->ifExpression());
    else if (ctx->blockExpression().size() > 1) {
        node->else_branch = visitBlockExpression(ctx->blockExpression(1));
    }
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitExpression(
    Parser::ExpressionContext* ctx) {
    requireContext(ctx);
    return visitAssignmentExpression(ctx->assignmentExpression());
}

std::unique_ptr<ExprNode> AstBuilder::visitAssignmentExpression(
    Parser::AssignmentExpressionContext* ctx) {
    auto target = visitExprContext(ctx->logicalOrExpression());
    if (!ctx->assignmentOperator()) return target;
    return makeAssignment(visitAssignmentOperator(ctx->assignmentOperator()),
                          std::move(target), visitExpression(ctx->expression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitCastExpression(
    Parser::CastExpressionContext* ctx) {
    auto expression = visitUnaryExpression(ctx->unaryExpression());
    for (auto* type : ctx->typeRef()) {
        auto node = std::make_unique<CastExprNode>();
        node->expression = std::move(expression);
        node->type = visitTypeRef(type);
        expression = std::move(node);
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitClosedCastExpression(
    Parser::ClosedCastExpressionContext* ctx) {
    if (ctx->unaryExpression()) return visitUnaryExpression(ctx->unaryExpression());
    auto node = std::make_unique<CastExprNode>();
    node->expression = visitCastExpression(ctx->castExpression());
    node->type = visitClosedCastType(ctx->closedCastType());
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitUnaryExpression(
    Parser::UnaryExpressionContext* ctx) {
    if (!ctx->unaryOperator()) return visitPostfixExpression(ctx->postfixExpression());
    return makeUnary(visitUnaryOperator(ctx->unaryOperator()),
                     visitUnaryExpression(ctx->unaryExpression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitPostfixExpression(
    Parser::PostfixExpressionContext* ctx) {
    auto expression = visitPrimaryExpression(ctx->primaryExpression());
    for (auto* suffix : ctx->postfixSuffix()) {
        expression = visitPostfixSuffix(suffix, std::move(expression));
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionExpression(
    Parser::ConditionExpressionContext* ctx) {
    return visitConditionAssignmentExpression(ctx->conditionAssignmentExpression());
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionAssignmentExpression(
    Parser::ConditionAssignmentExpressionContext* ctx) {
    auto target = visitExprContext(ctx->conditionLogicalOrExpression());
    if (!ctx->assignmentOperator()) return target;
    return makeAssignment(visitAssignmentOperator(ctx->assignmentOperator()),
                          std::move(target), visitConditionExpression(ctx->conditionExpression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionCastExpression(
    Parser::ConditionCastExpressionContext* ctx) {
    auto expression = visitConditionUnaryExpression(ctx->conditionUnaryExpression());
    for (auto* type : ctx->typeRef()) {
        auto node = std::make_unique<CastExprNode>();
        node->expression = std::move(expression);
        node->type = visitTypeRef(type);
        expression = std::move(node);
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionClosedCastExpression(
    Parser::ConditionClosedCastExpressionContext* ctx) {
    if (ctx->conditionUnaryExpression()) return visitConditionUnaryExpression(ctx->conditionUnaryExpression());
    auto node = std::make_unique<CastExprNode>();
    node->expression = visitConditionCastExpression(ctx->conditionCastExpression());
    node->type = visitClosedCastType(ctx->closedCastType());
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionUnaryExpression(
    Parser::ConditionUnaryExpressionContext* ctx) {
    if (!ctx->unaryOperator()) return visitConditionPostfixExpression(ctx->conditionPostfixExpression());
    return makeUnary(visitUnaryOperator(ctx->unaryOperator()),
                     visitConditionUnaryExpression(ctx->conditionUnaryExpression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionPostfixExpression(
    Parser::ConditionPostfixExpressionContext* ctx) {
    auto expression = visitConditionPrimary(ctx->conditionPrimary());
    for (auto* suffix : ctx->postfixSuffix()) {
        expression = visitPostfixSuffix(suffix, std::move(expression));
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionBreakExpression(
    Parser::ConditionBreakExpressionContext* ctx) {
    return visitConditionBreakAssignmentExpression(ctx->conditionBreakAssignmentExpression());
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionBreakAssignmentExpression(
    Parser::ConditionBreakAssignmentExpressionContext* ctx) {
    auto target = visitExprContext(ctx->conditionBreakLogicalOrExpression());
    if (!ctx->assignmentOperator()) return target;
    return makeAssignment(visitAssignmentOperator(ctx->assignmentOperator()),
                          std::move(target), visitConditionExpression(ctx->conditionExpression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionBreakCastExpression(
    Parser::ConditionBreakCastExpressionContext* ctx) {
    auto expression = visitConditionBreakUnaryExpression(ctx->conditionBreakUnaryExpression());
    for (auto* type : ctx->typeRef()) {
        auto node = std::make_unique<CastExprNode>();
        node->expression = std::move(expression);
        node->type = visitTypeRef(type);
        expression = std::move(node);
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionBreakClosedCastExpression(
    Parser::ConditionBreakClosedCastExpressionContext* ctx) {
    if (ctx->conditionBreakUnaryExpression()) return visitConditionBreakUnaryExpression(ctx->conditionBreakUnaryExpression());
    auto node = std::make_unique<CastExprNode>();
    node->expression = visitConditionBreakCastExpression(ctx->conditionBreakCastExpression());
    node->type = visitClosedCastType(ctx->closedCastType());
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionBreakUnaryExpression(
    Parser::ConditionBreakUnaryExpressionContext* ctx) {
    if (!ctx->unaryOperator()) return visitConditionBreakPostfixExpression(ctx->conditionBreakPostfixExpression());
    return makeUnary(visitUnaryOperator(ctx->unaryOperator()),
                     visitConditionUnaryExpression(ctx->conditionUnaryExpression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionBreakPostfixExpression(
    Parser::ConditionBreakPostfixExpressionContext* ctx) {
    auto expression = visitConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
    for (auto* suffix : ctx->postfixSuffix()) {
        expression = visitPostfixSuffix(suffix, std::move(expression));
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitStatementExpression(
    Parser::StatementExpressionContext* ctx) {
    return visitStatementAssignmentExpression(ctx->statementAssignmentExpression());
}

std::unique_ptr<ExprNode> AstBuilder::visitStatementAssignmentExpression(
    Parser::StatementAssignmentExpressionContext* ctx) {
    auto target = visitExprContext(ctx->statementLogicalOrExpression());
    if (!ctx->assignmentOperator()) return target;
    return makeAssignment(visitAssignmentOperator(ctx->assignmentOperator()),
                          std::move(target), visitExpression(ctx->expression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitStatementCastExpression(
    Parser::StatementCastExpressionContext* ctx) {
    auto expression = visitStatementUnaryExpression(ctx->statementUnaryExpression());
    for (auto* type : ctx->typeRef()) {
        auto node = std::make_unique<CastExprNode>();
        node->expression = std::move(expression);
        node->type = visitTypeRef(type);
        expression = std::move(node);
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitStatementClosedCastExpression(
    Parser::StatementClosedCastExpressionContext* ctx) {
    if (ctx->statementUnaryExpression()) return visitStatementUnaryExpression(ctx->statementUnaryExpression());
    auto node = std::make_unique<CastExprNode>();
    node->expression = visitStatementCastExpression(ctx->statementCastExpression());
    node->type = visitClosedCastType(ctx->closedCastType());
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitStatementUnaryExpression(
    Parser::StatementUnaryExpressionContext* ctx) {
    if (!ctx->unaryOperator()) return visitStatementPostfixExpression(ctx->statementPostfixExpression());
    return makeUnary(visitUnaryOperator(ctx->unaryOperator()),
                     visitUnaryExpression(ctx->unaryExpression()));
}

std::unique_ptr<ExprNode> AstBuilder::visitStatementPostfixExpression(
    Parser::StatementPostfixExpressionContext* ctx) {
    std::unique_ptr<ExprNode> expression;
    if (ctx->nonBlockPrimary()) {
        expression = visitNonBlockPrimary(ctx->nonBlockPrimary());
    } else {
        expression = visitDotSuffix(ctx->dotSuffix(),
                                    visitExpressionWithBlock(ctx->expressionWithBlock()));
    }
    for (auto* suffix : ctx->postfixSuffix()) {
        expression = visitPostfixSuffix(suffix, std::move(expression));
    }
    return expression;
}

std::unique_ptr<ExprNode> AstBuilder::visitPrimaryExpression(
    Parser::PrimaryExpressionContext* ctx) {
    if (ctx->nonBlockPrimary()) return visitNonBlockPrimary(ctx->nonBlockPrimary());
    return visitExpressionWithBlock(ctx->expressionWithBlock());
}

std::unique_ptr<ExprNode> AstBuilder::visitNonBlockPrimary(
    Parser::NonBlockPrimaryContext* ctx) {
    if (ctx->literalExpression()) return visitLiteralExpression(ctx->literalExpression());
    if (ctx->pathInExpression()) {
        if (ctx->LBRACE()) {
            auto node = std::make_unique<StructExprNode>();
            node->path = visitPathInExpression(ctx->pathInExpression());
            if (ctx->structExprFields()) node->fields = visitStructExprFields(ctx->structExprFields());
            return node;
        }
        return makePath(visitPathInExpression(ctx->pathInExpression()));
    }
    if (ctx->LPAREN()) {
        if (!ctx->expression()) return std::make_unique<UnitExprNode>();
        return visitExpression(ctx->expression());
    }
    if (ctx->arrayExpression()) return visitArrayExpression(ctx->arrayExpression());
    if (ctx->BREAK()) {
        auto node = std::make_unique<BreakExprNode>();
        if (ctx->expression()) node->value = visitExpression(ctx->expression());
        return node;
    }
    if (ctx->RETURN()) {
        auto node = std::make_unique<ReturnExprNode>();
        if (ctx->expression()) node->value = visitExpression(ctx->expression());
        return node;
    }
    return std::make_unique<ContinueExprNode>();
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionPrimary(
    Parser::ConditionPrimaryContext* ctx) {
    if (ctx->blockExpression()) return visitBlockExpression(ctx->blockExpression());
    return visitConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
}

std::unique_ptr<ExprNode> AstBuilder::visitConditionPrimaryWithoutBareBlock(
    Parser::ConditionPrimaryWithoutBareBlockContext* ctx) {
    if (ctx->literalExpression()) return visitLiteralExpression(ctx->literalExpression());
    if (ctx->pathInExpression()) {
        return makePath(visitPathInExpression(ctx->pathInExpression()));
    }
    if (ctx->LPAREN()) {
        if (!ctx->expression()) return std::make_unique<UnitExprNode>();
        return visitExpression(ctx->expression());
    }
    if (ctx->arrayExpression()) return visitArrayExpression(ctx->arrayExpression());
    if (ctx->ifExpression()) return visitIfExpression(ctx->ifExpression());
    if (ctx->LOOP() || ctx->WHILE()) {
        auto node = std::make_unique<LoopExprNode>();
        if (ctx->WHILE()) node->condition = visitConditionExpression(ctx->conditionExpression());
        node->body = visitBlockExpression(ctx->blockExpression());
        return node;
    }
    if (ctx->BREAK()) {
        auto node = std::make_unique<BreakExprNode>();
        if (ctx->conditionBreakExpression()) node->value = visitConditionBreakExpression(ctx->conditionBreakExpression());
        return node;
    }
    if (ctx->RETURN()) {
        auto node = std::make_unique<ReturnExprNode>();
        if (ctx->conditionExpression()) node->value = visitConditionExpression(ctx->conditionExpression());
        return node;
    }
    return std::make_unique<ContinueExprNode>();
}

std::unique_ptr<ExprNode> AstBuilder::visitLiteralExpression(
    Parser::LiteralExpressionContext* ctx) {
    if (ctx->INTEGER_LITERAL()) return makeInteger(ctx->INTEGER_LITERAL()->getText());
    return makeBool(ctx->TRUE() != nullptr);
}

std::vector<StructFieldInitializer> AstBuilder::visitStructExprFields(
    Parser::StructExprFieldsContext* ctx) {
    std::vector<StructFieldInitializer> fields;
    for (auto* field : ctx->structExprField()) {
        fields.push_back(visitStructExprField(field));
    }
    return fields;
}

StructFieldInitializer AstBuilder::visitStructExprField(
    Parser::StructExprFieldContext* ctx) {
    StructFieldInitializer field;
    field.name = visitIdentifier(ctx->identifier());
    field.value = visitExpression(ctx->expression());
    return field;
}

std::unique_ptr<ExprNode> AstBuilder::visitArrayExpression(
    Parser::ArrayExpressionContext* ctx) {
    if (ctx->SEMI()) {
        auto node = std::make_unique<RepeatArrayExprNode>();
        node->value = visitExpression(ctx->expression(0));
        node->count = visitConstValue(ctx->constValue());
        return node;
    }
    auto node = std::make_unique<ArrayExprNode>();
    for (auto* expression : ctx->expression()) {
        node->elements.push_back(visitExpression(expression));
    }
    return node;
}

std::unique_ptr<ExprNode> AstBuilder::visitPostfixSuffix(
    Parser::PostfixSuffixContext* ctx, std::unique_ptr<ExprNode> base) {
    if (ctx->callArguments()) {
        auto node = std::make_unique<CallExprNode>();
        node->callee = std::move(base);
        node->arguments = visitCallArguments(ctx->callArguments());
        return node;
    }
    if (ctx->LBRACKET()) {
        auto node = std::make_unique<IndexExprNode>();
        node->array = std::move(base);
        node->index = visitExpression(ctx->expression());
        return node;
    }
    return visitDotSuffix(ctx->dotSuffix(), std::move(base));
}

std::unique_ptr<ExprNode> AstBuilder::visitDotSuffix(
    Parser::DotSuffixContext* ctx, std::unique_ptr<ExprNode> base) {
    if (ctx->callArguments()) {
        auto node = std::make_unique<MethodCallExprNode>();
        node->receiver = std::move(base);
        node->method = visitPathExprSegment(ctx->pathExprSegment());
        node->arguments = visitCallArguments(ctx->callArguments());
        return node;
    }
    auto node = std::make_unique<FieldExprNode>();
    node->object = std::move(base);
    node->field = visitIdentifier(ctx->identifier());
    return node;
}

std::vector<std::unique_ptr<ExprNode>> AstBuilder::visitCallArguments(
    Parser::CallArgumentsContext* ctx) {
    std::vector<std::unique_ptr<ExprNode>> arguments;
    for (auto* expression : ctx->expression()) {
        arguments.push_back(visitExpression(expression));
    }
    return arguments;
}

std::string AstBuilder::visitUnaryOperator(
    Parser::UnaryOperatorContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitMultiplicativeOperator(
    Parser::MultiplicativeOperatorContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitAdditiveOperator(
    Parser::AdditiveOperatorContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitShiftRight(
    Parser::ShiftRightContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitComparisonExceptLt(
    Parser::ComparisonExceptLtContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitAssignmentOperator(
    Parser::AssignmentOperatorContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitEqualsSign(
    Parser::EqualsSignContext* ctx) {
    return ctx->getText();
}

std::string AstBuilder::visitIdentifier(
    Parser::IdentifierContext* ctx) {
    return ctx->getText();
}

std::unique_ptr<ExprNode> AstBuilder::visitExprContext(antlr4::ParserRuleContext* ctx) {
    switch (ctx->getRuleIndex()) {
    case Parser::RuleConstValue:
        return visitConstValue(static_cast<Parser::ConstValueContext*>(ctx));
    case Parser::RuleMagnitude:
        return visitMagnitude(static_cast<Parser::MagnitudeContext*>(ctx));
    case Parser::RuleBlockExpression:
        return visitBlockExpression(static_cast<Parser::BlockExpressionContext*>(ctx));
    case Parser::RuleExpressionWithBlock:
        return visitExpressionWithBlock(static_cast<Parser::ExpressionWithBlockContext*>(ctx));
    case Parser::RuleIfExpression:
        return visitIfExpression(static_cast<Parser::IfExpressionContext*>(ctx));
    case Parser::RuleExpression:
        return visitExpression(static_cast<Parser::ExpressionContext*>(ctx));
    case Parser::RuleAssignmentExpression:
        return visitAssignmentExpression(static_cast<Parser::AssignmentExpressionContext*>(ctx));
    case Parser::RuleLogicalOrExpression:
    case Parser::RuleLogicalAndExpression:
    case Parser::RuleComparisonExpression:
    case Parser::RuleBitOrExpression:
    case Parser::RuleClosedBitOrExpression:
    case Parser::RuleBitXorExpression:
    case Parser::RuleClosedBitXorExpression:
    case Parser::RuleBitAndExpression:
    case Parser::RuleClosedBitAndExpression:
    case Parser::RuleShiftExpression:
    case Parser::RuleClosedShiftExpression:
    case Parser::RuleAdditiveExpression:
    case Parser::RuleClosedAdditiveExpression:
    case Parser::RuleMultiplicativeExpression:
    case Parser::RuleClosedMultiplicativeExpression:
        return visitBinary(ctx);
    case Parser::RuleCastExpression:
        return visitCastExpression(static_cast<Parser::CastExpressionContext*>(ctx));
    case Parser::RuleClosedCastExpression:
        return visitClosedCastExpression(static_cast<Parser::ClosedCastExpressionContext*>(ctx));
    case Parser::RuleUnaryExpression:
        return visitUnaryExpression(static_cast<Parser::UnaryExpressionContext*>(ctx));
    case Parser::RulePostfixExpression:
        return visitPostfixExpression(static_cast<Parser::PostfixExpressionContext*>(ctx));
    case Parser::RuleConditionExpression:
        return visitConditionExpression(static_cast<Parser::ConditionExpressionContext*>(ctx));
    case Parser::RuleConditionAssignmentExpression:
        return visitConditionAssignmentExpression(static_cast<Parser::ConditionAssignmentExpressionContext*>(ctx));
    case Parser::RuleConditionLogicalOrExpression:
    case Parser::RuleConditionLogicalAndExpression:
    case Parser::RuleConditionComparisonExpression:
    case Parser::RuleConditionBitOrExpression:
    case Parser::RuleConditionClosedBitOrExpression:
    case Parser::RuleConditionBitXorExpression:
    case Parser::RuleConditionClosedBitXorExpression:
    case Parser::RuleConditionBitAndExpression:
    case Parser::RuleConditionClosedBitAndExpression:
    case Parser::RuleConditionShiftExpression:
    case Parser::RuleConditionClosedShiftExpression:
    case Parser::RuleConditionAdditiveExpression:
    case Parser::RuleConditionClosedAdditiveExpression:
    case Parser::RuleConditionMultiplicativeExpression:
    case Parser::RuleConditionClosedMultiplicativeExpression:
        return visitBinary(ctx);
    case Parser::RuleConditionCastExpression:
        return visitConditionCastExpression(static_cast<Parser::ConditionCastExpressionContext*>(ctx));
    case Parser::RuleConditionClosedCastExpression:
        return visitConditionClosedCastExpression(static_cast<Parser::ConditionClosedCastExpressionContext*>(ctx));
    case Parser::RuleConditionUnaryExpression:
        return visitConditionUnaryExpression(static_cast<Parser::ConditionUnaryExpressionContext*>(ctx));
    case Parser::RuleConditionPostfixExpression:
        return visitConditionPostfixExpression(static_cast<Parser::ConditionPostfixExpressionContext*>(ctx));
    case Parser::RuleConditionBreakExpression:
        return visitConditionBreakExpression(static_cast<Parser::ConditionBreakExpressionContext*>(ctx));
    case Parser::RuleConditionBreakAssignmentExpression:
        return visitConditionBreakAssignmentExpression(static_cast<Parser::ConditionBreakAssignmentExpressionContext*>(ctx));
    case Parser::RuleConditionBreakLogicalOrExpression:
    case Parser::RuleConditionBreakLogicalAndExpression:
    case Parser::RuleConditionBreakComparisonExpression:
    case Parser::RuleConditionBreakBitOrExpression:
    case Parser::RuleConditionBreakClosedBitOrExpression:
    case Parser::RuleConditionBreakBitXorExpression:
    case Parser::RuleConditionBreakClosedBitXorExpression:
    case Parser::RuleConditionBreakBitAndExpression:
    case Parser::RuleConditionBreakClosedBitAndExpression:
    case Parser::RuleConditionBreakShiftExpression:
    case Parser::RuleConditionBreakClosedShiftExpression:
    case Parser::RuleConditionBreakAdditiveExpression:
    case Parser::RuleConditionBreakClosedAdditiveExpression:
    case Parser::RuleConditionBreakMultiplicativeExpression:
    case Parser::RuleConditionBreakClosedMultiplicativeExpression:
        return visitBinary(ctx);
    case Parser::RuleConditionBreakCastExpression:
        return visitConditionBreakCastExpression(static_cast<Parser::ConditionBreakCastExpressionContext*>(ctx));
    case Parser::RuleConditionBreakClosedCastExpression:
        return visitConditionBreakClosedCastExpression(static_cast<Parser::ConditionBreakClosedCastExpressionContext*>(ctx));
    case Parser::RuleConditionBreakUnaryExpression:
        return visitConditionBreakUnaryExpression(static_cast<Parser::ConditionBreakUnaryExpressionContext*>(ctx));
    case Parser::RuleConditionBreakPostfixExpression:
        return visitConditionBreakPostfixExpression(static_cast<Parser::ConditionBreakPostfixExpressionContext*>(ctx));
    case Parser::RuleStatementExpression:
        return visitStatementExpression(static_cast<Parser::StatementExpressionContext*>(ctx));
    case Parser::RuleStatementAssignmentExpression:
        return visitStatementAssignmentExpression(static_cast<Parser::StatementAssignmentExpressionContext*>(ctx));
    case Parser::RuleStatementLogicalOrExpression:
    case Parser::RuleStatementLogicalAndExpression:
    case Parser::RuleStatementComparisonExpression:
    case Parser::RuleStatementBitOrExpression:
    case Parser::RuleStatementClosedBitOrExpression:
    case Parser::RuleStatementBitXorExpression:
    case Parser::RuleStatementClosedBitXorExpression:
    case Parser::RuleStatementBitAndExpression:
    case Parser::RuleStatementClosedBitAndExpression:
    case Parser::RuleStatementShiftExpression:
    case Parser::RuleStatementClosedShiftExpression:
    case Parser::RuleStatementAdditiveExpression:
    case Parser::RuleStatementClosedAdditiveExpression:
    case Parser::RuleStatementMultiplicativeExpression:
    case Parser::RuleStatementClosedMultiplicativeExpression:
        return visitBinary(ctx);
    case Parser::RuleStatementCastExpression:
        return visitStatementCastExpression(static_cast<Parser::StatementCastExpressionContext*>(ctx));
    case Parser::RuleStatementClosedCastExpression:
        return visitStatementClosedCastExpression(static_cast<Parser::StatementClosedCastExpressionContext*>(ctx));
    case Parser::RuleStatementUnaryExpression:
        return visitStatementUnaryExpression(static_cast<Parser::StatementUnaryExpressionContext*>(ctx));
    case Parser::RuleStatementPostfixExpression:
        return visitStatementPostfixExpression(static_cast<Parser::StatementPostfixExpressionContext*>(ctx));
    case Parser::RulePrimaryExpression:
        return visitPrimaryExpression(static_cast<Parser::PrimaryExpressionContext*>(ctx));
    case Parser::RuleNonBlockPrimary:
        return visitNonBlockPrimary(static_cast<Parser::NonBlockPrimaryContext*>(ctx));
    case Parser::RuleConditionPrimary:
        return visitConditionPrimary(static_cast<Parser::ConditionPrimaryContext*>(ctx));
    case Parser::RuleConditionPrimaryWithoutBareBlock:
        return visitConditionPrimaryWithoutBareBlock(static_cast<Parser::ConditionPrimaryWithoutBareBlockContext*>(ctx));
    case Parser::RuleLiteralExpression:
        return visitLiteralExpression(static_cast<Parser::LiteralExpressionContext*>(ctx));
    case Parser::RuleArrayExpression:
        return visitArrayExpression(static_cast<Parser::ArrayExpressionContext*>(ctx));
    default:
        throw std::logic_error("AstBuilder: expected an expression context");
    }
}

std::unique_ptr<ExprNode> AstBuilder::visitBinary(antlr4::ParserRuleContext* ctx) {
    std::unique_ptr<ExprNode> expression;
    std::string op;
    for (auto* child : ctx->children) {
        auto* rule = dynamic_cast<antlr4::ParserRuleContext*>(child);
        if (!rule) {
            op += child->getText();
            continue;
        }
        switch (rule->getRuleIndex()) {
        case Parser::RuleMultiplicativeOperator:
            op += visitMultiplicativeOperator(static_cast<Parser::MultiplicativeOperatorContext*>(rule));
            continue;
        case Parser::RuleAdditiveOperator:
            op += visitAdditiveOperator(static_cast<Parser::AdditiveOperatorContext*>(rule));
            continue;
        case Parser::RuleShiftRight:
            op += visitShiftRight(static_cast<Parser::ShiftRightContext*>(rule));
            continue;
        case Parser::RuleComparisonExceptLt:
            op += visitComparisonExceptLt(static_cast<Parser::ComparisonExceptLtContext*>(rule));
            continue;
        default:
            break;
        }
        auto operand = visitExprContext(rule);
        if (!expression) {
            expression = std::move(operand);
        } else {
            auto node = std::make_unique<BinaryExprNode>();
            node->op = std::move(op);
            node->left = std::move(expression);
            node->right = std::move(operand);
            expression = std::move(node);
            op.clear();
        }
    }
    return expression;
}

}