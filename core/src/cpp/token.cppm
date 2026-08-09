export module lito.cpp:token;

import rstd;
import lito.frontend.lexical;
import lito.frontend.static_name;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

namespace lexical = frontend::lexical;

using frontend::StaticName;
using frontend::StaticNameSet;

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

struct CppIdentifierTokenMatcher {
    auto matches(const lexical::Token& token) const noexcept -> bool {
        return token.kind == lexical::TokenKind::Identifier &&
               ! CppReservedIdentifierSet::contains(token.text.comparable_hash(),
                                                    token.text.as_str());
    }
};

static_assert(rstd::Impled<CppIdentifierTokenMatcher, lexical::TokenMatcher>);

} // namespace lito
