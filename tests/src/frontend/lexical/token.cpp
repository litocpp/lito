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
    static_assert(cpp::CppConceptKeyword::hash == uint64_t(0x62e792f6d2691013ull));
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

TEST(TokenMatcher, PreservesPrecomputedHashWhenSharingBorrowedText) {
    auto text   = TokenText::borrowed("concept"_str, uint64_t(17));
    auto shared = text.shared_clone();
    EXPECT_EQ(text.comparable_hash(), uint64_t(17));
    EXPECT_EQ(shared.comparable_hash(), uint64_t(17));
    EXPECT_EQ(shared.as_bytes().as_ptr(), text.as_bytes().as_ptr());
}

struct DirectiveTestReader {
    slice<Token>           tokens;
    const ScanFileStorage* compact {};

    auto kind(usize index) const -> TokenKind {
        return compact ? compact->token(usize {}, index).kind() : tokens[index].kind;
    }
    auto text(usize index) const -> TokenText {
        return compact ? compact->token(usize {}, index).text() : tokens[index].text.shared_clone();
    }
    auto start_of_line(usize index) const -> bool {
        return compact ? compact->token(usize {}, index).start_of_line()
                       : tokens[index].start_of_line;
    }
    auto comment(usize) const -> bool { return false; }
};

TEST(LexicalDirective, FullAndCompactReadersAgreeOnLogicalHeads) {
    struct Case {
        ref<str>      first;
        ref<str>      continuation;
        DirectiveKind kind;
        ref<str>      name;
        usize         begin {};
        bool          marker { true };
        bool          digraph {};
    };
    const Case cases[] = {
        { "#"_str, ""_str, DirectiveKind::Empty, ""_str },
        { "# 123"_str, ""_str, DirectiveKind::InvalidKeyword, ""_str },
        { "# unknown"_str, ""_str, DirectiveKind::Unknown, ""_str },
        { "#define"_str, ""_str, DirectiveKind::Define, ""_str },
        { "#define 42"_str, ""_str, DirectiveKind::Define, "42"_str },
        { "#define NAME 1"_str, ""_str, DirectiveKind::Define, "NAME"_str },
        { "/* lead */ # /* keyword */ define /* name */ NAME \\"_str,
          " 1"_str,
          DirectiveKind::Define,
          "NAME"_str },
        { "%:define NAME 1"_str, ""_str, DirectiveKind::Define, "NAME"_str, {}, true, true },
        { "prefix #define NAME"_str, ""_str, DirectiveKind::Define, "NAME"_str, usize(1), false },
    };
    const ref<str>              endings[]  = { "\n"_str, "\r\n"_str, "\r"_str };
    const DirectiveMarkerPolicy policies[] = {
        DirectiveMarkerPolicy::HashOnly,
        DirectiveMarkerPolicy::HashAndDigraph,
    };
    for (auto ending : endings) {
        for (const auto& test : cases) {
            auto source =
                SourceFile::make(usize {},
                                 SourceBuffer {
                                     .path     = rstd::path::PathBuf::from("directive.cpp"_str),
                                     .contents = rstd::format(
                                         "{}{}{}{}", test.first, ending, test.continuation, ending),
                                 });
            auto full    = lex_with_comments(source, true);
            auto compact = lex_scan_file(source);
            ASSERT_TRUE(full.is_ok() && compact.is_ok());
            ASSERT_EQ(full->tokens.len(), compact->tokens.len());
            auto full_reader    = DirectiveTestReader { .tokens = full->tokens.as_slice() };
            auto compact_reader = DirectiveTestReader { .compact = rstd::addressof(*compact) };
            auto end = logical_token_line_end(full_reader, test.begin, full->tokens.len());
            EXPECT_EQ(end,
                      logical_token_line_end(compact_reader, test.begin, compact->tokens.len()));
            if (! test.continuation.is_empty())
                EXPECT_TRUE(full_reader.text(end - usize(1)) == "1"_str);
            for (auto policy : policies) {
                auto a       = recognize_directive_head(full_reader, test.begin, end, policy);
                auto b       = recognize_directive_head(compact_reader, test.begin, end, policy);
                auto present = test.marker &&
                               (! test.digraph || policy == DirectiveMarkerPolicy::HashAndDigraph);
                ASSERT_EQ(a.is_some(), present) << test.first;
                ASSERT_EQ(b.is_some(), present) << test.first;
                if (! present) continue;
                EXPECT_EQ(a->kind, test.kind);
                EXPECT_EQ(a->marker_index, test.begin);
                EXPECT_TRUE(a->kind == b->kind && a->marker_index == b->marker_index &&
                            a->keyword_index == b->keyword_index &&
                            a->arguments_begin == b->arguments_begin &&
                            a->name_index == b->name_index);
                EXPECT_EQ(a->keyword_index == usize::MAX, test.kind == DirectiveKind::Empty);
                if (test.name.is_empty()) {
                    EXPECT_EQ(a->arguments_begin, end);
                    EXPECT_EQ(a->name_index, usize::MAX);
                } else {
                    ASSERT_TRUE(a->name_index < end);
                    EXPECT_EQ(a->name_index, a->arguments_begin);
                    EXPECT_TRUE(full_reader.text(a->name_index) == test.name);
                }
            }
        }
    }
}
