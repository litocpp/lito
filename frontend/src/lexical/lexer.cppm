export module lito.frontend.lexical:lexer;

import rstd;
import :token;
import :source;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

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
    for (auto cursor = index + usize(2); cursor < index + length; ++cursor) {
        if (! hex_digit(bytes[cursor])) return usize {};
    }
    return length;
}

auto identifier_length(slice<u8> bytes, usize index) -> usize {
    if (index >= bytes.len()) return usize {};
    auto cursor  = index;
    auto escaped = ucn_length(bytes, cursor);
    if (escaped != usize {}) {
        cursor += escaped;
    } else if (is_identifier_start(bytes[cursor])) {
        ++cursor;
    } else {
        return usize {};
    }
    while (cursor < bytes.len()) {
        escaped = ucn_length(bytes, cursor);
        if (escaped != usize {}) {
            cursor += escaped;
            continue;
        }
        if (! is_identifier_continue(bytes[cursor])) break;
        ++cursor;
    }
    return cursor - index;
}

struct ScannedPreprocessingToken {
    TokenKind kind { TokenKind::Punctuation };
    usize     end {};
    usize     line {};
    usize     column {};
};

auto scan_preprocessing_token(slice<u8> bytes, usize start, SourceLocation location)
    -> Result<ScannedPreprocessingToken> {
    auto index      = start;
    auto line       = location.line;
    auto column     = location.column;
    auto kind       = TokenKind::Punctuation;
    auto raw_prefix = raw_prefix_length(bytes, index);
    auto prefix     = literal_prefix_length(bytes, index);
    if (raw_prefix != usize {}) {
        kind = TokenKind::StringLiteral;
        index += raw_prefix + usize(1);
        column += raw_prefix + usize(1);
        auto delimiter_begin = index;
        while (index < bytes.len() && bytes[index] != u8('(') && bytes[index] != u8('\n') &&
               bytes[index] != u8('\r') && index - delimiter_begin <= usize(16)) {
            ++index;
            ++column;
        }
        if (index >= bytes.len() || bytes[index] != u8('(') ||
            index - delimiter_begin > usize(16)) {
            return lex_failure("invalid raw string delimiter"_str, location);
        }
        auto delimiter_end = index;
        ++index;
        ++column;
        auto closed = false;
        while (index < bytes.len()) {
            if (bytes[index] == u8(')')) {
                auto matches = true;
                auto length  = delimiter_end - delimiter_begin;
                for (auto offset = usize {}; offset < length; ++offset) {
                    if (index + usize(1) + offset >= bytes.len() ||
                        bytes[index + usize(1) + offset] != bytes[delimiter_begin + offset]) {
                        matches = false;
                        break;
                    }
                }
                auto quote = index + usize(1) + length;
                if (matches && quote < bytes.len() && bytes[quote] == u8('"')) {
                    column += quote + usize(1) - index;
                    index  = quote + usize(1);
                    closed = true;
                    break;
                }
            }
            if (bytes[index] == u8('\r') || bytes[index] == u8('\n')) {
                if (bytes[index] == u8('\r') && index + usize(1) < bytes.len() &&
                    bytes[index + usize(1)] == u8('\n')) {
                    ++index;
                }
                ++index;
                ++line;
                column = usize(1);
                continue;
            }
            ++index;
            ++column;
        }
        if (! closed) return lex_failure("unterminated raw string literal"_str, location);
    } else if (bytes[start] == u8('"') || bytes[start] == u8('\'') || prefix != usize {}) {
        index += prefix;
        column += prefix;
        auto quote = bytes[index];
        kind       = quote == u8('"') ? TokenKind::StringLiteral : TokenKind::CharacterLiteral;
        ++index;
        ++column;
        auto closed = false;
        while (index < bytes.len()) {
            if (bytes[index] == u8('\\') && index + usize(1) < bytes.len()) {
                index += usize(2);
                column += usize(2);
                continue;
            }
            if (bytes[index] == quote) {
                ++index;
                ++column;
                closed = true;
                break;
            }
            if (bytes[index] == u8('\n') || bytes[index] == u8('\r')) break;
            ++index;
            ++column;
        }
        if (! closed) return lex_failure("unterminated literal"_str, location);
    } else if (auto length = identifier_length(bytes, start); length != usize {}) {
        kind = TokenKind::Identifier;
        index += length;
        column += length;
    } else if ((bytes[start] >= u8('0') && bytes[start] <= u8('9')) ||
               (bytes[start] == u8('.') && start + usize(1) < bytes.len() &&
                bytes[start + usize(1)] >= u8('0') && bytes[start + usize(1)] <= u8('9'))) {
        kind = TokenKind::PpNumber;
        while (index < bytes.len()) {
            auto current = bytes[index];
            if (is_identifier_continue(current) || current == u8('.') || current == u8('\'')) {
                ++index;
                ++column;
                continue;
            }
            if ((current == u8('+') || current == u8('-')) && index != start &&
                (bytes[index - usize(1)] == u8('e') || bytes[index - usize(1)] == u8('E') ||
                 bytes[index - usize(1)] == u8('p') || bytes[index - usize(1)] == u8('P'))) {
                ++index;
                ++column;
                continue;
            }
            break;
        }
    } else {
        auto length = punctuation_length(bytes, index);
        index += length;
        column += length;
    }
    return Ok(
        ScannedPreprocessingToken { .kind = kind, .end = index, .line = line, .column = column });
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
        auto text = source.contents().get(offset, offset + length).unwrap();
        append_token(tokens_,
                     kind,
                     borrow_spelling_ ? TokenText::borrowed(text)
                                      : TokenText { String::make(text) },
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
        auto text = source.contents().get(begin, end).unwrap();
        comments_.push(CommentTrivia {
            .kind  = kind,
            .style = style,
            .text = borrow_spelling_ ? TokenText::borrowed(text) : TokenText { String::make(text) },
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
    explicit ScanFileStorageSink(SharedSourceSnapshot snapshot): builder_(rstd::move(snapshot)) {}

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
    auto bytes         = source.contents().as_bytes();
    auto index         = usize {};
    auto line          = usize(1);
    auto column        = usize(1);
    auto line_start    = true;
    auto pending_space = false;

    while (index < bytes.len()) {
        auto value = bytes[index];
        if (value == u8('\\') && index + usize(1) < bytes.len() &&
            (bytes[index + usize(1)] == u8('\n') || bytes[index + usize(1)] == u8('\r'))) {
            index += usize(2);
            if (index < bytes.len() && bytes[index - usize(1)] == u8('\r') &&
                bytes[index] == u8('\n')) {
                ++index;
            }
            ++line;
            column = usize(1);
            continue;
        }
        if (value == u8('\r') || value == u8('\n')) {
            auto location = SourceLocation {
                .source = source.id, .offset = index, .line = line, .column = column
            };
            if (value == u8('\r') && index + usize(1) < bytes.len() &&
                bytes[index + usize(1)] == u8('\n')) {
                ++index;
            }
            sink.push_token(
                source, TokenKind::Newline, location.offset, usize(1), location, line_start, false);
            ++index;
            ++line;
            column        = usize(1);
            line_start    = true;
            pending_space = false;
            continue;
        }
        if (value == u8(' ') || value == u8('\t') || value == u8('\f') || value == u8('\v')) {
            ++index;
            ++column;
            pending_space = true;
            continue;
        }
        if (value == u8('/') && index + usize(1) < bytes.len() &&
            bytes[index + usize(1)] == u8('/')) {
            auto start = SourceLocation {
                .source = source.id, .offset = index, .line = line, .column = column
            };
            auto comment_line_start = line_start;
            pending_space           = true;
            index += usize(2);
            column += usize(2);
            while (index < bytes.len() && bytes[index] != u8('\n') && bytes[index] != u8('\r')) {
                ++index;
                ++column;
            }
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
            sink.push_comment(
                source,
                kind,
                CommentStyle::Line,
                start.offset,
                index,
                start,
                SourceLocation {
                    .source = source.id, .offset = index, .line = line, .column = column },
                comment_line_start);
            continue;
        }
        if (value == u8('/') && index + usize(1) < bytes.len() &&
            bytes[index + usize(1)] == u8('*')) {
            auto start = SourceLocation {
                .source = source.id, .offset = index, .line = line, .column = column
            };
            auto comment_line_start = line_start;
            pending_space           = true;
            index += usize(2);
            column += usize(2);
            auto closed = false;
            while (index < bytes.len()) {
                if (bytes[index] == u8('*') && index + usize(1) < bytes.len() &&
                    bytes[index + usize(1)] == u8('/')) {
                    index += usize(2);
                    column += usize(2);
                    closed = true;
                    break;
                }
                if (bytes[index] == u8('\r') || bytes[index] == u8('\n')) {
                    auto location = SourceLocation {
                        .source = source.id, .offset = index, .line = line, .column = column
                    };
                    if (bytes[index] == u8('\r') && index + usize(1) < bytes.len() &&
                        bytes[index + usize(1)] == u8('\n')) {
                        ++index;
                    }
                    sink.push_token(source,
                                    TokenKind::Newline,
                                    location.offset,
                                    usize(1),
                                    location,
                                    line_start,
                                    false);
                    ++index;
                    ++line;
                    column     = usize(1);
                    line_start = true;
                    continue;
                }
                ++index;
                ++column;
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
            sink.push_comment(
                source,
                kind,
                CommentStyle::Block,
                start.offset,
                index,
                start,
                SourceLocation {
                    .source = source.id, .offset = index, .line = line, .column = column },
                comment_line_start);
            continue;
        }

        auto location =
            SourceLocation { .source = source.id, .offset = index, .line = line, .column = column };
        auto token_start = index;
        auto scanned     = scan_preprocessing_token(bytes, token_start, location);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        auto kind = scanned->kind;
        index     = scanned->end;
        line      = scanned->line;
        column    = scanned->column;

        auto spelling = source.contents().get(token_start, index);
        if (spelling.is_none()) return lex_failure("invalid UTF-8 token boundary"_str, location);
        sink.push_token(
            source, kind, token_start, index - token_start, location, line_start, pending_space);
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

auto lex_scan_file(const SourceFile& source) -> Result<ScanFileStorage> {
    if (source.contents().len().to_primitive() > uint32_t(-1)) {
        return lex_failure("source file exceeds compact scan offset range"_str,
                           SourceLocation { .source = source.id });
    }
    auto sink   = ScanFileStorageSink(source.snapshot.clone());
    auto result = lex_into(source, sink);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    return Ok(sink.finish());
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

auto classify_preprocessing_token(String spelling, SourceLocation origin = {})
    -> Result<TokenKind> {
    auto text      = spelling.as_str();
    auto bytes     = text.as_bytes();
    auto separator = bytes.is_empty() || bytes[usize {}] == u8(' ') ||
                     bytes[usize {}] == u8('\t') || bytes[usize {}] == u8('\f') ||
                     bytes[usize {}] == u8('\v') || bytes[usize {}] == u8('\r') ||
                     bytes[usize {}] == u8('\n') ||
                     (bytes.len() > usize(1) && bytes[usize {}] == u8('/') &&
                      (bytes[usize(1)] == u8('/') || bytes[usize(1)] == u8('*')));
    if (! separator) {
        auto scanned = scan_preprocessing_token(bytes, usize {}, origin);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        if (scanned->end == bytes.len()) return Ok(scanned->kind);
    }
    return Err(Error::at(rstd::format("'{}' does not form one preprocessing token", text), origin));
}

auto is_identifier_spelling(ref<str> spelling) -> bool {
    auto bytes = spelling.as_bytes();
    if (bytes.is_empty()) return false;
    return identifier_length(bytes, usize {}) == bytes.len();
}

} // namespace lito::frontend::lexical
