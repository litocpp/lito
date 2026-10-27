export module pmacro;

import std;

export namespace pmacro
{

enum class TokenKind : std::uint32_t
{
    Ident,
    Literal,
    Punct,
    Group,
};

enum class Delimiter : std::uint32_t
{
    None,
    Parenthesis,
    Brace,
    Bracket,
};

enum class Spacing : std::uint32_t
{
    Alone,
    Joint,
};

enum class DiagnosticLevel : std::uint32_t
{
    Note,
    Warning,
    Error,
};

enum class Error : std::uint32_t
{
    Failure,
    InvalidArgument,
};

class Span {
public:
    constexpr Span() noexcept = default;
    constexpr Span(std::uint64_t source, std::uint32_t begin, std::uint32_t end) noexcept
        : source_(source), begin_(begin), end_(end) {}

    constexpr auto source() const noexcept -> std::uint64_t { return source_; }
    constexpr auto begin() const noexcept -> std::uint32_t { return begin_; }
    constexpr auto end() const noexcept -> std::uint32_t { return end_; }

private:
    std::uint64_t source_ {};
    std::uint32_t begin_ {};
    std::uint32_t end_ {};
};

enum class RecordKind
{
    Struct,
    Class,
    Union
};
enum class Access
{
    Public,
    Protected,
    Private
};
enum class FieldKind
{
    Named,
    UnnamedBitField,
    AnonymousRecord
};
enum class CodeOrigin
{
    Spelled,
    Rendered
};

enum class SourceError
{
    Unavailable,
    NonContiguous
};
enum class SourceContextId : std::uint64_t
{
};
enum class RepresentationError
{
    Unavailable,
    Unnameable,
    RequiresTemplateContext,
    UndeducedType
};

struct SourceInfo {
    SourceContextId                  context {};
    std::expected<Span, SourceError> spelling  = std::unexpected(SourceError::Unavailable);
    std::expected<Span, SourceError> expansion = std::unexpected(SourceError::Unavailable);
    std::optional<Span>              diagnostic;
    bool                             from_macro = false;
};

struct CodeFragment {
    std::string text;
    CodeOrigin  origin = CodeOrigin::Rendered;
    SourceInfo  source;
};

struct TypeInfo {
    std::string                                      display;
    std::expected<CodeFragment, RepresentationError> type_id =
        std::unexpected(RepresentationError::Unavailable);
    SourceInfo source;
    bool       dependent = false;
};

struct ExpressionInfo {
    std::expected<CodeFragment, RepresentationError> code =
        std::unexpected(RepresentationError::Unavailable);
    SourceInfo source;
    bool       dependent = false;
};

struct AttributeInfo {
    std::string                                      name;
    std::expected<CodeFragment, RepresentationError> syntax =
        std::unexpected(RepresentationError::Unavailable);
    SourceInfo source;
    bool       implicit = false;
};

struct HelperInfo {
    std::string                                      owner;
    std::expected<CodeFragment, RepresentationError> arguments =
        std::unexpected(RepresentationError::Unavailable);
    SourceInfo source;
};

struct FieldInfo {
    std::optional<std::string>    name;
    FieldKind                     kind   = FieldKind::Named;
    Access                        access = Access::Public;
    TypeInfo                      type;
    bool                          mutable_ = false;
    std::optional<ExpressionInfo> bit_width;
    std::optional<ExpressionInfo> initializer;
    std::vector<AttributeInfo>    attributes;
    std::vector<HelperInfo>       helpers;
    SourceInfo                    source;
};

struct EnumeratorInfo {
    std::string                   name;
    std::optional<ExpressionInfo> initializer;
    std::vector<AttributeInfo>    attributes;
    std::vector<HelperInfo>       helpers;
    SourceInfo                    source;
};

struct RecordInfo {
    RecordKind             kind      = RecordKind::Struct;
    bool                   has_bases = false;
    bool                   dependent = false;
    std::vector<FieldInfo> fields;
};

struct EnumInfo {
    bool                        scoped                   = false;
    bool                        fixed_underlying_type    = false;
    bool                        explicit_underlying_type = false;
    TypeInfo                    underlying_type;
    std::vector<EnumeratorInfo> enumerators;
};

enum class DefaultArgumentState
{
    None,
    Available,
    Deferred
};
enum class ExceptionState
{
    PotentiallyThrowing,
    NonThrowing,
    Dependent,
    Unavailable
};
enum class FunctionForm
{
    Declaration,
    Definition,
    Deleted,
    Defaulted,
    Unavailable
};
enum class LanguageLinkage
{
    Cxx,
    C,
    Unavailable
};

struct DefaultArgumentInfo {
    DefaultArgumentState          state = DefaultArgumentState::None;
    std::optional<ExpressionInfo> expression;
    bool                          inherited = false;
};

struct ParameterInfo {
    std::optional<std::string> name;
    TypeInfo                   declared_type;
    TypeInfo                   type;
    DefaultArgumentInfo        default_argument;
    bool                       parameter_pack = false;
    std::vector<AttributeInfo> attributes;
    SourceInfo                 source;
};

struct ExceptionInfo {
    ExceptionState                state = ExceptionState::Unavailable;
    std::optional<ExpressionInfo> condition;
    SourceInfo                    source;
};

struct FunctionInfo {
    TypeInfo                   return_type;
    std::vector<ParameterInfo> parameters;
    ExceptionInfo              exception;
    FunctionForm               form                       = FunctionForm::Unavailable;
    LanguageLinkage            language_linkage           = LanguageLinkage::Unavailable;
    bool                       default_calling_convention = false;
    bool                       variadic                   = false;
    bool                       dependent                  = false;
    bool                       template_                  = false;
    bool                       static_                    = false;
    bool                       inline_                    = false;
    bool                       constexpr_                 = false;
    bool                       consteval_                 = false;
};

struct Declaration {
    std::optional<std::string>                       name;
    std::optional<std::string>                       qualified_name;
    std::vector<AttributeInfo>                       attributes;
    std::vector<HelperInfo>                          helpers;
    SourceInfo                                       source;
    std::variant<RecordInfo, EnumInfo, FunctionInfo> data;
};

enum class DeclarationError
{
    NotInInvocation,
    UnsupportedDeclaration,
    IncompleteDefinition,
    InvalidDeclaration,
    ChangedInput
};

enum class FragmentError
{
    NotInInvocation,
    ForeignSource,
    InvalidFragment
};

class TokenStream;
class TokenTree;
class Context;

namespace host
{

using StreamHandle = std::uint64_t;

struct RuntimeToken {
    TokenKind        kind { TokenKind::Literal };
    Delimiter        delimiter { Delimiter::None };
    Spacing          spacing { Spacing::Alone };
    std::string_view spelling;
    Span             span;
    StreamHandle     children {};
};

class Runtime;

} // namespace host

class TokenTree {
public:
    constexpr TokenTree() noexcept = default;

    constexpr auto kind() const noexcept -> TokenKind { return value_.kind; }
    constexpr auto delimiter() const noexcept -> Delimiter { return value_.delimiter; }
    constexpr auto spacing() const noexcept -> Spacing { return value_.spacing; }
    constexpr auto spelling() const noexcept -> std::string_view { return value_.spelling; }
    constexpr auto span() const noexcept -> Span { return value_.span; }
    auto           children() const noexcept -> TokenStream;

private:
    friend class TokenStream;

    constexpr TokenTree(host::Runtime* runtime, host::RuntimeToken value) noexcept
        : runtime_(runtime), value_(value) {}

    host::Runtime*     runtime_ {};
    host::RuntimeToken value_ {};
};

class TokenStream {
public:
    class Iterator;

    constexpr TokenStream() noexcept = default;

    auto           size() const noexcept -> std::size_t;
    auto           operator[](std::size_t index) const noexcept -> TokenTree;
    auto           append(const TokenTree& token) noexcept -> bool;
    auto           append(const TokenStream& stream) noexcept -> bool;
    constexpr auto valid() const noexcept -> bool { return runtime_ != nullptr && handle_ != 0; }
    auto           begin() const noexcept -> Iterator;
    auto           end() const noexcept -> Iterator;

private:
    friend class TokenTree;
    friend class host::Runtime;

    constexpr TokenStream(host::Runtime* runtime, host::StreamHandle handle) noexcept
        : runtime_(runtime), handle_(handle) {}

    host::Runtime*     runtime_ {};
    host::StreamHandle handle_ {};
};

class TokenStream::Iterator {
public:
    auto operator*() const noexcept -> TokenTree { return TokenStream(runtime_, handle_)[index_]; }
    constexpr auto operator++() noexcept -> Iterator& {
        ++index_;
        return *this;
    }
    constexpr auto operator==(const Iterator& other) const noexcept -> bool {
        return runtime_ == other.runtime_ && handle_ == other.handle_ && index_ == other.index_;
    }

private:
    friend class TokenStream;

    constexpr Iterator(host::Runtime*     runtime,
                       host::StreamHandle handle,
                       std::size_t        index) noexcept
        : runtime_(runtime), handle_(handle), index_(index) {}

    host::Runtime*     runtime_ {};
    host::StreamHandle handle_ {};
    std::size_t        index_ {};
};

enum class OutputLocation
{
    AfterItem,
    NamespaceEnd,
};

class Context {
public:
    auto declaration() const noexcept
        -> std::expected<std::shared_ptr<const Declaration>, DeclarationError>;
    auto tokens(const CodeFragment& fragment) const noexcept
        -> std::expected<TokenStream, FragmentError>;
    auto stream() const noexcept -> std::expected<TokenStream, Error>;
    auto parse(std::string_view source) const noexcept -> std::expected<TokenStream, Error>;
    auto emit(OutputLocation location, const TokenStream& output) const noexcept
        -> std::expected<void, Error>;
    auto diagnostic(DiagnosticLevel level, Span span, std::string_view message) const noexcept
        -> bool;
    auto call_site() const noexcept -> std::expected<Span, Error>;
    auto join(Span left, Span right) const noexcept -> std::expected<Span, Error>;
    auto subspan(Span span, std::uint32_t begin, std::uint32_t end) const noexcept
        -> std::expected<Span, Error>;
    auto macro_identity() const noexcept -> std::string_view;
    auto provider_identity() const noexcept -> std::string_view;
    auto target_triple() const noexcept -> std::string_view;

private:
    friend class host::Runtime;

    explicit constexpr Context(host::Runtime* runtime) noexcept: runtime_(runtime) {}

    host::Runtime* runtime_ {};
};

struct AttributeInput {
    TokenStream arguments;
    TokenStream item;
    Span        attribute_span;
    Span        item_span;
};

struct DeriveInput {
    TokenStream item;
    Span        attribute_span;
    Span        item_span;
};

namespace host
{

class Runtime {
public:
    virtual auto emit_stream(OutputLocation location, StreamHandle output) noexcept
        -> std::expected<void, Error> = 0;
    virtual auto current_declaration() noexcept
        -> std::expected<std::shared_ptr<const Declaration>, DeclarationError> = 0;
    virtual auto fragment_stream(const CodeFragment& fragment) noexcept
        -> std::expected<StreamHandle, FragmentError>                           = 0;
    virtual auto create_stream() noexcept -> std::expected<StreamHandle, Error> = 0;
    virtual auto parse_stream(std::string_view source) noexcept
        -> std::expected<StreamHandle, Error> = 0;
    virtual auto stream_size(StreamHandle handle) const noexcept
        -> std::expected<std::size_t, Error> = 0;
    virtual auto stream_token(StreamHandle handle, std::size_t index) const noexcept
        -> std::expected<RuntimeToken, Error> = 0;
    virtual auto push_token(StreamHandle handle, const RuntimeToken& token) noexcept
        -> std::expected<void, Error>                                                           = 0;
    virtual auto call_site_span() const noexcept -> std::expected<Span, Error>                  = 0;
    virtual auto join_spans(Span left, Span right) const noexcept -> std::expected<Span, Error> = 0;
    virtual auto subspan(Span span, std::uint32_t begin, std::uint32_t end) const noexcept
        -> std::expected<Span, Error>                                       = 0;
    virtual auto emit_diagnostic(DiagnosticLevel  level,
                                 Span             span,
                                 std::string_view message) noexcept -> bool = 0;
    virtual auto macro_identity() const noexcept -> std::string_view        = 0;
    virtual auto provider_identity() const noexcept -> std::string_view     = 0;
    virtual auto target_triple() const noexcept -> std::string_view         = 0;

protected:
    friend class pmacro::Context;

    constexpr Runtime() noexcept = default;
    ~Runtime()                   = default;

    constexpr auto make_token_stream(StreamHandle handle) noexcept -> TokenStream {
        return TokenStream(this, handle);
    }
    constexpr auto make_context() noexcept -> Context { return Context(this); }
    constexpr auto token_stream_handle(const TokenStream& stream) const noexcept -> StreamHandle {
        return stream.runtime_ == this ? stream.handle_ : StreamHandle {};
    }
};

using MacroResult    = std::expected<TokenStream, Error>;
using AttributeMacro = MacroResult (*)(AttributeInput, Context&) noexcept;
using DeriveMacro    = MacroResult (*)(DeriveInput, Context&) noexcept;

struct AttributeCallback {
    AttributeMacro function {};
};

struct DeriveCallback {
    DeriveMacro function {};
};

using MacroCallback = std::variant<AttributeCallback, DeriveCallback>;

enum class MacroKind
{
    Attr,
    Derive,
};

constexpr auto macro_kind(const MacroCallback& callback) noexcept -> MacroKind {
    return std::holds_alternative<AttributeCallback>(callback) ? MacroKind::Attr
                                                               : MacroKind::Derive;
}

struct MacroDescriptor {
    std::string_view name;
    MacroCallback    callback;
    std::string_view scope {};
};

class MacroRegistration {
public:
    MacroRegistration(std::string_view provider, MacroDescriptor macro) noexcept;

    constexpr auto provider() const noexcept -> std::string_view { return provider_; }
    constexpr auto macro() const noexcept -> const MacroDescriptor& { return macro_; }
    constexpr auto next() const noexcept -> const MacroRegistration* { return next_; }

private:
    std::string_view   provider_;
    MacroDescriptor    macro_;
    MacroRegistration* next_ {};
};

auto registered_macros() noexcept -> const MacroRegistration*;

} // namespace host

} // namespace pmacro

namespace pmacro
{

namespace host
{

auto macro_registration_head() noexcept -> MacroRegistration*& {
    static MacroRegistration* head {};
    return head;
}

MacroRegistration::MacroRegistration(std::string_view provider, MacroDescriptor macro) noexcept
    : provider_(provider), macro_(macro), next_(macro_registration_head()) {
    macro_registration_head() = this;
}

auto registered_macros() noexcept -> const MacroRegistration* {
    return macro_registration_head();
}

} // namespace host

auto TokenStream::size() const noexcept -> std::size_t {
    if (! valid()) return 0;
    auto size = runtime_->stream_size(handle_);
    return size ? *size : 0;
}

auto TokenStream::operator[](std::size_t index) const noexcept -> TokenTree {
    if (! valid()) return {};
    auto token = runtime_->stream_token(handle_, index);
    return token ? TokenTree(runtime_, *token) : TokenTree {};
}

auto TokenStream::append(const TokenTree& token) noexcept -> bool {
    return valid() && token.runtime_ == runtime_ && runtime_->push_token(handle_, token.value_);
}

auto TokenStream::append(const TokenStream& stream) noexcept -> bool {
    if (! valid() || ! stream.valid() || runtime_ != stream.runtime_) return false;
    const auto count = stream.size();
    for (std::size_t index = 0; index < count; ++index) {
        if (! append(stream[index])) return false;
    }
    return true;
}

auto TokenStream::begin() const noexcept -> Iterator {
    return Iterator(runtime_, handle_, 0);
}

auto TokenStream::end() const noexcept -> Iterator {
    return Iterator(runtime_, handle_, size());
}

auto TokenTree::children() const noexcept -> TokenStream {
    return runtime_ == nullptr ? TokenStream {} : TokenStream(runtime_, value_.children);
}

auto Context::declaration() const noexcept
    -> std::expected<std::shared_ptr<const Declaration>, DeclarationError> {
    if (runtime_ == nullptr) return std::unexpected(DeclarationError::NotInInvocation);
    return runtime_->current_declaration();
}

auto Context::tokens(const CodeFragment& fragment) const noexcept
    -> std::expected<TokenStream, FragmentError> {
    if (runtime_ == nullptr) return std::unexpected(FragmentError::NotInInvocation);
    auto handle = runtime_->fragment_stream(fragment);
    if (! handle) return std::unexpected(handle.error());
    return runtime_->make_token_stream(*handle);
}

auto Context::stream() const noexcept -> std::expected<TokenStream, Error> {
    if (runtime_ == nullptr) return std::unexpected(Error::InvalidArgument);
    auto handle = runtime_->create_stream();
    if (! handle) return std::unexpected(handle.error());
    return runtime_->make_token_stream(*handle);
}

auto Context::parse(std::string_view source) const noexcept -> std::expected<TokenStream, Error> {
    if (runtime_ == nullptr) return std::unexpected(Error::InvalidArgument);
    auto handle = runtime_->parse_stream(source);
    if (! handle) return std::unexpected(handle.error());
    return runtime_->make_token_stream(*handle);
}

auto Context::diagnostic(DiagnosticLevel level, Span span, std::string_view message) const noexcept
    -> bool {
    return runtime_ != nullptr && runtime_->emit_diagnostic(level, span, message);
}

auto Context::emit(OutputLocation location, const TokenStream& output) const noexcept
    -> std::expected<void, Error> {
    if (runtime_ == nullptr) return std::unexpected(Error::InvalidArgument);
    const auto handle = runtime_->token_stream_handle(output);
    if (! handle) return std::unexpected(Error::InvalidArgument);
    return runtime_->emit_stream(location, handle);
}

auto Context::call_site() const noexcept -> std::expected<Span, Error> {
    if (runtime_ == nullptr) return std::unexpected(Error::InvalidArgument);
    return runtime_->call_site_span();
}

auto Context::join(Span left, Span right) const noexcept -> std::expected<Span, Error> {
    if (runtime_ == nullptr) return std::unexpected(Error::InvalidArgument);
    return runtime_->join_spans(left, right);
}

auto Context::subspan(Span span, std::uint32_t begin, std::uint32_t end) const noexcept
    -> std::expected<Span, Error> {
    if (runtime_ == nullptr) return std::unexpected(Error::InvalidArgument);
    return runtime_->subspan(span, begin, end);
}

auto Context::macro_identity() const noexcept -> std::string_view {
    return runtime_ == nullptr ? std::string_view {} : runtime_->macro_identity();
}

auto Context::provider_identity() const noexcept -> std::string_view {
    return runtime_ == nullptr ? std::string_view {} : runtime_->provider_identity();
}

auto Context::target_triple() const noexcept -> std::string_view {
    return runtime_ == nullptr ? std::string_view {} : runtime_->target_triple();
}

} // namespace pmacro
