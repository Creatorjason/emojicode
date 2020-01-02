//
//  ASTStatements.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 04/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTStatements.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Compiler.hpp"
#include "Functions/FunctionType.hpp"
#include "Functions/Initializer.hpp"
#include "MemoryFlowAnalysis/MFFunctionAnalyser.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "Scoping/VariableNotFoundError.hpp"
#include "Types/Class.hpp"
#include "Types/TypeExpectation.hpp"
#include "ASTVariables.hpp"

namespace EmojicodeCompiler {

void ASTBlock::analyse(FunctionAnalyser *analyser) {
    for (size_t i = 0; i < stmts_.size(); i++) {
        if (!returnedCertainly_  && analyser->pathAnalyser().hasCertainly(PathAnalyserIncident::Returned)) {
            returnedCertainly_ = true;
            stop_ = i;
            scopeStats_ = analyser->scoper().createStats();
            hasStats_ = true;
            analyser->compiler()->warn(stmts_[i]->position(), "Code will never be executed.");
        }
        stmts_[i]->analyse(analyser);
    }

    if (!returnedCertainly_  && analyser->pathAnalyser().hasCertainly(PathAnalyserIncident::Returned)) {
        returnedCertainly_ = true;
        stop_ = stmts_.size();
    }
}

void ASTBlock::analyseMemoryFlow(MFFunctionAnalyser *analyser) {
    auto stop = !returnedCertainly_ ? stmts_.size() : stop_;
    for (size_t i = 0; i < stop; i++) {
        stmts_[i]->analyseMemoryFlow(analyser);
    }
}

ASTReturn* ASTBlock::getReturn() const {
    if (returnedCertainly()) {
        return dynamic_cast<ASTReturn*>(stmts_[stop_ - 1].get());
    }
    return nullptr;
}

void ASTBlock::popScope(FunctionAnalyser *analyser) {
    if (!hasStats_) {
        scopeStats_ = analyser->scoper().createStats();
        hasStats_ = true;
    }
    analyser->scoper().popScope(&analyser->pathAnalyser(), analyser->compiler());
}

void ASTExprStatement::analyse(FunctionAnalyser *analyser)  {
    analyser->ExpressionAnalyser::expect(TypeExpectation(), &expr_);
}

void ASTExprStatement::analyseMemoryFlow(MFFunctionAnalyser *analyser) {
    expr_->analyseMemoryFlow(analyser, MFFlowCategory::Borrowing);
}

void ASTReturn::analyse(FunctionAnalyser *analyser) {
    analyser->pathAnalyser().record(PathAnalyserIncident::Returned);

    if (analyser->function()->returnType()->type().type() == TypeType::NoReturn) {
        if (value_ != nullptr) {
            throw CompilerError(position(), "No return type declared. Use ↩️↩️.");
        }
        return;
    }

    if (value_ == nullptr) {
        throw CompilerError(position(), "↩️↩️ can only be used in functions without a return value.");
    }

    if (isReturnForbidden(analyser->function()->functionType())) {
        throw CompilerError(position(), "↩️ cannot be used inside an initializer.");
    }

    auto rtType = analyser->function()->returnType()->type();

    auto type = analyser->ExpressionAnalyser::analyse(value_);
    if (!type.compatibleTo(rtType, analyser->typeContext())) {
        analyser->error(CompilerError(position(), "Declared return type is ",
                                                  rtType.toString(analyser->typeContext())));
    }
    if (analyser->function()->returnType()->type().isReference()) {
        returnReference(analyser, type);
    }
    else {
        analyser->comply(TypeExpectation(rtType), &value_);
    }
}

void ASTReturn::returnReference(FunctionAnalyser *analyser, Type type) {
    if (auto varNode = dynamic_cast<ASTGetVariable*>(value_.get())) {
        if (!varNode->inInstanceScope()) {
            analyser->error(CompilerError(position(), "Only instance variables can be referenced."));
        }

        varNode->setReference();
        type.setReference(true);
        varNode->setExpressionType(type);
        return;
    }
    if (type.isReference()) {
        if (!analyser->isInUnsafeBlock()) {
            analyser->error(CompilerError(position(), "Forwarding reference is an unsafe operation."));
        }
        if (!type.isMutable()) {
            analyser->error(CompilerError(position(), "Cannot forward immutable reference."));
        }
        return;
    }
    analyser->error(CompilerError(position(), "The provided expression cannot produce a reference."));
}

void ASTRaise::analyse(FunctionAnalyser *analyser) {
    analyser->pathAnalyser().record(PathAnalyserIncident::Returned);
    if (!analyser->function()->errorProne()) {
        throw CompilerError(position(), "Function is not declared error-prone.");
    }

    analyser->expectType(analyser->function()->errorType()->type(), &value_);

    if (isReturnForbidden(analyser->function()->functionType())) {
        analyseInstanceVariables(analyser, position());
    }
}

void ASTReturn::analyseMemoryFlow(MFFunctionAnalyser *analyser) {
    if (value_ != nullptr && !initReturn_) {
        analyser->take(value_.get());
        value_->analyseMemoryFlow(analyser, MFFlowCategory::Return);
    }
}

}  // namespace EmojicodeCompiler
