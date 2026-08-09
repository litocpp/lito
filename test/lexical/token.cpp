#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;
using namespace lito::frontend::lexical;

TEST(CppReservedIdentifier, StaticTypes) {
    static_assert(CppConceptKeyword::name == "concept"_str);
    static_assert(CppReservedIdentifierSet::contains("co_await"_str));
    static_assert(CppReservedIdentifierSet::contains("xor_eq"_str));
    static_assert(! CppReservedIdentifierSet::contains("lito_identifier"_str));

    auto count   = usize {};
    auto visited = false;
    CppReservedIdentifierSet::for_each([&](auto) {
        ++count;
    });
    CppReservedIdentifierSet::visit("consteval"_str, [&](auto keyword) {
        using Keyword = typename decltype(keyword)::type;
        visited       = Keyword::name == CppConstevalKeyword::name;
    });
    EXPECT_EQ(count, usize(93));
    EXPECT_TRUE(visited);
}

TEST(CppReservedIdentifier, IdentifierToken) {
    auto keyword = Token {
        .kind = TokenKind::Identifier,
        .text = TokenText::borrowed(CppRequiresKeyword::name),
    };
    auto identifier = Token {
        .kind = TokenKind::Identifier,
        .text = TokenText::borrowed("lito_identifier"_str),
    };
    auto matcher = CppIdentifierTokenMatcher {};
    EXPECT_FALSE(matches_token(matcher, keyword));
    EXPECT_TRUE(matches_token(matcher, identifier));
    EXPECT_EQ(keyword.text.comparable_hash(), CppRequiresKeyword::hash);
}

TEST(TokenMatcher, GenericKindAndTypedText) {
    auto token = Token {
        .kind = TokenKind::Identifier,
        .text = TokenText::borrowed(CppConceptKeyword::name),
    };
    EXPECT_TRUE(matches_token(TokenKindMatcher { TokenKind::Identifier }, token));
    EXPECT_TRUE(matches_token(TokenTextMatcher<CppConceptKeyword> {}, token));
    EXPECT_FALSE(matches_token(TokenTextMatcher<CppRequiresKeyword> {}, token));
}
