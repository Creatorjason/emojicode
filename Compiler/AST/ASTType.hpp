//
// Created by Theo Weidmann on 04.06.18.
//

#ifndef EMOJICODE_ASTTYPE_HPP
#define EMOJICODE_ASTTYPE_HPP

#include "Types/Type.hpp"
#include "ASTNode.hpp"
#include <iterator>
#include <memory>
#include <algorithm>

namespace EmojicodeCompiler {

enum class TokenType;

/// Abstract parent class of all abstract syntax tree nodes representing a $type$.
class ASTType : public ASTNode {
public:
    Type& analyseType(const TypeContext &typeContext, bool allowReference = false, bool allowGenericInference = false);
    Type& type() { assert(wasAnalysed()); return type_; }
    const Type& type() const { assert(wasAnalysed()); return type_; }
    void setOptional(bool optional) { optional_ = optional; type_ = type_.optionalized(optional); }
    void setReference() { reference_ = true; type_.setReference(); }
    bool wasAnalysed() const { return package_ == nullptr; }

    void toCode(PrettyStream &pretty) const override;

    virtual ~ASTType() = default;
protected:
    ASTType(SourcePosition p, Package *package) : ASTNode(std::move(p)), package_(package) {}
    ASTType(Type type) : ASTNode(SourcePosition()), type_(type.applyMinimalBoxing()), package_(nullptr) {}
    virtual Type getType(const TypeContext &typeContext, bool allowGenericInference) const = 0;
    Package* package() const { return package_; }
    virtual void toCodeType(PrettyStream &pretty) const = 0;
private:
    Type type_ = Type::noReturn();
    bool optional_ = false;
    bool reference_ = false;
    Package *package_;
};

template <typename Ptr>
std::vector<Type> transformTypeAstVector(const std::vector<Ptr> &vector, const TypeContext &typeContext) {
    std::vector<Type> newVector;
    newVector.reserve(vector.size());
    std::transform(vector.begin(), vector.end(), std::back_inserter(newVector), [&](auto &param) {
        return param->analyseType(typeContext);
    });
    return newVector;
}

/// Represents $type-identifier$.
class ASTTypeId : public ASTType {
public:
    ASTTypeId(std::u32string name, std::u32string ns, SourcePosition p, Package *package)
            : ASTType(std::move(p), package), name_(std::move(name)), namespace_(std::move(ns)) {}

    void addGenericArgument(std::unique_ptr<ASTType> type) { genericArgs_.emplace_back(std::move(type)); }

    void toCodeType(PrettyStream &pretty) const override;
    Type getType(const TypeContext &typeContext, bool allowGenericInference) const override;
private:
    std::u32string name_;
    std::u32string namespace_;
    std::vector<std::unique_ptr<ASTType>> genericArgs_;
};

/// Represents $callable-type$.
class ASTCallableType : public ASTType {
public:
    ASTCallableType(std::unique_ptr<ASTType> returnType, std::vector<std::unique_ptr<ASTType>> params,
                    std::unique_ptr<ASTType> errorType,
                    SourcePosition p, Package *package)
            : ASTType(std::move(p), package), return_(std::move(returnType)), params_(std::move(params)),
                errorType_(std::move(errorType)) {}

    void toCodeType(PrettyStream &pretty) const override;
    Type getType(const TypeContext &typeContext, bool allowGenericInference) const override;
private:
    std::unique_ptr<ASTType> return_;
    std::vector<std::unique_ptr<ASTType>> params_;
    std::unique_ptr<ASTType> errorType_;
};

class ASTLiteralType : public ASTType {
public:
    explicit ASTLiteralType(Type type) : ASTType(std::move(type)) {}

    void toCode(PrettyStream &pretty) const override;
    Type getType(const TypeContext &typeContext, bool allowGenericInference) const override { return Type::noReturn(); }
protected:
    void toCodeType(PrettyStream &pretty) const override {}
};

class ASTMultiProtocol : public ASTType {
public:
    ASTMultiProtocol(std::vector<std::unique_ptr<ASTType>> protocols, SourcePosition p, Package *package)
            : ASTType(std::move(p), package), protocols_(std::move(protocols)) {}

    void toCodeType(PrettyStream &pretty) const override;
    Type getType(const TypeContext &typeContext, bool allowGenericInference) const override;
private:
    std::vector<std::unique_ptr<ASTType>> protocols_;
};

class ASTTypeValueType : public ASTType {
public:
    ASTTypeValueType(std::unique_ptr<ASTType> type, TokenType tokenType, SourcePosition p, Package *package)
            : ASTType(std::move(p), package), type_(std::move(type)), tokenType_(tokenType) {}

    void toCodeType(PrettyStream &pretty) const override;
    Type getType(const TypeContext &typeContext, bool allowGenericInference) const override;

    static void checkTypeValue(TokenType tokenType, const Type &type, const TypeContext &typeContext,
                               const SourcePosition &p, Package *package);
    static std::string toString(TokenType tokenType);
private:
    std::unique_ptr<ASTType> type_;
    TokenType tokenType_;
};

class ASTGenericVariable : public ASTType {
public:
    ASTGenericVariable(std::u32string name, SourcePosition p, Package *package)
            : ASTType(p, package), name_(std::move(name)) {}

    void toCodeType(PrettyStream &pretty) const override;
    Type getType(const TypeContext &typeContext, bool allowGenericInference) const override;
private:
    std::u32string name_;
};

}  // namespace EmojicodeCompiler

#endif //EMOJICODE_ASTTYPE_HPP
