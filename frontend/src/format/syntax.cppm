export module lito.frontend.format:syntax;

import rstd;
import lito.frontend.lexical;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

enum class BlockKind
{
    None,
    Function,
    Control,
    Namespace,
    Record
};

enum class OperatorRole
{
    Unknown,
    Binary
};

enum class ParenRole
{
    Unknown,
    Control,
    Call,
    Expression
};

enum class ArrowRole
{
    None,
    Member,
    TrailingReturn
};

struct TextRange {
    usize begin;
    usize end;
};

struct TokenSource {
    usize              begin;
    usize              end;
    lexical::TokenKind kind;
    bool               comment {};
    bool               line_comment {};
    bool               start_of_line {};
};

struct TokenSyntax {
    usize        pair { usize::MAX };
    BlockKind    block { BlockKind::None };
    OperatorRole operator_role { OperatorRole::Unknown };
    ParenRole    paren_role { ParenRole::Unknown };
    ArrowRole    arrow_role { ArrowRole::None };
    bool         protected_text {};
    bool         module_statement {};
    bool         word_like {};
    bool         expression_prefix {};
    bool         block_continuation {};
};

struct FormatToken {
    TokenSource source;
    TokenSyntax syntax;
};

struct FormatSyntax {
    lexical::SharedSourceSnapshot snapshot;
    Vec<FormatToken>              tokens;
    Vec<TextRange>                protected_ranges;
    Vec<TextRange>                directives;
    Vec<String>                   macros;
    bool                          preserve_all {};

    auto bytes() const -> slice<u8> { return snapshot->contents.as_bytes(); }
    auto text(usize begin, usize end) const -> ref<str> {
        return rstd::str_::from_utf8(
                   slice<u8>::from_raw_parts(bytes().as_ptr() + begin.to_primitive(), end - begin))
            .unwrap();
    }
    auto text(usize index) const -> ref<str> {
        return text(tokens[index].source.begin, tokens[index].source.end);
    }
    auto is(usize index, ref<str> spelling) const -> bool {
        return index < tokens.len() && text(index) == spelling;
    }
    auto next_token(usize index, usize end = usize::MAX) const -> usize {
        if (index < tokens.len()) ++index;
        while (index < tokens.len() && tokens[index].source.comment) ++index;
        return index < tokens.len() && tokens[index].source.begin < end ? index : tokens.len();
    }
    auto previous_token(usize index) const -> usize {
        while (index != usize {}) {
            --index;
            if (! tokens[index].source.comment) return index;
        }
        return usize::MAX;
    }
    auto line_begin(usize offset) const -> usize {
        return lexical::physical_line_begin(bytes(), offset);
    }
    auto line_end(usize offset) const -> usize {
        return lexical::physical_line_end(bytes(), offset);
    }
    auto overlaps(usize begin, usize end) const -> bool {
        for (const auto& range : protected_ranges) {
            if (range.begin < end && begin < range.end) return true;
        }
        return false;
    }
    auto directive(usize offset) const -> bool {
        for (const auto& range : directives) {
            if (range.begin > offset) break;
            if (offset < range.end) return true;
        }
        return false;
    }
};

class FormatSink {
    FormatSyntax syntax_;

public:
    explicit FormatSink(lexical::SharedSourceSnapshot snapshot)
        : syntax_ { .snapshot = rstd::move(snapshot) } {}

    auto push_token(const lexical::SourceFile& source,
                    lexical::TokenKind         kind,
                    usize                      offset,
                    usize                      length,
                    lexical::SourceLocation,
                    bool start_of_line,
                    bool) -> void {
        if (kind == lexical::TokenKind::Newline) return;
        auto end   = offset + length;
        auto bytes = source.contents();
        if (offset == usize {} && end >= usize(3) && bytes[usize {}] == u8(0xef) &&
            bytes[usize(1)] == u8(0xbb) && bytes[usize(2)] == u8(0xbf))
            offset = usize(3);
        if (offset != end)
            syntax_.tokens.push(FormatToken {
                .source = { .begin         = offset,
                            .end           = end,
                            .kind          = kind,
                            .start_of_line = start_of_line },
            });
    }

    auto push_comment(const lexical::SourceFile&,
                      lexical::CommentKind,
                      lexical::CommentStyle style,
                      usize                 begin,
                      usize                 end,
                      lexical::SourceLocation,
                      lexical::SourceLocation,
                      bool start_of_line) -> void {
        syntax_.tokens.push(FormatToken {
            .source = {
                .begin = begin,
                .end = end,
                .kind = lexical::TokenKind::Punctuation,
                .comment = true,
                .line_comment = style == lexical::CommentStyle::Line,
                .start_of_line = start_of_line,
            },
        });
    }

    auto finish() -> FormatSyntax { return rstd::move(syntax_); }
};

auto whitespace(u8 value) -> bool {
    return value == u8(' ') || value == u8('\t') || value == u8('\r') || value == u8('\n') ||
           value == u8('\v') || value == u8('\f');
}

struct DirectiveReader {
    const FormatSyntax& syntax;

    auto kind(usize index) const -> lexical::TokenKind { return syntax.tokens[index].source.kind; }
    auto text(usize index) const -> lexical::TokenText {
        return lexical::TokenText::borrowed(syntax.text(index));
    }
    auto comment(usize index) const -> bool { return syntax.tokens[index].source.comment; }
    auto start_of_line(usize index) const -> bool {
        const auto& source = syntax.tokens[index].source;
        if (source.start_of_line) return true;
        auto bytes = syntax.bytes();
        for (auto offset = syntax.line_begin(source.begin); offset < source.begin; ++offset) {
            if (offset == usize {} && source.begin >= usize(3) && bytes[offset] == u8(0xef) &&
                bytes[usize(1)] == u8(0xbb) && bytes[usize(2)] == u8(0xbf)) {
                offset = usize(2);
                continue;
            }
            if (! whitespace(bytes[offset])) return false;
        }
        return true;
    }
};

auto normalize_ranges(Vec<TextRange>& ranges) -> void {
    rstd::slice_::sort_unstable_by(ranges.as_mut_slice().as_mut_ref(),
                                   [](const TextRange& a, const TextRange& b) {
                                       return a.begin < b.begin;
                                   });
    auto merged = Vec<TextRange>::make();
    for (const auto& range : ranges) {
        if (! merged.is_empty() && range.begin <= merged[merged.len() - usize(1)].end) {
            auto& previous = merged[merged.len() - usize(1)];
            if (range.end > previous.end) previous.end = range.end;
        } else
            merged.push(TextRange { range.begin, range.end });
    }
    ranges = rstd::move(merged);
}

auto marker(ref<str> text, ref<str> name, bool on) -> bool {
    auto length = text.len();
    while (length != usize {}) {
        auto value = text.as_bytes()[length - usize(1)];
        if (value != u8(' ') && value != u8('\t') && value != u8('\v') && value != u8('\f')) break;
        --length;
    }
    text       = text.get(usize {}, length).unwrap();
    auto line  = rstd::format("// {} {}", name, on ? "on"_str : "off"_str);
    auto block = rstd::format("/* {} {} */", name, on ? "on"_str : "off"_str);
    return text == block.as_str() || text == line.as_str() ||
           text.starts_with(rstd::format("{}:", line).as_str());
}

auto collect_protection(FormatSyntax& syntax) -> void {
    auto     reader      = DirectiveReader { syntax };
    auto     disabled    = Option<usize> {};
    auto     conditional = Option<usize> {};
    unsigned conditional_depth {};
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        const auto& token = syntax.tokens[index];
        auto        text  = syntax.text(index);
        if (token.source.comment) {
            auto off =
                marker(text, "clang-format"_str, false) || marker(text, "lito-format"_str, false);
            auto on =
                marker(text, "clang-format"_str, true) || marker(text, "lito-format"_str, true);
            if (off && disabled.is_none()) disabled = Some(syntax.line_begin(token.source.begin));
            if (on && disabled.is_some()) {
                syntax.protected_ranges.push({ *disabled, syntax.line_end(token.source.end) });
                disabled = None();
            }
            continue;
        }
        if (text != "#"_str && text != "%:"_str) continue;
        if (! reader.start_of_line(index)) continue;
        auto begin     = syntax.line_begin(token.source.begin);
        auto end       = lexical::continued_line_end(syntax.bytes(), token.source.end);
        auto token_end = index + usize(1);
        while (token_end < syntax.tokens.len() && syntax.tokens[token_end].source.begin < end)
            ++token_end;
        auto head = lexical::recognize_directive_head(
            reader, index, token_end, lexical::DirectiveMarkerPolicy::HashAndDigraph);
        if (head.is_none()) continue;
        syntax.directives.push({ begin, end });
        syntax.protected_ranges.push({ begin, end });
        if (head->kind == lexical::DirectiveKind::If ||
            head->kind == lexical::DirectiveKind::Ifdef ||
            head->kind == lexical::DirectiveKind::Ifndef) {
            if (conditional_depth++ == 0) conditional = Some(begin);
        } else if (head->kind == lexical::DirectiveKind::Endif) {
            if (conditional_depth == 0) {
                syntax.preserve_all = true;
                continue;
            }
            if (--conditional_depth == 0) {
                syntax.protected_ranges.push({ *conditional, end });
                conditional = None();
            }
        } else if (head->kind == lexical::DirectiveKind::Define && head->name_index != usize::MAX) {
            syntax.macros.push(String::make(syntax.text(head->name_index)));
        }
    }
    if (disabled.is_some()) syntax.protected_ranges.push({ *disabled, syntax.bytes().len() });
    if (conditional.is_some()) syntax.preserve_all = true;
    for (auto index = usize {}; index + usize(1) < syntax.bytes().len(); ++index) {
        if (lexical::escaped_newline_length(syntax.bytes(), index) == usize {}) continue;
        auto begin = syntax.line_begin(index);
        auto end   = syntax.line_end(syntax.line_end(index));
        syntax.protected_ranges.push({ begin, end });
    }
    normalize_ranges(syntax.directives);
    normalize_ranges(syntax.protected_ranges);
}

auto opening(ref<str> text) -> bool {
    return text == "("_str || text == "["_str || text == "{"_str;
}
auto closing(ref<str> text) -> bool {
    return text == ")"_str || text == "]"_str || text == "}"_str;
}
auto corresponding(ref<str> left, ref<str> right) -> bool {
    return (left == "("_str && right == ")"_str) || (left == "["_str && right == "]"_str) ||
           (left == "{"_str && right == "}"_str);
}

auto pair_tokens(FormatSyntax& syntax) -> void {
    Vec<usize> stack;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (token.source.comment || syntax.directive(token.source.begin)) continue;
        auto text = syntax.text(index);
        if (opening(text))
            stack.push(usize(index));
        else if (closing(text)) {
            if (stack.is_empty() ||
                ! corresponding(syntax.text(stack[stack.len() - usize(1)]), text)) {
                syntax.preserve_all = true;
                return;
            }
            auto other                       = stack.pop().unwrap();
            token.syntax.pair                = other;
            syntax.tokens[other].syntax.pair = index;
        }
    }
    if (! stack.is_empty()) syntax.preserve_all = true;
    for (const auto& range : syntax.protected_ranges) {
        for (const auto& token : syntax.tokens) {
            if (token.source.begin >= range.end) break;
            if (token.source.begin < range.begin || token.syntax.pair == usize::MAX) continue;
            const auto& pair = syntax.tokens[token.syntax.pair];
            if (pair.source.begin < range.begin || pair.source.end > range.end)
                syntax.preserve_all = true;
        }
    }
}

auto control(ref<str> text) -> bool {
    return text == "if"_str || text == "for"_str || text == "while"_str || text == "switch"_str ||
           text == "catch"_str;
}

auto binary_operator(const FormatSyntax& syntax, usize index) -> bool {
    if (index == usize {} || index + usize(1) >= syntax.tokens.len()) return false;
    auto text = syntax.text(index);
    if (text == "="_str || text == "+="_str || text == "-="_str || text == "*="_str ||
        text == "/="_str || text == "%="_str || text == "=="_str || text == "!="_str ||
        text == "||"_str || text == "<="_str || text == ">="_str)
        return true;
    auto left = index - usize(1), right = index + usize(1);
    if (syntax.tokens[left].syntax.expression_prefix) return false;
    if (! (syntax.tokens[left].syntax.word_like || syntax.is(left, ")"_str) ||
           syntax.is(left, "]"_str)) ||
        ! (syntax.tokens[right].syntax.word_like || syntax.is(right, "("_str)))
        return false;
    if (text == "+"_str || text == "-"_str || text == "/"_str || text == "%"_str || text == "^"_str)
        return true;
    return (text == "<"_str || text == ">"_str) &&
           syntax.tokens[right].source.kind == lexical::TokenKind::PpNumber;
}

auto annotate_roles(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token            = syntax.tokens[index];
        auto  text             = syntax.text(index);
        token.syntax.word_like = token.source.kind == lexical::TokenKind::Identifier ||
                                 token.source.kind == lexical::TokenKind::PpNumber ||
                                 token.source.kind == lexical::TokenKind::StringLiteral ||
                                 token.source.kind == lexical::TokenKind::CharacterLiteral;
        token.syntax.expression_prefix =
            text == "return"_str || text == "co_return"_str || text == "throw"_str;
        token.syntax.block_continuation =
            text == "else"_str || text == "catch"_str || text == "while"_str;
    }
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (binary_operator(syntax, index)) token.syntax.operator_role = OperatorRole::Binary;
        if (syntax.is(index, "->"_str)) {
            token.syntax.arrow_role = index != usize {} && syntax.is(index - usize(1), ")"_str)
                                          ? ArrowRole::TrailingReturn
                                          : ArrowRole::Member;
        }
        if (index == usize {} || ! syntax.is(index, "("_str)) continue;
        const auto& previous = syntax.tokens[index - usize(1)];
        auto        text     = syntax.text(index - usize(1));
        if (control(text) || text == "constexpr"_str)
            token.syntax.paren_role = ParenRole::Control;
        else if (previous.syntax.expression_prefix)
            token.syntax.paren_role = ParenRole::Expression;
        else if (previous.source.kind == lexical::TokenKind::Identifier || text == ")"_str ||
                 text == "]"_str)
            token.syntax.paren_role = ParenRole::Call;
    }
}

auto classify_blocks(FormatSyntax& syntax) -> void {
    auto head = usize {};
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (token.source.comment || syntax.overlaps(token.source.begin, token.source.end)) continue;
        auto text = syntax.text(index);
        auto next = syntax.next_token(index);
        if (text == "import"_str && next < syntax.tokens.len() &&
            (syntax.is(next, "<"_str) ||
             syntax.tokens[next].source.kind == lexical::TokenKind::StringLiteral)) {
            auto end = next;
            while (end < syntax.tokens.len() && ! syntax.is(end, ";"_str)) ++end;
            if (end < syntax.tokens.len())
                syntax.protected_ranges.push({ token.source.begin, syntax.tokens[end].source.end });
            index = end;
            head  = end + usize(1);
            continue;
        }
        if (text == "("_str || text == "["_str) {
            if (token.syntax.pair != usize::MAX) index = token.syntax.pair;
            continue;
        }
        if (text == ";"_str || text == "}"_str) {
            head = index + usize(1);
            continue;
        }
        if (text != "{"_str || token.syntax.pair == usize::MAX) continue;
        auto kind           = BlockKind::None;
        auto function_paren = usize::MAX;
        bool unsafe         = false;
        bool assignment     = false;
        bool namespace_seen = false;
        bool record_seen    = false;
        for (auto scan = head; scan < index; ++scan) {
            if (syntax.tokens[scan].source.comment ||
                syntax.directive(syntax.tokens[scan].source.begin))
                continue;
            auto value = syntax.text(scan);
            if (value == "namespace"_str) namespace_seen = true;
            if (value == "struct"_str || value == "class"_str || value == "union"_str)
                record_seen = true;
            if (value == "requires"_str || value == "template"_str || value == "["_str ||
                value == "enum"_str || value == "operator"_str)
                unsafe = true;
            if (value == "="_str) assignment = true;
            if (value == "("_str && syntax.tokens[scan].syntax.pair < index) {
                if (function_paren == usize::MAX) function_paren = scan;
                scan = syntax.tokens[scan].syntax.pair;
            } else if (value == ":"_str && function_paren != usize::MAX)
                unsafe = true;
        }
        if (! unsafe && ! assignment) {
            if (namespace_seen)
                kind = BlockKind::Namespace;
            else if (record_seen)
                kind = BlockKind::Record;
            else if (function_paren != usize::MAX && function_paren != usize {}) {
                auto before = syntax.previous_token(function_paren);
                if (syntax.is(before, "constexpr"_str)) before = syntax.previous_token(before);
                if (before != usize::MAX && control(syntax.text(before)))
                    kind = BlockKind::Control;
                else if (before != usize::MAX &&
                         syntax.tokens[before].source.kind == lexical::TokenKind::Identifier)
                    kind = BlockKind::Function;
            } else if (auto before = syntax.previous_token(index); syntax.is(before, "else"_str) ||
                                                                   syntax.is(before, "do"_str) ||
                                                                   syntax.is(before, "try"_str))
                kind = BlockKind::Control;
            else if (index == head)
                kind = BlockKind::Control;
        }
        if (kind == BlockKind::None) {
            syntax.protected_ranges.push(
                { token.source.begin, syntax.tokens[token.syntax.pair].source.end });
            index = token.syntax.pair;
            continue;
        }
        token.syntax.block                            = kind;
        syntax.tokens[token.syntax.pair].syntax.block = kind;
        head                                          = index + usize(1);
    }
}

auto protect_constructs(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        const auto& token = syntax.tokens[index];
        if (token.source.comment || syntax.directive(token.source.begin)) continue;
        auto text  = syntax.text(index);
        auto next  = syntax.next_token(index);
        bool macro = false;
        for (const auto& name : syntax.macros)
            if (name == text) {
                macro = true;
                break;
            }
        if (! macro && syntax.is(next, "("_str)) {
            bool upper = false;
            macro      = true;
            for (auto value : text.as_bytes()) {
                if (value >= u8('a') && value <= u8('z')) {
                    macro = false;
                    break;
                }
                if (value >= u8('A') && value <= u8('Z')) upper = true;
            }
            macro = macro && upper;
        }
        if (macro) {
            auto end = token.source.end;
            if (syntax.is(next, "("_str) && syntax.tokens[next].syntax.pair != usize::MAX)
                end = syntax.tokens[syntax.tokens[next].syntax.pair].source.end;
            syntax.protected_ranges.push({ token.source.begin, end });
        }
        auto control_index = next;
        if (syntax.is(control_index, "constexpr"_str))
            control_index = syntax.next_token(control_index);
        if (control(text) && syntax.is(control_index, "("_str) &&
            syntax.tokens[control_index].syntax.pair != usize::MAX) {
            auto after = syntax.next_token(syntax.tokens[control_index].syntax.pair);
            if (! syntax.is(after, "{"_str)) {
                auto end = after;
                while (end < syntax.tokens.len() && ! syntax.is(end, ";"_str)) ++end;
                if (end < syntax.tokens.len())
                    syntax.protected_ranges.push(
                        { token.source.begin, syntax.tokens[end].source.end });
            }
        }
        if (text == "case"_str || text == "default"_str || text == "public"_str ||
            text == "private"_str || text == "protected"_str || text == "else"_str) {
            if (text == "else"_str && (syntax.is(next, "{"_str) || syntax.is(next, "if"_str)))
                continue;
            auto end = syntax.line_end(token.source.end);
            syntax.protected_ranges.push({ syntax.line_begin(token.source.begin), end });
        }
    }
    normalize_ranges(syntax.protected_ranges);
}

auto annotate_module_statements(FormatSyntax& syntax) -> void {
    unsigned parens {};
    auto     module_begin = usize::MAX;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token                 = syntax.tokens[index];
        token.syntax.protected_text = syntax.overlaps(token.source.begin, token.source.end);
        auto text                   = syntax.text(index);
        if (! token.source.comment && ! token.syntax.protected_text) {
            if ((text == ")"_str || text == "]"_str) && parens != 0) --parens;
        }
        if (token.source.comment || token.syntax.protected_text) continue;
        if (text == "("_str || text == "["_str) ++parens;
        if ((text == "module"_str || text == "import"_str) && parens == 0) module_begin = index;
        if (module_begin != usize::MAX) token.syntax.module_statement = true;
        if (text == ";"_str || text == "{"_str || text == "}"_str) module_begin = usize::MAX;
    }
}

auto parse_source(const lexical::SourceFile& source) -> lexical::Result<FormatSyntax> {
    if (rstd::str_::from_utf8(source.contents()).is_err())
        return Err(lexical::Error::make("format source is not UTF-8"_str));
    auto sink  = FormatSink(source.snapshot.clone());
    auto lexed = lexical::lex_into(source, sink);
    if (lexed.is_err()) return Err(rstd::move(lexed).unwrap_err());
    auto syntax = sink.finish();
    collect_protection(syntax);
    pair_tokens(syntax);
    if (! syntax.preserve_all) {
        protect_constructs(syntax);
        classify_blocks(syntax);
        normalize_ranges(syntax.protected_ranges);
        annotate_roles(syntax);
        annotate_module_statements(syntax);
    }
    return Ok(rstd::move(syntax));
}

} // namespace lito::frontend::format
