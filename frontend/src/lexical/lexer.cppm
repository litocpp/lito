export module tenon.frontend.lexical:lexer;

import rstd;
import :token;
import :source;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::frontend::lexical
{

auto lex_failure(ref<str> message, SourceLocation location) -> Result<Vec<Token>> {
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

} // namespace tenon::frontend::lexical

export namespace tenon::frontend::lexical
{

auto lex(const SourceFile& source, bool borrow_spelling = false) -> Result<Vec<Token>> {
    auto tokens     = Vec<Token>::make();
    auto bytes      = source.contents().as_bytes();
    auto token_text = [borrow_spelling](ref<str> text) -> TokenText {
        return borrow_spelling ? TokenText::borrowed(text) : TokenText { String::make(text) };
    };
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
            append_token(
                tokens,
                TokenKind::Newline,
                token_text(
                    source.contents().get(location.offset, location.offset + usize(1)).unwrap()),
                location,
                line_start,
                false);
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
            pending_space = true;
            index += usize(2);
            column += usize(2);
            while (index < bytes.len() && bytes[index] != u8('\n') && bytes[index] != u8('\r')) {
                ++index;
                ++column;
            }
            continue;
        }
        if (value == u8('/') && index + usize(1) < bytes.len() &&
            bytes[index + usize(1)] == u8('*')) {
            auto start = SourceLocation {
                .source = source.id, .offset = index, .line = line, .column = column
            };
            pending_space = true;
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
                    append_token(tokens,
                                 TokenKind::Newline,
                                 token_text(source.contents()
                                                .get(location.offset, location.offset + usize(1))
                                                .unwrap()),
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
            continue;
        }

        auto location =
            SourceLocation { .source = source.id, .offset = index, .line = line, .column = column };
        auto token_start = index;
        auto kind        = TokenKind::Punctuation;
        auto raw_prefix  = raw_prefix_length(bytes, index);
        auto prefix      = literal_prefix_length(bytes, index);
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
        } else if (value == u8('"') || value == u8('\'') || prefix != usize {}) {
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
        } else if (is_identifier_start(value) || ucn_length(bytes, index) != usize {}) {
            kind = TokenKind::Identifier;
            while (index < bytes.len()) {
                auto escaped = ucn_length(bytes, index);
                if (escaped != usize {}) {
                    index += escaped;
                    column += escaped;
                    continue;
                }
                if (! is_identifier_continue(bytes[index])) break;
                ++index;
                ++column;
            }
        } else if ((value >= u8('0') && value <= u8('9')) ||
                   (value == u8('.') && index + usize(1) < bytes.len() &&
                    bytes[index + usize(1)] >= u8('0') && bytes[index + usize(1)] <= u8('9'))) {
            kind = TokenKind::PpNumber;
            while (index < bytes.len()) {
                auto current = bytes[index];
                if (is_identifier_continue(current) || current == u8('.') || current == u8('\'')) {
                    ++index;
                    ++column;
                    continue;
                }
                if ((current == u8('+') || current == u8('-')) && index != token_start &&
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

        auto spelling = source.contents().get(token_start, index);
        if (spelling.is_none()) return lex_failure("invalid UTF-8 token boundary"_str, location);
        append_token(tokens, kind, token_text(*spelling), location, line_start, pending_space);
        line_start    = false;
        pending_space = false;
    }
    return Ok(rstd::move(tokens));
}

} // namespace tenon::frontend::lexical
