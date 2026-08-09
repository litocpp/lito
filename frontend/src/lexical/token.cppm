export module lito.frontend.lexical:token;

import rstd;
import lito.frontend.static_name;

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

using CppAtomicKeyword          = StaticName<"_Atomic">;
using CppDatasizeofKeyword      = StaticName<"__datasizeof">;
using CppAlignasKeyword         = StaticName<"alignas">;
using CppAlignofKeyword         = StaticName<"alignof">;
using CppAndKeyword             = StaticName<"and">;
using CppAndEqKeyword           = StaticName<"and_eq">;
using CppAsmKeyword             = StaticName<"asm">;
using CppAutoKeyword            = StaticName<"auto">;
using CppBitandKeyword          = StaticName<"bitand">;
using CppBitorKeyword           = StaticName<"bitor">;
using CppBoolKeyword            = StaticName<"bool">;
using CppBreakKeyword           = StaticName<"break">;
using CppCaseKeyword            = StaticName<"case">;
using CppCatchKeyword           = StaticName<"catch">;
using CppCharKeyword            = StaticName<"char">;
using CppChar16Keyword          = StaticName<"char16_t">;
using CppChar32Keyword          = StaticName<"char32_t">;
using CppChar8Keyword           = StaticName<"char8_t">;
using CppClassKeyword           = StaticName<"class">;
using CppCoAwaitKeyword         = StaticName<"co_await">;
using CppCoReturnKeyword        = StaticName<"co_return">;
using CppCoYieldKeyword         = StaticName<"co_yield">;
using CppComplKeyword           = StaticName<"compl">;
using CppConceptKeyword         = StaticName<"concept">;
using CppConstKeyword           = StaticName<"const">;
using CppConstCastKeyword       = StaticName<"const_cast">;
using CppConstevalKeyword       = StaticName<"consteval">;
using CppConstexprKeyword       = StaticName<"constexpr">;
using CppConstinitKeyword       = StaticName<"constinit">;
using CppContinueKeyword        = StaticName<"continue">;
using CppDecltypeKeyword        = StaticName<"decltype">;
using CppDefaultKeyword         = StaticName<"default">;
using CppDeleteKeyword          = StaticName<"delete">;
using CppDoKeyword              = StaticName<"do">;
using CppDoubleKeyword          = StaticName<"double">;
using CppDynamicCastKeyword     = StaticName<"dynamic_cast">;
using CppElseKeyword            = StaticName<"else">;
using CppEnumKeyword            = StaticName<"enum">;
using CppExplicitKeyword        = StaticName<"explicit">;
using CppExportKeyword          = StaticName<"export">;
using CppExternKeyword          = StaticName<"extern">;
using CppFalseKeyword           = StaticName<"false">;
using CppFloatKeyword           = StaticName<"float">;
using CppForKeyword             = StaticName<"for">;
using CppFriendKeyword          = StaticName<"friend">;
using CppGotoKeyword            = StaticName<"goto">;
using CppIfKeyword              = StaticName<"if">;
using CppIntKeyword             = StaticName<"int">;
using CppLongKeyword            = StaticName<"long">;
using CppMutableKeyword         = StaticName<"mutable">;
using CppNamespaceKeyword       = StaticName<"namespace">;
using CppNewKeyword             = StaticName<"new">;
using CppNoexceptKeyword        = StaticName<"noexcept">;
using CppNotKeyword             = StaticName<"not">;
using CppNotEqKeyword           = StaticName<"not_eq">;
using CppNullptrKeyword         = StaticName<"nullptr">;
using CppOperatorKeyword        = StaticName<"operator">;
using CppOrKeyword              = StaticName<"or">;
using CppOrEqKeyword            = StaticName<"or_eq">;
using CppPrivateKeyword         = StaticName<"private">;
using CppProtectedKeyword       = StaticName<"protected">;
using CppPublicKeyword          = StaticName<"public">;
using CppRegisterKeyword        = StaticName<"register">;
using CppReinterpretCastKeyword = StaticName<"reinterpret_cast">;
using CppRequiresKeyword        = StaticName<"requires">;
using CppReturnKeyword          = StaticName<"return">;
using CppShortKeyword           = StaticName<"short">;
using CppSignedKeyword          = StaticName<"signed">;
using CppSizeofKeyword          = StaticName<"sizeof">;
using CppStaticKeyword          = StaticName<"static">;
using CppStaticAssertKeyword    = StaticName<"static_assert">;
using CppStaticCastKeyword      = StaticName<"static_cast">;
using CppStructKeyword          = StaticName<"struct">;
using CppSwitchKeyword          = StaticName<"switch">;
using CppTemplateKeyword        = StaticName<"template">;
using CppThisKeyword            = StaticName<"this">;
using CppThreadLocalKeyword     = StaticName<"thread_local">;
using CppThrowKeyword           = StaticName<"throw">;
using CppTrueKeyword            = StaticName<"true">;
using CppTryKeyword             = StaticName<"try">;
using CppTypedefKeyword         = StaticName<"typedef">;
using CppTypeidKeyword          = StaticName<"typeid">;
using CppTypenameKeyword        = StaticName<"typename">;
using CppUnionKeyword           = StaticName<"union">;
using CppUnsignedKeyword        = StaticName<"unsigned">;
using CppUsingKeyword           = StaticName<"using">;
using CppVirtualKeyword         = StaticName<"virtual">;
using CppVoidKeyword            = StaticName<"void">;
using CppVolatileKeyword        = StaticName<"volatile">;
using CppWcharKeyword           = StaticName<"wchar_t">;
using CppWhileKeyword           = StaticName<"while">;
using CppXorKeyword             = StaticName<"xor">;
using CppXorEqKeyword           = StaticName<"xor_eq">;

using CppReservedIdentifierSet = StaticNameSet<CppAtomicKeyword,
                                               CppDatasizeofKeyword,
                                               CppAlignasKeyword,
                                               CppAlignofKeyword,
                                               CppAndKeyword,
                                               CppAndEqKeyword,
                                               CppAsmKeyword,
                                               CppAutoKeyword,
                                               CppBitandKeyword,
                                               CppBitorKeyword,
                                               CppBoolKeyword,
                                               CppBreakKeyword,
                                               CppCaseKeyword,
                                               CppCatchKeyword,
                                               CppCharKeyword,
                                               CppChar16Keyword,
                                               CppChar32Keyword,
                                               CppChar8Keyword,
                                               CppClassKeyword,
                                               CppCoAwaitKeyword,
                                               CppCoReturnKeyword,
                                               CppCoYieldKeyword,
                                               CppComplKeyword,
                                               CppConceptKeyword,
                                               CppConstKeyword,
                                               CppConstCastKeyword,
                                               CppConstevalKeyword,
                                               CppConstexprKeyword,
                                               CppConstinitKeyword,
                                               CppContinueKeyword,
                                               CppDecltypeKeyword,
                                               CppDefaultKeyword,
                                               CppDeleteKeyword,
                                               CppDoKeyword,
                                               CppDoubleKeyword,
                                               CppDynamicCastKeyword,
                                               CppElseKeyword,
                                               CppEnumKeyword,
                                               CppExplicitKeyword,
                                               CppExportKeyword,
                                               CppExternKeyword,
                                               CppFalseKeyword,
                                               CppFloatKeyword,
                                               CppForKeyword,
                                               CppFriendKeyword,
                                               CppGotoKeyword,
                                               CppIfKeyword,
                                               CppIntKeyword,
                                               CppLongKeyword,
                                               CppMutableKeyword,
                                               CppNamespaceKeyword,
                                               CppNewKeyword,
                                               CppNoexceptKeyword,
                                               CppNotKeyword,
                                               CppNotEqKeyword,
                                               CppNullptrKeyword,
                                               CppOperatorKeyword,
                                               CppOrKeyword,
                                               CppOrEqKeyword,
                                               CppPrivateKeyword,
                                               CppProtectedKeyword,
                                               CppPublicKeyword,
                                               CppRegisterKeyword,
                                               CppReinterpretCastKeyword,
                                               CppRequiresKeyword,
                                               CppReturnKeyword,
                                               CppShortKeyword,
                                               CppSignedKeyword,
                                               CppSizeofKeyword,
                                               CppStaticKeyword,
                                               CppStaticAssertKeyword,
                                               CppStaticCastKeyword,
                                               CppStructKeyword,
                                               CppSwitchKeyword,
                                               CppTemplateKeyword,
                                               CppThisKeyword,
                                               CppThreadLocalKeyword,
                                               CppThrowKeyword,
                                               CppTrueKeyword,
                                               CppTryKeyword,
                                               CppTypedefKeyword,
                                               CppTypeidKeyword,
                                               CppTypenameKeyword,
                                               CppUnionKeyword,
                                               CppUnsignedKeyword,
                                               CppUsingKeyword,
                                               CppVirtualKeyword,
                                               CppVoidKeyword,
                                               CppVolatileKeyword,
                                               CppWcharKeyword,
                                               CppWhileKeyword,
                                               CppXorKeyword,
                                               CppXorEqKeyword>;

auto is_cpp_identifier_token(const Token& token, ref<str>) -> bool {
    return token.kind == TokenKind::Identifier &&
           ! CppReservedIdentifierSet::contains(token.text.as_str());
}

} // namespace lito::frontend::lexical
