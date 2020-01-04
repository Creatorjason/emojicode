//
//  AbstractParser.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 27/04/16.
//  Copyright © 2016 Theo Weidmann. All rights reserved.
//

#include "AbstractParser.hpp"
#include "AST/ASTType.hpp"
#include "Compiler.hpp"
#include "Emojis.h"
#include "Functions/Function.hpp"
#include "Functions/Initializer.hpp"
#include "Package/Package.hpp"
#include "Types/Protocol.hpp"
#include "OperatorHelper.hpp"
#include <map>
#include <vector>

namespace EmojicodeCompiler {

TypeIdentifier AbstractParser::parseTypeIdentifier() {
    std::u32string namespase = stream_.consumeTokenIf(E_ORANGE_TRIANGLE) ? parseTypeEmoji().value() : U"";
    auto typeName = parseTypeEmoji();
    return TypeIdentifier(typeName.value(), namespase, typeName.position());
}

Token AbstractParser::parseTypeEmoji() const {
    if (stream_.nextTokenIs(E_CANDY) || stream_.nextTokenIs(E_MEDIUM_BLACK_CIRCLE) ||
        stream_.nextTokenIs(E_MEDIUM_WHITE_CIRCLE) || stream_.nextTokenIs(E_LARGE_BLUE_CIRCLE) ||
        stream_.nextTokenIs(E_BENTO_BOX) || stream_.nextTokenIs(E_ORANGE_TRIANGLE) ||
        stream_.nextTokenIs(E_EIGHT_POINTED_STAR)) {
        auto token = stream_.consumeToken();
        throw CompilerError(token.position(), "Unexpected identifier ", utf8(token.value()), " with special meaning.");
    }
    if (stream_.nextTokenIs(TokenType::ForIn)) {
        return stream_.consumeToken();
    }
    return stream_.consumeToken(TokenType::Identifier);
}

std::unique_ptr<ASTType> AbstractParser::parseType() {
    if (stream_.nextTokenIs(TokenType::Class) || stream_.nextTokenIs(TokenType::Enumeration) ||
            stream_.nextTokenIs(TokenType::ValueType) || stream_.nextTokenIs(TokenType::Protocol)) {
        return parseTypeAsValueType();
    }

    if (stream_.consumeTokenIf(E_BLACK_MEDIUM_SQUARE)) {
        return std::make_unique<ASTLiteralType>(Type::noReturn());
    }

    bool optional = stream_.consumeTokenIf(E_CANDY);
    bool reference = stream_.consumeTokenIf(E_EIGHT_POINTED_STAR);

    if (stream_.nextTokenIs(E_MEDIUM_WHITE_CIRCLE)) {
        auto token = stream_.consumeToken();
        if (optional) {
            package_->compiler()->error(CompilerError(token.position(), "🍬⚪️ is invalid."));
        }
        return std::make_unique<ASTLiteralType>(Type::something());
    }
    auto type = parseTypeMain();
    type->setOptional(optional);
    if (reference) {
        type->setReference();
    }
    return type;
}

std::unique_ptr<ASTType> AbstractParser::parseTypeAsValueType() {
    auto token = stream_.consumeToken();
    return std::make_unique<ASTTypeValueType>(parseType(), token.type(), token.position(), package_);
}

std::unique_ptr<ASTType> AbstractParser::parseTypeMain() {
    if (stream_.consumeTokenIf(TokenType::BlockBegin)) {
        return parseCallableType();
    }
    if (stream_.nextTokenIs(TokenType::Variable)) {
        return parseGenericVariable();
    }
    if (stream_.nextTokenIs(E_BENTO_BOX)) {
        return parseMultiProtocol();
    }
    if (stream_.consumeTokenIf(E_LARGE_BLUE_CIRCLE)) {
        return std::make_unique<ASTLiteralType>(Type::someobject());
    }

    return paresTypeId();
}

std::unique_ptr<ASTType> AbstractParser::paresTypeId() {
    auto typeId = parseTypeIdentifier();
    auto type = std::make_unique<ASTTypeId>(typeId.name, typeId.ns, typeId.position, package_);
    parseGenericArguments(type.get());
    return type;
}

std::unique_ptr<ASTType> AbstractParser::parseMultiProtocol() {
    auto bentoToken = stream_.consumeToken(TokenType::Identifier);

    std::vector<std::unique_ptr<ASTType>> protocols;
    while (stream_.nextTokenIsEverythingBut(E_BENTO_BOX)) {
        protocols.emplace_back(parseType());
    }
    stream_.consumeToken(TokenType::Identifier);

    return std::make_unique<ASTMultiProtocol>(std::move(protocols), bentoToken.position(), package_);
}

std::unique_ptr<ASTType> AbstractParser::parseCallableType() {
    std::vector<std::unique_ptr<ASTType>> params;
    while (stream_.nextTokenIsEverythingBut(TokenType::BlockEnd) &&
            stream_.nextTokenIsEverythingBut(TokenType::RightProductionOperator)) {
        params.emplace_back(parseType());
    }
    auto returnType = stream_.consumeTokenIf(TokenType::RightProductionOperator) ? parseType()
                                                                                 : nullptr;
    auto errorType = stream_.consumeTokenIf(E_CONSTRUCTION_SIGN) ? parseType() : nullptr;
    auto pos = stream_.consumeToken(TokenType::BlockEnd).position();
    return std::make_unique<ASTCallableType>(std::move(returnType), std::move(params), std::move(errorType),
                                             pos, package_);
}

std::unique_ptr<ASTType> AbstractParser::parseGenericVariable() {
    auto varToken = stream_.consumeToken(TokenType::Variable);
    return std::make_unique<ASTGenericVariable>(varToken.value(), varToken.position(), package_);
}

void AbstractParser::parseParameters(Function *function, bool initializer, bool allowEscaping) {
    std::vector<Parameter> params;

    while (true) {
        bool argumentToVariable = stream_.nextTokenIs(E_BABY_BOTTLE);
        if (argumentToVariable) {
            auto token = stream_.consumeToken(TokenType::Identifier);
            if (!initializer) {
                throw CompilerError(token.position(), "🍼 can only be used with initializers.");
            }
        }

        bool escaping = allowEscaping && stream_.consumeTokenIf(E_TAKEOUT_BOX, TokenType::Decorator);
        if (!argumentToVariable && !stream_.nextTokenIs(TokenType::Variable)) {
            break;
        }

        auto variableToken = stream_.consumeToken(TokenType::Variable);
        params.emplace_back(variableToken.value(), parseType(), escaping ? MFFlowCategory::Escaping : MFFlowCategory::Borrowing);

        if (argumentToVariable) {
            dynamic_cast<Initializer *>(function)->addArgumentToVariable(variableToken.value(), variableToken.position());
        }
    }
    function->setParameters(std::move(params));
}

void AbstractParser::parseReturnType(Function *function) {
    if (stream_.consumeTokenIf(TokenType::RightProductionOperator)) {
        function->setReturnType(parseType());
    }
}

bool AbstractParser::parseErrorType(Function *function) {
    if (stream_.nextTokenIs(E_CONSTRUCTION_SIGN)) {
        auto token = stream_.consumeToken(TokenType::Identifier);
        function->setErrorType(parseType());
        return true;
    }
    return false;
}

std::u32string AbstractParser::parseInitializerName() {
    std::u32string name = std::u32string(1, E_NEW_SIGN);
    if (stream_.nextTokenIs(TokenType::Operator) &&
        operatorType(stream_.nextToken().value()) == OperatorType::Greater) {
        stream_.consumeToken();
        name = stream_.consumeToken(TokenType::Identifier).value();
    }
    return name;
}

}  // namespace EmojicodeCompiler
