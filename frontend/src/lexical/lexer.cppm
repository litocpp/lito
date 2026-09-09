export module lito.frontend.lexical:lexer;

import rstd;
import rstd.parse.core;
import lito.frontend.memory;
import :token;
import :source;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::parse::PositionedCursor;

namespace lito::frontend::lexical
{

auto lex_failure(ref<str> message, SourceLocation location) {
    return Err(Error::at(String::make(message), location));
}

auto append_token(Vec<Token>&    tokens,
                  TokenKind      kind,
                  TokenText      text,
                  SourceLocation location,
                  bool           start_of_line,
                  bool           leading_space) -> void {
    tokens.push(Token {
        .kind          = kind,
        .text          = rstd::move(text),
        .spelling      = location,
        .expansion     = location,
        .start_of_line = start_of_line,
        .leading_space = leading_space,
    });
}

auto punctuation_length(slice<u8> bytes, usize index) -> usize {
    if (index + usize(2) < bytes.len()) {
        auto a = bytes[index];
        auto b = bytes[index + usize(1)];
        auto c = bytes[index + usize(2)];
        if ((a == u8('<') && b == u8('<') && c == u8('=')) ||
            (a == u8('>') && b == u8('>') && c == u8('=')) ||
            (a == u8('.') && b == u8('.') && c == u8('.')) ||
            (a == u8('-') && b == u8('>') && c == u8('*'))) {
            return usize(3);
        }
    }
    if (index + usize(1) < bytes.len()) {
        auto a = bytes[index];
        auto b = bytes[index + usize(1)];
        if ((a == u8('#') && b == u8('#')) || (a == u8(':') && b == u8(':')) ||
            (a == u8('-') && b == u8('>')) || (a == u8('.') && b == u8('*')) ||
            (a == u8('+') && b == u8('+')) || (a == u8('-') && b == u8('-')) ||
            (a == u8('<') && b == u8('<')) || (a == u8('>') && b == u8('>')) ||
            (a == u8('<') && b == u8('=')) || (a == u8('>') && b == u8('=')) ||
            (a == u8('=') && b == u8('=')) || (a == u8('!') && b == u8('=')) ||
            (a == u8('&') && b == u8('&')) || (a == u8('|') && b == u8('|')) ||
            (a == u8('*') && b == u8('=')) || (a == u8('/') && b == u8('=')) ||
            (a == u8('%') && b == u8('=')) || (a == u8('+') && b == u8('=')) ||
            (a == u8('-') && b == u8('=')) || (a == u8('&') && b == u8('=')) ||
            (a == u8('^') && b == u8('=')) || (a == u8('|') && b == u8('=')) ||
            (a == u8('<') && b == u8(':')) || (a == u8(':') && b == u8('>')) ||
            (a == u8('<') && b == u8('%')) || (a == u8('%') && b == u8('>')) ||
            (a == u8('%') && b == u8(':'))) {
            return usize(2);
        }
    }
    return usize(1);
}

auto literal_prefix_length(slice<u8> bytes, usize index) -> usize {
    if (index + usize(2) < bytes.len() && bytes[index] == u8('u') &&
        bytes[index + usize(1)] == u8('8') &&
        (bytes[index + usize(2)] == u8('"') || bytes[index + usize(2)] == u8('\''))) {
        return usize(2);
    }
    if (index + usize(1) < bytes.len() &&
        (bytes[index] == u8('u') || bytes[index] == u8('U') || bytes[index] == u8('L')) &&
        (bytes[index + usize(1)] == u8('"') || bytes[index + usize(1)] == u8('\''))) {
        return usize(1);
    }
    return usize {};
}

auto raw_prefix_length(slice<u8> bytes, usize index) -> usize {
    if (index + usize(3) < bytes.len() && bytes[index] == u8('u') &&
        bytes[index + usize(1)] == u8('8') && bytes[index + usize(2)] == u8('R') &&
        bytes[index + usize(3)] == u8('"')) {
        return usize(3);
    }
    if (index + usize(2) < bytes.len() &&
        (bytes[index] == u8('u') || bytes[index] == u8('U') || bytes[index] == u8('L')) &&
        bytes[index + usize(1)] == u8('R') && bytes[index + usize(2)] == u8('"')) {
        return usize(2);
    }
    if (index + usize(1) < bytes.len() && bytes[index] == u8('R') &&
        bytes[index + usize(1)] == u8('"')) {
        return usize(1);
    }
    return usize {};
}

auto hex_digit(u8 value) noexcept -> bool {
    return (value >= u8('0') && value <= u8('9')) || (value >= u8('a') && value <= u8('f')) ||
           (value >= u8('A') && value <= u8('F'));
}

auto ucn_length(slice<u8> bytes, usize index) -> usize {
    if (index + usize(1) >= bytes.len() || bytes[index] != u8('\\') ||
        (bytes[index + usize(1)] != u8('u') && bytes[index + usize(1)] != u8('U'))) {
        return usize {};
    }
    auto length = bytes[index + usize(1)] == u8('u') ? usize(6) : usize(10);
    if (index + length > bytes.len()) return usize {};
    auto cursor = rstd::parse::TextCursor(rstd::parse::Input<u8>(slice<u8>::from_raw_parts(
        bytes.as_raw_ptr() + (index + usize(2)).to_primitive(), length - usize(2))));
    return rstd::parse::consume_n(cursor, length - usize(2), hex_digit).is_some() ? length
                                                                                  : usize();
}

auto identifier_length(slice<u8> bytes) -> usize {
    auto cursor = rstd::parse::TextCursor(rstd::parse::Input<u8>(bytes));
    if (cursor.is_eof()) return usize();
    auto escaped = ucn_length(bytes, cursor.position());
    if (escaped != usize()) {
        (void)cursor.advance(escaped);
    } else if (rstd::parse::consume_if(cursor, is_identifier_start).is_none()) {
        return usize();
    }
    while (! cursor.is_eof()) {
        escaped = ucn_length(bytes, cursor.position());
        if (escaped != usize()) {
            (void)cursor.advance(escaped);
        } else if (rstd::parse::consume_if(cursor, is_identifier_continue).is_none()) {
            break;
        }
    }
    return cursor.position();
}

struct ScannedPreprocessingToken {
    TokenKind kind;
    usize     length;
};

auto scan_preprocessing_token(slice<u8> bytes, SourceLocation location)
    -> Result<ScannedPreprocessingToken> {
    auto cursor     = rstd::parse::TextCursor(rstd::parse::Input<u8>(bytes));
    auto start      = cursor.position();
    auto kind       = TokenKind::Punctuation;
    auto raw_prefix = raw_prefix_length(bytes, cursor.position());
    auto prefix     = literal_prefix_length(bytes, cursor.position());
    if (raw_prefix != usize {}) {
        kind = TokenKind::StringLiteral;
        (void)cursor.advance(raw_prefix + usize(1));
        auto delimiter_begin = cursor.position();
        while (cursor.position() < bytes.len() && bytes[cursor.position()] != u8('(') &&
               bytes[cursor.position()] != u8('\n') && bytes[cursor.position()] != u8('\r') &&
               cursor.position() - delimiter_begin <= usize(16)) {
            (void)cursor.advance(usize(1));
        }
        if (cursor.position() >= bytes.len() || bytes[cursor.position()] != u8('(') ||
            cursor.position() - delimiter_begin > usize(16)) {
            return lex_failure("invalid raw string delimiter"_str, location);
        }
        auto delimiter_end = cursor.position();
        (void)cursor.advance(usize(1));
        auto closed = false;
        while (cursor.position() < bytes.len()) {
            if (bytes[cursor.position()] == u8(')')) {
                auto matches = true;
                auto length  = delimiter_end - delimiter_begin;
                for (auto offset = usize {}; offset < length; ++offset) {
                    if (cursor.position() + usize(1) + offset >= bytes.len() ||
                        bytes[cursor.position() + usize(1) + offset] !=
                            bytes[delimiter_begin + offset]) {
                        matches = false;
                        break;
                    }
                }
                auto quote = cursor.position() + usize(1) + length;
                if (matches && quote < bytes.len() && bytes[quote] == u8('"')) {
                    (void)cursor.advance(quote + usize(1) - cursor.position());
                    closed = true;
                    break;
                }
            }
            (void)cursor.advance(usize(1));
        }
        if (! closed) return lex_failure("unterminated raw string literal"_str, location);
    } else if (bytes[start] == u8('"') || bytes[start] == u8('\'') || prefix != usize {}) {
        (void)cursor.advance(prefix);
        auto quote = bytes[cursor.position()];
        kind       = quote == u8('"') ? TokenKind::StringLiteral : TokenKind::CharacterLiteral;
        (void)cursor.advance(usize(1));
        auto closed = false;
        while (cursor.position() < bytes.len()) {
            if (bytes[cursor.position()] == u8('\\') &&
                cursor.position() + usize(1) < bytes.len()) {
                (void)cursor.advance(usize(2));
                continue;
            }
            if (bytes[cursor.position()] == quote) {
                (void)cursor.advance(usize(1));
                closed = true;
                break;
            }
            if (bytes[cursor.position()] == u8('\n') || bytes[cursor.position()] == u8('\r')) break;
            (void)cursor.advance(usize(1));
        }
        if (! closed) return lex_failure("unterminated literal"_str, location);
    } else if (auto length = identifier_length(cursor.remaining_input()); length != usize()) {
        kind = TokenKind::Identifier;
        (void)cursor.advance(length);
    } else if ((bytes[start] >= u8('0') && bytes[start] <= u8('9')) ||
               (bytes[start] == u8('.') && start + usize(1) < bytes.len() &&
                bytes[start + usize(1)] >= u8('0') && bytes[start + usize(1)] <= u8('9'))) {
        kind = TokenKind::PpNumber;
        while (cursor.position() < bytes.len()) {
            auto current = bytes[cursor.position()];
            if (is_identifier_continue(current) || current == u8('.') || current == u8('\'')) {
                (void)cursor.advance(usize(1));
                continue;
            }
            if ((current == u8('+') || current == u8('-')) && cursor.position() != start &&
                (bytes[cursor.position() - usize(1)] == u8('e') ||
                 bytes[cursor.position() - usize(1)] == u8('E') ||
                 bytes[cursor.position() - usize(1)] == u8('p') ||
                 bytes[cursor.position() - usize(1)] == u8('P'))) {
                (void)cursor.advance(usize(1));
                continue;
            }
            break;
        }
    } else {
        auto length = punctuation_length(bytes, cursor.position());
        (void)cursor.advance(length);
    }
    return Ok(ScannedPreprocessingToken { kind, cursor.position() });
}

} // namespace lito::frontend::lexical

export namespace lito::frontend::lexical
{

class LexedFileSink {
public:
    explicit LexedFileSink(bool borrow_spelling): borrow_spelling_(borrow_spelling) {}

    void push_token(const SourceFile& source,
                    TokenKind         kind,
                    usize             offset,
                    usize             length,
                    SourceLocation    location,
                    bool              start_of_line,
                    bool              leading_space) {
        auto text =
            slice<u8>::from_raw_parts(source.contents().as_ptr() + offset.to_primitive(), length);
        append_token(tokens_,
                     kind,
                     borrow_spelling_ ? TokenText::borrowed(text)
                                      : TokenText { Vec<u8>::from(text) },
                     location,
                     start_of_line,
                     leading_space);
    }

    void push_comment(const SourceFile& source,
                      CommentKind       kind,
                      CommentStyle      style,
                      usize             begin,
                      usize             end,
                      SourceLocation    begin_location,
                      SourceLocation    end_location,
                      bool              start_of_line) {
        auto text = slice<u8>::from_raw_parts(source.contents().as_ptr() + begin.to_primitive(),
                                              end - begin);
        comments_.push(CommentTrivia {
            .kind  = kind,
            .style = style,
            .text =
                borrow_spelling_ ? TokenText::borrowed(text) : TokenText { Vec<u8>::from(text) },
            .begin         = begin_location,
            .end           = end_location,
            .start_of_line = start_of_line,
        });
    }

    auto finish() -> LexedFile {
        return LexedFile { .tokens = rstd::move(tokens_), .comments = rstd::move(comments_) };
    }

private:
    Vec<Token>         tokens_;
    Vec<CommentTrivia> comments_;
    bool               borrow_spelling_;
};

class ScanFileStorageSink {
public:
    ScanFileStorageSink(SharedSourceSnapshot snapshot, ScanMemoryAllocator allocator)
        : builder_(rstd::move(snapshot), rstd::move(allocator)) {}

    void push_token(const SourceFile&,
                    TokenKind kind,
                    usize     offset,
                    usize     length,
                    SourceLocation,
                    bool start_of_line,
                    bool leading_space) {
        builder_.push_token(kind, offset, length, start_of_line, leading_space);
    }

    void push_comment(const SourceFile&,
                      CommentKind  kind,
                      CommentStyle style,
                      usize        begin,
                      usize        end,
                      SourceLocation,
                      SourceLocation,
                      bool start_of_line) {
        builder_.push_comment(kind, style, begin, end, start_of_line);
    }

    auto finish() -> ScanFileStorage { return builder_.finish(); }

private:
    ScanFileStorageBuilder builder_;
};

template<typename Sink>
auto lex_into(const SourceFile& source, Sink& sink) -> Result<empty> {
    auto bytes         = source.contents();
    auto cursor        = PositionedCursor(rstd::parse::Input<u8>(bytes));
    auto line_start    = true;
    auto pending_space = false;

    while (cursor.position() < bytes.len()) {
        auto value = bytes[cursor.position()];
        if (value == u8('\\') && cursor.position() + usize(1) < bytes.len() &&
            (bytes[cursor.position() + usize(1)] == u8('\n') ||
             bytes[cursor.position() + usize(1)] == u8('\r'))) {
            (void)cursor.advance(usize(2));
            if (cursor.position() < bytes.len() &&
                bytes[cursor.position() - usize(1)] == u8('\r') &&
                bytes[cursor.position()] == u8('\n')) {
                (void)cursor.advance(usize(1));
            }

            continue;
        }
        if (value == u8('\r') || value == u8('\n')) {
            auto location = SourceLocation { .source = source.id,
                                             .offset = cursor.position(),
                                             .line   = cursor.source_position().line,
                                             .column = cursor.source_position().column };
            if (value == u8('\r') && cursor.position() + usize(1) < bytes.len() &&
                bytes[cursor.position() + usize(1)] == u8('\n')) {
                (void)cursor.advance(usize(1));
            }
            sink.push_token(
                source, TokenKind::Newline, location.offset, usize(1), location, line_start, false);
            (void)cursor.advance(usize(1));
            line_start    = true;
            pending_space = false;
            continue;
        }
        if (value == u8(' ') || value == u8('\t') || value == u8('\f') || value == u8('\v')) {
            (void)cursor.advance_single_line_unchecked(usize(1));
            pending_space = true;
            continue;
        }
        if (value == u8('/') && cursor.position() + usize(1) < bytes.len() &&
            bytes[cursor.position() + usize(1)] == u8('/')) {
            auto start              = SourceLocation { .source = source.id,
                                                       .offset = cursor.position(),
                                                       .line   = cursor.source_position().line,
                                                       .column = cursor.source_position().column };
            auto comment_line_start = line_start;
            pending_space           = true;
            (void)cursor.advance_single_line_unchecked(usize(2));
            auto body = rstd::parse::TextCursor(rstd::parse::Input<u8>(cursor.remaining_input()));
            auto span = rstd::parse::consume_until(body, [](u8 value) {
                return value == u8('\n') || value == u8('\r');
            });
            (void)cursor.advance_single_line_unchecked(span.len());
            auto kind = CommentKind::Ordinary;
            if (start.offset + usize(2) < bytes.len() &&
                bytes[start.offset + usize(2)] == u8('!')) {
                kind = CommentKind::InnerDocumentation;
            } else if (start.offset + usize(2) < bytes.len() &&
                       bytes[start.offset + usize(2)] == u8('/') &&
                       (start.offset + usize(3) >= bytes.len() ||
                        bytes[start.offset + usize(3)] != u8('/'))) {
                kind = CommentKind::OuterDocumentation;
            }
            sink.push_comment(source,
                              kind,
                              CommentStyle::Line,
                              start.offset,
                              cursor.position(),
                              start,
                              SourceLocation { .source = source.id,
                                               .offset = cursor.position(),
                                               .line   = cursor.source_position().line,
                                               .column = cursor.source_position().column },
                              comment_line_start);
            continue;
        }
        if (value == u8('/') && cursor.position() + usize(1) < bytes.len() &&
            bytes[cursor.position() + usize(1)] == u8('*')) {
            auto start              = SourceLocation { .source = source.id,
                                                       .offset = cursor.position(),
                                                       .line   = cursor.source_position().line,
                                                       .column = cursor.source_position().column };
            auto comment_line_start = line_start;
            pending_space           = true;
            (void)cursor.advance_single_line_unchecked(usize(2));
            auto closed = false;
            while (cursor.position() < bytes.len()) {
                if (bytes[cursor.position()] == u8('*') &&
                    cursor.position() + usize(1) < bytes.len() &&
                    bytes[cursor.position() + usize(1)] == u8('/')) {
                    (void)cursor.advance_single_line_unchecked(usize(2));
                    closed = true;
                    break;
                }
                if (bytes[cursor.position()] == u8('\r') || bytes[cursor.position()] == u8('\n')) {
                    auto location = SourceLocation { .source = source.id,
                                                     .offset = cursor.position(),
                                                     .line   = cursor.source_position().line,
                                                     .column = cursor.source_position().column };
                    if (bytes[cursor.position()] == u8('\r') &&
                        cursor.position() + usize(1) < bytes.len() &&
                        bytes[cursor.position() + usize(1)] == u8('\n')) {
                        (void)cursor.advance(usize(1));
                    }
                    sink.push_token(source,
                                    TokenKind::Newline,
                                    location.offset,
                                    usize(1),
                                    location,
                                    line_start,
                                    false);
                    (void)cursor.advance(usize(1));
                    line_start = true;
                    continue;
                }
                (void)cursor.advance_single_line_unchecked(usize(1));
            }
            if (! closed) return lex_failure("unterminated block comment"_str, start);
            auto kind = CommentKind::Ordinary;
            if (start.offset + usize(2) < bytes.len() &&
                bytes[start.offset + usize(2)] == u8('!')) {
                kind = CommentKind::InnerDocumentation;
            } else if (start.offset + usize(2) < bytes.len() &&
                       bytes[start.offset + usize(2)] == u8('*') &&
                       (start.offset + usize(3) >= bytes.len() ||
                        bytes[start.offset + usize(3)] != u8('*'))) {
                kind = CommentKind::OuterDocumentation;
            }
            sink.push_comment(source,
                              kind,
                              CommentStyle::Block,
                              start.offset,
                              cursor.position(),
                              start,
                              SourceLocation { .source = source.id,
                                               .offset = cursor.position(),
                                               .line   = cursor.source_position().line,
                                               .column = cursor.source_position().column },
                              comment_line_start);
            continue;
        }

        auto location    = SourceLocation { .source = source.id,
                                            .offset = cursor.position(),
                                            .line   = cursor.source_position().line,
                                            .column = cursor.source_position().column };
        auto token_start = cursor.position();
        auto scanned     = scan_preprocessing_token(cursor.remaining_input(), location);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        auto kind = scanned->kind;
        if (kind == TokenKind::Identifier || kind == TokenKind::PpNumber ||
            kind == TokenKind::Punctuation) {
            (void)cursor.advance_single_line_unchecked(scanned->length);
        } else {
            (void)cursor.advance(scanned->length);
        }

        sink.push_token(source,
                        kind,
                        token_start,
                        cursor.position() - token_start,
                        location,
                        line_start,
                        pending_space);
        line_start    = false;
        pending_space = false;
    }
    return Ok(empty {});
}

auto lex_with_comments(const SourceFile& source, bool borrow_spelling = false)
    -> Result<LexedFile> {
    auto sink   = LexedFileSink(borrow_spelling);
    auto result = lex_into(source, sink);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    return Ok(sink.finish());
}

auto lex_scan_file(const SourceFile& source, ScanMemoryAllocator allocator)
    -> Result<ScanFileStorage> {
    if (source.contents().len().to_primitive() > uint32_t(-1)) {
        return lex_failure("source file exceeds compact scan offset range"_str,
                           SourceLocation { .source = source.id });
    }
    auto sink   = ScanFileStorageSink(source.snapshot.clone(), rstd::move(allocator));
    auto result = lex_into(source, sink);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    return Ok(sink.finish());
}

auto lex_scan_file(const SourceFile& source) -> Result<ScanFileStorage> {
    auto domain = ScanMemoryDomain::make();
    return lex_scan_file(source, domain.allocator());
}

auto lex(const SourceFile& source, bool borrow_spelling = false) -> Result<Vec<Token>> {
    auto result = lex_with_comments(source, borrow_spelling);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    return Ok(rstd::move(result).unwrap().tokens);
}

auto lex_preprocessing_fragment(String contents, SourceLocation origin) -> Result<Vec<Token>> {
    auto source = SourceFile::make(origin.source,
                                   SourceBuffer {
                                       .path     = rstd::path::PathBuf::make(),
                                       .contents = rstd::move(contents),
                                   });
    auto tokens = lex(source);
    if (tokens.is_err()) return tokens;
    for (auto& token : *tokens) {
        token.spelling  = origin;
        token.expansion = origin;
    }
    return tokens;
}

auto classify_preprocessing_token(Vec<u8> spelling, SourceLocation origin = {})
    -> Result<TokenKind> {
    auto bytes     = spelling.as_slice();
    auto separator = bytes.is_empty() || bytes[usize {}] == u8(' ') ||
                     bytes[usize {}] == u8('\t') || bytes[usize {}] == u8('\f') ||
                     bytes[usize {}] == u8('\v') || bytes[usize {}] == u8('\r') ||
                     bytes[usize {}] == u8('\n') ||
                     (bytes.len() > usize(1) && bytes[usize {}] == u8('/') &&
                      (bytes[usize(1)] == u8('/') || bytes[usize(1)] == u8('*')));
    if (! separator) {
        auto scanned = scan_preprocessing_token(bytes, origin);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        if (scanned->length == bytes.len()) return Ok(scanned->kind);
    }
    return Err(Error::at(rstd::format("'{}' does not form one preprocessing token",
                                      TokenText::borrowed(bytes).display().as_str()),
                         origin));
}

auto classify_preprocessing_token(String spelling, SourceLocation origin = {})
    -> Result<TokenKind> {
    return classify_preprocessing_token(rstd::move(spelling).into_bytes(), origin);
}

auto is_identifier_spelling(ref<str> spelling) -> bool {
    auto bytes = spelling.as_bytes();
    if (bytes.is_empty()) return false;
    return identifier_length(bytes) == bytes.len();
}

} // namespace lito::frontend::lexical
