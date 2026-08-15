#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.frontend;
import lito.frontend.static_name;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;
using namespace lito::frontend::lexical;

struct CollidingFirstName {
    static constexpr auto name = "first"_str;
    static constexpr auto hash = uint64_t(17);
};

struct CollidingSecondName {
    static constexpr auto name = "second"_str;
    static constexpr auto hash = uint64_t(17);
};

TEST(CppReservedIdentifier, StaticTypes) {
    static_assert(cpp::CppConceptKeyword::name == "concept"_str);
    static_assert(cpp::CppReservedIdentifierSet::contains("co_await"_str));
    static_assert(cpp::CppReservedIdentifierSet::contains("xor_eq"_str));
    static_assert(! cpp::CppReservedIdentifierSet::contains("lito_identifier"_str));

    auto count   = usize {};
    auto visited = false;
    cpp::CppReservedIdentifierSet::for_each([&](auto) {
        ++count;
    });
    cpp::CppReservedIdentifierSet::visit("consteval"_str, [&](auto keyword) {
        using Keyword = typename decltype(keyword)::type;
        visited       = Keyword::name == cpp::CppConstevalKeyword::name;
    });
    EXPECT_EQ(count, usize(93));
    EXPECT_TRUE(visited);
}

TEST(CppReservedIdentifier, IdentifierToken) {
    auto keyword = Token {
        .kind = TokenKind::Identifier,
        .text = TokenText::borrowed(cpp::CppRequiresKeyword::name),
    };
    auto identifier = Token {
        .kind = TokenKind::Identifier,
        .text = TokenText::borrowed("lito_identifier"_str),
    };
    auto matcher = cpp::CppIdentifierTokenMatcher {};
    EXPECT_FALSE(matches_token(matcher, keyword));
    EXPECT_TRUE(matches_token(matcher, identifier));
    EXPECT_EQ(keyword.text.comparable_hash(), cpp::CppRequiresKeyword::hash);
}

TEST(TokenMatcher, GenericKindAndTypedText) {
    auto token = Token {
        .kind = TokenKind::Identifier,
        .text = TokenText::borrowed(cpp::CppConceptKeyword::name),
    };
    EXPECT_TRUE(matches_token(TokenKindMatcher { TokenKind::Identifier }, token));
    EXPECT_TRUE(matches_token(TokenTextMatcher<cpp::CppConceptKeyword> {}, token));
    EXPECT_FALSE(matches_token(TokenTextMatcher<cpp::CppRequiresKeyword> {}, token));
}

TEST(StaticNameSet, ConfirmsTextAfterHashMatch) {
    using CollidingNames = lito::frontend::StaticNameSet<CollidingFirstName, CollidingSecondName>;
    EXPECT_TRUE(CollidingNames::contains(uint64_t(17), "first"_str));
    EXPECT_TRUE(CollidingNames::contains(uint64_t(17), "second"_str));
    EXPECT_FALSE(CollidingNames::contains(uint64_t(17), "third"_str));
    auto visited = false;
    CollidingNames::visit(uint64_t(17), "second"_str, [&](auto name) {
        using Name = typename decltype(name)::type;
        visited    = rstd::mtp::same_as<Name, CollidingSecondName>;
    });
    EXPECT_TRUE(visited);
}
