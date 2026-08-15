#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;
using namespace lito::frontend::lexical;

template<typename T>
using LexicalResult = lito::frontend::lexical::Result<T>;

struct FixtureLanguageA;
struct FixtureLanguageB;

inline constexpr auto FIXTURE_A_SHORT = SymbolId::of<FixtureLanguageA>(u32(0));
inline constexpr auto FIXTURE_A_LONG  = SymbolId::of<FixtureLanguageA>(u32(1));
inline constexpr auto FIXTURE_A_WRONG = SymbolId::of<FixtureLanguageA>(u32(2));
inline constexpr auto FIXTURE_B_SHORT = SymbolId::of<FixtureLanguageB>(u32(0));

struct FixtureLanguageA {
    auto identity() const noexcept -> LanguageIdentity {
        return LanguageIdentity { .name = "fixture-a"_str, .revision = "v1"_str };
    }

    auto symbol_info(SymbolId symbol) const noexcept -> Option<SymbolInfo> {
        if (! symbol.belongs_to<FixtureLanguageA>()) return None();
        if (symbol == FIXTURE_A_SHORT) {
            return Some(SymbolInfo { .id = symbol, .name = "short"_str, .named = true });
        }
        if (symbol == FIXTURE_A_LONG) {
            return Some(SymbolInfo { .id = symbol, .name = "long"_str, .named = true });
        }
        if (symbol == FIXTURE_A_WRONG) {
            return Some(SymbolInfo { .id = symbol, .name = "wrong"_str, .named = true });
        }
        return None();
    }
};

struct FixtureLanguageB {
    auto identity() const noexcept -> LanguageIdentity {
        return LanguageIdentity { .name = "fixture-b"_str, .revision = "v1"_str };
    }

    auto symbol_info(SymbolId symbol) const noexcept -> Option<SymbolInfo> {
        if (symbol != FIXTURE_B_SHORT) return None();
        return Some(SymbolInfo { .id = symbol, .name = "short"_str, .named = true });
    }
};

struct FixtureScanner {
    auto scan(ScannerCursor& cursor, ValidSymbols valid_symbols)
        -> LexicalResult<Option<SymbolId>> {
        while (auto lookahead = cursor.lookahead()) {
            if (*lookahead != u8(' ') && *lookahead != u8('\r') && *lookahead != u8('\n')) break;
            cursor.advance(true);
        }
        auto first = cursor.lookahead();
        if (first.is_none() || *first != u8('a')) return Ok(None());
        cursor.advance();
        cursor.mark_end();
        if (valid_symbols.contains(FIXTURE_A_LONG)) {
            auto second = cursor.lookahead();
            if (second.is_some() && *second == u8('b')) {
                cursor.advance();
                cursor.mark_end();
                if (cursor.lookahead().is_some()) cursor.advance();
                return Ok(Some<SymbolId>(FIXTURE_A_LONG));
            }
        }
        if (valid_symbols.contains(FIXTURE_A_SHORT)) return Ok(Some<SymbolId>(FIXTURE_A_SHORT));
        return Ok(None());
    }
};

struct ConsumingFailureScanner {
    auto scan(ScannerCursor& cursor, ValidSymbols) -> LexicalResult<Option<SymbolId>> {
        if (! cursor.is_eof()) cursor.advance();
        return Ok(None());
    }
};

struct InvalidSymbolScanner {
    SymbolId result { FIXTURE_A_WRONG };

    auto scan(ScannerCursor& cursor, ValidSymbols) -> LexicalResult<Option<SymbolId>> {
        if (! cursor.is_eof()) cursor.advance();
        return Ok(Some(result));
    }
};

struct EmptyScanner {
    auto scan(ScannerCursor&, ValidSymbols) -> LexicalResult<Option<SymbolId>> {
        return Ok(Some<SymbolId>(FIXTURE_A_SHORT));
    }
};

static_assert(Impled<FixtureLanguageA, Language>);
static_assert(Impled<FixtureLanguageB, Language>);
static_assert(Impled<FixtureScanner, Scanner>);

auto fixture_source(ref<str> contents) -> SourceFile {
    return SourceFile::make(usize(7),
                            SourceBuffer {
                                .path     = rstd::path::PathBuf::from("/fixture.lex"_str),
                                .contents = String::make(contents),
                            });
}

TEST(LexicalLanguage, KeepsLocalSymbolsScoped) {
    auto first  = FixtureLanguageA {};
    auto second = FixtureLanguageB {};
    EXPECT_TRUE(FIXTURE_A_SHORT != FIXTURE_B_SHORT);
    EXPECT_EQ(language_identity(first).name, "fixture-a"_str);
    EXPECT_EQ(language_identity(second).name, "fixture-b"_str);
    EXPECT_TRUE(language_symbol_info(first, FIXTURE_A_SHORT).is_some());
    EXPECT_TRUE(language_symbol_info(first, FIXTURE_B_SHORT).is_none());
    EXPECT_TRUE(language_symbol_info(second, FIXTURE_B_SHORT).is_some());
}

TEST(LexicalScanner, UsesValidSymbolsAndMarkedEnd) {
    auto session = ScannerSession::make<FixtureLanguageA>(fixture_source(" \r\nabx"_str));
    auto scanner = FixtureScanner {};
    auto valid   = array<SymbolId, 1> { FIXTURE_A_LONG };
    auto token   = session.scan(scanner, valid.as_slice());
    ASSERT_TRUE(token.is_ok());
    ASSERT_TRUE(token->is_some());
    EXPECT_TRUE((**token).symbol == FIXTURE_A_LONG);
    EXPECT_EQ((**token).begin.offset, usize(3));
    EXPECT_EQ((**token).begin.line, usize(2));
    EXPECT_EQ((**token).begin.column, usize(1));
    EXPECT_EQ((**token).end.offset, usize(5));
    EXPECT_EQ((**token).end.line, usize(2));
    EXPECT_EQ((**token).end.column, usize(3));
    EXPECT_EQ(session.position().offset, usize(5));

    auto short_session = ScannerSession::make<FixtureLanguageA>(fixture_source("ab"_str));
    auto short_valid   = array<SymbolId, 1> { FIXTURE_A_SHORT };
    auto short_token   = short_session.scan(scanner, short_valid.as_slice());
    ASSERT_TRUE(short_token.is_ok());
    ASSERT_TRUE(short_token->is_some());
    EXPECT_TRUE((**short_token).symbol == FIXTURE_A_SHORT);
    EXPECT_EQ((**short_token).end.offset, usize(1));
}

TEST(LexicalScanner, RollsBackAndRejectsInvalidResults) {
    auto valid = array<SymbolId, 1> { FIXTURE_A_SHORT };

    auto failed_session = ScannerSession::make<FixtureLanguageA>(fixture_source("abc"_str));
    auto failed_scanner = ConsumingFailureScanner {};
    auto failed         = failed_session.scan(failed_scanner, valid.as_slice());
    ASSERT_TRUE(failed.is_ok());
    EXPECT_TRUE(failed->is_none());
    EXPECT_EQ(failed_session.position().offset, usize {});

    auto invalid_session = ScannerSession::make<FixtureLanguageA>(fixture_source("abc"_str));
    auto invalid_scanner = InvalidSymbolScanner {};
    auto invalid         = invalid_session.scan(invalid_scanner, valid.as_slice());
    EXPECT_TRUE(invalid.is_err());
    EXPECT_EQ(invalid_session.position().offset, usize {});

    auto foreign_session   = ScannerSession::make<FixtureLanguageA>(fixture_source("abc"_str));
    invalid_scanner.result = FIXTURE_B_SHORT;
    auto foreign           = foreign_session.scan(invalid_scanner, valid.as_slice());
    EXPECT_TRUE(foreign.is_err());
    EXPECT_EQ(foreign_session.position().offset, usize {});

    auto wrong_valid = array<SymbolId, 1> { FIXTURE_B_SHORT };
    auto mixed       = failed_session.scan(failed_scanner, wrong_valid.as_slice());
    EXPECT_TRUE(mixed.is_err());

    auto empty_session = ScannerSession::make<FixtureLanguageA>(fixture_source("abc"_str));
    auto empty_scanner = EmptyScanner {};
    auto empty         = empty_session.scan(empty_scanner, valid.as_slice());
    EXPECT_TRUE(empty.is_err());
    EXPECT_EQ(empty_session.position().offset, usize {});
}

TEST(LexicalScanner, UsesByteOffsetsForUtf8) {
    struct Utf8Scanner {
        auto scan(ScannerCursor& cursor, ValidSymbols) -> LexicalResult<Option<SymbolId>> {
            cursor.advance();
            cursor.advance();
            cursor.mark_end();
            return Ok(Some<SymbolId>(FIXTURE_A_SHORT));
        }
    };

    static_assert(Impled<Utf8Scanner, Scanner>);
    auto session = ScannerSession::make<FixtureLanguageA>(fixture_source("é"_str));
    auto scanner = Utf8Scanner {};
    auto valid   = array<SymbolId, 1> { FIXTURE_A_SHORT };
    auto token   = session.scan(scanner, valid.as_slice());
    ASSERT_TRUE(token.is_ok());
    ASSERT_TRUE(token->is_some());
    EXPECT_EQ((**token).begin.offset, usize {});
    EXPECT_EQ((**token).end.offset, usize(2));
    EXPECT_EQ((**token).end.column, usize(3));
    EXPECT_TRUE(session.is_eof());
}

TEST(CppWordScanner, OwnsCppWordClassification) {
    auto session = ScannerSession::make<cpp::CppLexicalLanguage>(
        fixture_source("module import export name class"_str));
    auto scanner = cpp::CppWordScanner {};

    auto identifier = array<SymbolId, 1> { cpp::CPP_IDENTIFIER_SYMBOL };
    auto module     = session.scan(scanner, identifier.as_slice());
    ASSERT_TRUE(module.is_ok());
    ASSERT_TRUE(module->is_some());
    EXPECT_TRUE((**module).symbol == cpp::CPP_IDENTIFIER_SYMBOL);

    auto import_symbols = array<SymbolId, 2> { cpp::CPP_IMPORT_SYMBOL, cpp::CPP_IDENTIFIER_SYMBOL };
    auto import_token   = session.scan(scanner, import_symbols.as_slice());
    ASSERT_TRUE(import_token.is_ok());
    ASSERT_TRUE(import_token->is_some());
    EXPECT_TRUE((**import_token).symbol == cpp::CPP_IMPORT_SYMBOL);

    auto export_symbols = array<SymbolId, 1> { cpp::CPP_EXPORT_SYMBOL };
    auto export_token   = session.scan(scanner, export_symbols.as_slice());
    ASSERT_TRUE(export_token.is_ok());
    ASSERT_TRUE(export_token->is_some());
    EXPECT_TRUE((**export_token).symbol == cpp::CPP_EXPORT_SYMBOL);

    auto name = session.scan(scanner, identifier.as_slice());
    ASSERT_TRUE(name.is_ok());
    ASSERT_TRUE(name->is_some());
    EXPECT_TRUE((**name).symbol == cpp::CPP_IDENTIFIER_SYMBOL);

    auto keyword = session.scan(scanner, identifier.as_slice());
    ASSERT_TRUE(keyword.is_ok());
    EXPECT_TRUE(keyword->is_none());
}
