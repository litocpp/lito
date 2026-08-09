#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
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
    EXPECT_FALSE(is_cpp_identifier_token(keyword, "c++20"_str));
    EXPECT_TRUE(is_cpp_identifier_token(identifier, "c++20"_str));
}
