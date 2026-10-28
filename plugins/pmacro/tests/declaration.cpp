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
    std::shared_ptr<const Declaration> function;
    std::shared_ptr<const Declaration> methods;
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
            const auto& function = std::get<FunctionInfo>(state_.function->data);
            require(function.parameters[0].name == "value" &&
                        function.parameters[0].default_argument.expression->code->text == "9" &&
                        function.return_type.type_id->text == "int",
                    "function snapshot did not outlive AST");
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
            const auto& methods = std::get<RecordInfo>(state_.methods->data).methods;
            require(methods[0].kind == MethodKind::Constructor && methods[1].const_ &&
                        methods[1].ref_qualifier == RefQualifier::LValue &&
                        methods[1].function.parameters[0].type.type_id->text == "int",
                    "method snapshot did not outlive AST");
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
            } else if (name == "function_owned") {
                require(result && again && *result == *again, "function snapshot not cached");
                const auto& function = std::get<FunctionInfo>((*result)->data);
                require(function.form == FunctionForm::Definition &&
                            function.parameters.size() == 1 &&
                            function.exception.state == ExceptionState::NonThrowing,
                        "completed function signature");
                state_.function = *result;
            } else if (name == "function_deleted") {
                require(result &&
                            std::get<FunctionInfo>((*result)->data).form == FunctionForm::Deleted,
                        "completed deleted function form");
            } else if (name == "Methods") {
                require(result.has_value(), "method record unavailable");
                state_.methods      = *result;
                const auto& methods = std::get<RecordInfo>((*result)->data).methods;
                require(methods.size() >= 8 && methods[0].kind == MethodKind::Constructor &&
                            methods[1].name == "read" && methods[1].const_ &&
                            methods[2].ref_qualifier == RefQualifier::RValue &&
                            methods[3].function.static_ && methods[4].access == Access::Private &&
                            methods[5].kind == MethodKind::Destructor && methods[5].virtual_ &&
                            methods[6].kind == MethodKind::Conversion &&
                            methods[7].kind == MethodKind::Operator,
                        "method facts missing");
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
                "struct Anonymous { struct { int x; } value; }; "
                "int function_owned(int value = 9) noexcept { return value; } "
                "void function_deleted() = delete; "
                "class Methods { public: explicit Methods(int); int read(int) const &; void "
                "consume() &&; "
                "static int count(); private: void hidden(); public: virtual ~Methods(); "
                "operator bool() const; int operator+(int) const; };",
                { "-std=c++23" },
                "first.cpp"),
            "first AST failed");
    require(state.checked == 8 && state.saved && state.function, "missing first AST checks");
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
    require(state.checked == 10, "missing lifetime/error checks");
    std::cout << "declaration owner: snapshot lifetime, source identity, cached errors and "
                 "representation passed\n";
}
