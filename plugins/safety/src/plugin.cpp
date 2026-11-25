module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/DiagnosticFrontend.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>

module safety;

import std;

#include "checker.hpp"

namespace safety_plugin
{

auto attribute_diagnostic(clang::Sema&          sema,
                          clang::SourceLocation location,
                          llvm::StringRef       message) -> void {
    sema.getDiagnostics().Report(location, clang::diag::err_fe_backend_plugin) << message;
}

class UnsafeAttribute final : public clang::ParsedAttrInfo {
public:
    UnsafeAttribute() {
        static constexpr Spelling spellings[] = { { clang::ParsedAttr::AS_CXX11, "unsafe" } };
        Spellings                             = spellings;
    }

    auto diagAppertainsToDecl(clang::Sema&             sema,
                              const clang::ParsedAttr& attribute,
                              const clang::Decl*       declaration) const -> bool override {
        if (llvm::isa<clang::FunctionDecl>(declaration)) return true;
        attribute_diagnostic(
            sema, attribute.getLoc(), "'unsafe' applies only to functions and compound statements");
        return false;
    }

    auto diagAppertainsToStmt(clang::Sema&             sema,
                              const clang::ParsedAttr& attribute,
                              const clang::Stmt*       statement) const -> bool override {
        if (llvm::isa<clang::CompoundStmt>(statement)) return true;
        attribute_diagnostic(
            sema, attribute.getLoc(), "'unsafe' applies only to functions and compound statements");
        return false;
    }

    auto handleDeclAttribute(clang::Sema&             sema,
                             clang::Decl*             declaration,
                             const clang::ParsedAttr& attribute) const -> AttrHandling override {
        if (! no_arguments(sema, attribute)) return AttributeNotApplied;
        declaration->addAttr(
            clang::AnnotateAttr::Create(sema.Context, "unsafe", nullptr, 0, attribute.getRange()));
        return AttributeApplied;
    }

    auto handleStmtAttribute(clang::Sema& sema,
                             clang::Stmt*,
                             const clang::ParsedAttr& attribute,
                             clang::Attr*&            result) const -> AttrHandling override {
        if (! no_arguments(sema, attribute)) return AttributeNotApplied;
        result =
            clang::AnnotateAttr::Create(sema.Context, "unsafe", nullptr, 0, attribute.getRange());
        return AttributeApplied;
    }

private:
    static auto no_arguments(clang::Sema& sema, const clang::ParsedAttr& attribute) -> bool {
        if (attribute.getNumArgs() == 0) return true;
        attribute_diagnostic(sema, attribute.getLoc(), "'unsafe' accepts no arguments");
        return false;
    }
};

class Plugin final : public clang::PluginASTAction {
    bool annotations_only_ { false };

public:
    auto CreateASTConsumer(clang::CompilerInstance& compiler, llvm::StringRef)
        -> std::unique_ptr<clang::ASTConsumer> override {
        if (annotations_only_) return std::make_unique<clang::ASTConsumer>();
        return make_consumer(compiler);
    }

    auto ParseArgs(const clang::CompilerInstance&  compiler,
                   const std::vector<std::string>& arguments) -> bool override {
        if (arguments.empty()) return true;
        if (arguments.size() == 1 && arguments.front() == "annotations-only") {
            annotations_only_ = true;
            return true;
        }
        compiler.getDiagnostics().Report(clang::diag::err_fe_backend_plugin)
            << "safety plugin accepts only the 'annotations-only' argument";
        return false;
    }

    auto getActionType() -> ActionType override { return AddBeforeMainAction; }
};

} // namespace safety_plugin

static clang::ParsedAttrInfoRegistry::Add<safety_plugin::UnsafeAttribute>
    unsafe_attribute("unsafe", "marks unsafe functions and compound statements");
static clang::FrontendPluginRegistry::Add<safety_plugin::Plugin>
    safety_plugin_registration("safety", "checks explicit unsafe operations");
