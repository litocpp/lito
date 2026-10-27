#pragma once

namespace pmacro::clang_bridge
{

struct FragmentSource {
    Span span;
    bool exact;
};

class SourceRegistry {
public:
    explicit SourceRegistry(clang::CompilerInstance& compiler);

    auto context() const -> SourceContextId;
    auto spelling(clang::CharSourceRange range) const -> std::expected<Span, SourceError>;
    auto info(clang::SourceRange range, clang::SourceLocation anchor = {}) const -> SourceInfo;
    auto attribute_span(const clang::AnnotateAttr& attribute) const -> std::optional<Span>;
    auto rewrite_span(clang::SourceRange range) const -> std::optional<Span>;
    auto main_span(std::uint32_t begin, std::uint32_t end) const -> Span;
    auto valid(Span span) const -> bool;
    auto is_main(Span span) const -> bool;
    auto location(Span span) const -> clang::SourceLocation;
    auto text(Span span) const -> std::optional<std::string>;
    auto fragment(clang::CharSourceRange range) const
        -> std::expected<CodeFragment, RepresentationError>;
    auto fragment_source(const CodeFragment& fragment, Span fallback) const
        -> std::expected<FragmentSource, FragmentError>;

private:
    auto file_span(clang::CharSourceRange range) const -> std::expected<Span, SourceError>;
    auto file(Span span) const -> clang::FileID;
    auto identify(clang::FileID file) const -> std::uint64_t;

    clang::CompilerInstance&                                                    compiler_;
    SourceContextId                                                             context_;
    mutable std::vector<std::pair<std::uint64_t, clang::FileID>>                files_;
    mutable std::unordered_map<const clang::AnnotateAttr*, std::optional<Span>> attribute_spans_;
};

struct PreparedHelper {
    const clang::AnnotateAttr* attribute;
    HelperInfo                 value;
};

struct PreparedAttributes {
    std::vector<AttributeInfo>  attributes;
    std::vector<HelperInfo>     helpers;
    std::vector<PreparedHelper> helper_attributes;
};

class DeclarationStore {
public:
    DeclarationStore(clang::CompilerInstance& compiler, SourceRegistry& sources);

    auto attributes(const clang::Decl& declaration) -> const PreparedAttributes&;
    auto helpers(const clang::Decl& declaration) -> const std::vector<PreparedHelper>&;
    auto get(const clang::Decl& declaration)
        -> std::expected<std::shared_ptr<const Declaration>, DeclarationError>;

private:
    struct PreparedDeclaration {
        std::optional<PreparedAttributes>          attributes;
        std::optional<std::vector<PreparedHelper>> helpers;
        std::optional<std::expected<std::shared_ptr<const Declaration>, DeclarationError>> snapshot;
    };

    auto build(const clang::Decl& declaration)
        -> std::expected<std::shared_ptr<const Declaration>, DeclarationError>;
    auto type(clang::QualType type, SourceInfo source) const -> TypeInfo;
    auto expression(const clang::Expr* expression) const -> ExpressionInfo;

    clang::CompilerInstance&                                    compiler_;
    SourceRegistry&                                             sources_;
    std::unordered_map<const clang::Decl*, PreparedDeclaration> declarations_;
};

} // namespace pmacro::clang_bridge
