export module lito.frontend.lexical:directive;

import rstd;
import :token;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::lexical
{

enum class DirectiveMarkerPolicy
{
    HashOnly,
    HashAndDigraph,
};

enum class DirectiveKind
{
    Empty,
    InvalidKeyword,
    InvalidUtf8,
    Unknown,
    Define,
    Undef,
    Include,
    IncludeNext,
    Import,
    Embed,
    Pragma,
    If,
    Ifdef,
    Ifndef,
    Elif,
    Elifdef,
    Elifndef,
    Else,
    Endif,
    Error,
    Warning,
    Line,
};

struct DirectiveHead {
    DirectiveKind kind { DirectiveKind::Empty };
    usize         marker_index { usize::MAX };
    usize         keyword_index { usize::MAX };
    usize         arguments_begin { usize::MAX };
    usize         name_index { usize::MAX };
};

constexpr auto escaped_newline_length(slice<u8> bytes, usize position) -> usize {
    if (position + usize(1) >= bytes.len() || bytes[position] != u8('\\')) return usize {};
    auto next = bytes[position + usize(1)];
    if (next == u8('\n')) return usize(2);
    if (next != u8('\r')) return usize {};
    return position + usize(2) < bytes.len() && bytes[position + usize(2)] == u8('\n') ? usize(3)
                                                                                       : usize(2);
}

auto physical_line_begin(slice<u8> bytes, usize offset) -> usize {
    while (offset != usize {} && bytes[offset - usize(1)] != u8('\n') &&
           bytes[offset - usize(1)] != u8('\r'))
        --offset;
    return offset;
}

auto physical_line_end(slice<u8> bytes, usize offset) -> usize {
    while (offset < bytes.len() && bytes[offset] != u8('\n') && bytes[offset] != u8('\r')) ++offset;
    if (offset < bytes.len() && bytes[offset] == u8('\r')) ++offset;
    if (offset < bytes.len() && bytes[offset] == u8('\n')) ++offset;
    return offset;
}

auto continued_line_end(slice<u8> bytes, usize offset) -> usize {
    auto end = physical_line_end(bytes, offset);
    for (;;) {
        auto newline = end;
        if (newline != usize {} && bytes[newline - usize(1)] == u8('\n')) --newline;
        if (newline != usize {} && bytes[newline - usize(1)] == u8('\r')) --newline;
        if (newline == usize {} || bytes[newline - usize(1)] != u8('\\') || end == bytes.len())
            return end;
        end = physical_line_end(bytes, end);
    }
}

template<typename Reader>
auto logical_token_line_end(const Reader& reader, usize begin, usize count) -> usize {
    while (begin < count && reader.kind(begin) != TokenKind::Newline) ++begin;
    return begin;
}

} // namespace lito::frontend::lexical

namespace lito::frontend::lexical
{

auto classify_directive_keyword(ref<str> keyword) -> DirectiveKind {
    if (keyword == "define"_str) return DirectiveKind::Define;
    if (keyword == "undef"_str) return DirectiveKind::Undef;
    if (keyword == "include"_str) return DirectiveKind::Include;
    if (keyword == "include_next"_str) return DirectiveKind::IncludeNext;
    if (keyword == "import"_str) return DirectiveKind::Import;
    if (keyword == "embed"_str) return DirectiveKind::Embed;
    if (keyword == "pragma"_str) return DirectiveKind::Pragma;
    if (keyword == "if"_str) return DirectiveKind::If;
    if (keyword == "ifdef"_str) return DirectiveKind::Ifdef;
    if (keyword == "ifndef"_str) return DirectiveKind::Ifndef;
    if (keyword == "elif"_str) return DirectiveKind::Elif;
    if (keyword == "elifdef"_str) return DirectiveKind::Elifdef;
    if (keyword == "elifndef"_str) return DirectiveKind::Elifndef;
    if (keyword == "else"_str) return DirectiveKind::Else;
    if (keyword == "endif"_str) return DirectiveKind::Endif;
    if (keyword == "error"_str) return DirectiveKind::Error;
    if (keyword == "warning"_str) return DirectiveKind::Warning;
    if (keyword == "line"_str) return DirectiveKind::Line;
    return DirectiveKind::Unknown;
}

} // namespace lito::frontend::lexical

export namespace lito::frontend::lexical
{

template<typename Reader>
auto recognize_directive_head(const Reader&         reader,
                              usize                 begin,
                              usize                 end,
                              DirectiveMarkerPolicy policy) -> Option<DirectiveHead> {
    while (begin < end && reader.comment(begin)) ++begin;
    if (begin == end || ! reader.start_of_line(begin)) return None();
    auto marker = reader.text(begin);
    if (marker != "#"_str &&
        (policy != DirectiveMarkerPolicy::HashAndDigraph || marker != "%:"_str))
        return None();
    auto result = DirectiveHead { .marker_index = begin, .arguments_begin = end };
    auto cursor = begin + usize(1);
    while (cursor < end && reader.comment(cursor)) ++cursor;
    if (cursor == end) return Some(result);
    result.keyword_index = cursor;
    if (reader.kind(cursor) != TokenKind::Identifier) {
        result.kind = DirectiveKind::InvalidKeyword;
        return Some(result);
    }
    auto spelling = reader.text(cursor);
    auto keyword  = spelling.utf8();
    if (keyword.is_err()) {
        result.kind = DirectiveKind::InvalidUtf8;
        return Some(result);
    }
    result.kind = classify_directive_keyword(*keyword);
    ++cursor;
    while (cursor < end && reader.comment(cursor)) ++cursor;
    result.arguments_begin = cursor;
    if (cursor < end) result.name_index = cursor;
    return Some(result);
}

} // namespace lito::frontend::lexical
