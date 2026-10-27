module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/Parser.h>
#include <clang/Sema/SemaConsumer.h>
#include <llvm/Support/MemoryBuffer.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticFrontend.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/LiteralSupport.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/raw_ostream.h>

module pmacro;

import std;

#include "declarations.hpp"

namespace pmacro::clang_bridge
{

auto prepare_invocation(clang::Sema&, clang::Decl&) -> void;

constexpr llvm::StringLiteral define_annotation         = "pmacro.define";
constexpr llvm::StringLiteral define_scope_annotation   = "pmacro.define:";
constexpr llvm::StringLiteral attr_annotation           = "pmacro.attr:";
constexpr llvm::StringLiteral attr_arguments_annotation = "pmacro.attr.arguments:";
constexpr llvm::StringLiteral derive_annotation         = "pmacro.derive:";
constexpr llvm::StringLiteral helper_annotation         = "pmacro.helper:";

auto report(clang::DiagnosticsEngine&       diagnostics,
            clang::SourceLocation           location,
            clang::DiagnosticsEngine::Level level,
            llvm::StringRef                 message) -> void {
    // Custom warning IDs do not survive PCM diagnostic mapping serialization.
    const auto id = level == clang::DiagnosticsEngine::Warning ? clang::diag::warn_fe_backend_plugin
                    : level == clang::DiagnosticsEngine::Note  ? clang::diag::note_fe_backend_plugin
                                                               : clang::diag::err_fe_backend_plugin;
    diagnostics.Report(location, id) << message;
}

struct ParsedStringArguments {
    std::vector<std::string> values;
    clang::SourceRange       range;
    clang::CharSourceRange   tail;
};

auto parse_string_arguments(clang::Sema&             sema,
                            const clang::ParsedAttr& attribute,
                            llvm::StringRef          spelling,
                            bool                     first_only,
                            bool allow_empty = false) -> std::optional<ParsedStringArguments> {
    auto result = ParsedStringArguments { .range = attribute.getRange() };
    if (attribute.getNumArgs() != 0) {
        const auto count = first_only ? 1u : attribute.getNumArgs();
        for (unsigned index = 0; index < count; ++index) {
            const auto* expression = attribute.getArgAsExpr(index);
            const auto* literal =
                expression == nullptr
                    ? nullptr
                    : llvm::dyn_cast<clang::StringLiteral>(expression->IgnoreParenCasts());
            if (literal == nullptr || literal->getString().empty()) return std::nullopt;
            result.values.push_back(literal->getString().str());
        }
        if (first_only) {
            auto begin = attribute.getRange().getEnd();
            auto end   = begin;
            if (attribute.getNumArgs() > 1) {
                begin = attribute.getArgAsExpr(1)->getBeginLoc();
                end   = clang::Lexer::getLocForEndOfToken(
                    attribute.getArgAsExpr(attribute.getNumArgs() - 1)->getEndLoc(),
                    0,
                    sema.getSourceManager(),
                    sema.getLangOpts());
            }
            result.tail = clang::CharSourceRange::getCharRange(begin, end);
        }
        return result;
    }

    auto&       manager  = sema.getSourceManager();
    const auto& language = sema.getLangOpts();
    auto        location =
        clang::Lexer::getLocForEndOfToken(attribute.getRange().getEnd(), 0, manager, language);
    auto token = clang::Token {};
    if (clang::Lexer::getRawToken(location, token, manager, language, true) ||
        ! token.is(clang::tok::l_paren)) {
        if (allow_empty) return result;
        return std::nullopt;
    }
    location = clang::Lexer::getLocForEndOfToken(token.getLocation(), 0, manager, language);
    auto       tail_begin       = clang::SourceLocation {};
    auto       depth            = 0u;
    auto       argument_tokens  = 0u;
    auto       validating       = true;
    auto       argument_literal = std::optional<std::string> {};
    const auto finish_argument  = [&]() -> bool {
        if (! validating) return true;
        if (argument_tokens != 1 || ! argument_literal.has_value() || argument_literal->empty())
            return false;
        result.values.push_back(std::move(*argument_literal));
        argument_tokens  = 0;
        argument_literal = std::nullopt;
        if (first_only) validating = false;
        return true;
    };
    for (;;) {
        if (clang::Lexer::getRawToken(location, token, manager, language, true) ||
            token.is(clang::tok::eof))
            return std::nullopt;
        if (token.is(clang::tok::r_paren) && depth == 0) {
            if ((! allow_empty || argument_tokens != 0 || ! result.values.empty()) &&
                ! finish_argument())
                return std::nullopt;
            result.range.setEnd(token.getLocation());
            if (first_only)
                result.tail = clang::CharSourceRange::getCharRange(
                    tail_begin.isValid() ? tail_begin : token.getLocation(), token.getLocation());
            break;
        }
        if (token.is(clang::tok::comma) && depth == 0) {
            if (! finish_argument()) return std::nullopt;
            if (first_only && tail_begin.isInvalid())
                tail_begin =
                    clang::Lexer::getLocForEndOfToken(token.getLocation(), 0, manager, language);
            location = clang::Lexer::getLocForEndOfToken(token.getLocation(), 0, manager, language);
            continue;
        }
        if (validating) {
            ++argument_tokens;
            if (argument_tokens == 1 && token.is(clang::tok::string_literal)) {
                const auto parser = clang::StringLiteralParser(
                    llvm::ArrayRef<clang::Token>(&token, 1), sema.getPreprocessor());
                if (! parser.hadError) argument_literal = parser.GetString().str();
            }
        }
        if (token.isOneOf(clang::tok::l_paren, clang::tok::l_square, clang::tok::l_brace)) {
            ++depth;
        } else if (token.isOneOf(clang::tok::r_paren, clang::tok::r_square, clang::tok::r_brace)) {
            if (depth == 0) return std::nullopt;
            --depth;
        }
        location = clang::Lexer::getLocForEndOfToken(token.getLocation(), 0, manager, language);
    }
    if (result.values.empty() && ! allow_empty) {
        auto message = spelling.str();
        message.append(" requires at least one macro identity");
        report(sema.getDiagnostics(), attribute.getLoc(), clang::DiagnosticsEngine::Error, message);
        return std::nullopt;
    }
    return result;
}

class DefineAttribute final : public clang::ParsedAttrInfo {
public:
    DefineAttribute() {
        static constexpr Spelling spellings[] = {
            { clang::ParsedAttr::AS_CXX11, "pmacro::define" },
        };
        Spellings = spellings;
        OptArgs   = 1;
    }

    auto diagAppertainsToDecl(clang::Sema&             sema,
                              const clang::ParsedAttr& attribute,
                              const clang::Decl*       declaration) const -> bool override {
        if (llvm::isa<clang::FunctionDecl>(declaration)) return true;
        report(sema.getDiagnostics(),
               attribute.getLoc(),
               clang::DiagnosticsEngine::Error,
               "'pmacro::define' applies only to functions");
        return false;
    }

    auto handleDeclAttribute(clang::Sema&             sema,
                             clang::Decl*             declaration,
                             const clang::ParsedAttr& attribute) const -> AttrHandling override {
        auto parsed = parse_string_arguments(sema, attribute, "'pmacro::define'", false, true);
        if (! parsed.has_value() || parsed->values.size() > 1 ||
            (! parsed->values.empty() && llvm::StringRef(parsed->values.front()).contains("::"))) {
            report(sema.getDiagnostics(),
                   attribute.getLoc(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::define' accepts one optional non-empty string literal macro scope "
                   "without '::'");
            return AttributeNotApplied;
        }
        auto annotation = define_annotation.str();
        if (! parsed->values.empty())
            annotation = define_scope_annotation.str() + parsed->values.front();
        declaration->addAttr(
            clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, parsed->range));
        return AttributeApplied;
    }
};

class AttrAttribute final : public clang::ParsedAttrInfo {
public:
    AttrAttribute() {
        OptArgs                               = 15;
        static constexpr Spelling spellings[] = {
            { clang::ParsedAttr::AS_CXX11, "pmacro::attr" },
        };
        Spellings = spellings;
    }

    auto diagAppertainsToDecl(clang::Sema&             sema,
                              const clang::ParsedAttr& attribute,
                              const clang::Decl*       declaration) const -> bool override {
        auto* context = declaration->getDeclContext();
        while (context != nullptr) {
            if (context->isFileContext() || context->isRecord()) return true;
            if (context->isFunctionOrMethod()) break;
            context = context->getParent();
        }
        report(sema.getDiagnostics(),
               attribute.getLoc(),
               clang::DiagnosticsEngine::Error,
               "'pmacro::attr' requires a file-scope or record-member declaration");
        return false;
    }

    auto handleDeclAttribute(clang::Sema&             sema,
                             clang::Decl*             declaration,
                             const clang::ParsedAttr& attribute) const -> AttrHandling override {
        auto identity_text  = std::string {};
        auto arguments_text = std::string {};
        auto semantic_range = attribute.getRange();
        if (attribute.getNumArgs() == 0) {
            auto&       source_manager = sema.getSourceManager();
            const auto& language       = sema.getLangOpts();
            auto        location       = clang::Lexer::getLocForEndOfToken(
                attribute.getRange().getEnd(), 0, source_manager, language);
            auto token = clang::Token {};
            if (! clang::Lexer::getRawToken(location, token, source_manager, language, true) &&
                token.is(clang::tok::l_paren)) {
                location = clang::Lexer::getLocForEndOfToken(
                    token.getLocation(), 0, source_manager, language);
                if (! clang::Lexer::getRawToken(location, token, source_manager, language, true) &&
                    token.is(clang::tok::string_literal)) {
                    const auto parser = clang::StringLiteralParser(
                        llvm::ArrayRef<clang::Token>(&token, 1), sema.getPreprocessor());
                    if (! parser.hadError) identity_text = parser.GetString().str();
                    location = clang::Lexer::getLocForEndOfToken(
                        token.getLocation(), 0, source_manager, language);
                    if (clang::Lexer::getRawToken(
                            location, token, source_manager, language, true)) {
                        identity_text.clear();
                    } else if (token.is(clang::tok::r_paren)) {
                        semantic_range.setEnd(token.getLocation());
                    } else if (token.is(clang::tok::comma)) {
                        const auto arguments_begin = clang::Lexer::getLocForEndOfToken(
                            token.getLocation(), 0, source_manager, language);
                        auto arguments_end = clang::SourceLocation {};
                        auto depth         = 0u;
                        location           = arguments_begin;
                        for (;;) {
                            if (clang::Lexer::getRawToken(
                                    location, token, source_manager, language, true) ||
                                token.is(clang::tok::eof)) {
                                identity_text.clear();
                                break;
                            }
                            if (token.isOneOf(clang::tok::l_paren,
                                              clang::tok::l_square,
                                              clang::tok::l_brace)) {
                                ++depth;
                            } else if (token.is(clang::tok::r_paren) && depth == 0) {
                                arguments_end = token.getLocation();
                                semantic_range.setEnd(token.getLocation());
                                break;
                            } else if (token.isOneOf(clang::tok::r_paren,
                                                     clang::tok::r_square,
                                                     clang::tok::r_brace) &&
                                       depth != 0) {
                                --depth;
                            }
                            location = clang::Lexer::getLocForEndOfToken(
                                token.getLocation(), 0, source_manager, language);
                        }
                        if (arguments_end.isValid()) {
                            arguments_text =
                                clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(
                                                                arguments_begin, arguments_end),
                                                            source_manager,
                                                            language)
                                    .trim()
                                    .str();
                        }
                    } else {
                        identity_text.clear();
                    }
                }
            }
        }
        if (attribute.getNumArgs() == 0 && identity_text.empty()) {
            report(sema.getDiagnostics(),
                   attribute.getLoc(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::attr' requires an ordinary string literal macro identity");
            return AttributeNotApplied;
        }
        if (attribute.getNumArgs() != 0) {
            auto* identity =
                llvm::dyn_cast<clang::StringLiteral>(attribute.getArgAsExpr(0)->IgnoreParenCasts());
            if (identity == nullptr) {
                report(sema.getDiagnostics(),
                       attribute.getLoc(),
                       clang::DiagnosticsEngine::Error,
                       "first argument to 'pmacro::attr' must be a string literal");
                return AttributeNotApplied;
            }
            identity_text = identity->getString().str();
            if (attribute.getNumArgs() > 1) {
                auto&       source_manager = sema.getSourceManager();
                const auto& language       = sema.getLangOpts();
                arguments_text             = clang::Lexer::getSourceText(
                                                 clang::CharSourceRange::getTokenRange(
                                                     attribute.getArgAsExpr(1)->getSourceRange().getBegin(),
                                                     attribute.getArgAsExpr(attribute.getNumArgs() - 1)
                                                         ->getSourceRange()
                                                         .getEnd()),
                                                 source_manager,
                                                 language)
                                                 .str();
            }
        }

        auto annotation = std::string(attr_annotation);
        annotation.append(identity_text);
        declaration->addAttr(
            clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, semantic_range));
        auto arguments = std::string(attr_arguments_annotation);
        arguments.append(arguments_text);
        declaration->addAttr(
            clang::AnnotateAttr::Create(sema.Context, arguments, nullptr, 0, semantic_range));
        prepare_invocation(sema, *declaration);
        return AttributeApplied;
    }
};

class DeriveAttribute final : public clang::ParsedAttrInfo {
public:
    DeriveAttribute() {
        OptArgs                               = 15;
        static constexpr Spelling spellings[] = {
            { clang::ParsedAttr::AS_CXX11, "pmacro::derive" },
        };
        Spellings = spellings;
    }

    auto diagAppertainsToDecl(clang::Sema&             sema,
                              const clang::ParsedAttr& attribute,
                              const clang::Decl*       declaration) const -> bool override {
        if (llvm::isa<clang::RecordDecl, clang::EnumDecl>(declaration)) return true;
        report(sema.getDiagnostics(),
               attribute.getLoc(),
               clang::DiagnosticsEngine::Error,
               "'pmacro::derive' requires a complete record, union, or enum definition");
        return false;
    }

    auto handleDeclAttribute(clang::Sema&             sema,
                             clang::Decl*             declaration,
                             const clang::ParsedAttr& attribute) const -> AttrHandling override {
        auto parsed = parse_string_arguments(sema, attribute, "pmacro::derive", false);
        if (! parsed.has_value()) {
            report(sema.getDiagnostics(),
                   attribute.getLoc(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::derive' arguments must be non-empty ordinary string literals");
            return AttributeNotApplied;
        }
        for (const auto& identity : parsed->values) {
            auto annotation = std::string(derive_annotation);
            annotation.append(identity);
            declaration->addAttr(
                clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, parsed->range));
        }
        prepare_invocation(sema, *declaration);
        return AttributeApplied;
    }
};

class HelperAttribute final : public clang::ParsedAttrInfo {
public:
    HelperAttribute() {
        OptArgs                               = 15;
        static constexpr Spelling spellings[] = {
            { clang::ParsedAttr::AS_CXX11, "pmacro::helper" },
        };
        Spellings = spellings;
    }

    auto handleDeclAttribute(clang::Sema&             sema,
                             clang::Decl*             declaration,
                             const clang::ParsedAttr& attribute) const -> AttrHandling override {
        auto parsed = parse_string_arguments(sema, attribute, "pmacro::helper", true);
        if (! parsed.has_value()) {
            report(sema.getDiagnostics(),
                   attribute.getLoc(),
                   clang::DiagnosticsEngine::Error,
                   "first argument to 'pmacro::helper' must be a non-empty string literal");
            return AttributeNotApplied;
        }
        auto annotation = std::string(helper_annotation);
        annotation.append(parsed->values.front());
        clang::Expr* locations[] = {
            clang::IntegerLiteral::Create(sema.Context,
                                          llvm::APInt(32, parsed->tail.getBegin().getRawEncoding()),
                                          sema.Context.UnsignedIntTy,
                                          {}),
            clang::IntegerLiteral::Create(sema.Context,
                                          llvm::APInt(32, parsed->tail.getEnd().getRawEncoding()),
                                          sema.Context.UnsignedIntTy,
                                          {})
        };
        declaration->addAttr(
            clang::AnnotateAttr::Create(sema.Context, annotation, locations, 2, parsed->range));
        return AttributeApplied;
    }
};

struct ExpansionOptions {
    std::string provider;
};

struct StoredToken {
    pmacro::TokenKind          kind { pmacro::TokenKind::Literal };
    pmacro::Delimiter          delimiter { pmacro::Delimiter::None };
    pmacro::Spacing            spacing { pmacro::Spacing::Alone };
    std::string                spelling;
    pmacro::Span               span {};
    pmacro::host::StreamHandle children {};
    bool                       leading_space {};
};

struct StoredStream {
    std::vector<StoredToken> tokens;
};

enum class MacroLookupStatus
{
    Found,
    Missing,
    InvalidRegistration,
    Duplicate,
    KindMismatch,
};

struct MacroLookup {
    const pmacro::host::MacroDescriptor* macro {};
    llvm::StringRef                      provider;
    MacroLookupStatus                    status { MacroLookupStatus::Missing };
};

class ExpansionState final : public pmacro::host::Runtime {
public:
    explicit ExpansionState(clang::CompilerInstance& compiler)
        : compiler_(compiler), sources_(compiler), declarations_(compiler, sources_) {
        target_triple_ = compiler_.getTarget().getTriple().str();
    }

    auto sources() -> SourceRegistry& { return sources_; }
    using Snapshot = std::expected<std::shared_ptr<const Declaration>, DeclarationError>;
    std::function<Snapshot(const clang::Decl&)> declaration_query;
    auto declaration(const clang::Decl& value, FunctionForm form = FunctionForm::Unavailable)
        -> Snapshot {
        return declarations_.get(value, form);
    }
    auto take_outputs() -> std::vector<std::pair<OutputLocation, TokenStream>> {
        return std::exchange(outputs_, {});
    }
    auto allow_namespace_output(bool allowed) -> void { namespace_output_ = allowed; }
    auto accept_output(std::string_view text, clang::SourceLocation location) -> bool {
        constexpr std::size_t limit = 16 * 1024 * 1024;
        if (text.size() > limit - output_bytes_) {
            report(compiler_.getDiagnostics(),
                   location,
                   clang::DiagnosticsEngine::Error,
                   "proc-macro generated output budget exceeded (16 MiB)");
            return false;
        }
        output_bytes_ += text.size();
        return true;
    }
    auto helpers(const clang::Decl& declaration) -> const std::vector<PreparedHelper>& {
        return declarations_.helpers(declaration);
    }

    auto make_stream(llvm::StringRef source,
                     pmacro::Span    span,
                     bool            exact_spans     = true,
                     bool            validate_groups = false) -> pmacro::host::StreamHandle {
        const auto root = create_stored_stream();
        if (source.empty()) return root;

        auto                  storage = source.str();
        const llvm::StringRef input(storage);

        struct GroupFrame {
            pmacro::host::StreamHandle parent;
            std::size_t                token;
            clang::tok::TokenKind      closing;
        };
        auto frames  = std::vector<GroupFrame> {};
        auto current = root;
        auto lexer   = clang::Lexer(clang::SourceLocation {},
                                    compiler_.getLangOpts(),
                                    input.begin(),
                                    input.begin(),
                                    input.end());
        for (;;) {
            auto token = clang::Token {};
            lexer.LexFromRawLexer(token);
            if (token.is(clang::tok::eof)) break;
            if (token.is(clang::tok::comment)) continue;
            const auto* end    = lexer.getBufferLocation();
            const auto* begin  = end - token.getLength();
            const auto  offset = static_cast<std::uint32_t>(begin - input.begin());
            auto token_span = exact_spans ? pmacro::Span(span.source(),
                                                         span.begin() + offset,
                                                         span.begin() + offset + token.getLength())
                                          : span;

            const auto closing = matching_close(token.getKind());
            if (closing != clang::tok::unknown) {
                const auto children = create_stored_stream();
                auto&      parent   = *get_stream(current);
                parent.tokens.push_back(StoredToken {
                    .kind          = pmacro::TokenKind::Group,
                    .delimiter     = delimiter(token.getKind()),
                    .span          = token_span,
                    .children      = children,
                    .leading_space = token.hasLeadingSpace(),
                });
                frames.push_back(GroupFrame {
                    .parent  = current,
                    .token   = parent.tokens.size() - 1,
                    .closing = closing,
                });
                current = children;
                continue;
            }
            if (! frames.empty() && token.is(frames.back().closing)) {
                auto frame = frames.back();
                frames.pop_back();
                auto& group = get_stream(frame.parent)->tokens[frame.token];
                group.span =
                    pmacro::Span(group.span.source(), group.span.begin(), token_span.end());
                current = frame.parent;
                continue;
            }

            if (validate_groups &&
                token.isOneOf(clang::tok::r_paren, clang::tok::r_square, clang::tok::r_brace))
                return 0;
            auto kind = pmacro::TokenKind::Punct;
            if (token.isAnyIdentifier())
                kind = pmacro::TokenKind::Ident;
            else if (token.isLiteral())
                kind = pmacro::TokenKind::Literal;
            auto  spelling = std::string(begin, token.getLength());
            auto& stream   = *get_stream(current);
            if (! stream.tokens.empty() && stream.tokens.back().kind == pmacro::TokenKind::Punct &&
                kind == pmacro::TokenKind::Punct && ! token.hasLeadingSpace()) {
                stream.tokens.back().spacing = pmacro::Spacing::Joint;
            }
            stream.tokens.push_back(StoredToken {
                .kind          = kind,
                .spelling      = std::move(spelling),
                .span          = token_span,
                .leading_space = token.hasLeadingSpace(),
            });
        }
        if (validate_groups && ! frames.empty()) return 0;
        return root;
    }

    auto render(const pmacro::TokenStream& stream) const -> std::optional<std::string> {
        auto       output = std::string {};
        const auto handle = token_stream_handle(stream);
        if (handle == 0) return std::nullopt;
        if (! render_stream(handle, output)) return std::nullopt;
        return output;
    }

    auto token_stream(pmacro::host::StreamHandle handle) noexcept -> pmacro::TokenStream {
        return make_token_stream(handle);
    }

    auto context() noexcept -> pmacro::Context { return make_context(); }

    auto find(llvm::StringRef package, llvm::StringRef name, pmacro::host::MacroKind kind) const
        -> MacroLookup {
        const pmacro::host::MacroDescriptor* found {};
        auto                                 found_provider = llvm::StringRef {};
        for (auto* node = pmacro::host::registered_macros(); node != nullptr; node = node->next()) {
            const auto  provider = llvm::StringRef(node->provider());
            const auto& macro    = node->macro();
            if (provider.empty()) return { .status = MacroLookupStatus::InvalidRegistration };
            const auto scope = macro.scope.empty() ? provider : llvm::StringRef(macro.scope);
            if (scope.contains("::")) return { .status = MacroLookupStatus::InvalidRegistration };
            if (scope != package) continue;
            const auto valid_callback =
                (std::holds_alternative<pmacro::host::AttributeCallback>(macro.callback) &&
                 std::get<pmacro::host::AttributeCallback>(macro.callback).function != nullptr) ||
                (std::holds_alternative<pmacro::host::DeriveCallback>(macro.callback) &&
                 std::get<pmacro::host::DeriveCallback>(macro.callback).function != nullptr);
            if (macro.name.empty() || ! valid_callback)
                return { .status = MacroLookupStatus::InvalidRegistration };
            if (llvm::StringRef(macro.name) != name) continue;
            if (pmacro::host::macro_kind(macro.callback) != kind)
                return { .status = MacroLookupStatus::KindMismatch };
            if (found != nullptr) return { .status = MacroLookupStatus::Duplicate };
            found          = &macro;
            found_provider = provider;
        }
        return found == nullptr ? MacroLookup {}
                                : MacroLookup { .macro    = found,
                                                .provider = found_provider,
                                                .status   = MacroLookupStatus::Found };
    }

    auto contains_module_declaration(const pmacro::TokenStream& output) -> bool {
        const auto  handle = token_stream_handle(output);
        const auto* stream = get_stream(handle);
        if (stream == nullptr) return false;
        auto declaration_start = true;
        auto exported          = false;
        for (const auto& token : stream->tokens) {
            if (token.kind == TokenKind::Group && token.delimiter == Delimiter::Brace &&
                contains_module_declaration(make_token_stream(token.children)))
                return true;
            if (declaration_start && token.kind == pmacro::TokenKind::Ident) {
                if (! exported && token.spelling == "export") {
                    exported = true;
                    continue;
                }
                if (token.spelling == "module" || token.spelling == "import") return true;
                declaration_start = false;
                exported          = false;
            } else if (declaration_start && token.kind != pmacro::TokenKind::Punct) {
                declaration_start = false;
                exported          = false;
            }
            if ((token.kind == pmacro::TokenKind::Punct && token.spelling == ";") ||
                (token.kind == pmacro::TokenKind::Group &&
                 token.delimiter == pmacro::Delimiter::Brace)) {
                declaration_start = true;
                exported          = false;
            }
        }
        return false;
    }

    auto enter_invocation(llvm::StringRef    package,
                          llvm::StringRef    name,
                          llvm::StringRef    provider,
                          pmacro::Span       call_site,
                          const clang::Decl& declaration) -> void {
        macro_identity_.assign(package.data(), package.size());
        macro_identity_.append("::");
        macro_identity_.append(name.data(), name.size());
        provider_identity_.assign(provider.data(), provider.size());
        call_site_   = call_site;
        declaration_ = &declaration;
        outputs_.clear();
    }

    auto leave_invocation() -> void {
        macro_identity_.clear();
        provider_identity_.clear();
        call_site_   = {};
        declaration_ = nullptr;
    }

private:
    static auto matching_close(clang::tok::TokenKind kind) noexcept -> clang::tok::TokenKind {
        if (kind == clang::tok::l_paren) return clang::tok::r_paren;
        if (kind == clang::tok::l_brace) return clang::tok::r_brace;
        if (kind == clang::tok::l_square) return clang::tok::r_square;
        return clang::tok::unknown;
    }

    static auto delimiter(clang::tok::TokenKind kind) noexcept -> pmacro::Delimiter {
        if (kind == clang::tok::l_paren) return pmacro::Delimiter::Parenthesis;
        if (kind == clang::tok::l_brace) return pmacro::Delimiter::Brace;
        return pmacro::Delimiter::Bracket;
    }

    auto create_stored_stream() -> pmacro::host::StreamHandle {
        streams_.push_back(StoredStream {});
        return streams_.size();
    }

    static auto group_spelling(pmacro::Delimiter delimiter) noexcept
        -> std::pair<llvm::StringRef, llvm::StringRef> {
        if (delimiter == pmacro::Delimiter::Parenthesis) return { "(", ")" };
        if (delimiter == pmacro::Delimiter::Brace) return { "{", "}" };
        return { "[", "]" };
    }

    auto render_stream(pmacro::host::StreamHandle handle, std::string& output) const -> bool {
        const auto* stream = get_stream(handle);
        if (stream == nullptr) return false;
        for (const auto& token : stream->tokens) {
            if (token.leading_space && ! output.empty()) output.push_back(' ');
            if (token.kind != pmacro::TokenKind::Group) {
                output.append(token.spelling);
                continue;
            }
            const auto [opening, closing] = group_spelling(token.delimiter);
            output.append(opening);
            if (! render_stream(token.children, output)) return false;
            output.append(closing);
        }
        return true;
    }

    auto get_stream(pmacro::host::StreamHandle handle) -> StoredStream* {
        if (handle == 0 || handle > streams_.size()) return nullptr;
        return &streams_[handle - 1];
    }

    auto get_stream(pmacro::host::StreamHandle handle) const -> const StoredStream* {
        if (handle == 0 || handle > streams_.size()) return nullptr;
        return &streams_[handle - 1];
    }

    auto emit_stream(OutputLocation location, pmacro::host::StreamHandle output) noexcept
        -> std::expected<void, Error> override {
        if (! declaration_ || ! get_stream(output) ||
            (location != OutputLocation::AfterItem && location != OutputLocation::NamespaceEnd) ||
            (location == OutputLocation::NamespaceEnd && ! namespace_output_))
            return std::unexpected(Error::InvalidArgument);
        if (contains_module_declaration(make_token_stream(output))) {
            report(compiler_.getDiagnostics(),
                   sources_.location(call_site_),
                   clang::DiagnosticsEngine::Error,
                   "pmacro output must not change module dependencies");
            return std::unexpected(Error::InvalidArgument);
        }
        auto text = render(make_token_stream(output));
        if (! text || ! accept_output(*text, sources_.location(call_site_)))
            return std::unexpected(Error::Failure);
        outputs_.emplace_back(location, make_token_stream(output));
        return {};
    }

    auto create_stream() noexcept
        -> std::expected<pmacro::host::StreamHandle, pmacro::Error> override {
        return create_stored_stream();
    }

    auto parse_stream(std::string_view source) noexcept
        -> std::expected<pmacro::host::StreamHandle, pmacro::Error> override {
        return make_stream(llvm::StringRef(source), {});
    }

    auto stream_size(pmacro::host::StreamHandle handle) const noexcept
        -> std::expected<std::size_t, pmacro::Error> override {
        const auto* stream = get_stream(handle);
        if (stream == nullptr) return std::unexpected(pmacro::Error::InvalidArgument);
        return stream->tokens.size();
    }

    auto stream_token(pmacro::host::StreamHandle handle, std::size_t index) const noexcept
        -> std::expected<pmacro::host::RuntimeToken, pmacro::Error> override {
        const auto* stream = get_stream(handle);
        if (stream == nullptr || index >= stream->tokens.size())
            return std::unexpected(pmacro::Error::InvalidArgument);
        const auto& token = stream->tokens[index];
        return pmacro::host::RuntimeToken {
            .kind      = token.kind,
            .delimiter = token.delimiter,
            .spacing   = token.spacing,
            .spelling  = token.spelling,
            .span      = token.span,
            .children  = token.children,
        };
    }

    auto push_token(pmacro::host::StreamHandle        handle,
                    const pmacro::host::RuntimeToken& token) noexcept
        -> std::expected<void, pmacro::Error> override {
        auto* stream = get_stream(handle);
        if (stream == nullptr) return std::unexpected(pmacro::Error::InvalidArgument);
        if (token.kind == pmacro::TokenKind::Group) {
            if (token.delimiter == pmacro::Delimiter::None || get_stream(token.children) == nullptr)
                return std::unexpected(pmacro::Error::InvalidArgument);
        } else if (token.delimiter != pmacro::Delimiter::None || token.children != 0) {
            return std::unexpected(pmacro::Error::InvalidArgument);
        }
        auto leading_space = false;
        if (! stream->tokens.empty()) {
            const auto& previous = stream->tokens.back();
            leading_space        = (previous.kind == pmacro::TokenKind::Ident ||
                                    previous.kind == pmacro::TokenKind::Literal) &&
                                   (token.kind == pmacro::TokenKind::Ident ||
                                    token.kind == pmacro::TokenKind::Literal);
            if (previous.kind == pmacro::TokenKind::Punct &&
                token.kind == pmacro::TokenKind::Punct) {
                leading_space = previous.spacing == pmacro::Spacing::Alone;
            }
        }
        stream->tokens.push_back(StoredToken {
            .kind          = token.kind,
            .delimiter     = token.delimiter,
            .spacing       = token.spacing,
            .spelling      = std::string(token.spelling),
            .span          = token.span,
            .children      = token.children,
            .leading_space = leading_space,
        });
        return {};
    }

    auto current_declaration() noexcept
        -> std::expected<std::shared_ptr<const Declaration>, DeclarationError> override {
        if (! declaration_) return std::unexpected(DeclarationError::NotInInvocation);
        return declaration_query ? declaration_query(*declaration_)
                                 : declarations_.get(*declaration_);
    }

    auto fragment_stream(const CodeFragment& fragment) noexcept
        -> std::expected<pmacro::host::StreamHandle, FragmentError> override {
        if (! declaration_) return std::unexpected(FragmentError::NotInInvocation);
        auto source = sources_.fragment_source(fragment, call_site_);
        if (! source) return std::unexpected(source.error());
        auto handle = make_stream(fragment.text, source->span, source->exact, true);
        if (! handle) return std::unexpected(FragmentError::InvalidFragment);
        return handle;
    }

    auto valid_span(pmacro::Span span) const -> bool { return sources_.valid(span); }

    auto call_site_span() const noexcept -> std::expected<pmacro::Span, pmacro::Error> override {
        if (! valid_span(call_site_)) return std::unexpected(pmacro::Error::InvalidArgument);
        return call_site_;
    }

    auto join_spans(pmacro::Span left, pmacro::Span right) const noexcept
        -> std::expected<pmacro::Span, pmacro::Error> override {
        if (left.source() != right.source() || ! valid_span(left) || ! valid_span(right))
            return std::unexpected(pmacro::Error::InvalidArgument);
        return pmacro::Span(left.source(),
                            std::min(left.begin(), right.begin()),
                            std::max(left.end(), right.end()));
    }

    auto subspan(pmacro::Span span, std::uint32_t begin, std::uint32_t end) const noexcept
        -> std::expected<pmacro::Span, pmacro::Error> override {
        if (! valid_span(span) || begin > end || end > span.end() - span.begin())
            return std::unexpected(pmacro::Error::InvalidArgument);
        return pmacro::Span(span.source(), span.begin() + begin, span.begin() + end);
    }

    auto emit_diagnostic(pmacro::DiagnosticLevel level,
                         pmacro::Span            span,
                         std::string_view        message) noexcept -> bool override {
        if (! valid_span(span)) return false;
        auto diagnostic_level = clang::DiagnosticsEngine::Note;
        if (level == pmacro::DiagnosticLevel::Warning)
            diagnostic_level = clang::DiagnosticsEngine::Warning;
        else if (level == pmacro::DiagnosticLevel::Error)
            diagnostic_level = clang::DiagnosticsEngine::Error;
        const auto location = sources_.location(span);
        report(compiler_.getDiagnostics(), location, diagnostic_level, llvm::StringRef(message));
        return true;
    }

    auto macro_identity() const noexcept -> std::string_view override { return macro_identity_; }
    auto provider_identity() const noexcept -> std::string_view override {
        return provider_identity_;
    }
    auto target_triple() const noexcept -> std::string_view override { return target_triple_; }

    clang::CompilerInstance&                            compiler_;
    SourceRegistry                                      sources_;
    DeclarationStore                                    declarations_;
    const clang::Decl*                                  declaration_ {};
    std::vector<StoredStream>                           streams_;
    std::string                                         macro_identity_;
    std::string                                         provider_identity_;
    std::string                                         target_triple_;
    pmacro::Span                                        call_site_ {};
    std::vector<std::pair<OutputLocation, TokenStream>> outputs_;
    bool                                                namespace_output_ {};
    std::size_t                                         output_bytes_ {};
};

struct MacroDefinition {
    const clang::FunctionDecl* declaration {};
    std::string                name;
    std::string                scope;
    std::string                qualified_name;
    pmacro::host::MacroKind    kind { pmacro::host::MacroKind::Attr };
    pmacro::Span               attribute_span;
};

auto record_name(clang::QualType type) -> std::string {
    const auto* record = type.getCanonicalType()->getAs<clang::RecordType>();
    return record == nullptr ? std::string {} : record->getDecl()->getQualifiedNameAsString();
}

auto enum_name(clang::QualType type) -> std::string {
    const auto* enumeration = type.getCanonicalType()->getAs<clang::EnumType>();
    return enumeration == nullptr ? std::string {}
                                  : enumeration->getDecl()->getQualifiedNameAsString();
}

auto is_token_stream_expected(clang::QualType type) -> bool {
    const auto* record = type.getCanonicalType()->getAs<clang::RecordType>();
    if (record == nullptr) return false;
    const auto* specialization =
        llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record->getDecl());
    if (specialization == nullptr || specialization->getTemplateArgs().size() != 2 ||
        specialization->getSpecializedTemplate()->getTemplatedDecl()->getQualifiedNameAsString() !=
            "std::expected" ||
        specialization->getTemplateArgs()[0].getKind() != clang::TemplateArgument::Type ||
        specialization->getTemplateArgs()[1].getKind() != clang::TemplateArgument::Type)
        return false;
    return record_name(specialization->getTemplateArgs()[0].getAsType()) == "pmacro::TokenStream" &&
           enum_name(specialization->getTemplateArgs()[1].getAsType()) == "pmacro::Error";
}

class DefinitionRegistry final {
public:
    DefinitionRegistry(clang::CompilerInstance& compiler, const ExpansionOptions& options)
        : compiler_(compiler), options_(options), sources_(compiler) {}

    auto traverse(clang::DeclContext& context) -> void {
        for (auto* declaration : context.decls()) traverse(declaration);
    }

    auto observe(clang::Decl* declaration) -> void { traverse(declaration); }
    auto validate() -> void {
        for (const auto& definition : definitions_) {
            const auto& function = *definition.declaration;
            for (const auto* candidate :
                 function.getDeclContext()->lookup(function.getDeclName())) {
                const auto* overload = llvm::dyn_cast<clang::FunctionDecl>(candidate);
                if (overload &&
                    overload->getTopLevelOwningNamedModule() ==
                        function.getTopLevelOwningNamedModule() &&
                    overload->getCanonicalDecl() != function.getCanonicalDecl()) {
                    report(compiler_.getDiagnostics(),
                           function.getLocation(),
                           clang::DiagnosticsEngine::Error,
                           "'pmacro::define' function must not be overloaded");
                    break;
                }
            }
        }
    }
    auto registrations() -> std::string {
        auto                     text = std::string {};
        llvm::raw_string_ostream out(text);
        for (; emitted_ < definitions_.size(); ++emitted_) {
            const auto& definition = definitions_[emitted_];
            out << "namespace { const pmacro::host::MacroRegistration pmacro_registration_"
                << emitted_ << " { \"";
            llvm::printEscapedString(options_.provider, out);
            out << "\", pmacro::host::MacroDescriptor { \"";
            llvm::printEscapedString(definition.name, out);
            out << "\", pmacro::host::"
                << (definition.kind == host::MacroKind::Attr ? "AttributeCallback"
                                                             : "DeriveCallback")
                << " { &::" << definition.qualified_name << " }, \"";
            llvm::printEscapedString(definition.scope, out);
            out << "\" } }; }\n";
        }
        out.flush();
        return text;
    }

private:
    auto traverse(clang::Decl* declaration) -> void {
        if (declaration == nullptr || declaration->isFromASTFile()) return;
        auto* function = llvm::dyn_cast<clang::FunctionDecl>(declaration);
        if (function != nullptr && function->hasAttr<clang::AnnotateAttr>()) {
            for (const auto* attribute : function->specific_attrs<clang::AnnotateAttr>()) {
                if (attribute->getAnnotation() == define_annotation ||
                    attribute->getAnnotation().starts_with(define_scope_annotation))
                    collect(*function, *attribute);
            }
        }
        if (auto* nested = llvm::dyn_cast<clang::DeclContext>(declaration)) traverse(*nested);
    }

    auto collect(clang::FunctionDecl& function, const clang::AnnotateAttr& attribute) -> void {
        for (const auto& definition : definitions_)
            if (definition.declaration == &function) return;
        auto reject = [&](llvm::StringRef message) {
            report(compiler_.getDiagnostics(),
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   message);
            failed_ = true;
        };
        if (! compiler_.getSourceManager().isWrittenInMainFile(attribute.getLocation())) {
            reject("'pmacro::define' must be written in the provider module source");
            return;
        }
        const auto* namespace_context =
            llvm::dyn_cast<clang::NamespaceDecl>(function.getDeclContext());
        if (! llvm::isa<clang::TranslationUnitDecl>(function.getDeclContext()) &&
            namespace_context == nullptr) {
            reject("'pmacro::define' requires a namespace-scope free function");
            return;
        }
        if (namespace_context != nullptr && namespace_context->isAnonymousNamespace()) {
            reject("'pmacro::define' function must not be in an anonymous namespace");
            return;
        }
        if (function.getTemplatedKind() != clang::FunctionDecl::TK_NonTemplate) {
            reject("'pmacro::define' function must not be a template");
            return;
        }
        for (const auto* candidate : function.getDeclContext()->lookup(function.getDeclName())) {
            const auto* overload = llvm::dyn_cast<clang::FunctionDecl>(candidate);
            if (overload != nullptr &&
                overload->getTopLevelOwningNamedModule() ==
                    function.getTopLevelOwningNamedModule() &&
                overload->getCanonicalDecl() != function.getCanonicalDecl()) {
                reject("'pmacro::define' function must not be overloaded");
                return;
            }
        }
        if (! function.doesThisDeclarationHaveABody()) {
            reject("'pmacro::define' requires a function definition");
            return;
        }
        if (function.getNumParams() != 2) {
            reject("'pmacro::define' function must take AttributeInput or DeriveInput and "
                   "Context&");
            return;
        }
        const auto input_name = record_name(function.getParamDecl(0)->getType());
        auto       kind       = pmacro::host::MacroKind::Attr;
        if (input_name == "pmacro::DeriveInput") {
            kind = pmacro::host::MacroKind::Derive;
        } else if (input_name != "pmacro::AttributeInput") {
            reject("first 'pmacro::define' parameter must be pmacro::AttributeInput or "
                   "pmacro::DeriveInput");
            return;
        }
        const auto context_type = function.getParamDecl(1)->getType();
        if (! context_type->isLValueReferenceType() ||
            context_type.getNonReferenceType().isConstQualified() ||
            record_name(context_type.getNonReferenceType()) != "pmacro::Context") {
            reject("second 'pmacro::define' parameter must be pmacro::Context&");
            return;
        }
        if (! is_token_stream_expected(function.getReturnType())) {
            reject("'pmacro::define' must return "
                   "std::expected<pmacro::TokenStream, pmacro::Error>");
            return;
        }
        const auto* function_type = function.getType()->getAs<clang::FunctionProtoType>();
        if (function_type == nullptr || ! function_type->isNothrow()) {
            reject("'pmacro::define' function must be noexcept");
            return;
        }
        const auto name       = function.getNameAsString();
        const auto annotation = attribute.getAnnotation();
        const auto scope      = annotation.starts_with(define_scope_annotation)
                                    ? annotation.drop_front(define_scope_annotation.size()).str()
                                    : options_.provider;
        for (const auto& definition : definitions_) {
            if (definition.name == name && definition.scope == scope) {
                reject("'pmacro::define' macro name is duplicated in this provider");
                return;
            }
        }
        auto definition_attribute_span = sources_.attribute_span(attribute);
        if (! definition_attribute_span.has_value()) {
            reject("cannot resolve the source range for 'pmacro::define'");
            return;
        }
        definitions_.push_back(MacroDefinition {
            .declaration    = &function,
            .name           = name,
            .scope          = scope,
            .qualified_name = function.getQualifiedNameAsString(),
            .kind           = kind,
            .attribute_span = *definition_attribute_span,
        });
    }

    clang::CompilerInstance&     compiler_;
    const ExpansionOptions&      options_;
    SourceRegistry               sources_;
    std::vector<MacroDefinition> definitions_;
    std::size_t                  emitted_ {};
    bool                         failed_ {};
};

#include "compilation.hpp"

class Plugin final : public clang::PluginASTAction {
public:
    auto CreateASTConsumer(clang::CompilerInstance& compiler, llvm::StringRef)
        -> std::unique_ptr<clang::ASTConsumer> override {
        return std::make_unique<Compilation>(compiler, options_);
    }

    auto ParseArgs(const clang::CompilerInstance&  compiler,
                   const std::vector<std::string>& arguments) -> bool override {
        for (const auto& argument : arguments) {
            if (llvm::StringRef(argument).starts_with("provider=")) {
                options_.provider = llvm::StringRef(argument).drop_front(9).str();
                continue;
            }
            report(compiler.getDiagnostics(),
                   {},
                   clang::DiagnosticsEngine::Error,
                   "unknown pmacro plugin argument");
            return false;
        }
        return true;
    }

    auto getActionType() -> ActionType override { return AddBeforeMainAction; }

private:
    ExpansionOptions options_;
};

} // namespace pmacro::clang_bridge

static clang::ParsedAttrInfoRegistry::Add<pmacro::clang_bridge::DefineAttribute>
    define_attribute("pmacro_define", "defines a C++ procedural macro");
static clang::ParsedAttrInfoRegistry::Add<pmacro::clang_bridge::AttrAttribute>
    attr_attribute("pmacro_attr", "applies a C++ procedural attribute macro");
static clang::ParsedAttrInfoRegistry::Add<pmacro::clang_bridge::DeriveAttribute>
    derive_attribute("pmacro_derive", "applies C++ procedural derive macros");
static clang::ParsedAttrInfoRegistry::Add<pmacro::clang_bridge::HelperAttribute>
    helper_attribute("pmacro_helper", "provides inert input to a procedural derive macro");
static clang::FrontendPluginRegistry::Add<pmacro::clang_bridge::Plugin>
    plugin("pmacro", "expands C++ procedural macros");
