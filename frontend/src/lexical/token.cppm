export module lito.frontend.lexical:token;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::lexical
{

using SourceId = usize;

struct SourceLocation {
    SourceId source {};
    usize    offset {};
    usize    line { usize(1) };
    usize    column { usize(1) };
};

enum class TokenKind
{
    Identifier,
    PpNumber,
    StringLiteral,
    CharacterLiteral,
    HeaderName,
    Punctuation,
    Newline,
};

class TokenTextIdentity {
public:
    TokenTextIdentity() = default;

    auto is_valid() const noexcept -> bool { return data_ != nullptr; }

private:
    explicit TokenTextIdentity(const byte* data) noexcept: data_(data) {}

    const byte* data_ {};

    friend class TokenText;
};

class TokenText {
public:
    TokenText() = default;
    TokenText(String text): owned_(rstd::move(text)) {}

    static auto borrowed(ref<str> text) -> TokenText {
        auto result         = TokenText {};
        result.borrowed_    = text;
        result.is_borrowed_ = true;
        return result;
    }

    auto as_str() const noexcept -> ref<str> { return is_borrowed_ ? borrowed_ : owned_.as_str(); }

    auto len() const noexcept -> usize { return as_str().len(); }
    auto is_empty() const noexcept -> bool { return as_str().is_empty(); }
    auto clone() const -> String { return String::make(as_str()); }

    auto borrowed_identity() const noexcept -> TokenTextIdentity {
        return TokenTextIdentity(is_borrowed_ ? borrowed_.data() : nullptr);
    }

    auto matches(TokenTextIdentity identity) const noexcept -> bool {
        return identity.is_valid() && is_borrowed_ && borrowed_.data() == identity.data_;
    }

    auto shared_clone() const -> TokenText {
        return is_borrowed_ ? borrowed(borrowed_) : TokenText { owned_.clone() };
    }

    auto operator=(String text) -> TokenText& {
        borrowed_    = ref<str> {};
        is_borrowed_ = false;
        owned_       = rstd::move(text);
        return *this;
    }

    auto push_str(ref<str> text) -> void {
        if (is_borrowed_) {
            owned_       = String::make(borrowed_);
            borrowed_    = ref<str> {};
            is_borrowed_ = false;
        }
        owned_.push_str(text);
    }

private:
    ref<str> borrowed_;
    String   owned_;
    bool     is_borrowed_ { false };
};

struct Token {
    TokenKind                   kind { TokenKind::Punctuation };
    TokenText                   text;
    SourceLocation              spelling;
    SourceLocation              expansion;
    Option<rstd::path::PathBuf> presumed_path;
    bool                        start_of_line { false };
    bool                        leading_space { false };
    bool                        disable_expand { false };
    u32                         unavailable_macro_revision {};
    TokenTextIdentity           unavailable_macro;

    auto is_known_unavailable_macro(u32 revision) const noexcept -> bool {
        return unavailable_macro_revision == revision && text.matches(unavailable_macro);
    }

    auto mark_unavailable_macro(u32 revision) noexcept -> void {
        unavailable_macro_revision = revision;
        unavailable_macro          = text.borrowed_identity();
    }

    auto clone() const -> Token {
        return Token {
            .kind           = kind,
            .text           = text.shared_clone(),
            .spelling       = spelling,
            .expansion      = expansion,
            .presumed_path  = presumed_path.is_some()
                                  ? Some(rstd::path::PathBuf::from((*presumed_path).as_path()))
                                  : Option<rstd::path::PathBuf> {},
            .start_of_line  = start_of_line,
            .leading_space  = leading_space,
            .disable_expand = disable_expand,
            .unavailable_macro_revision = unavailable_macro_revision,
            .unavailable_macro          = unavailable_macro,
        };
    }
};

auto is_identifier_start(u8 value) noexcept -> bool {
    return (value >= u8('a') && value <= u8('z')) || (value >= u8('A') && value <= u8('Z')) ||
           value == u8('_') || value >= u8(0x80);
}

auto is_identifier_continue(u8 value) noexcept -> bool {
    return is_identifier_start(value) || (value >= u8('0') && value <= u8('9'));
}

inline constexpr auto CPP_IDENTIFIER_RULE_ID =
    "lito-cpp20-clang-standard-library-identifiers-v1"_str;

inline constexpr auto CPP_RESERVED_IDENTIFIERS = R"LITO(_Atomic
__datasizeof
alignas
alignof
and
and_eq
asm
auto
bitand
bitor
bool
break
case
catch
char
char16_t
char32_t
char8_t
class
co_await
co_return
co_yield
compl
concept
const
const_cast
consteval
constexpr
constinit
continue
decltype
default
delete
do
double
dynamic_cast
else
enum
explicit
export
extern
false
float
for
friend
goto
if
int
long
mutable
namespace
new
noexcept
not
not_eq
nullptr
operator
or
or_eq
private
protected
public
register
reinterpret_cast
requires
return
short
signed
sizeof
static
static_assert
static_cast
struct
switch
template
this
thread_local
throw
true
try
typedef
typeid
typename
union
unsigned
using
virtual
void
volatile
wchar_t
while
xor
xor_eq
)LITO"_str;

auto is_cpp_reserved_identifier(ref<str> value) -> bool {
    auto begin = usize {};
    while (begin < CPP_RESERVED_IDENTIFIERS.len()) {
        auto end = begin;
        while (end < CPP_RESERVED_IDENTIFIERS.len() &&
               CPP_RESERVED_IDENTIFIERS.as_bytes()[end] != u8('\n')) {
            ++end;
        }
        auto keyword = CPP_RESERVED_IDENTIFIERS.get(begin, end);
        if (keyword.is_some() && *keyword == value) return true;
        begin = end + usize(1);
    }
    return false;
}

auto is_cpp_identifier_token(const Token& token, ref<str>) -> bool {
    return token.kind == TokenKind::Identifier && ! is_cpp_reserved_identifier(token.text.as_str());
}

} // namespace lito::frontend::lexical
