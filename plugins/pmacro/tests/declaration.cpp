module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>

module pmacro;

import std;

#include "../src/clang/declarations.hpp"

namespace pmacro::clang_bridge
{

auto require(bool condition, std::string_view message) -> void {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct TestState {
    std::shared_ptr<const Declaration> saved;
    CodeFragment                       fragment;
    Span                               span;
    unsigned                           pass    = 0;
    unsigned                           checked = 0;
};

class DeclarationConsumer final : public clang::ASTConsumer {
public:
    DeclarationConsumer(clang::CompilerInstance& compiler, TestState& state)
        : sources_(compiler), declarations_(compiler, sources_), state_(state) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        if (state_.pass == 1) {
            require(state_.saved->name == "Owned", "snapshot name did not outlive AST");
            const auto& field = std::get<RecordInfo>(state_.saved->data).fields.front();
            require(field.name == "value" && field.initializer->code->text == "42",
                    "snapshot text did not outlive AST");
            require(! sources_.valid(state_.span), "old file identity was reused");
            const auto result = sources_.fragment_source(state_.fragment, sources_.main_span(0, 0));
            require(! result && result.error() == FragmentError::ForeignSource,
                    "old fragment was accepted in a new context");
            state_.fragment.source.spelling  = std::unexpected(SourceError::Unavailable);
            state_.fragment.source.expansion = std::unexpected(SourceError::Unavailable);
            state_.fragment.source.diagnostic.reset();
            const auto no_span =
                sources_.fragment_source(state_.fragment, sources_.main_span(0, 0));
            require(! no_span && no_span.error() == FragmentError::ForeignSource,
                    "old fragment without spans was accepted");
            ++state_.checked;
            return;
        }
        for (auto* declaration : context.getTranslationUnitDecl()->decls()) {
            auto* named = llvm::dyn_cast<clang::NamedDecl>(declaration);
            if (! named || named->isImplicit()) continue;
            const auto name   = named->getNameAsString();
            auto       result = declarations_.get(*declaration);
            auto       again  = declarations_.get(*declaration);
            if (name == "Forward") {
                require(! result && ! again &&
                            result.error() == DeclarationError::IncompleteDefinition &&
                            again.error() == result.error(),
                        "forward declaration error/cache");
            } else if (name == "ordinary") {
                require(! result && result.error() == DeclarationError::UnsupportedDeclaration,
                        "ordinary declaration error");
            } else if (name == "Broken") {
                require(! result && result.error() == DeclarationError::InvalidDeclaration,
                        "invalid definition error");
            } else if (name == "Owned") {
                require(result && again && *result == *again, "snapshot not cached");
                state_.saved       = *result;
                const auto& record = std::get<RecordInfo>((*result)->data);
                require(record.fields.size() == 1, "owned field count");
                state_.fragment = *record.fields.front().initializer->code;
                state_.span     = *state_.fragment.source.spelling;
                require(sources_.fragment_source(state_.fragment, state_.span).has_value(),
                        "valid spelled fragment rejected");
                auto changed = state_.fragment;
                changed.text += "0";
                require(! sources_.fragment_source(changed, state_.span),
                        "changed spelling accepted");
                CodeFragment rendered { "a_long_generated_type",
                                        CodeOrigin::Rendered,
                                        (*result)->source };
                auto         anchor = sources_.fragment_source(rendered, state_.span);
                require(anchor && ! anchor->exact &&
                            anchor->span.end() - anchor->span.begin() < rendered.text.size(),
                        "rendered fragment fabricated a text-sized source span");
                rendered.source.diagnostic.reset();
                require(sources_.fragment_source(rendered, state_.span).has_value(),
                        "fallback anchor rejected");
            } else if (name == "Empty") {
                require(result && std::get<RecordInfo>((*result)->data).fields.empty(),
                        "empty record rejected");
            } else if (name == "Anonymous") {
                require(result.has_value(), "anonymous record parent rejected");
                const auto& fields = std::get<RecordInfo>((*result)->data).fields;
                require(fields.size() == 1 && ! fields.front().type.type_id &&
                            fields.front().type.type_id.error() == RepresentationError::Unnameable,
                        "unnameable field type rendered");
            } else
                continue;
            ++state_.checked;
        }
    }

private:
    SourceRegistry   sources_;
    DeclarationStore declarations_;
    TestState&       state_;
};

class DeclarationAction final : public clang::ASTFrontendAction {
public:
    explicit DeclarationAction(TestState& state): state_(state) {}
    auto CreateASTConsumer(clang::CompilerInstance& compiler, llvm::StringRef)
        -> std::unique_ptr<clang::ASTConsumer> override {
        return std::make_unique<DeclarationConsumer>(compiler, state_);
    }

private:
    TestState& state_;
};

} // namespace pmacro::clang_bridge

extern "C++" int main() {
    using namespace pmacro::clang_bridge;
    TestState state;
    require(clang::tooling::runToolOnCodeWithArgs(
                std::make_unique<DeclarationAction>(state),
                "struct Forward; int ordinary; struct Owned { int value = 42; }; struct Empty {}; "
                "struct Anonymous { struct { int x; } value; };",
                { "-std=c++23" },
                "first.cpp"),
            "first AST failed");
    require(state.checked == 5 && state.saved, "missing first AST checks");
    state.pass = 1;
    require(clang::tooling::runToolOnCodeWithArgs(std::make_unique<DeclarationAction>(state),
                                                  "int second;",
                                                  { "-std=c++23" },
                                                  "second.cpp"),
            "second AST failed");
    state.pass = 2;
    require(! clang::tooling::runToolOnCodeWithArgs(std::make_unique<DeclarationAction>(state),
                                                    "struct Broken { Missing value; };",
                                                    { "-std=c++23" },
                                                    "invalid.cpp"),
            "invalid source unexpectedly compiled");
    require(state.checked == 7, "missing lifetime/error checks");
    std::cout << "declaration owner: snapshot lifetime, source identity, cached errors and "
                 "representation passed\n";
}
