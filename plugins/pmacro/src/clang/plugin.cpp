module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/LiteralSupport.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

module pmacro;

namespace pmacro::clang_bridge
{

constexpr llvm::StringLiteral define_annotation         = "pmacro.define";
constexpr llvm::StringLiteral attr_annotation           = "pmacro.attr:";
constexpr llvm::StringLiteral attr_arguments_annotation = "pmacro.attr.arguments:";
constexpr llvm::StringLiteral derive_annotation         = "pmacro.derive:";
constexpr llvm::StringLiteral helper_annotation         = "pmacro.helper:";

auto report(clang::DiagnosticsEngine&       diagnostics,
            clang::SourceLocation           location,
            clang::DiagnosticsEngine::Level level,
            llvm::StringRef                 message) -> void {
    const auto id = diagnostics.getCustomDiagID(level, "%0");
    diagnostics.Report(location, id) << message;
}

struct ParsedStringArguments {
    std::vector<std::string> values;
    clang::SourceRange       range;
};

auto parse_string_arguments(clang::Sema&             sema,
                            const clang::ParsedAttr& attribute,
                            llvm::StringRef          spelling,
                            bool first_only) -> std::optional<ParsedStringArguments> {
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
        return result;
    }

    auto&       manager  = sema.getSourceManager();
    const auto& language = sema.getLangOpts();
    auto        location =
        clang::Lexer::getLocForEndOfToken(attribute.getRange().getEnd(), 0, manager, language);
    auto token = clang::Token {};
    if (clang::Lexer::getRawToken(location, token, manager, language, true) ||
        ! token.is(clang::tok::l_paren))
        return std::nullopt;
    location         = clang::Lexer::getLocForEndOfToken(token.getLocation(), 0, manager, language);
    auto       depth = 0u;
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
            if (! finish_argument()) return std::nullopt;
            result.range.setEnd(token.getLocation());
            break;
        }
        if (token.is(clang::tok::comma) && depth == 0) {
            if (! finish_argument()) return std::nullopt;
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
    if (result.values.empty()) {
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
        declaration->addAttr(clang::AnnotateAttr::Create(
            sema.Context, define_annotation, nullptr, 0, attribute.getRange()));
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
        declaration->addAttr(
            clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, parsed->range));
        return AttributeApplied;
    }
};

struct ExpansionOptions {
    std::string mode;
    std::string output;
    std::string trace;
    std::string status;
    std::string provider;
};

auto contains_proc_macro_invocation(clang::CompilerInstance& compiler, llvm::StringRef source)
    -> bool {
    auto lexer = clang::Lexer(clang::SourceLocation {},
                              compiler.getLangOpts(),
                              source.begin(),
                              source.begin(),
                              source.end());
    auto state = 0u;
    for (;;) {
        auto token = clang::Token {};
        lexer.LexFromRawLexer(token);
        if (token.is(clang::tok::eof)) return false;
        if (token.is(clang::tok::comment)) continue;
        const auto* end      = lexer.getBufferLocation();
        const auto  spelling = llvm::StringRef(end - token.getLength(), token.getLength());
        if (state == 0u) {
            state = token.is(clang::tok::l_square) ? 1u : 0u;
        } else if (state == 1u) {
            state = token.is(clang::tok::l_square) ? 2u : 0u;
        } else if (state == 2u) {
            state = token.is(clang::tok::identifier) && spelling == "pmacro" ? 3u : 0u;
        } else if (state == 3u) {
            state = token.is(clang::tok::coloncolon) ? 4u : 0u;
        } else {
            if (token.is(clang::tok::identifier) && (spelling == "attr" || spelling == "derive"))
                return true;
            state = 0u;
        }
    }
}

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
    explicit ExpansionState(clang::CompilerInstance& compiler): compiler_(compiler) {
        target_triple_ = compiler_.getTarget().getTriple().str();
    }

    auto make_stream(llvm::StringRef source, pmacro::Span span) -> pmacro::host::StreamHandle {
        const auto root = create_stored_stream();
        if (source.empty()) return root;

        auto                  storage = source.str();
        const llvm::StringRef input(storage);

        struct GroupFrame {
            pmacro::host::StreamHandle parent;
            size_t                     token;
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
            const auto* end        = lexer.getBufferLocation();
            const auto* begin      = end - token.getLength();
            const auto  offset     = static_cast<uint32_t>(begin - input.begin());
            auto        token_span = pmacro::Span(
                span.source(), span.begin() + offset, span.begin() + offset + token.getLength());

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
            if (provider != package) continue;
            found_provider = provider;
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
            found = &macro;
        }
        return found == nullptr ? MacroLookup {}
                                : MacroLookup { .macro    = found,
                                                .provider = found_provider,
                                                .status   = MacroLookupStatus::Found };
    }

    auto contains_module_declaration(const pmacro::TokenStream& output) const -> bool {
        const auto  handle = token_stream_handle(output);
        const auto* stream = get_stream(handle);
        if (stream == nullptr) return false;
        auto declaration_start = true;
        auto exported          = false;
        for (const auto& token : stream->tokens) {
            if (declaration_start && token.kind == pmacro::TokenKind::Ident) {
                if (! exported && token.spelling == "export") {
                    exported = true;
                    continue;
                }
                if (token.spelling == "module") return true;
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

    auto enter_invocation(llvm::StringRef package,
                          llvm::StringRef name,
                          llvm::StringRef provider,
                          pmacro::Span    call_site) -> void {
        macro_identity_.assign(package.data(), package.size());
        macro_identity_.append("::");
        macro_identity_.append(name.data(), name.size());
        provider_identity_.assign(provider.data(), provider.size());
        call_site_ = call_site;
    }

    auto leave_invocation() -> void {
        macro_identity_.clear();
        provider_identity_.clear();
        call_site_ = {};
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

    auto create_stream() noexcept
        -> std::expected<pmacro::host::StreamHandle, pmacro::Error> override {
        return create_stored_stream();
    }

    auto parse_stream(std::string_view source) noexcept
        -> std::expected<pmacro::host::StreamHandle, pmacro::Error> override {
        return make_stream(llvm::StringRef(source), {});
    }

    auto stream_size(pmacro::host::StreamHandle handle) const noexcept
        -> std::expected<size_t, pmacro::Error> override {
        const auto* stream = get_stream(handle);
        if (stream == nullptr) return std::unexpected(pmacro::Error::InvalidArgument);
        return stream->tokens.size();
    }

    auto stream_token(pmacro::host::StreamHandle handle, size_t index) const noexcept
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

    auto valid_span(pmacro::Span span) const -> bool {
        if (span.source() != 1 || span.begin() > span.end()) return false;
        const auto file = compiler_.getSourceManager().getMainFileID();
        return span.end() <= compiler_.getSourceManager().getBufferData(file).size();
    }

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

    auto subspan(pmacro::Span span, uint32_t begin, uint32_t end) const noexcept
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
        const auto location =
            compiler_.getSourceManager()
                .getLocForStartOfFile(compiler_.getSourceManager().getMainFileID())
                .getLocWithOffset(span.begin());
        report(compiler_.getDiagnostics(), location, diagnostic_level, llvm::StringRef(message));
        return true;
    }

    auto macro_identity() const noexcept -> std::string_view override { return macro_identity_; }
    auto provider_identity() const noexcept -> std::string_view override {
        return provider_identity_;
    }
    auto target_triple() const noexcept -> std::string_view override { return target_triple_; }

    clang::CompilerInstance&  compiler_;
    std::vector<StoredStream> streams_;
    std::string               macro_identity_;
    std::string               provider_identity_;
    std::string               target_triple_;
    pmacro::Span              call_site_ {};
};

struct MacroDefinition {
    std::string             name;
    std::string             qualified_name;
    pmacro::host::MacroKind kind { pmacro::host::MacroKind::Attr };
    pmacro::Span            attribute_span;
};

auto source_span(clang::CompilerInstance& compiler, clang::SourceRange range)
    -> std::optional<pmacro::Span> {
    auto&      manager   = compiler.getSourceManager();
    const auto begin     = manager.getSpellingLoc(range.getBegin());
    const auto end_token = manager.getSpellingLoc(range.getEnd());
    if (begin.isInvalid() || end_token.isInvalid() || ! manager.isWrittenInMainFile(begin) ||
        ! manager.isWrittenInMainFile(end_token))
        return std::nullopt;
    const auto end =
        clang::Lexer::getLocForEndOfToken(end_token, 0, manager, compiler.getLangOpts());
    if (end.isInvalid()) return std::nullopt;
    return pmacro::Span(1, manager.getFileOffset(begin), manager.getFileOffset(end));
}

auto attribute_span(clang::CompilerInstance& compiler, const clang::AnnotateAttr& attribute)
    -> std::optional<pmacro::Span> {
    auto span = source_span(compiler, attribute.getRange());
    if (! span.has_value()) return std::nullopt;
    const auto file    = compiler.getSourceManager().getMainFileID();
    const auto source  = compiler.getSourceManager().getBufferData(file);
    const auto opening = source.take_front(span->begin()).rfind("[[");
    const auto closing = source.find("]]", span->end());
    if (opening == llvm::StringRef::npos || closing == llvm::StringRef::npos) return span;
    const auto before = source.slice(opening + 2, span->begin());
    const auto after  = source.slice(span->end(), closing);
    if (before.trim().empty() && after.trim().empty()) {
        return pmacro::Span(
            span->source(), static_cast<uint32_t>(opening), static_cast<uint32_t>(closing + 2));
    }
    const auto next_comma = after.find(',');
    if (next_comma != llvm::StringRef::npos &&
        (before.trim().empty() || after.take_front(next_comma).trim().empty())) {
        auto end = next_comma + 1;
        while (end < after.size() && llvm::isSpace(after[end])) ++end;
        return pmacro::Span(
            span->source(), span->begin(), span->end() + static_cast<uint32_t>(end));
    }
    const auto previous_comma = before.rfind(',');
    if (previous_comma != llvm::StringRef::npos &&
        before.drop_front(previous_comma + 1).trim().empty()) {
        return pmacro::Span(
            span->source(), static_cast<uint32_t>(opening + 2 + previous_comma), span->end());
    }
    return span;
}

auto mask_span(std::string& source, pmacro::Span span) -> bool {
    if (span.begin() > span.end() || span.end() > source.size()) return false;
    for (auto index = static_cast<size_t>(span.begin()); index < static_cast<size_t>(span.end());
         ++index) {
        if (source[index] != '\n' && source[index] != '\r') source[index] = ' ';
    }
    return true;
}

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

class DefinitionTransformer final {
public:
    DefinitionTransformer(clang::CompilerInstance& compiler, const ExpansionOptions& options)
        : compiler_(compiler), options_(options) {}

    auto traverse(clang::DeclContext& context) -> void {
        for (auto* declaration : context.decls()) traverse(declaration);
    }

    auto write() -> bool {
        if (failed_) return false;
        if (options_.provider.empty() || options_.output.empty()) {
            report(compiler_.getDiagnostics(),
                   {},
                   clang::DiagnosticsEngine::Error,
                   "pmacro define transformation requires provider and output arguments");
            return false;
        }
        auto escaped = [](llvm::StringRef value) {
            auto result = std::string {};
            auto stream = llvm::raw_string_ostream(result);
            stream << '"';
            llvm::printEscapedString(value, stream);
            stream << '"';
            stream.flush();
            return result;
        };
        std::error_code error;
        auto output = llvm::ToolOutputFile(options_.output, error, llvm::sys::fs::OF_Text);
        if (error) {
            report(compiler_.getDiagnostics(),
                   {},
                   clang::DiagnosticsEngine::Error,
                   "cannot open transformed proc-macro provider source");
            return false;
        }
        auto source = compiler_.getSourceManager()
                          .getBufferData(compiler_.getSourceManager().getMainFileID())
                          .str();
        for (const auto& definition : definitions_) {
            if (! mask_span(source, definition.attribute_span)) {
                report(compiler_.getDiagnostics(),
                       {},
                       clang::DiagnosticsEngine::Error,
                       "cannot remove 'pmacro::define' from provider source");
                return false;
            }
        }
        auto& stream = output.os();
        stream << source << "\nnamespace {\n";
        for (size_t index = 0; index < definitions_.size(); ++index) {
            const auto& definition = definitions_[index];
            stream << "static const pmacro::host::MacroRegistration "
                   << "pmacro_generated_registration_" << index << " {\n"
                   << "    " << escaped(options_.provider) << ",\n"
                   << "    pmacro::host::MacroDescriptor { " << escaped(definition.name)
                   << ", pmacro::host::"
                   << (definition.kind == pmacro::host::MacroKind::Attr ? "AttributeCallback"
                                                                        : "DeriveCallback")
                   << " { &" << definition.qualified_name << " } },\n"
                   << "};\n";
        }
        stream << "}\n";
        output.keep();
        return true;
    }

private:
    auto traverse(clang::Decl* declaration) -> void {
        if (declaration == nullptr) return;
        auto* function = llvm::dyn_cast<clang::FunctionDecl>(declaration);
        if (function != nullptr && function->hasAttr<clang::AnnotateAttr>()) {
            for (const auto* attribute : function->specific_attrs<clang::AnnotateAttr>()) {
                if (attribute->getAnnotation() == define_annotation) collect(*function, *attribute);
            }
        }
        if (auto* nested = llvm::dyn_cast<clang::DeclContext>(declaration)) traverse(*nested);
    }

    auto collect(clang::FunctionDecl& function, const clang::AnnotateAttr& attribute) -> void {
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
        const auto name = function.getNameAsString();
        for (const auto& definition : definitions_) {
            if (definition.name == name) {
                reject("'pmacro::define' macro name is duplicated in this provider");
                return;
            }
        }
        auto definition_attribute_span = attribute_span(compiler_, attribute);
        if (! definition_attribute_span.has_value()) {
            reject("cannot resolve the source range for 'pmacro::define'");
            return;
        }
        definitions_.push_back(MacroDefinition {
            .name           = name,
            .qualified_name = function.getQualifiedNameAsString(),
            .kind           = kind,
            .attribute_span = *definition_attribute_span,
        });
    }

    clang::CompilerInstance&     compiler_;
    const ExpansionOptions&      options_;
    std::vector<MacroDefinition> definitions_;
    bool                         failed_ {};
};

class ExpansionVisitor final {
public:
    ExpansionVisitor(clang::CompilerInstance& compiler, clang::Rewriter& rewriter)
        : compiler_(compiler), rewriter_(rewriter), state_(compiler) {}

    auto traverse(clang::DeclContext& context) -> void {
        for (auto* declaration : context.decls()) traverse(declaration);
    }

    auto failed() const noexcept -> bool { return failed_; }

    auto expansions() const noexcept -> const std::vector<std::string>& { return expansions_; }
    auto pending() const noexcept -> bool { return pending_; }

private:
    auto traverse(clang::Decl* declaration) -> void {
        if (declaration == nullptr) return;
        const clang::AnnotateAttr* invocation = nullptr;
        auto                       derives    = std::vector<const clang::AnnotateAttr*> {};
        auto                       arguments  = llvm::StringRef {};
        const clang::AnnotateAttr* helper     = nullptr;
        for (const auto* attribute : declaration->specific_attrs<clang::AnnotateAttr>()) {
            auto annotation = attribute->getAnnotation();
            if (annotation.starts_with(attr_arguments_annotation)) {
                arguments = annotation.drop_front(attr_arguments_annotation.size());
                continue;
            }
            if (annotation.starts_with(derive_annotation)) {
                derives.push_back(attribute);
                continue;
            }
            if (annotation.starts_with(helper_annotation)) {
                helper = attribute;
                continue;
            }
            if (! annotation.starts_with(attr_annotation)) continue;
            if (invocation != nullptr) {
                report(compiler_.getDiagnostics(),
                       attribute->getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "a declaration may have only one 'pmacro::attr' attribute");
                failed_ = true;
                continue;
            }
            invocation = attribute;
        }
        if (invocation != nullptr && ! derives.empty()) {
            report(compiler_.getDiagnostics(),
                   invocation->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "a declaration cannot combine 'pmacro::attr' and 'pmacro::derive'");
            failed_ = true;
            return;
        }
        if (invocation != nullptr) {
            expand_attr(*declaration, *invocation, arguments);
            return;
        }
        if (! derives.empty()) {
            expand_derive(*declaration, derives);
            return;
        }
        if (helper != nullptr) {
            report(compiler_.getDiagnostics(),
                   helper->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::helper' is not owned by an active derive invocation");
            failed_ = true;
            return;
        }
        if (auto* nested = llvm::dyn_cast<clang::DeclContext>(declaration)) traverse(*nested);
    }

    auto source_span(clang::SourceRange range) const -> std::optional<pmacro::Span> {
        return clang_bridge::source_span(compiler_, range);
    }

    auto attribute_span(const clang::AnnotateAttr& attribute) const -> std::optional<pmacro::Span> {
        return clang_bridge::attribute_span(compiler_, attribute);
    }

    auto item_span(clang::Decl& declaration, const clang::AnnotateAttr& attribute) const
        -> std::optional<pmacro::Span> {
        auto declaration_span = source_span(declaration.getSourceRange());
        auto invocation_span  = attribute_span(attribute);
        if (! declaration_span.has_value() || ! invocation_span.has_value()) return std::nullopt;
        if (llvm::isa<clang::TagDecl>(declaration)) {
            auto& manager = compiler_.getSourceManager();
            auto  start   = manager.getLocForStartOfFile(manager.getMainFileID());
            auto  token   = clang::Token {};
            if (start.isValid() &&
                ! clang::Lexer::getRawToken(start.getLocWithOffset(declaration_span->end()),
                                            token,
                                            manager,
                                            compiler_.getLangOpts(),
                                            true) &&
                token.is(clang::tok::semi)) {
                const auto end = clang::Lexer::getLocForEndOfToken(
                    token.getLocation(), 0, manager, compiler_.getLangOpts());
                if (end.isValid()) {
                    declaration_span = pmacro::Span(declaration_span->source(),
                                                    declaration_span->begin(),
                                                    manager.getFileOffset(end));
                }
            }
        }
        return pmacro::Span(declaration_span->source(),
                            std::min(declaration_span->begin(), invocation_span->begin()),
                            std::max(declaration_span->end(), invocation_span->end()));
    }

    auto source_text(pmacro::Span span) const -> std::optional<std::string> {
        const auto source = compiler_.getSourceManager().getBufferData(
            compiler_.getSourceManager().getMainFileID());
        if (span.begin() > span.end() || span.end() > source.size()) return std::nullopt;
        return source.slice(span.begin(), span.end()).str();
    }

    auto replace_text(pmacro::Span span, llvm::StringRef replacement) -> bool {
        auto& manager = compiler_.getSourceManager();
        auto  begin   = manager.getLocForStartOfFile(manager.getMainFileID());
        if (begin.isInvalid()) return false;
        return ! rewriter_.ReplaceText(
            begin.getLocWithOffset(span.begin()), span.end() - span.begin(), replacement);
    }

    auto remove_span(std::string& item, pmacro::Span item_range, pmacro::Span removal) -> bool {
        if (removal.begin() < item_range.begin() || removal.end() > item_range.end() ||
            removal.begin() > removal.end())
            return false;
        item.erase(static_cast<size_t>(removal.begin() - item_range.begin()),
                   static_cast<size_t>(removal.end() - removal.begin()));
        return true;
    }

    auto mask_span(std::string& item, pmacro::Span item_range, pmacro::Span removal) -> bool {
        if (removal.begin() < item_range.begin() || removal.end() > item_range.end() ||
            removal.begin() > removal.end())
            return false;
        for (auto index = static_cast<size_t>(removal.begin() - item_range.begin());
             index < static_cast<size_t>(removal.end() - item_range.begin());
             ++index) {
            if (item[index] != '\n' && item[index] != '\r') item[index] = ' ';
        }
        return true;
    }

    auto expand_attr(clang::Decl&               declaration,
                     const clang::AnnotateAttr& attribute,
                     llvm::StringRef            arguments_source) -> void {
        auto&      diagnostics = compiler_.getDiagnostics();
        const auto annotation  = attribute.getAnnotation().drop_front(attr_annotation.size());
        const auto separator   = annotation.find("::");
        if (separator == llvm::StringRef::npos || separator == 0 ||
            separator + 2 >= annotation.size() ||
            annotation.drop_front(separator + 2).contains("::")) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "pmacro identity must be 'package-name::macro-name'");
            failed_ = true;
            return;
        }
        const auto package = annotation.take_front(separator);
        const auto name    = annotation.drop_front(separator + 2);
        const auto lookup  = state_.find(package, name, pmacro::host::MacroKind::Attr);
        if (lookup.status != MacroLookupStatus::Found) {
            auto message =
                llvm::StringRef("pmacro identity is not provided by the aggregate plugin");
            if (lookup.status == MacroLookupStatus::Duplicate)
                message = "pmacro identity is duplicated by its provider";
            else if (lookup.status == MacroLookupStatus::InvalidRegistration)
                message = "pmacro aggregate contains an invalid provider registration";
            else if (lookup.status == MacroLookupStatus::KindMismatch)
                message = "pmacro identity is registered as a derive macro";
            report(diagnostics, attribute.getLocation(), clang::DiagnosticsEngine::Error, message);
            failed_ = true;
            return;
        }
        expansions_.push_back("attr " + annotation.str());
        const auto item_span      = this->item_span(declaration, attribute);
        const auto attribute_span = this->attribute_span(attribute);
        if (item_span == std::nullopt || attribute_span == std::nullopt) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::attr' is only supported in the main source file");
            failed_ = true;
            return;
        }
        auto source = source_text(*item_span);
        if (! source.has_value()) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "cannot read the declaration source for 'pmacro::attr'");
            failed_ = true;
            return;
        }
        auto item = std::move(*source);
        if (! mask_span(item, *item_span, *attribute_span)) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::attr' source range is outside its declaration");
            failed_ = true;
            return;
        }
        const auto arguments   = state_.make_stream(arguments_source, *attribute_span);
        const auto item_stream = state_.make_stream(std::move(item), *item_span);
        auto       input       = pmacro::AttributeInput {
            .arguments      = state_.token_stream(arguments),
            .item           = state_.token_stream(item_stream),
            .attribute_span = *attribute_span,
            .item_span      = *item_span,
        };
        auto context = state_.context();
        state_.enter_invocation(package, name, lookup.provider, *attribute_span);
        auto result = std::get<pmacro::host::AttributeCallback>(lookup.macro->callback)
                          .function(std::move(input), context);
        state_.leave_invocation();
        if (! result) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "pmacro provider returned an expansion error");
            failed_ = true;
            return;
        }
        auto output   = std::move(*result);
        auto rendered = state_.render(output);
        if (rendered == std::nullopt) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "pmacro provider returned an invalid TokenStream");
            failed_ = true;
            return;
        }
        if (state_.contains_module_declaration(output)) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "pmacro output must not contain a module declaration");
            failed_ = true;
            return;
        }
        pending_ = pending_ || contains_proc_macro_invocation(compiler_, *rendered);
        if (! replace_text(*item_span, *rendered)) {
            report(diagnostics,
                   attribute.getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "cannot replace the declaration source for 'pmacro::attr'");
            failed_ = true;
        }
    }

    auto append_helpers(clang::Decl& declaration, std::vector<const clang::AnnotateAttr*>& helpers)
        -> void {
        for (const auto* attribute : declaration.specific_attrs<clang::AnnotateAttr>()) {
            if (attribute->getAnnotation().starts_with(helper_annotation))
                helpers.push_back(attribute);
        }
    }

    auto collect_helpers(clang::Decl& declaration, std::vector<const clang::AnnotateAttr*>& helpers)
        -> void {
        append_helpers(declaration, helpers);
        if (auto* nested = llvm::dyn_cast<clang::DeclContext>(&declaration)) {
            for (auto* child : nested->decls()) {
                if (child == nullptr) continue;
                append_helpers(*child, helpers);
                if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(child)) {
                    for (auto* parameter : function->parameters()) {
                        if (parameter != nullptr) append_helpers(*parameter, helpers);
                    }
                }
            }
        }
    }

    auto remove_attribute(std::string&               item,
                          pmacro::Span               item_span,
                          const clang::AnnotateAttr& attribute) -> bool {
        auto span = attribute_span(attribute);
        return span.has_value() && remove_span(item, item_span, *span);
    }

    auto expand_derive(clang::Decl&                                   declaration,
                       const std::vector<const clang::AnnotateAttr*>& invocations) -> void {
        auto&      diagnostics          = compiler_.getDiagnostics();
        const auto first_attribute_span = this->attribute_span(*invocations.front());
        for (size_t index = 1; index < invocations.size(); ++index) {
            const auto span = this->attribute_span(*invocations[index]);
            if (! first_attribute_span.has_value() || ! span.has_value() ||
                span->begin() != first_attribute_span->begin() ||
                span->end() != first_attribute_span->end()) {
                report(diagnostics,
                       invocations[index]->getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "derive identities must use one 'pmacro::derive' attribute");
                failed_ = true;
                return;
            }
        }
        const auto* record      = llvm::dyn_cast<clang::RecordDecl>(&declaration);
        const auto* enumeration = llvm::dyn_cast<clang::EnumDecl>(&declaration);
        if ((record == nullptr || ! record->isCompleteDefinition()) &&
            (enumeration == nullptr || ! enumeration->isCompleteDefinition())) {
            report(diagnostics,
                   invocations.front()->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::derive' requires a complete record, union, or enum definition");
            failed_ = true;
            return;
        }
        const auto item_span       = this->item_span(declaration, *invocations.front());
        const auto invocation_span = this->attribute_span(*invocations.front());
        if (! item_span.has_value() || ! invocation_span.has_value()) {
            report(diagnostics,
                   invocations.front()->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "'pmacro::derive' is only supported in the main source file");
            failed_ = true;
            return;
        }
        auto source = source_text(*item_span);
        if (! source.has_value()) {
            report(diagnostics,
                   invocations.front()->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "cannot read the declaration source for 'pmacro::derive'");
            failed_ = true;
            return;
        }
        auto retained = *source;
        auto helpers  = std::vector<const clang::AnnotateAttr*> {};
        collect_helpers(declaration, helpers);
        for (const auto* helper : helpers) {
            const auto owner = helper->getAnnotation().drop_front(helper_annotation.size());
            auto       found = false;
            for (const auto* invocation : invocations) {
                if (invocation->getAnnotation().drop_front(derive_annotation.size()) == owner) {
                    found = true;
                    break;
                }
            }
            if (! found) {
                report(diagnostics,
                       helper->getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "'pmacro::helper' owner is not present in the enclosing derive group");
                failed_ = true;
                return;
            }
        }
        auto removals = helpers;
        removals.push_back(invocations.front());
        std::sort(removals.begin(), removals.end(), [&](const auto* left, const auto* right) {
            const auto left_span  = attribute_span(*left);
            const auto right_span = attribute_span(*right);
            return left_span.has_value() && right_span.has_value() &&
                   left_span->begin() > right_span->begin();
        });
        for (const auto* removal : removals) {
            if (! remove_attribute(retained, *item_span, *removal)) {
                report(diagnostics,
                       removal->getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "cannot remove a consumed pmacro attribute from the retained item");
                failed_ = true;
                return;
            }
        }

        auto provider_item = *source;
        if (! first_attribute_span.has_value() ||
            ! mask_span(provider_item, *item_span, *first_attribute_span)) {
            report(diagnostics,
                   invocations.front()->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "cannot remove the active derive attribute from provider input");
            failed_ = true;
            return;
        }
        const auto item_stream = state_.make_stream(provider_item, *item_span);
        auto       generated   = std::string {};
        for (size_t index = 0; index < invocations.size(); ++index) {
            const auto& attribute  = *invocations[index];
            const auto  annotation = attribute.getAnnotation().drop_front(derive_annotation.size());
            for (size_t prior = 0; prior < index; ++prior) {
                if (invocations[prior]->getAnnotation().drop_front(derive_annotation.size()) ==
                    annotation) {
                    report(diagnostics,
                           attribute.getLocation(),
                           clang::DiagnosticsEngine::Error,
                           "a derive macro identity may appear only once on an item");
                    failed_ = true;
                    return;
                }
            }
            const auto separator = annotation.find("::");
            if (separator == llvm::StringRef::npos || separator == 0 ||
                separator + 2 >= annotation.size() ||
                annotation.drop_front(separator + 2).contains("::")) {
                report(diagnostics,
                       attribute.getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "pmacro identity must be 'package-name::macro-name'");
                failed_ = true;
                return;
            }
            const auto package = annotation.take_front(separator);
            const auto name    = annotation.drop_front(separator + 2);
            const auto lookup  = state_.find(package, name, pmacro::host::MacroKind::Derive);
            if (lookup.status != MacroLookupStatus::Found) {
                auto message = llvm::StringRef(
                    "pmacro derive identity is not provided by the aggregate plugin");
                if (lookup.status == MacroLookupStatus::Duplicate)
                    message = "pmacro derive identity is duplicated by its provider";
                else if (lookup.status == MacroLookupStatus::InvalidRegistration)
                    message = "pmacro aggregate contains an invalid provider registration";
                else if (lookup.status == MacroLookupStatus::KindMismatch)
                    message = "pmacro identity is registered as an attribute macro";
                report(
                    diagnostics, attribute.getLocation(), clang::DiagnosticsEngine::Error, message);
                failed_ = true;
                return;
            }
            auto input = pmacro::DeriveInput {
                .item           = state_.token_stream(item_stream),
                .attribute_span = *invocation_span,
                .item_span      = *item_span,
            };
            auto context = state_.context();
            state_.enter_invocation(package, name, lookup.provider, *invocation_span);
            auto result = std::get<pmacro::host::DeriveCallback>(lookup.macro->callback)
                              .function(std::move(input), context);
            state_.leave_invocation();
            if (! result) {
                report(diagnostics,
                       attribute.getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "pmacro derive provider returned an expansion error");
                failed_ = true;
                return;
            }
            auto output   = std::move(*result);
            auto rendered = state_.render(output);
            if (! rendered.has_value() || state_.contains_module_declaration(output)) {
                report(diagnostics,
                       attribute.getLocation(),
                       clang::DiagnosticsEngine::Error,
                       "pmacro derive provider returned an invalid TokenStream");
                failed_ = true;
                return;
            }
            pending_ = pending_ || contains_proc_macro_invocation(compiler_, *rendered);
            generated.append("\n");
            generated.append(*rendered);
            expansions_.push_back("derive " + annotation.str());
        }
        retained.append(generated);
        if (! replace_text(*item_span, retained)) {
            report(diagnostics,
                   invocations.front()->getLocation(),
                   clang::DiagnosticsEngine::Error,
                   "cannot replace the declaration source for 'pmacro::derive'");
            failed_ = true;
        }
    }

    clang::CompilerInstance& compiler_;
    clang::Rewriter&         rewriter_;
    ExpansionState           state_;
    std::vector<std::string> expansions_;
    bool                     failed_ {};
    bool                     pending_ {};
};

class ExpansionConsumer final : public clang::ASTConsumer {
public:
    ExpansionConsumer(clang::CompilerInstance& compiler, ExpansionOptions options)
        : compiler_(compiler), options_(std::move(options)) {
        rewriter_.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    }

    auto HandleTranslationUnit(clang::ASTContext& context) -> void override {
        if (options_.mode == "define") {
            auto transformer = DefinitionTransformer(compiler_, options_);
            transformer.traverse(*context.getTranslationUnitDecl());
            transformer.write();
            return;
        }
        if (options_.mode != "expand") return;
        auto visitor = ExpansionVisitor(compiler_, rewriter_);
        visitor.traverse(*context.getTranslationUnitDecl());
        if (visitor.failed() || compiler_.getDiagnostics().hasErrorOccurred()) return;
        if (options_.output.empty() || options_.status.empty()) {
            report(compiler_.getDiagnostics(),
                   {},
                   clang::DiagnosticsEngine::Error,
                   "pmacro expansion requires output=<path> and status=<path>");
            return;
        }

        std::error_code error;
        auto output = llvm::ToolOutputFile(options_.output, error, llvm::sys::fs::OF_Text);
        if (error) {
            report(compiler_.getDiagnostics(),
                   {},
                   clang::DiagnosticsEngine::Error,
                   "cannot open pmacro expansion output");
            return;
        }
        auto trace = std::optional<llvm::ToolOutputFile> {};
        if (! options_.trace.empty()) {
            trace.emplace(options_.trace, error, llvm::sys::fs::OF_Text);
            if (error) {
                report(compiler_.getDiagnostics(),
                       {},
                       clang::DiagnosticsEngine::Error,
                       "cannot open pmacro expansion trace");
                return;
            }
            for (const auto& identity : visitor.expansions()) trace->os() << identity << '\n';
        }
        auto status = llvm::ToolOutputFile(options_.status, error, llvm::sys::fs::OF_Text);
        if (error) {
            report(compiler_.getDiagnostics(),
                   {},
                   clang::DiagnosticsEngine::Error,
                   "cannot open pmacro expansion status");
            return;
        }
        status.os() << (visitor.pending() ? "pending\n" : "complete\n");
        auto&      stream = output.os();
        const auto file   = compiler_.getSourceManager().getMainFileID();
        if (const auto* buffer = rewriter_.getRewriteBufferFor(file)) {
            buffer->write(stream);
        } else {
            stream << compiler_.getSourceManager().getBufferData(file);
        }
        if (trace.has_value()) trace->keep();
        status.keep();
        output.keep();
    }

private:
    clang::CompilerInstance& compiler_;
    ExpansionOptions         options_;
    clang::Rewriter          rewriter_;
};

class Plugin final : public clang::PluginASTAction {
public:
    auto CreateASTConsumer(clang::CompilerInstance& compiler, llvm::StringRef)
        -> std::unique_ptr<clang::ASTConsumer> override {
        return std::make_unique<ExpansionConsumer>(compiler, options_);
    }

    auto ParseArgs(const clang::CompilerInstance&  compiler,
                   const std::vector<std::string>& arguments) -> bool override {
        for (const auto& argument : arguments) {
            if (llvm::StringRef(argument).starts_with("mode=")) {
                options_.mode = llvm::StringRef(argument).drop_front(5).str();
                continue;
            }
            if (llvm::StringRef(argument).starts_with("output=")) {
                options_.output = llvm::StringRef(argument).drop_front(7).str();
                continue;
            }
            if (llvm::StringRef(argument).starts_with("trace=")) {
                options_.trace = llvm::StringRef(argument).drop_front(6).str();
                continue;
            }
            if (llvm::StringRef(argument).starts_with("status=")) {
                options_.status = llvm::StringRef(argument).drop_front(7).str();
                continue;
            }
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

    auto getActionType() -> ActionType override { return AddAfterMainAction; }

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
