module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/DiagnosticFrontend.h>
#include <clang/Frontend/CompilerInstance.h>

module unsafe;

import std;

#include "checker.hpp"

namespace unsafe_plugin
{

auto has_unsafe_annotation(const clang::Decl& declaration) -> bool {
    for (const auto* attribute : declaration.specific_attrs<clang::AnnotateAttr>())
        if (attribute->getAnnotation() == "unsafe") return true;
    return false;
}

auto own_unsafe_contract(const clang::FunctionDecl& function) -> bool {
    for (const auto* declaration : function.redecls())
        if (has_unsafe_annotation(*declaration)) return true;
    if (const auto* pattern = function.getTemplateInstantiationPattern();
        pattern != nullptr && pattern != &function)
        return own_unsafe_contract(*pattern);
    return false;
}

enum class Operation : unsigned
{
    Annotation,
    Dereference,
    Arrow,
    Subscript,
    Arithmetic,
    New,
    Delete,
    Cast,
    Call,
    Address,
    Override,
    Destructor
};

struct Evaluation {
    bool                  allowed {};
    const clang::Expr*    direct_reference {};
    clang::SourceLocation use;
};

class Checker : public clang::RecursiveASTVisitor<Checker> {
    using Visitor = clang::RecursiveASTVisitor<Checker>;

    clang::CompilerInstance&                             compiler_;
    Evaluation                                           evaluation_;
    std::unordered_set<const clang::FunctionDecl*>       functions_;
    std::unordered_map<const clang::FunctionDecl*, bool> contracts_;
    std::unordered_set<std::uint64_t>                    reported_;
    std::unordered_set<const clang::CXXCtorInitializer*> initializers_;

    auto local(clang::SourceLocation location) const -> bool {
        const auto& sources = compiler_.getSourceManager();
        return location.isValid() && ! sources.isInSystemHeader(sources.getExpansionLoc(location));
    }

    auto report(clang::SourceLocation location, Operation operation, llvm::StringRef message)
        -> void {
        if (! local(location)) return;
        const auto key = (static_cast<std::uint64_t>(location.getRawEncoding()) << 8) |
                         static_cast<unsigned>(operation);
        if (! reported_.insert(key).second) return;
        compiler_.getDiagnostics().Report(location, clang::diag::err_fe_backend_plugin) << message;
    }

    auto require(const clang::Stmt& statement,
                 Evaluation         evaluation,
                 Operation          operation,
                 llvm::StringRef    message) -> void {
        if (! evaluation.allowed)
            report(evaluation.use.isValid() ? evaluation.use : statement.getBeginLoc(),
                   operation,
                   message);
    }

    auto contract(const clang::FunctionDecl& function) -> bool {
        const auto* canonical = function.getCanonicalDecl();
        if (const auto found = contracts_.find(canonical); found != contracts_.end())
            return found->second;
        auto unsafe = own_unsafe_contract(function);
        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function))
            for (const auto* overridden : method->overridden_methods())
                unsafe |= contract(*overridden);
        contracts_.emplace(canonical, unsafe);
        return unsafe;
    }

    auto validate_function(clang::FunctionDecl& function) -> void {
        if (! own_unsafe_contract(function)) return;
        if (llvm::isa<clang::CXXDestructorDecl>(function))
            report(function.getLocation(),
                   Operation::Destructor,
                   "unsafe: destructor declarations cannot have an unsafe contract");
        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function))
            for (const auto* overridden : method->overridden_methods())
                if (! contract(*overridden)) {
                    report(function.getLocation(),
                           Operation::Override,
                           "unsafe: override cannot strengthen a safe base contract");
                    break;
                }
    }

    auto declaration(clang::Decl* declaration, Evaluation evaluation) -> void {
        const auto saved = std::exchange(evaluation_, evaluation);
        TraverseDecl(declaration);
        evaluation_ = saved;
    }

    auto function(clang::FunctionDecl& function) -> void {
        validate_function(function);
        if (function.isDependentContext() || function.getDescribedFunctionTemplate() != nullptr ||
            ! function.isThisDeclarationADefinition() ||
            ! functions_.insert(function.getCanonicalDecl()).second)
            return;
        if (function.isImplicit() && ! llvm::isa<clang::CXXConstructorDecl>(function)) {
            const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
            if (method == nullptr || ! method->getParent()->isLambda() ||
                method->getOverloadedOperator() != clang::OO_Call)
                return;
        }
        if (auto* constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(&function))
            for (const auto* initializer : constructor->inits())
                if (! initializer->isInClassMemberInitializer())
                    statement(initializer->getInit(), {});
        statement(function.getBody(), {});
    }

    auto function_address(const clang::FunctionDecl& function,
                          const clang::Stmt&         expression,
                          Evaluation                 evaluation) -> void {
        if (&expression == evaluation.direct_reference) return;
        if (contract(function))
            require(expression,
                    evaluation,
                    Operation::Address,
                    "unsafe: taking an unsafe function address requires an unsafe block");
    }

    static auto pointer(clang::QualType type) -> bool {
        return ! type.isNull() && ! type->isDependentType() && type->isPointerType();
    }

    static auto safe_arrow_base(const clang::Expr& expression) -> bool {
        const auto* base = expression.IgnoreParenImpCasts();
        if (llvm::isa<clang::CXXThisExpr>(base)) return true;
        const auto* call   = llvm::dyn_cast<clang::CallExpr>(base);
        const auto* callee = call == nullptr ? nullptr : call->getDirectCallee();
        return callee != nullptr && callee->getOverloadedOperator() == clang::OO_Arrow;
    }

    static auto removes_qualifiers(clang::QualType from, clang::QualType to) -> bool {
        if (to->isReferenceType()) to = to->getPointeeType();
        while (! from.isNull() && ! to.isNull()) {
            if ((from.getCVRQualifiers() & ~to.getCVRQualifiers()) != 0) return true;
            if (! from->isPointerType() || ! to->isPointerType()) break;
            from = from->getPointeeType();
            to   = to->getPointeeType();
        }
        return false;
    }

    static auto unsafe_cast(const clang::ExplicitCastExpr& cast) -> bool {
        const auto from = cast.getSubExpr()->getType();
        const auto to   = cast.getTypeAsWritten();
        if (from->isDependentType() || to->isDependentType()) return false;
        const auto pointer_or_reference = pointer(from) || pointer(to) || to->isReferenceType();
        if (! pointer_or_reference) return false;
        if (llvm::isa<clang::CXXReinterpretCastExpr>(cast)) return true;
        if (removes_qualifiers(from, to)) return true;
        if (llvm::isa<clang::CXXConstCastExpr, clang::CXXDynamicCastExpr>(cast)) return false;
        switch (cast.getCastKind()) {
        case clang::CK_BitCast:
            return ! (pointer(to) && to->getPointeeType()->isVoidType() && pointer(from) &&
                      from->getPointeeType()->isObjectType());
        case clang::CK_LValueBitCast:
        case clang::CK_IntegralToPointer:
        case clang::CK_PointerToIntegral:
        case clang::CK_BaseToDerived: return true;
        default: return false;
        }
    }

    auto call(clang::CallExpr& call, Evaluation evaluation) -> void {
        if (call.isTypeDependent() || call.isValueDependent()) return;
        if (call.isUnevaluatedBuiltinCall(compiler_.getASTContext())) return;
        auto* callee = call.getDirectCallee();
        if (callee == nullptr || contract(*callee))
            require(call,
                    evaluation,
                    Operation::Call,
                    callee == nullptr ? "unsafe: indirect call requires an unsafe block"
                                      : "unsafe: unsafe function call requires an unsafe block");
        auto callee_evaluation             = evaluation;
        callee_evaluation.direct_reference = call.getCallee()->IgnoreParenImpCasts();
        statement(call.getCallee(), callee_evaluation);
        for (auto* argument : call.arguments()) statement(argument, evaluation);
    }

    auto construction(clang::CXXConstructExpr& expression, Evaluation evaluation) -> void {
        if (expression.isTypeDependent()) return;
        const auto* constructor = expression.getConstructor();
        if (constructor != nullptr && contract(*constructor))
            require(expression,
                    evaluation,
                    Operation::Call,
                    "unsafe: unsafe constructor call requires an unsafe block");
        for (auto* argument : expression.arguments()) statement(argument, evaluation);
        if (constructor != nullptr)
            for (const auto* initializer : constructor->inits())
                if (initializer->isInClassMemberInitializer() &&
                    initializers_.insert(initializer).second) {
                    auto use = evaluation;
                    use.use  = expression.getExprLoc();
                    statement(initializer->getInit(), use);
                    initializers_.erase(initializer);
                }
    }

    auto statement(clang::Stmt* node, Evaluation evaluation) -> void {
        if (node == nullptr) return;
        if (auto* attributes = llvm::dyn_cast<clang::AttributedStmt>(node)) {
            for (const auto* attribute : attributes->getAttrs())
                if (const auto* annotation = llvm::dyn_cast<clang::AnnotateAttr>(attribute);
                    annotation != nullptr && annotation->getAnnotation() == "unsafe") {
                    if (llvm::isa<clang::CompoundStmt>(attributes->getSubStmt()))
                        evaluation.allowed = true;
                    else
                        report(attribute->getLocation(),
                               Operation::Annotation,
                               "unsafe: a statement annotation requires a compound block");
                }
            statement(attributes->getSubStmt(), evaluation);
            return;
        }
        if (llvm::isa<clang::UnaryExprOrTypeTraitExpr,
                      clang::CXXNoexceptExpr,
                      clang::RequiresExpr,
                      clang::CXXUuidofExpr>(node))
            return;
        if (auto* typeid_expression = llvm::dyn_cast<clang::CXXTypeidExpr>(node)) {
            if (typeid_expression->isPotentiallyEvaluated())
                statement(typeid_expression->getExprOperand(), evaluation);
            return;
        }
        if (auto* lambda = llvm::dyn_cast<clang::LambdaExpr>(node)) {
            for (auto* initializer : lambda->capture_inits()) statement(initializer, evaluation);
            declaration(lambda->getLambdaClass(), {});
            return;
        }
        if (auto* declarations = llvm::dyn_cast<clang::DeclStmt>(node)) {
            for (auto* child : declarations->decls()) declaration(child, evaluation);
            return;
        }
        if (auto* argument = llvm::dyn_cast<clang::CXXDefaultArgExpr>(node)) {
            evaluation.use = argument->getUsedLocation();
            statement(argument->getExpr(), evaluation);
            return;
        }
        if (auto* initializer = llvm::dyn_cast<clang::CXXDefaultInitExpr>(node)) {
            if (evaluation.use.isInvalid()) evaluation.use = initializer->getUsedLocation();
            statement(initializer->getExpr(), evaluation);
            return;
        }
        if (auto* conditional = llvm::dyn_cast<clang::IfStmt>(node);
            conditional != nullptr && conditional->isConstexpr()) {
            statement(conditional->getInit(), evaluation);
            statement(conditional->getConditionVariableDeclStmt(), evaluation);
            statement(conditional->getCond(), evaluation);
            if (const auto chosen = conditional->getNondiscardedCase(compiler_.getASTContext());
                chosen.has_value())
                statement(*chosen, evaluation);
            return;
        }
        if (auto* selection = llvm::dyn_cast<clang::GenericSelectionExpr>(node)) {
            if (! selection->isResultDependent()) statement(selection->getResultExpr(), evaluation);
            return;
        }
        if (auto* selection = llvm::dyn_cast<clang::ChooseExpr>(node)) {
            if (! selection->isConditionDependent())
                statement(selection->getChosenSubExpr(), evaluation);
            return;
        }
        if (auto* invocation = llvm::dyn_cast<clang::CallExpr>(node)) {
            call(*invocation, evaluation);
            return;
        }
        if (auto* expression = llvm::dyn_cast<clang::CXXConstructExpr>(node)) {
            construction(*expression, evaluation);
            return;
        }
        if (const auto* unary = llvm::dyn_cast<clang::UnaryOperator>(node)) {
            if (unary->getOpcode() == clang::UO_Deref && pointer(unary->getSubExpr()->getType()) &&
                ! llvm::isa<clang::CXXThisExpr>(unary->getSubExpr()->IgnoreParenImpCasts()))
                require(*unary,
                        evaluation,
                        Operation::Dereference,
                        "unsafe: dereference requires an unsafe block");
            if (unary->isIncrementDecrementOp() && pointer(unary->getSubExpr()->getType()))
                require(*unary,
                        evaluation,
                        Operation::Arithmetic,
                        "unsafe: pointer arithmetic requires an unsafe block");
        }
        if (const auto* binary = llvm::dyn_cast<clang::BinaryOperator>(node)) {
            const auto opcode = binary->getOpcode();
            if ((opcode == clang::BO_Add || opcode == clang::BO_Sub ||
                 opcode == clang::BO_AddAssign || opcode == clang::BO_SubAssign) &&
                (pointer(binary->getLHS()->getType()) || pointer(binary->getRHS()->getType())))
                require(*binary,
                        evaluation,
                        Operation::Arithmetic,
                        "unsafe: pointer arithmetic requires an unsafe block");
            if (opcode == clang::BO_PtrMemI)
                require(*binary,
                        evaluation,
                        Operation::Arrow,
                        "unsafe: raw pointer arrow requires an unsafe block");
        }
        if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(node)) {
            if (member->isArrow() && pointer(member->getBase()->getType()) &&
                ! safe_arrow_base(*member->getBase()))
                require(*member,
                        evaluation,
                        Operation::Arrow,
                        "unsafe: raw pointer arrow requires an unsafe block");
            if (const auto* method = llvm::dyn_cast<clang::FunctionDecl>(member->getMemberDecl()))
                function_address(*method, *member, evaluation);
        }
        if (const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(node))
            if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(reference->getDecl()))
                function_address(*function, *reference, evaluation);
        if (const auto* subscript = llvm::dyn_cast<clang::ArraySubscriptExpr>(node);
            subscript != nullptr && ! subscript->isTypeDependent())
            require(*subscript,
                    evaluation,
                    Operation::Subscript,
                    "unsafe: built-in subscript requires an unsafe block");
        if (const auto* allocation = llvm::dyn_cast<clang::CXXNewExpr>(node))
            require(
                *allocation, evaluation, Operation::New, "unsafe: new requires an unsafe block");
        if (const auto* deletion = llvm::dyn_cast<clang::CXXDeleteExpr>(node))
            require(*deletion,
                    evaluation,
                    Operation::Delete,
                    "unsafe: delete requires an unsafe block");
        if (const auto* cast = llvm::dyn_cast<clang::ExplicitCastExpr>(node);
            cast != nullptr && unsafe_cast(*cast))
            require(*cast,
                    evaluation,
                    Operation::Cast,
                    "unsafe: pointer or reference cast requires an unsafe block");
        for (auto* child : node->children()) statement(child, evaluation);
    }

public:
    explicit Checker(clang::CompilerInstance& compiler): compiler_(compiler) {}

    auto shouldVisitTemplateInstantiations() const -> bool { return true; }
    auto shouldVisitImplicitCode() const -> bool { return true; }
    auto TraverseTypeLoc(clang::TypeLoc, bool = true) -> bool { return true; }
    auto TraverseStmt(clang::Stmt* node, Visitor::DataRecursionQueue* = nullptr) -> bool {
        statement(node, evaluation_);
        return true;
    }

    auto TraverseDecl(clang::Decl* node) -> bool {
        if (node == nullptr || node->isInvalidDecl() || node->isFromASTFile() ||
            (node->getLocation().isValid() && ! local(node->getLocation())))
            return true;
        if (has_unsafe_annotation(*node) && ! llvm::isa<clang::FunctionDecl>(node))
            report(node->getLocation(),
                   Operation::Annotation,
                   "unsafe: declaration annotation requires a function");
        if (auto* callable = llvm::dyn_cast<clang::FunctionDecl>(node)) {
            function(*callable);
            return true;
        }
        if (llvm::isa<clang::ParmVarDecl, clang::FieldDecl>(node)) return true;
        if (auto* variable = llvm::dyn_cast<clang::VarDecl>(node)) {
            if (! variable->getDeclContext()->isDependentContext())
                statement(variable->getInit(), evaluation_);
            return true;
        }
        if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(node)) {
            if (record->isDependentContext()) return true;
            const auto saved  = std::exchange(evaluation_, Evaluation {});
            const auto result = Visitor::TraverseDecl(node);
            evaluation_       = saved;
            return result;
        }
        return Visitor::TraverseDecl(node);
    }
};

class UnsafeConsumer final : public clang::ASTConsumer {
    Checker checker_;

public:
    explicit UnsafeConsumer(clang::CompilerInstance& compiler): checker_(compiler) {}

    auto HandleTranslationUnit(clang::ASTContext& context) -> void override {
        checker_.TraverseAST(context);
    }
};

auto make_consumer(clang::CompilerInstance& compiler) -> std::unique_ptr<clang::ASTConsumer> {
    return std::make_unique<UnsafeConsumer>(compiler);
}

} // namespace unsafe_plugin
