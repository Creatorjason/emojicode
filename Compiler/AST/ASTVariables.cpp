//
//  ASTVariables.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 07/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTVariables.hpp"
#include "ASTBinaryOperator.hpp"
#include "ASTInitialization.hpp"
#include "ASTType.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "MemoryFlowAnalysis/MFFunctionAnalyser.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "Scoping/VariableNotFoundError.hpp"
#include "Types/TypeExpectation.hpp"
#include "Functions/Function.hpp"

namespace EmojicodeCompiler {

void AccessesAnyVariable::setVariableAccess(const ResolvedVariable &var, ExpressionAnalyser *analyser) {
    id_ = var.variable.id();
    inInstanceScope_ = var.inInstanceScope;
    variableType_ = var.variable.type();
    if (inInstanceScope_) {
        analyser->pathAnalyser().record(PathAnalyserIncident::UsedSelf);
    }
}

Type ASTGetVariable::analyse(ExpressionAnalyser *analyser) {
    auto var = analyser->scoper().getVariable(name(), position());
    setVariableAccess(var, analyser);
    analyser->pathAnalyser().uninitalizedError(var, position());
    auto type = var.variable.type();
    if (var.inInstanceScope && analyser->typeContext().calleeType().type() == TypeType::ValueType &&
        !analyser->typeContext().calleeType().isMutable()) {
        type.setMutable(false);
    }
    assert(!type.is<TypeType::NoReturn>());
    return type;
}

void ASTGetVariable::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    if (type.isReturn()) {
        returned_ = true;
    }
    if (!inInstanceScope()) {
        analyser->recordVariableGet(id(), type);
    }
}

void ASTGetVariable::mutateReference(ExpressionAnalyser *analyser) {
    analyser->scoper().getVariable(name(), position()).variable.mutate(position());
}

Type ASTIsOnlyReference::analyse(ExpressionAnalyser *analyser) {
    auto rvar = analyser->scoper().getVariable(name(), position());
    if (rvar.variable.type().type() != TypeType::Someobject && rvar.variable.type().type() != TypeType::Class) {
        analyser->error(CompilerError(position(), "🏮 can only be used with objects."));
    }
    setVariableAccess(rvar, analyser);
    return analyser->boolean();
}

void ASTIsOnlyReference::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
}

ASTVariableDeclaration::ASTVariableDeclaration(std::unique_ptr<ASTType> type,
    std::u32string name,
    const SourcePosition &p) : ASTStatement(p), varName_(std::move(name)), type_(std::move(type)) {}

void ASTVariableDeclaration::analyse(FunctionAnalyser *analyser) {
    auto &type = type_->analyseType(analyser->typeContext());
    analyser->scoper().checkForShadowing(varName_, position(), analyser->compiler());
    auto &var = analyser->scoper().currentScope().declareVariable(varName_, type, false, position());
    if (type.type() == TypeType::Optional) {
        analyser->pathAnalyser().record(PathAnalyserIncident(false, var.id()));
    }
    id_ = var.id();
}

ASTVariableDeclaration::~ASTVariableDeclaration() = default;

void ASTVariableAssignment::analyse(FunctionAnalyser *analyser) {
    auto rvar = analyser->scoper().getVariable(name(), position());

    if (rvar.inInstanceScope && !analyser->function()->mutating() &&
        !isFullyInitializedCheckRequired(analyser->function()->functionType())) {
        auto ce = CompilerError(position(), "Can’t mutate instance variable as method is not marked with 🖍.");
        ce.addNotes(analyser->function()->position(), "Add 🖍 to method attributes to allow mutation.");
        analyser->error(ce);
    }

    setVariableAccess(rvar, analyser);
    analyser->expectType(rvar.variable.type(), &expr_);

    auto incident = PathAnalyserIncident(rvar.inInstanceScope, rvar.variable.id());
    wasInitialized_ = analyser->pathAnalyser().hasCertainly(incident);
    analyser->pathAnalyser().record(incident);

    rvar.variable.mutate(position());
}

void ASTVariableAssignment::analyseMemoryFlow(MFFunctionAnalyser *analyser) {
    analyser->take(expr_.get());
    if (!inInstanceScope()) {
        analyser->recordVariableSet(id(), expr_.get(), variableType());
    }
    else {
        expr_->analyseMemoryFlow(analyser, MFFlowCategory::Escaping);
    }
}

void ASTVariableDeclareAndAssign::analyse(FunctionAnalyser *analyser) {
    Type t = analyser->expect(TypeExpectation(false, true), &expr_).inexacted();
    analyser->scoper().checkForShadowing(name(), position(), analyser->compiler());
    auto &var = analyser->scoper().currentScope().declareVariable(name(), t, false, position());
    analyser->pathAnalyser().record(PathAnalyserIncident(false, var.id()));
    setVariableAccess(ResolvedVariable(var, false), analyser);
}

void ASTVariableDeclareAndAssign::analyseMemoryFlow(EmojicodeCompiler::MFFunctionAnalyser *analyser) {
    analyser->take(expr_.get());
    analyser->recordVariableSet(id(), expr_.get(), variableType());
}

void ASTInstanceVariableInitialization::analyse(FunctionAnalyser *analyser) {
    auto &var = analyser->scoper().instanceScope()->getLocalVariable(name());
    analyser->pathAnalyser().record(PathAnalyserIncident(true, var.id()));
    var.mutate(position());
    setVariableAccess(ResolvedVariable(var, true), analyser);
    if (analyseExpr_) {
        analyser->expectType(var.type(), &expr_);
    }
}

void ASTConstantVariable::analyse(FunctionAnalyser *analyser) {
    Type t = analyser->expect(TypeExpectation(false, false), &expr_);
    analyser->scoper().checkForShadowing(name(), position(), analyser->compiler());
    auto &var = analyser->scoper().currentScope().declareVariable(name(), t, true, position());
    analyser->pathAnalyser().record(PathAnalyserIncident(false, var.id()));
    setVariableAccess(ResolvedVariable(var, false), analyser);
}

void ASTConstantVariable::analyseMemoryFlow(MFFunctionAnalyser *analyser) {
    analyser->take(expr_.get());
    analyser->recordVariableSet(id(), expr_.get(), expr_->expressionType());
}

ASTOperatorAssignment::ASTOperatorAssignment(std::u32string name, const std::shared_ptr<ASTExpr> &e,
                                             const SourcePosition &p, OperatorType opType) :
ASTVariableAssignment(name,
                      std::make_shared<ASTBinaryOperator>(opType, std::make_shared<ASTGetVariable>(name, p), e, p),
                      p) {}

}  // namespace EmojicodeCompiler
