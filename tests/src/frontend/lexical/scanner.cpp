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

TEST(Lexical, ByteSourcePreservesCommentsOffsetsAndStorage) {
    auto bytes = Vec<u8>::from("/* "_str.as_bytes());
    bytes.push(u8(0xA9));
    bytes.extend_from_slice(" */\r\nint value;\n/// "_str.as_bytes());
    bytes.push(u8(0xE2));
    bytes.extend_from_slice("\n"_str.as_bytes());
    auto directory = rstd::fs::TempDir::make("lito-byte-source"_str);
    ASSERT_TRUE(directory.is_ok());
    auto path = rstd::path::PathBuf::from(directory->path())
                    .join(rstd::path::PathBuf::from("source.cpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(path.as_path(), bytes.as_slice()).is_ok());
    auto domain = frontend::ScanMemoryDomain::make();
    auto loaded = SourceText::read(path.as_path(), domain.allocator());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->len(), bytes.len());
    auto source = SourceFile {
        .snapshot = make_source_snapshot(path.clone(), rstd::move(loaded).unwrap()),
    };
    auto full    = lex_with_comments(source);
    auto compact = lex_scan_file(source);
    ASSERT_TRUE(full.is_ok() && compact.is_ok());
    ASSERT_EQ(full->comments.len(), usize(2));
    EXPECT_EQ(full->comments[usize {}].text.display().as_str(), "/* \\xA9 */"_str);
    EXPECT_EQ(full->comments[usize(1)].text.display().as_str(), "/// \\xE2"_str);
    EXPECT_EQ(full->comments[usize {}].text.as_bytes()[usize(3)], u8(0xA9));
    EXPECT_TRUE(
        full->comments[usize {}].text.same_bytes(compact->comment(usize {}, usize {}).text));
    EXPECT_EQ(full->tokens.len(), compact->tokens.len());
    for (auto i = usize {}; i < full->tokens.len(); ++i) {
        auto token = compact->token(usize {}, i);
        EXPECT_TRUE(full->tokens[i].text.same_bytes(token.text()));
        EXPECT_EQ(full->tokens[i].spelling.offset, token.offset());
        EXPECT_EQ(full->tokens[i].spelling.line, token.location().line);
    }
    auto text  = TokenText::borrowed(bytes.as_slice());
    auto owned = TokenText(text.clone_bytes());
    auto moved = rstd::move(owned);
    EXPECT_TRUE(text.same_bytes(moved));
    EXPECT_TRUE(moved.utf8().is_err());
}

struct FixtureLanguageA;

TEST(Lexical, PreservesOrdinaryRawAndUnicodeLiteralBytes) {
    struct Case {
        ref<str> prefix;
        ref<str> suffix;
    };
    const Case cases[] = {
        { "\""_str, "\""_str },
        { "R\"("_str, ")\""_str },
        { "u8\""_str, "\""_str },
        { "'"_str, "'"_str },
    };
    for (const auto& item : cases) {
        auto bytes = Vec<u8>::from(item.prefix.as_bytes());
        bytes.push(u8(0xA9));
        bytes.extend_from_slice(item.suffix.as_bytes());
        auto source = SourceFile {
            .snapshot =
                make_source_snapshot(rstd::path::PathBuf::make(),
                                     SourceText::from_bytes(Vec<u8>::from(bytes.as_slice()))),
        };
        auto tokens = lex(source);
        ASSERT_TRUE(tokens.is_ok());
        ASSERT_EQ(tokens->len(), usize(1));
        EXPECT_TRUE((*tokens)[usize {}].text.same_bytes(TokenText::borrowed(bytes.as_slice())));
        EXPECT_TRUE((*tokens)[usize {}].text.utf8().is_err());
        EXPECT_EQ((*tokens)[usize {}].spelling.offset, usize {});
    }
}

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
    ASSERT_TRUE(invalid.is_err());
    auto invalid_error = rstd::move(invalid).unwrap_err();
    ASSERT_TRUE(invalid_error.location.is_some());
    EXPECT_EQ(invalid_error.location->offset, usize());
    EXPECT_EQ(invalid_error.location->column, usize(1));
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

TEST(LexicalFragment, ClassifiesCompletePreprocessingTokens) {
    auto origin = SourceLocation {
        .source = usize(9), .offset = usize(17), .line = usize(3), .column = usize(5)
    };
    auto identifier = classify_preprocessing_token(String::make("name\\u0030"_str), origin);
    ASSERT_TRUE(identifier.is_ok());
    EXPECT_TRUE(*identifier == TokenKind::Identifier);
    auto unicode = classify_preprocessing_token(String::make("变量"_str), origin);
    ASSERT_TRUE(unicode.is_ok());
    EXPECT_TRUE(*unicode == TokenKind::Identifier);
    auto number = classify_preprocessing_token(String::make("1.0e+4f"_str), origin);
    ASSERT_TRUE(number.is_ok());
    EXPECT_TRUE(*number == TokenKind::PpNumber);
    auto literal = classify_preprocessing_token(String::make("u8\"text\""_str), origin);
    ASSERT_TRUE(literal.is_ok());
    EXPECT_TRUE(*literal == TokenKind::StringLiteral);
    auto punctuation = classify_preprocessing_token(String::make(">>="_str), origin);
    ASSERT_TRUE(punctuation.is_ok());
    EXPECT_TRUE(*punctuation == TokenKind::Punctuation);

    auto multiple = classify_preprocessing_token(String::make("first second"_str), origin);
    ASSERT_TRUE(multiple.is_err());
    ASSERT_TRUE(multiple.unwrap_err().location.is_some());
    EXPECT_EQ(multiple.unwrap_err().location->source, origin.source);
    auto invalid = classify_preprocessing_token(String::make("\"unterminated"_str), origin);
    ASSERT_TRUE(invalid.is_err());
    ASSERT_TRUE(invalid.unwrap_err().location.is_some());
    EXPECT_EQ(invalid.unwrap_err().location->line, origin.line);
}

TEST(LexicalFragment, ValidatesIdentifiersAndPreservesOrigin) {
    EXPECT_TRUE(is_identifier_spelling("name_17"_str));
    EXPECT_TRUE(is_identifier_spelling("$extension"_str));
    EXPECT_TRUE(is_identifier_spelling("\\u0061lpha"_str));
    EXPECT_TRUE(is_identifier_spelling("变量"_str));
    EXPECT_FALSE(is_identifier_spelling("name+suffix"_str));
    EXPECT_FALSE(is_identifier_spelling("17name"_str));

    auto origin = SourceLocation {
        .source = usize(4), .offset = usize(28), .line = usize(7), .column = usize(2)
    };
    auto fragment = lex_preprocessing_fragment(String::make("push_macro(\"NAME\")"_str), origin);
    ASSERT_TRUE(fragment.is_ok());
    ASSERT_EQ(fragment->len(), usize(4));
    for (const auto& token : *fragment) {
        EXPECT_EQ(token.spelling.source, origin.source);
        EXPECT_EQ(token.spelling.offset, origin.offset);
        EXPECT_EQ(token.expansion.line, origin.line);
        EXPECT_EQ(token.expansion.column, origin.column);
    }
}

TEST(LexicalSourceStorage, PreservesTokensAcrossBlocks) {
    auto contents = String::make();
    for (auto line = usize {}; line < usize(300); ++line) contents.push_str("name\n"_str);
    auto source  = fixture_source(contents.as_str());
    auto storage = lex_scan_file(source);
    ASSERT_TRUE(storage.is_ok());
    ASSERT_EQ(storage->tokens.len(), usize(600));

    auto token = storage->token(usize(11), usize(256));
    EXPECT_EQ(token.text(), "name"_str);
    EXPECT_EQ(token.location().source, usize(11));
    EXPECT_EQ(token.location().line, usize(129));
    EXPECT_EQ(token.location().column, usize(1));
    EXPECT_TRUE(token.start_of_line());

    auto statistics = storage->statistics();
    EXPECT_EQ(statistics.source_bytes, contents.len());
    EXPECT_EQ(statistics.token_count, usize(600));
    EXPECT_EQ(statistics.token_bytes, usize(600 * sizeof(CompactSourceToken)));
    EXPECT_GE(statistics.arena_used_bytes, statistics.token_bytes);
    EXPECT_GE(statistics.arena_reserved_bytes, statistics.arena_used_bytes);
    EXPECT_GE(statistics.retained_bytes, statistics.source_reserved_bytes);
}

TEST(LexicalScanner, MarkedEndAndFailuresRestoreCarriageReturnState) {
    struct Scanner {
        int  mode {};
        auto scan(ScannerCursor& cursor, ValidSymbols) -> LexicalResult<Option<SymbolId>> {
            cursor.advance();
            cursor.mark_end();
            if (mode == 0) {
                cursor.advance();
                return Ok(Some<SymbolId>(FIXTURE_A_SHORT));
            }
            cursor.advance(true);
            if (mode == 1) return Ok(None());
            return Err(Error::at(String::make("fixture failure"_str), cursor.location()));
        }
    };
    auto session = ScannerSession::make<FixtureLanguageA>(fixture_source("\r\na"_str));
    auto scanner = Scanner {};
    auto valid   = array<SymbolId, 1> { FIXTURE_A_SHORT };
    ASSERT_TRUE(session.scan(scanner, valid.as_slice()).is_ok());
    EXPECT_EQ(session.position().offset, usize(1));
    EXPECT_EQ(session.position().line, usize(2));
    const int modes[] { 1, 2 };
    for (int mode : modes) {
        scanner.mode = mode;
        auto result  = session.scan(scanner, valid.as_slice());
        EXPECT_EQ(result.is_err(), mode == 2);
        EXPECT_EQ(session.position().offset, usize(1));
        EXPECT_EQ(session.position().line, usize(2));
        EXPECT_EQ(session.position().column, usize(1));
    }
    scanner.mode = 0;
    ASSERT_TRUE(session.scan(scanner, valid.as_slice()).is_ok());
    EXPECT_EQ(session.position().offset, usize(2));
    EXPECT_EQ(session.position().line, usize(2));
    EXPECT_EQ(session.position().column, usize(1));
}

TEST(LexicalScanner, CallbacksObserveOnlyCommittedSessionPosition) {
    auto session = ScannerSession::make<FixtureLanguageA>(fixture_source("a"_str));
    struct Scanner {
        ScannerSession* session;
        bool            stayed_committed {};
        auto scan(ScannerCursor& cursor, ValidSymbols) -> LexicalResult<Option<SymbolId>> {
            cursor.advance();
            stayed_committed = session->position().offset == usize() && ! session->is_eof();
            return Ok(Some<SymbolId>(FIXTURE_A_SHORT));
        }
    };
    auto scanner = Scanner { &session };
    auto valid   = array<SymbolId, 1> { FIXTURE_A_SHORT };
    auto result  = session.scan(scanner, valid.as_slice());
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(scanner.stayed_committed);
    EXPECT_EQ(session.position().offset, usize(1));
    EXPECT_TRUE(session.is_eof());
}

TEST(Lexical, FullAndCompactSinksAgreeOnPhysicalLocations) {
    auto source  = fixture_source("// comment\r\n/* multi\rline\ncomment */\r"
                                  "name\\u0061 12e+3 >>= u8\"text\" R\"tag(raw\r\nline)tag\"\n"
                                  "\\\r\nnext \"escaped\\\nline\" end\n"_str);
    auto full    = lex_with_comments(source, true);
    auto compact = lex_scan_file(source);
    ASSERT_TRUE(full.is_ok());
    ASSERT_TRUE(compact.is_ok());
    ASSERT_EQ(full->tokens.len(), compact->tokens.len());
    SourcePositionIndex positions(source.contents());
    for (auto index = usize(); index < full->tokens.len(); ++index) {
        auto& token  = full->tokens[index];
        auto  stored = compact->token(source.id, index);
        EXPECT_EQ(token.kind, stored.kind());
        EXPECT_TRUE(token.text.same_bytes(stored.text()));
        EXPECT_EQ(token.spelling.offset, stored.location().offset);
        EXPECT_EQ(token.spelling.line, stored.location().line);
        EXPECT_EQ(token.spelling.column, stored.location().column);
        EXPECT_EQ(token.start_of_line, stored.start_of_line());
        EXPECT_EQ(token.leading_space, stored.leading_space());
    }
    for (const auto& comment : full->comments) {
        auto end =
            positions.location(source.id, static_cast<uint32_t>(comment.end.offset.to_primitive()));
        EXPECT_EQ(comment.end.line, end.line);
        EXPECT_EQ(comment.end.column, end.column);
    }
}
