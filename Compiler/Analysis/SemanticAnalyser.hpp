//
// Created by Theo Weidmann on 26.02.18.
//

#ifndef EMOJICODE_SEMANTICANALYSER_HPP
#define EMOJICODE_SEMANTICANALYSER_HPP

#include <queue>
#include <memory>

namespace EmojicodeCompiler {

class Function;
class Package;
class TypeDefinition;
class Type;
class TypeContext;
class Compiler;
class Class;
struct ProtocolConformance;
struct SourcePosition;

/// Manages the semantic analysis of a package.
class SemanticAnalyser {
public:
    explicit SemanticAnalyser(Package *package, bool imported) : package_(package), imported_(imported) {}

    /// Analyses the package.
    /// @throws CompilerError if an unrecoverable error occurs, e.g. if the start flag function is not present but
    /// must be.
    /// @param executable True if this package will be linked to an executable. Requires, for instance, that a start
    /// flag function be present.
    void analyse(bool executable);

    void enqueueFunction(Function *);

    /// Iff `type` is a literal type, returns the default inferred type for the literal type. Otherwise the type is
    /// returned.
    Type defaultLiteralType(const Type &literal) const;

    Compiler* compiler() const;

    /// Checks that no promises were broken and builds a boxing layer to keep promises if necessary.
    /// @returns A function that can serve as boxing layer, if necessary, or nullptr.
    std::unique_ptr<Function> enforcePromises(Function *sub, Function *super, const Type &superSource,
                                              const TypeContext &subContext, const TypeContext &superContext);

    void analyseFunctionDeclaration(Function *function) const;

    void declareInstanceVariables(const Type &type);

private:
    void analyseQueue();
    void enqueueFunctionsOfTypeDefinition(TypeDefinition *typeDef);
    void finalizeProtocols(const Type &type);
    void checkProtocolConformance(const Type &type);
    void finalizeProtocol(const Type &type, ProtocolConformance &conformance);
    void checkStartFlagFunction(bool executable);

    Package *package_;
    std::queue<Function *> queue_;
    bool imported_;

    bool checkArgumentPromise(const Function *sub, const Function *super, const TypeContext &subContext,
                                  const TypeContext &superContext) const;
    bool checkReturnPromise(const Function *sub, const TypeContext &subContext, const Function *super,
                            const TypeContext &superContext, const Type &superSource) const;
};

}  // namespace EmojicodeCompiler

#endif //EMOJICODE_SEMANTICANALYSER_HPP
