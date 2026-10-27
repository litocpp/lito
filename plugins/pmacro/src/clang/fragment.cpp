module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTImporter.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Sema/ExternalSemaSource.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

module pmacro;

import std;

#include "declarations.hpp"

namespace pmacro::clang_bridge
{

class FragmentDiagnostics final : public clang::DiagnosticConsumer {
public:
    auto HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                          const clang::Diagnostic&        diagnostic) -> void override {
        clang::DiagnosticConsumer::HandleDiagnostic(level, diagnostic);
        if (level < clang::DiagnosticsEngine::Error) return;
        llvm::SmallString<256> text;
        diagnostic.FormatDiagnostic(text);
        errors.emplace_back(text.str());
    }
    std::vector<std::string> errors;
};

class FragmentImporter final : public clang::ASTImporter {
public:
    using clang::ASTImporter::ASTImporter;

protected:
    auto ImportImpl(clang::Decl* declaration) -> llvm::Expected<clang::Decl*> override {
        if (llvm::isa<clang::ExportDecl>(declaration)) {
            auto context = ImportContext(declaration->getDeclContext());
            if (! context) return context.takeError();
            // Export affects module visibility, not the fragment's lexical lookup context.
            return MapImported(declaration, clang::Decl::castFromDeclContext(*context));
        }
        return clang::ASTImporter::ImportImpl(declaration);
    }
};

class FragmentDependencies final : public clang::ExternalSemaSource {
public:
    FragmentDependencies(clang::Sema&             host,
                         clang::CompilerInstance& child,
                         const clang::Decl&       input)
        : host_(host),
          child_(child),
          input_(input),
          scope_(host.getCurScope()),
          importer_(child.getASTContext(),
                    child.getFileManager(),
                    host.Context,
                    host.getSourceManager().getFileManager(),
                    false) {}

    auto LookupUnqualified(clang::LookupResult& result, clang::Scope*) -> bool override {
        if (! result.getLookupName().isIdentifier()) return false;
        map_contexts();
        auto                name = result.getLookupName().getAsString();
        clang::LookupResult found(host_,
                                  clang::DeclarationName(&host_.Context.Idents.get(name)),
                                  {},
                                  result.getLookupKind());
        found.suppressDiagnostics();
        if (! host_.LookupName(found, scope_) || found.empty()) return false;
        for (auto* declaration : found) {
            if (llvm::isa<clang::TemplateTypeParmDecl,
                          clang::NonTypeTemplateParmDecl,
                          clang::TemplateTemplateParmDecl>(declaration))
                continue;
            if (auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(declaration))
                if (record->isBeingDefined() || record->isInjectedClassName()) continue;
            bool enclosing = false;
            for (auto* context = input_.getLexicalDeclContext(); context;
                 context       = context->getLexicalParent())
                if (clang::Decl::castFromDeclContext(context) == declaration) enclosing = true;
            if (enclosing) continue;
            auto imported = importer_.Import(declaration);
            if (! imported) {
                llvm::consumeError(imported.takeError());
                continue;
            }
            if (auto* named = llvm::dyn_cast<clang::NamedDecl>(*imported)) result.addDecl(named);
        }
        result.resolveKind();
        return ! result.empty();
    }

private:
    auto map_contexts() -> void {
        // Reuse the lexical contexts parsed for this input instead of importing incomplete records.
        for (auto* to = child_.getSema().CurContext; to; to = to->getLexicalParent()) {
            auto* named_to = llvm::dyn_cast<clang::NamedDecl>(to);
            if (! named_to) continue;
            for (auto* from = llvm::dyn_cast<clang::DeclContext>(&input_); from;
                 from       = from->getLexicalParent()) {
                const auto* named_from = llvm::dyn_cast<clang::NamedDecl>(from);
                if (! named_from || named_from->getKind() != named_to->getKind() ||
                    named_from->getQualifiedNameAsString() != named_to->getQualifiedNameAsString())
                    continue;
                auto* declaration = const_cast<clang::NamedDecl*>(named_from);
                if (! importer_.GetAlreadyImportedOrNull(declaration))
                    importer_.MapImported(declaration, named_to);
                if (! importer_.GetAlreadyImportedOrNull(declaration->getCanonicalDecl()))
                    importer_.MapImported(declaration->getCanonicalDecl(),
                                          named_to->getCanonicalDecl());
                break;
            }
        }
    }
    clang::Sema&             host_;
    clang::CompilerInstance& child_;
    const clang::Decl&       input_;
    clang::Scope*            scope_;
    FragmentImporter         importer_;
};

class FragmentConsumer final : public clang::ASTConsumer {
public:
    using Snapshot = std::expected<std::shared_ptr<const Declaration>, DeclarationError>;
    FragmentConsumer(clang::CompilerInstance& compiler,
                     SourceRegistry&          parent,
                     std::uint32_t            offset,
                     Span                     input,
                     std::string              name,
                     Snapshot&                result)
        : sources_(compiler, parent, offset, input),
          declarations_(compiler, sources_),
          name_(std::move(name)),
          result_(result) {}

    auto HandleTagDeclDefinition(clang::TagDecl* declaration) -> void override {
        if (declaration->getQualifiedNameAsString() == name_)
            result_ = declarations_.get(*declaration);
    }

private:
    SourceRegistry   sources_;
    DeclarationStore declarations_;
    std::string      name_;
    Snapshot&        result_;
};

class FragmentAction final : public clang::ASTFrontendAction {
public:
    FragmentAction(clang::Sema&                host,
                   SourceRegistry&             parent,
                   std::uint32_t               offset,
                   Span                        input,
                   const clang::Decl&          declaration,
                   std::string                 name,
                   FragmentConsumer::Snapshot& result)
        : host_(host),
          parent_(parent),
          offset_(offset),
          input_(input),
          declaration_(declaration),
          name_(std::move(name)),
          result_(result) {}

    auto CreateASTConsumer(clang::CompilerInstance& child, llvm::StringRef)
        -> std::unique_ptr<clang::ASTConsumer> override {
        child.setExternalSemaSource(llvm::IntrusiveRefCntPtr<clang::ExternalSemaSource>(
            new FragmentDependencies(host_, child, declaration_)));
        return std::make_unique<FragmentConsumer>(child, parent_, offset_, input_, name_, result_);
    }

private:
    clang::Sema&                host_;
    SourceRegistry&             parent_;
    std::uint32_t               offset_;
    Span                        input_;
    const clang::Decl&          declaration_;
    std::string                 name_;
    FragmentConsumer::Snapshot& result_;
};

auto analyze_declaration(clang::CompilerInstance& host,
                         SourceRegistry&          sources,
                         const clang::Decl&       declaration,
                         std::string_view         input,
                         Span                     span)
    -> std::expected<std::shared_ptr<const Declaration>, DeclarationError> {
    const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(&declaration);
    if (! record || ! record->isBeingDefined())
        return std::unexpected(DeclarationError::IncompleteDefinition);
    auto contexts = std::vector<const clang::DeclContext*> {};
    for (auto* context = declaration.getLexicalDeclContext();
         context && ! context->isTranslationUnit();
         context = context->getLexicalParent())
        contexts.push_back(context);
    std::string prefix, suffix;
    for (auto context = contexts.rbegin(); context != contexts.rend(); ++context) {
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(*context))
            prefix += std::string(ns->isInline() ? "inline namespace " : "namespace ") +
                      ns->getNameAsString() + " {\n";
        else if (const auto* outer = llvm::dyn_cast<clang::RecordDecl>(*context))
            prefix += std::string(outer->isUnion()   ? "union "
                                  : outer->isClass() ? "class "
                                                     : "struct ") +
                      outer->getNameAsString() + " {\n";
        else
            continue;
        suffix = (llvm::isa<clang::RecordDecl>(*context) ? "};\n" : "}\n") + suffix;
    }
    if (auto* templated = record->getDescribedClassTemplate()) {
        llvm::raw_string_ostream out(prefix);
        templated->getTemplateParameters()->print(out, host.getASTContext());
        out.flush();
    }
    const auto              offset     = static_cast<std::uint32_t>(prefix.size());
    auto                    source     = prefix + std::string(input) + "\n" + suffix;
    auto                    invocation = std::make_shared<clang::CompilerInvocation>();
    clang::CompilerInstance child(invocation);
    auto*                   diagnostics = new FragmentDiagnostics;
    child.createDiagnostics(diagnostics);
    auto        triple      = host.getTarget().getTriple().str();
    const char* arguments[] = { "-x",      "c++",         "-std=c++23", "-fsyntax-only",
                                "-triple", triple.c_str() };
    if (! clang::CompilerInvocation::CreateFromArgs(*invocation, arguments, child.getDiagnostics()))
        return std::unexpected(DeclarationError::InvalidDeclaration);
    invocation->getDiagnosticOpts().ShowCarets = false;
    invocation->getTargetOpts()                = host.getInvocation().getTargetOpts();
    auto& language                             = invocation->getLangOpts();
    language                                   = host.getLangOpts();
    language.setCompilingModule(clang::LangOptions::CMK_None);
    language.ModuleName.clear();
    language.CurrentModule.clear();
    language.IsHeaderFile = false;
    auto buffer           = llvm::MemoryBuffer::getMemBufferCopy(source, "<pmacro-input>");
    invocation->getFrontendOpts().Inputs.emplace_back(buffer->getMemBufferRef(),
                                                      clang::InputKind(clang::Language::CXX));
    FragmentConsumer::Snapshot result = std::unexpected(DeclarationError::InvalidDeclaration);
    FragmentAction             action(host.getSema(),
                                      sources,
                                      offset,
                                      span,
                                      declaration,
                                      record->getQualifiedNameAsString(),
                                      result);
    if (! child.ExecuteAction(action) || child.getDiagnostics().hasErrorOccurred())
        return std::unexpected(DeclarationError::InvalidDeclaration);
    return result;
}

} // namespace pmacro::clang_bridge
