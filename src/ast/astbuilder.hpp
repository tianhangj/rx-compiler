#pragma once

#include "node.hpp"
#include "Parser.h"

#include <memory>
#include <string>
#include <vector>

namespace rx {

class AstBuilder {
public:
    std::unique_ptr<ProgramNode> visitCrate(
        Parser::CrateContext* ctx);
    std::unique_ptr<DeclNode> visitItem(
        Parser::ItemContext* ctx);
    std::unique_ptr<TypeNode> visitTypeRef(
        Parser::TypeRefContext* ctx);
    std::unique_ptr<ExprNode> visitExpression(
        Parser::ExpressionContext* ctx);
    std::unique_ptr<LetStmtNode> visitLetStatement(
        Parser::LetStatementContext* ctx);

private:
    std::unique_ptr<ExprNode> visitExprContext(antlr4::ParserRuleContext* ctx);
    std::unique_ptr<ExprNode> visitBinary(antlr4::ParserRuleContext* ctx);

    std::unique_ptr<FunctionDeclNode> visitFunctionDefinition(
        Parser::FunctionDefinitionContext* ctx);
    void visitFunctionParameters(
        Parser::FunctionParametersContext* ctx, FunctionDeclNode& function);
    SelfParameter visitSelfParam(
        Parser::SelfParamContext* ctx);
    FunctionParameter visitFunctionParam(
        Parser::FunctionParamContext* ctx);
    std::unique_ptr<StructDeclNode> visitStructDefinition(
        Parser::StructDefinitionContext* ctx);
    StructField visitStructField(
        Parser::StructFieldContext* ctx);
    std::string visitDeriveName(
        Parser::DeriveNameContext* ctx);
    std::unique_ptr<ConstDeclNode> visitConstantItem(
        Parser::ConstantItemContext* ctx);
    std::unique_ptr<ImplDeclNode> visitInherentImpl(
        Parser::InherentImplContext* ctx);
    std::unique_ptr<DeclNode> visitAssociatedItem(
        Parser::AssociatedItemContext* ctx);

    std::unique_ptr<ReferenceTypeNode> visitReferenceType(
        Parser::ReferenceTypeContext* ctx);
    std::unique_ptr<ArrayTypeNode> visitArrayType(
        Parser::ArrayTypeContext* ctx);
    Path visitTypePath(
        Parser::TypePathContext* ctx);
    PathSegment visitTypePathSegment(
        Parser::TypePathSegmentContext* ctx);
    Path visitPathInExpression(
        Parser::PathInExpressionContext* ctx);
    PathSegment visitPathExprSegment(
        Parser::PathExprSegmentContext* ctx);
    std::string visitPathIdentSegment(
        Parser::PathIdentSegmentContext* ctx);
    std::vector<std::unique_ptr<TypeNode>> visitGenericArgs(
        Parser::GenericArgsContext* ctx);
    std::unique_ptr<TypeNode> visitClosedCastType(
        Parser::ClosedCastTypeContext* ctx);

    std::unique_ptr<ExprNode> visitConstValue(
        Parser::ConstValueContext* ctx);
    std::unique_ptr<ExprNode> visitMagnitude(
        Parser::MagnitudeContext* ctx);
    Binding visitIdentifierBinding(
        Parser::IdentifierBindingContext* ctx);
    std::unique_ptr<BlockExprNode> visitBlockExpression(
        Parser::BlockExpressionContext* ctx);
    std::unique_ptr<StmtNode> visitStatement(
        Parser::StatementContext* ctx);
    std::unique_ptr<ExprNode> visitExpressionWithBlock(
        Parser::ExpressionWithBlockContext* ctx);
    std::unique_ptr<IfExprNode> visitIfExpression(
        Parser::IfExpressionContext* ctx);

    std::unique_ptr<ExprNode> visitAssignmentExpression(
        Parser::AssignmentExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitCastExpression(
        Parser::CastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitClosedCastExpression(
        Parser::ClosedCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitUnaryExpression(
        Parser::UnaryExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitPostfixExpression(
        Parser::PostfixExpressionContext* ctx);

    std::unique_ptr<ExprNode> visitConditionExpression(
        Parser::ConditionExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionAssignmentExpression(
        Parser::ConditionAssignmentExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionCastExpression(
        Parser::ConditionCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionClosedCastExpression(
        Parser::ConditionClosedCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionUnaryExpression(
        Parser::ConditionUnaryExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionPostfixExpression(
        Parser::ConditionPostfixExpressionContext* ctx);

    std::unique_ptr<ExprNode> visitConditionBreakExpression(
        Parser::ConditionBreakExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionBreakAssignmentExpression(
        Parser::ConditionBreakAssignmentExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionBreakCastExpression(
        Parser::ConditionBreakCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionBreakClosedCastExpression(
        Parser::ConditionBreakClosedCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionBreakUnaryExpression(
        Parser::ConditionBreakUnaryExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitConditionBreakPostfixExpression(
        Parser::ConditionBreakPostfixExpressionContext* ctx);

    std::unique_ptr<ExprNode> visitStatementExpression(
        Parser::StatementExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitStatementAssignmentExpression(
        Parser::StatementAssignmentExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitStatementCastExpression(
        Parser::StatementCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitStatementClosedCastExpression(
        Parser::StatementClosedCastExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitStatementUnaryExpression(
        Parser::StatementUnaryExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitStatementPostfixExpression(
        Parser::StatementPostfixExpressionContext* ctx);

    std::unique_ptr<ExprNode> visitPrimaryExpression(
        Parser::PrimaryExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitNonBlockPrimary(
        Parser::NonBlockPrimaryContext* ctx);
    std::unique_ptr<ExprNode> visitConditionPrimary(
        Parser::ConditionPrimaryContext* ctx);
    std::unique_ptr<ExprNode> visitConditionPrimaryWithoutBareBlock(
        Parser::ConditionPrimaryWithoutBareBlockContext* ctx);
    std::unique_ptr<ExprNode> visitLiteralExpression(
        Parser::LiteralExpressionContext* ctx);
    std::vector<StructFieldInitializer> visitStructExprFields(
        Parser::StructExprFieldsContext* ctx);
    StructFieldInitializer visitStructExprField(
        Parser::StructExprFieldContext* ctx);
    std::unique_ptr<ExprNode> visitArrayExpression(
        Parser::ArrayExpressionContext* ctx);
    std::unique_ptr<ExprNode> visitPostfixSuffix(
        Parser::PostfixSuffixContext* ctx, std::unique_ptr<ExprNode> base);
    std::unique_ptr<ExprNode> visitDotSuffix(
        Parser::DotSuffixContext* ctx, std::unique_ptr<ExprNode> base);
    std::vector<std::unique_ptr<ExprNode>> visitCallArguments(
        Parser::CallArgumentsContext* ctx);

    std::string visitUnaryOperator(
        Parser::UnaryOperatorContext* ctx);
    std::string visitMultiplicativeOperator(
        Parser::MultiplicativeOperatorContext* ctx);
    std::string visitAdditiveOperator(
        Parser::AdditiveOperatorContext* ctx);
    std::string visitShiftRight(
        Parser::ShiftRightContext* ctx);
    std::string visitComparisonExceptLt(
        Parser::ComparisonExceptLtContext* ctx);
    std::string visitAssignmentOperator(
        Parser::AssignmentOperatorContext* ctx);
    std::string visitEqualsSign(
        Parser::EqualsSignContext* ctx);
    std::string visitIdentifier(
        Parser::IdentifierContext* ctx);
};

} // namespace rx
