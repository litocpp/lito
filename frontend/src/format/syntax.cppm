export module lito.frontend.format:syntax;

import rstd;
import lito.frontend.lexical;
export import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

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
                syntax.locked_ranges.push({ *disabled, syntax.line_end(token.source.end) });
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
        syntax.directives.push(FormatDirective { .begin         = begin,
                                                 .end           = end,
                                                 .kind          = head->kind,
                                                 .marker_index  = index,
                                                 .keyword_index = head->keyword_index,
                                                 .name_index    = head->name_index });
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
                syntax.locked_ranges.push({ *conditional, end });
                conditional = None();
            }
        } else if (head->kind == lexical::DirectiveKind::Define && head->name_index != usize::MAX) {
            syntax.macros.push(String::make(syntax.text(head->name_index)));
        }
    }
    if (disabled.is_some()) {
        syntax.protected_ranges.push({ *disabled, syntax.bytes().len() });
        syntax.locked_ranges.push({ *disabled, syntax.bytes().len() });
    }
    if (conditional.is_some()) syntax.preserve_all = true;
    for (auto index = usize {}; index + usize(1) < syntax.bytes().len(); ++index) {
        if (lexical::escaped_newline_length(syntax.bytes(), index) == usize {}) continue;
        auto begin = syntax.line_begin(index);
        auto end   = syntax.line_end(syntax.line_end(index));
        syntax.protected_ranges.push({ begin, end });
    }
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

auto add_group(FormatSyntax& syntax, GroupKind kind, usize open, usize close) -> usize {
    auto group = FormatGroup {
        .kind = kind, .open = open, .close = close, .scope_id = syntax.tokens[open].syntax.scope_id
    };
    auto begin = open + usize(1);
    for (auto index = begin; index < close; ++index) {
        const auto& token = syntax.tokens[index];
        if (token.source.comment) continue;
        if (syntax.is(index, ","_str)) {
            if (begin < index) group.items.push({ begin, index });
            group.separators.push(usize(index));
            begin = index + usize(1);
        } else if (token.syntax.pair != usize::MAX && token.syntax.pair > index)
            index = token.syntax.pair;
    }
    if (begin < close) group.items.push({ begin, close });
    auto result = syntax.groups.len();
    syntax.groups.push(rstd::move(group));
    syntax.tokens[open].syntax.group_id = result;
    return result;
}

auto builtin_type(ref<str> text) -> bool;

auto simple_atom(const FormatSyntax& syntax, usize index) -> bool {
    if (index >= syntax.tokens.len()) return false;
    auto kind = syntax.tokens[index].source.kind;
    return kind == lexical::TokenKind::Identifier || kind == lexical::TokenKind::PpNumber ||
           kind == lexical::TokenKind::StringLiteral ||
           kind == lexical::TokenKind::CharacterLiteral;
}

auto comparison_chain(const FormatSyntax& syntax, usize open, usize close, usize condition_end)
    -> bool {
    auto first   = syntax.next_token(open);
    auto logical = syntax.next_token(first);
    auto second  = syntax.next_token(logical);
    auto end     = syntax.next_token(second);
    auto right   = syntax.next_token(close);
    if (! simple_atom(syntax, syntax.previous_token(open)) || ! simple_atom(syntax, first) ||
        (! syntax.is(logical, "&&"_str) && ! syntax.is(logical, "||"_str)) ||
        ! simple_atom(syntax, second) || end != close || ! simple_atom(syntax, right))
        return false;
    auto tail = syntax.next_token(right);
    return tail == condition_end || syntax.is(tail, "&&"_str) || syntax.is(tail, "||"_str);
}

auto annotate_delimiters(FormatSyntax& syntax) -> void {
    Vec<usize> conditions;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        while (! conditions.is_empty() && conditions[conditions.len() - usize(1)] < index)
            (void)conditions.pop();
        auto& token = syntax.tokens[index];
        if (token.source.comment || syntax.overlaps(token.source.begin, token.source.end)) continue;
        auto previous = syntax.previous_token(index);
        if (syntax.is(index, "("_str) && token.syntax.pair != usize::MAX) {
            auto keyword = previous;
            if (syntax.is(keyword, "constexpr"_str)) keyword = syntax.previous_token(keyword);
            if (syntax.is(keyword, "if"_str) || syntax.is(keyword, "for"_str) ||
                syntax.is(keyword, "while"_str) || syntax.is(keyword, "switch"_str))
                conditions.push(usize(token.syntax.pair));
        }
        if (syntax.is(index, "["_str) && token.syntax.pair != usize::MAX) {
            auto close = token.syntax.pair;
            if (syntax.is(index + usize(1), "["_str) &&
                syntax.tokens[index + usize(1)].syntax.pair + usize(1) == close) {
                token.syntax.square_role                           = SquareRole::Attribute;
                syntax.tokens[index + usize(1)].syntax.square_role = SquareRole::Attribute;
                syntax.tokens[close].syntax.square_role            = SquareRole::Attribute;
                syntax.tokens[close].syntax.attribute_end          = true;
                (void)add_group(syntax, GroupKind::Attribute, index, close);
                index = close;
                continue;
            }
            auto after      = syntax.next_token(close);
            auto expression = previous == usize::MAX || syntax.is(previous, "="_str) ||
                              syntax.is(previous, "("_str) || syntax.is(previous, ","_str) ||
                              syntax.is(previous, "return"_str) || syntax.is(previous, "{"_str);
            auto capture = expression && (syntax.is(after, "("_str) || syntax.is(after, "{"_str) ||
                                          syntax.is(after, "mutable"_str) ||
                                          syntax.is(after, "->"_str) || syntax.is(after, "<"_str));
            token.syntax.square_role = capture                           ? SquareRole::Capture
                                       : syntax.is(previous, "auto"_str) ? SquareRole::Binding
                                                                         : SquareRole::Subscript;
            syntax.tokens[close].syntax.square_role = token.syntax.square_role;
            if (capture) (void)add_group(syntax, GroupKind::LambdaCapture, index, close);
        }
        if (! syntax.is(index, "<"_str) || previous == usize::MAX) continue;
        auto declaration = syntax.is(previous, "template"_str);
        auto candidate   = declaration ||
                           syntax.tokens[previous].source.kind == lexical::TokenKind::Identifier ||
                           syntax.is(previous, "]"_str);
        if (! candidate) continue;
        Vec<usize>                     opens;
        Vec<rstd::tuple<usize, usize>> pairs;
        opens.push(usize(index));
        auto end = usize::MAX;
        bool expression {};
        for (auto scan = index + usize(1); scan < syntax.tokens.len(); ++scan) {
            if (syntax.tokens[scan].source.comment) continue;
            if (syntax.is(scan, ";"_str) || syntax.is(scan, "{"_str) || syntax.is(scan, "}"_str))
                break;
            if (syntax.is(scan, "||"_str) || syntax.is(scan, "&&"_str) ||
                syntax.is(scan, "=="_str) || syntax.is(scan, "!="_str) || syntax.is(scan, "?"_str))
                expression = true;
            if ((syntax.is(scan, "("_str) || syntax.is(scan, "["_str)) &&
                syntax.tokens[scan].syntax.pair > scan &&
                syntax.tokens[scan].syntax.pair != usize::MAX) {
                scan = syntax.tokens[scan].syntax.pair;
                continue;
            }
            if (syntax.is(scan, "<"_str)) opens.push(usize(scan));
            if (syntax.is(scan, ">"_str) || syntax.is(scan, ">>"_str)) {
                auto count = syntax.is(scan, ">>"_str) ? 2u : 1u;
                while (count-- != 0 && ! opens.is_empty())
                    pairs.push({ opens.pop().unwrap(), usize(scan) });
                if (opens.is_empty()) {
                    end = scan;
                    break;
                }
            }
        }
        if (end == usize::MAX) continue;
        auto type_argument = false;
        for (auto scan = index + usize(1); scan < end; ++scan)
            if (syntax.is(scan, "class"_str) || syntax.is(scan, "typename"_str) ||
                syntax.is(scan, "auto"_str) || builtin_type(syntax.text(scan)))
                type_argument = true;
        if (! declaration && ! type_argument && expression && ! conditions.is_empty() &&
            end < conditions[conditions.len() - usize(1)] &&
            comparison_chain(syntax, index, end, conditions[conditions.len() - usize(1)])) {
            token.syntax.operator_role              = OperatorRole::Binary;
            syntax.tokens[end].syntax.operator_role = OperatorRole::Binary;
            continue;
        }
        if (expression || (! declaration && ! type_argument)) {
            syntax.protected_ranges.push({ token.source.begin, syntax.tokens[end].source.end });
            index = end;
            continue;
        }
        for (const auto& pair : pairs) {
            auto open = pair.template get<0>(), close = pair.template get<1>();
            syntax.tokens[open].syntax.template_open = true;
            syntax.tokens[open].syntax.pair          = close;
            ++syntax.tokens[close].syntax.template_closes;
            syntax.tokens[close].syntax.pair = open;
        }
        for (auto scan = index; scan <= end; ++scan)
            if (syntax.tokens[scan].syntax.template_open)
                (void)add_group(syntax,
                                scan == index && declaration ? GroupKind::TemplateParameters
                                                             : GroupKind::TemplateArguments,
                                scan,
                                syntax.tokens[scan].syntax.pair);
        if (declaration) syntax.tokens[end].syntax.template_declaration_end = true;
        index = end;
    }
}

auto horizontal(const FormatSyntax& syntax, usize begin, usize end) -> bool {
    for (auto offset = begin; offset < end; ++offset)
        if (syntax.bytes()[offset] != u8(' ') && syntax.bytes()[offset] != u8('\t')) return false;
    return true;
}

auto annotate_directives(FormatSyntax& syntax) -> void {
    for (auto& directive : syntax.directives) {
        for (const auto& locked : syntax.locked_ranges)
            if (locked.begin < directive.end && directive.begin < locked.end)
                directive.locked = true;
        const auto slot =
            [&](WhitespaceSlotKind kind, usize begin, usize end, usize line, bool nonempty) {
                auto safe = ! directive.locked && begin <= end && (! nonempty || begin != end) &&
                            horizontal(syntax, begin, end);
                directive.safe_slots.push({ kind, { begin, end }, line, safe });
            };
        const auto& marker  = syntax.tokens[directive.marker_index];
        auto        leading = directive.begin;
        if (leading == usize {} && marker.source.begin >= usize(3) &&
            syntax.bytes()[usize {}] == u8(0xef) && syntax.bytes()[usize(1)] == u8(0xbb) &&
            syntax.bytes()[usize(2)] == u8(0xbf))
            leading = usize(3);
        slot(WhitespaceSlotKind::Leading, leading, marker.source.begin, directive.begin, false);
        if (directive.keyword_index != usize::MAX)
            slot(WhitespaceSlotKind::AfterMarker,
                 marker.source.end,
                 syntax.tokens[directive.keyword_index].source.begin,
                 directive.begin,
                 false);
        if (directive.kind == lexical::DirectiveKind::Define &&
            directive.name_index != usize::MAX) {
            auto last = directive.name_index;
            auto next = last + usize(1);
            if (syntax.is(next, "("_str) &&
                syntax.tokens[last].source.end == syntax.tokens[next].source.begin) {
                directive.function_like = true;
                unsigned depth {};
                for (auto scan = next;
                     scan < syntax.tokens.len() && syntax.tokens[scan].source.begin < directive.end;
                     ++scan) {
                    if (syntax.tokens[scan].source.comment) continue;
                    if (syntax.is(scan, "("_str)) ++depth;
                    if (syntax.is(scan, ")"_str) && --depth == 0) {
                        last = scan;
                        break;
                    }
                }
            }
            directive.prefix_end = syntax.tokens[last].source.end;
            next                 = last + usize(1);
            if (next < syntax.tokens.len() && syntax.tokens[next].source.begin < directive.end) {
                directive.replacement_begin = syntax.tokens[next].source.begin;
                slot(WhitespaceSlotKind::MacroReplacement,
                     directive.prefix_end,
                     directive.replacement_begin,
                     directive.begin,
                     true);
            }
            for (auto scan = next;
                 scan < syntax.tokens.len() && syntax.tokens[scan].source.begin < directive.end;
                 ++scan) {
                if (syntax.is(scan, "#"_str)) directive.has_stringize = true;
                if (syntax.is(scan, "##"_str) || syntax.is(scan, "%:%:"_str))
                    directive.has_paste = true;
            }
        }
        for (auto offset = directive.begin; offset < directive.end; ++offset) {
            if (lexical::escaped_newline_length(syntax.bytes(), offset) == usize {}) continue;
            bool opaque {};
            for (const auto& token : syntax.tokens) {
                if (token.source.begin > offset) break;
                if (token.source.end > offset &&
                    (token.source.comment ||
                     token.source.kind == lexical::TokenKind::StringLiteral ||
                     token.source.kind == lexical::TokenKind::CharacterLiteral))
                    opaque = true;
            }
            if (opaque) continue;
            auto begin = offset;
            while (begin != directive.begin && (syntax.bytes()[begin - usize(1)] == u8(' ') ||
                                                syntax.bytes()[begin - usize(1)] == u8('\t')))
                --begin;
            slot(WhitespaceSlotKind::Continuation, begin, offset, syntax.line_begin(offset), true);
        }
    }
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

auto control_kind(ref<str> text) -> ControlKind {
    if (text == "if"_str) return ControlKind::If;
    if (text == "for"_str) return ControlKind::For;
    if (text == "while"_str) return ControlKind::While;
    if (text == "switch"_str) return ControlKind::Switch;
    if (text == "catch"_str) return ControlKind::Catch;
    if (text == "else"_str) return ControlKind::Else;
    if (text == "do"_str) return ControlKind::Do;
    if (text == "try"_str) return ControlKind::Try;
    return ControlKind::None;
}

auto record(BlockKind kind) -> bool {
    return kind == BlockKind::Class || kind == BlockKind::Struct || kind == BlockKind::Union;
}

auto contains_newline(ref<str> text) -> bool {
    for (auto byte : text.as_bytes())
        if (byte == u8('\r') || byte == u8('\n')) return true;
    return false;
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

auto assignment_operator(ref<str> text) -> bool {
    return text == "="_str || text == "+="_str || text == "-="_str || text == "*="_str ||
           text == "/="_str || text == "%="_str || text == "&="_str || text == "^="_str ||
           text == "|="_str || text == "<<="_str || text == ">>="_str;
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
        token.syntax.brace_continuation = text == "else"_str || text == "catch"_str;
        token.syntax.else_continuation  = text == "else"_str;
    }
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (assignment_operator(syntax.text(index)))
            token.syntax.operator_role = OperatorRole::Assignment;
        else if (binary_operator(syntax, index))
            token.syntax.operator_role = OperatorRole::Binary;
        if (syntax.is(index, "->"_str)) {
            token.syntax.arrow_role = index != usize {} && syntax.is(index - usize(1), ")"_str)
                                          ? ArrowRole::TrailingReturn
                                          : ArrowRole::Member;
        }
        if (! syntax.is(index, "("_str) || token.syntax.pair == usize::MAX) continue;
        auto before = syntax.previous_token(index);
        auto role   = ParenRole::Unknown;
        auto text   = before == usize::MAX ? ""_str : syntax.text(before);
        if (control(text) || text == "constexpr"_str)
            role = ParenRole::Control;
        else if (before != usize::MAX && syntax.tokens[before].syntax.expression_prefix)
            role = ParenRole::Expression;
        else if ((before != usize::MAX &&
                  syntax.tokens[before].source.kind == lexical::TokenKind::Identifier) ||
                 text == ")"_str || text == "]"_str)
            role = ParenRole::Call;
        token.syntax.paren_role = role;
        auto close              = token.syntax.pair;
        token.syntax.paren_spacing =
            role == ParenRole::Control ? ParenSpacing::Conditional : ParenSpacing::Other;
        syntax.tokens[close].syntax.paren_spacing = token.syntax.paren_spacing;
        auto inner                                = index + usize(1);
        if (syntax.is(inner, "("_str) && syntax.tokens[inner].syntax.pair != usize::MAX &&
            syntax.tokens[inner].syntax.pair + usize(1) == close) {
            token.syntax.double_paren                = true;
            syntax.tokens[close].syntax.double_paren = true;
        }
    }
}

auto annotate_simple_blocks(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& open = syntax.tokens[index];
        if (! syntax.is(index, "{"_str) ||
            (open.syntax.block != BlockKind::Function && open.syntax.block != BlockKind::Control) ||
            open.syntax.pair == usize::MAX)
            continue;
        auto close = open.syntax.pair;
        auto begin = open.syntax.header_begin == usize::MAX ? index : open.syntax.header_begin;
        auto simple =
            ! syntax.overlaps(syntax.tokens[begin].source.begin, syntax.tokens[close].source.end);
        for (auto scan = begin; simple && scan < index; ++scan)
            if (syntax.tokens[scan].source.comment) simple = false;
        unsigned statements {};
        unsigned parens {};
        for (auto scan = index + usize(1); simple && scan < close; ++scan) {
            const auto& token = syntax.tokens[scan];
            auto        text  = syntax.text(scan);
            if (token.source.comment || token.syntax.protected_text || text == "{"_str ||
                text == "}"_str || text == ":"_str || control(text) || text == "else"_str ||
                text == "do"_str || text == "try"_str)
                simple = false;
            if (text == "("_str || text == "["_str) ++parens;
            if ((text == ")"_str || text == "]"_str) && parens != 0) --parens;
            if (text == ";"_str && parens == 0) ++statements;
        }
        if (close != index + usize(1) && ! syntax.is(close - usize(1), ";"_str)) simple = false;
        open.syntax.simple_block                     = simple;
        open.syntax.block_statements                 = statements;
        syntax.tokens[close].syntax.simple_block     = simple;
        syntax.tokens[close].syntax.block_statements = statements;
    }
}

auto builtin_type(ref<str> text) -> bool {
    return text == "void"_str || text == "bool"_str || text == "char"_str ||
           text == "char8_t"_str || text == "char16_t"_str || text == "char32_t"_str ||
           text == "wchar_t"_str || text == "short"_str || text == "int"_str ||
           text == "long"_str || text == "float"_str || text == "double"_str ||
           text == "signed"_str || text == "unsigned"_str || text == "__int128"_str;
}

auto annotate_casts(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (! syntax.is(index, "("_str) || token.syntax.pair == usize::MAX ||
            token.syntax.paren_role == ParenRole::Control ||
            token.syntax.paren_role == ParenRole::Call)
            continue;
        auto close = token.syntax.pair;
        auto after = syntax.next_token(close);
        if (after == syntax.tokens.len()) continue;
        auto following = syntax.text(after);
        auto word      = syntax.tokens[after].syntax.word_like && following != "const"_str &&
                         following != "noexcept"_str && following != "override"_str &&
                         following != "final"_str;
        auto unary     = following == "*"_str || following == "&"_str || following == "+"_str ||
                         following == "-"_str || following == "!"_str || following == "~"_str;
        if (! word && ! unary && following != "("_str) continue;
        auto     simple = index + usize(1) < close;
        bool     builtin {};
        unsigned names {};
        for (auto scan = index + usize(1); scan < close; ++scan) {
            const auto& item = syntax.tokens[scan];
            if (item.source.comment) continue;
            auto text = syntax.text(scan);
            if (text == "const"_str || text == "volatile"_str) continue;
            if (builtin_type(text)) {
                builtin = true;
                continue;
            }
            if (text == "*"_str || text == "&"_str || text == "&&"_str || text == "::"_str)
                continue;
            if (item.source.kind == lexical::TokenKind::Identifier && text != "this"_str &&
                text != "true"_str && text != "false"_str && text != "nullptr"_str) {
                auto previous = syntax.previous_token(scan);
                if (! syntax.is(previous, "::"_str)) ++names;
            } else
                simple = false;
        }
        simple       = simple && (builtin || names != 0) && names <= 1 && ! (builtin && names != 0);
        auto cast    = simple && (word || builtin);
        auto spacing = cast ? ParenSpacing::Cast : ParenSpacing::Unknown;
        token.syntax.paren_spacing                = spacing;
        syntax.tokens[close].syntax.paren_spacing = spacing;
        if (cast) syntax.tokens[after].syntax.cast_operand = true;
        if (! cast)
            syntax.protected_ranges.push({ token.source.begin, syntax.tokens[close].source.end });
    }
}

auto classify_blocks(FormatSyntax& syntax) -> void {
    auto       head = usize {};
    Vec<usize> scopes;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (token.source.comment || syntax.overlaps(token.source.begin, token.source.end)) continue;
        auto text             = syntax.text(index);
        auto next             = syntax.next_token(index);
        auto owner            = scopes.is_empty() ? usize::MAX : scopes[scopes.len() - usize(1)];
        token.syntax.scope_id = owner;
        if ((text == "public"_str || text == "protected"_str || text == "private"_str) &&
            owner != usize::MAX && record(syntax.tokens[owner].syntax.block) &&
            syntax.is(next, ":"_str)) {
            token.syntax.access_label               = true;
            syntax.tokens[next].syntax.access_colon = true;
            syntax.tokens[next].syntax.access_end   = true;
            auto trivia                             = next + usize(1);
            while (trivia < syntax.tokens.len() && syntax.tokens[trivia].source.comment)
                syntax.tokens[trivia++].syntax.access_end = true;
            head  = next + usize(1);
            index = next;
            continue;
        }
        if ((text == "public"_str || text == "protected"_str || text == "private"_str) &&
            owner != usize::MAX && record(syntax.tokens[owner].syntax.block) && index == head) {
            syntax.protected_ranges.push(
                { syntax.line_begin(token.source.begin), syntax.line_end(token.source.end) });
            continue;
        }
        if (text == ","_str && owner != usize::MAX &&
            syntax.tokens[owner].syntax.block == BlockKind::Enum)
            token.syntax.enum_separator = true;
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
            if (text == "}"_str && owner != usize::MAX && syntax.tokens[owner].syntax.pair == index)
                (void)scopes.pop();
            head = index + usize(1);
            continue;
        }
        if (text != "{"_str || token.syntax.pair == usize::MAX) continue;
        auto kind              = BlockKind::None;
        auto function_paren    = usize::MAX;
        bool unsafe            = false;
        bool assignment        = false;
        bool namespace_seen    = false;
        auto record_seen       = BlockKind::None;
        auto control_seen      = ControlKind::None;
        auto ctor_colon        = usize::MAX;
        auto inheritance_colon = usize::MAX;
        bool lambda_seen {};
        for (auto scan = head; scan < index; ++scan) {
            if (syntax.tokens[scan].source.comment ||
                syntax.overlaps(syntax.tokens[scan].source.begin, syntax.tokens[scan].source.end))
                continue;
            auto value = syntax.text(scan);
            if (syntax.tokens[scan].syntax.template_open ||
                (value == "["_str &&
                 syntax.tokens[scan].syntax.square_role == SquareRole::Attribute) ||
                (value == "{"_str && syntax.tokens[scan].syntax.initializer_open)) {
                scan = syntax.tokens[scan].syntax.pair;
                continue;
            }
            if (value == "["_str && syntax.tokens[scan].syntax.square_role == SquareRole::Capture) {
                lambda_seen = true;
                scan        = syntax.tokens[scan].syntax.pair;
                continue;
            }
            if (value == "namespace"_str) namespace_seen = true;
            if (value == "enum"_str)
                record_seen = BlockKind::Enum;
            else if (record_seen != BlockKind::Enum) {
                if (value == "class"_str) record_seen = BlockKind::Class;
                if (value == "struct"_str) record_seen = BlockKind::Struct;
                if (value == "union"_str) record_seen = BlockKind::Union;
            }
            if (value == "requires"_str) unsafe = true;
            if (value == "="_str) assignment = true;
            if (value == "("_str && syntax.tokens[scan].syntax.pair < index) {
                if (function_paren == usize::MAX) function_paren = scan;
                scan = syntax.tokens[scan].syntax.pair;
            } else if (value == ":"_str) {
                if (function_paren != usize::MAX)
                    ctor_colon = scan;
                else if (record_seen != BlockKind::None)
                    inheritance_colon = scan;
            }
        }
        if (! unsafe && lambda_seen)
            kind = BlockKind::Lambda;
        else if (! unsafe && ! assignment) {
            if (namespace_seen)
                kind = BlockKind::Namespace;
            else if (record_seen != BlockKind::None)
                kind = record_seen;
            else if (function_paren != usize::MAX && function_paren != usize {}) {
                auto before = syntax.previous_token(function_paren);
                if (syntax.is(before, "constexpr"_str)) before = syntax.previous_token(before);
                if (before != usize::MAX && control(syntax.text(before))) {
                    kind         = BlockKind::Control;
                    control_seen = control_kind(syntax.text(before));
                } else if (before != usize::MAX &&
                           syntax.tokens[before].source.kind == lexical::TokenKind::Identifier)
                    kind = BlockKind::Function;
            } else if (auto before = syntax.previous_token(index); syntax.is(before, "else"_str) ||
                                                                   syntax.is(before, "do"_str) ||
                                                                   syntax.is(before, "try"_str)) {
                kind         = BlockKind::Control;
                control_seen = control_kind(syntax.text(before));
            } else if (index == head)
                kind = BlockKind::Control;
        }
        auto before = syntax.previous_token(index);
        auto initialized_member =
            ctor_colon != usize::MAX && before != usize::MAX &&
            (syntax.tokens[before].source.kind == lexical::TokenKind::Identifier ||
             syntax.tokens[before].syntax.template_closes != 0);
        if (initialized_member ||
            (kind == BlockKind::None && ! unsafe && before != usize::MAX &&
             (syntax.tokens[before].source.kind == lexical::TokenKind::Identifier ||
              syntax.is(before, "="_str) || syntax.tokens[before].syntax.template_closes != 0 ||
              syntax.is(before, "]"_str)))) {
            token.syntax.block                                        = BlockKind::Initializer;
            token.syntax.initializer_open                             = true;
            syntax.tokens[token.syntax.pair].syntax.block             = BlockKind::Initializer;
            syntax.tokens[token.syntax.pair].syntax.initializer_close = true;
            (void)add_group(syntax, GroupKind::Initializer, index, token.syntax.pair);
            index = token.syntax.pair;
            continue;
        }
        if (kind == BlockKind::None) {
            syntax.protected_ranges.push(
                { token.source.begin, syntax.tokens[token.syntax.pair].source.end });
            index = token.syntax.pair;
            continue;
        }
        auto header = head;
        while (header < index && (syntax.tokens[header].source.comment ||
                                  syntax.directive(syntax.tokens[header].source.begin) ||
                                  syntax.overlaps(syntax.tokens[header].source.begin,
                                                  syntax.tokens[header].source.end)))
            ++header;
        token.syntax.block               = kind;
        token.syntax.header_begin        = header;
        token.syntax.control             = control_seen;
        token.syntax.inline_record_scope = kind == BlockKind::Function && owner != usize::MAX &&
                                           record(syntax.tokens[owner].syntax.block);
        if (ctor_colon != usize::MAX && kind == BlockKind::Function) {
            syntax.tokens[ctor_colon].syntax.colon_role = ColonRole::Constructor;
            (void)add_group(syntax, GroupKind::ConstructorInitializers, ctor_colon, index);
        }
        if (inheritance_colon != usize::MAX && record(kind)) {
            syntax.tokens[inheritance_colon].syntax.colon_role = ColonRole::Inheritance;
            (void)add_group(syntax, GroupKind::Inheritance, inheritance_colon, index);
        }
        auto last_header = syntax.previous_token(index);
        token.syntax.header_multiline =
            last_header != usize::MAX && header <= last_header &&
            contains_newline(syntax.text(syntax.tokens[header].source.begin,
                                         syntax.tokens[last_header].source.end));
        syntax.tokens[token.syntax.pair].syntax.block            = kind;
        syntax.tokens[token.syntax.pair].syntax.header_begin     = header;
        syntax.tokens[token.syntax.pair].syntax.header_multiline = token.syntax.header_multiline;
        syntax.tokens[token.syntax.pair].syntax.control          = control_seen;
        scopes.push(usize(index));
        head = index + usize(1);
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

auto annotate_lambda_blocks(FormatSyntax& syntax) -> void {
    for (const auto& group : syntax.groups) {
        if (group.kind != GroupKind::LambdaCapture) continue;
        for (auto index = group.close + usize(1); index < syntax.tokens.len(); ++index) {
            if (syntax.tokens[index].source.comment) continue;
            if (syntax.is(index, ";"_str) || syntax.is(index, "requires"_str)) break;
            if (syntax.is(index, "{"_str)) {
                auto close = syntax.tokens[index].syntax.pair;
                if (close == usize::MAX) break;
                syntax.tokens[index].syntax.block        = BlockKind::Lambda;
                syntax.tokens[index].syntax.header_begin = group.open;
                syntax.tokens[close].syntax.block        = BlockKind::Lambda;
                syntax.tokens[close].syntax.header_begin = group.open;
                break;
            }
            auto pair = syntax.tokens[index].syntax.pair;
            if (pair != usize::MAX && pair > index) index = pair;
        }
    }
}

auto annotate_scopes(FormatSyntax& syntax) -> void {
    Vec<usize> scopes;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token           = syntax.tokens[index];
        auto  scope           = scopes.is_empty() ? usize::MAX : scopes[scopes.len() - usize(1)];
        token.syntax.scope_id = scope;
        if (token.source.comment || token.syntax.protected_text) continue;
        if (syntax.is(index, "{"_str) && token.syntax.block != BlockKind::None &&
            token.syntax.block != BlockKind::Initializer) {
            token.syntax.inline_record_scope = token.syntax.block == BlockKind::Function &&
                                               scope != usize::MAX &&
                                               record(syntax.tokens[scope].syntax.block);
            scopes.push(usize(index));
        }
        if (syntax.is(index, "}"_str) && ! scopes.is_empty() &&
            syntax.tokens[scopes[scopes.len() - usize(1)]].syntax.pair == index)
            (void)scopes.pop();
    }
    for (auto& group : syntax.groups) group.scope_id = syntax.tokens[group.open].syntax.scope_id;
}

auto annotate_initializer_groups(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        if (! syntax.tokens[index].syntax.initializer_open) continue;
        auto close = syntax.tokens[index].syntax.pair;
        for (auto nested = index + usize(1); nested < close; ++nested) {
            auto& token = syntax.tokens[nested];
            if (! syntax.is(nested, "{"_str) || token.syntax.pair == usize::MAX ||
                token.syntax.block != BlockKind::None || token.syntax.protected_text)
                continue;
            auto end                                    = token.syntax.pair;
            token.syntax.block                          = BlockKind::Initializer;
            token.syntax.initializer_open               = true;
            syntax.tokens[end].syntax.block             = BlockKind::Initializer;
            syntax.tokens[end].syntax.initializer_close = true;
            (void)add_group(syntax, GroupKind::Initializer, nested, end);
        }
    }
}

auto storage_word(ref<str> text) -> bool {
    return text == "const"_str || text == "volatile"_str || text == "static"_str ||
           text == "inline"_str || text == "constexpr"_str || text == "consteval"_str ||
           text == "constinit"_str || text == "extern"_str || text == "mutable"_str ||
           text == "virtual"_str || text == "friend"_str || text == "typename"_str;
}

auto declaration_facts(FormatSyntax&    syntax,
                       FormatStatement& statement,
                       usize            end,
                       bool             parameter = false) -> void {
    auto begin = statement.extent.begin;
    while (begin < end && syntax.tokens[begin].source.comment) ++begin;
    if (begin == end) return;
    if (statement.kind == StatementKind::Return || syntax.is(begin, "throw"_str)) return;
    auto first    = begin;
    auto boundary = end;
    auto paren    = usize::MAX;
    auto name     = usize::MAX;
    bool valid = true, explicit_type = false, pointer = false;
    for (auto index = begin; index < end; ++index) {
        auto& token = syntax.tokens[index];
        if (token.source.comment) continue;
        auto text = syntax.text(index);
        if (token.syntax.template_open || token.syntax.square_role == SquareRole::Attribute) {
            if (token.syntax.pair != usize::MAX && token.syntax.pair > index)
                index = token.syntax.pair;
            continue;
        }
        if (text == "template"_str) {
            first = index + usize(1);
            continue;
        }
        if (token.syntax.operator_role == OperatorRole::Assignment) {
            statement.assignment_index    = index;
            statement.compound_assignment = text != "="_str;
            boundary                      = index;
            break;
        }
        if (text == "("_str) {
            paren    = index;
            boundary = index;
            break;
        }
        if (token.syntax.initializer_open || text == ";"_str) {
            boundary = index;
            break;
        }
        if (syntax.is(index, "["_str) && token.syntax.pair != usize::MAX) {
            index = token.syntax.pair;
            continue;
        }
        if (text == "*"_str || text == "&"_str || text == "&&"_str) {
            pointer = true;
            continue;
        }
        if (text == "::"_str || text == "~"_str) continue;
        if (builtin_type(text) || text == "auto"_str || storage_word(text)) explicit_type = true;
        if (token.source.kind == lexical::TokenKind::Identifier && ! storage_word(text) &&
            ! builtin_type(text) && text != "auto"_str)
            name = index;
        else if (token.source.kind != lexical::TokenKind::Identifier)
            valid = false;
    }
    auto declaration_scope =
        statement.scope_id == usize::MAX ||
        (statement.scope_id < syntax.tokens.len() &&
         (record(syntax.tokens[statement.scope_id].syntax.block) ||
          syntax.tokens[statement.scope_id].syntax.block == BlockKind::Namespace));
    auto confirmed = valid && name != usize::MAX && name > first &&
                     (! pointer || explicit_type || declaration_scope || parameter);
    auto function  = statement.kind == StatementKind::Function ||
                     statement.kind == StatementKind::Lambda ||
                     (confirmed && paren != usize::MAX && statement.assignment_index == usize::MAX);
    if (function && paren != usize::MAX) {
        statement.name_index      = name == usize::MAX ? syntax.previous_token(paren) : name;
        statement.type_end        = statement.name_index;
        statement.return_type_end = statement.name_index;
        auto group                = syntax.tokens[paren].syntax.group_id;
        if (group == usize::MAX)
            group =
                add_group(syntax, GroupKind::Parameters, paren, syntax.tokens[paren].syntax.pair);
        else
            syntax.groups[group].kind = GroupKind::Parameters;
        statement.parameters_group = group;
        if (statement.kind != StatementKind::Lambda) statement.kind = StatementKind::Function;
    } else if (confirmed && statement.kind != StatementKind::Lambda) {
        statement.kind       = StatementKind::Declaration;
        statement.name_index = name;
        statement.type_end   = name;
    } else if (statement.assignment_index != usize::MAX &&
               statement.kind == StatementKind::Expression)
        statement.kind = StatementKind::Assignment;
    if (confirmed || parameter) {
        for (auto index = begin; index < boundary; ++index) {
            if (syntax.tokens[index].syntax.template_open) {
                index = syntax.tokens[index].syntax.pair;
                continue;
            }
            if (syntax.is(index, "*"_str) || syntax.is(index, "&"_str) ||
                syntax.is(index, "&&"_str))
                syntax.tokens[index].syntax.operator_role = OperatorRole::PointerReference;
        }
    }
}

auto statement_simple(const FormatSyntax& syntax, usize begin, usize end) -> bool {
    if (begin >= end) return false;
    for (auto index = begin; index < end; ++index) {
        const auto& token = syntax.tokens[index];
        if (token.source.comment || token.syntax.protected_text ||
            (syntax.is(index, "{"_str) && ! token.syntax.initializer_open) ||
            control(syntax.text(index)))
            return false;
    }
    return true;
}

auto parse_statement(FormatSyntax& syntax, usize begin, usize limit, unsigned depth) -> usize;
auto parse_statements(FormatSyntax& syntax, usize begin, usize end, unsigned depth) -> void {
    while (begin < end && depth < 128) {
        if (syntax.tokens[begin].source.comment ||
            syntax.directive(syntax.tokens[begin].source.begin)) {
            ++begin;
            continue;
        }
        if (syntax.tokens[begin].syntax.access_label) {
            auto colon = syntax.next_token(begin);
            begin      = syntax.next_token(colon);
            continue;
        }
        auto id   = parse_statement(syntax, begin, end, depth + 1);
        auto next = syntax.statements[id].extent.end;
        if (syntax.statements[id].kind == StatementKind::CaseLabel)
            next = syntax.statements[id].body_end;
        begin = next > begin ? next : begin + usize(1);
    }
}

auto parse_statement(FormatSyntax& syntax, usize begin, usize limit, unsigned depth) -> usize {
    auto id        = syntax.statements.len();
    auto scope     = syntax.tokens[begin].syntax.scope_id;
    auto kind      = control_kind(syntax.text(begin));
    auto statement = FormatStatement { .kind     = StatementKind::Expression,
                                       .extent   = { begin, begin + usize(1) },
                                       .scope_id = scope };
    syntax.statements.push(rstd::move(statement));
    if (kind != ControlKind::None && depth < 128) {
        auto header = syntax.next_token(begin);
        if (syntax.is(header, "constexpr"_str)) header = syntax.next_token(header);
        if (kind == ControlKind::For && syntax.is(header, "("_str)) {
            auto     close = syntax.tokens[header].syntax.pair;
            unsigned conditional {};
            for (auto index = header + usize(1); close != usize::MAX && index < close; ++index) {
                if (syntax.is(index, "?"_str))
                    ++conditional;
                else if (syntax.is(index, ":"_str)) {
                    if (conditional != 0)
                        --conditional;
                    else
                        syntax.tokens[index].syntax.colon_role = ColonRole::RangeFor;
                }
                auto pair = syntax.tokens[index].syntax.pair;
                if (pair != usize::MAX && pair > index) index = pair;
            }
        }
        auto body = kind == ControlKind::Else || kind == ControlKind::Do || kind == ControlKind::Try
                        ? header
                    : syntax.is(header, "("_str) && syntax.tokens[header].syntax.pair != usize::MAX
                        ? syntax.next_token(syntax.tokens[header].syntax.pair)
                        : limit;
        if (body < limit) {
            syntax.statements[id].kind    = StatementKind::Control;
            syntax.statements[id].control = kind;
            auto end                      = body;
            if (syntax.is(body, "{"_str) && syntax.tokens[body].syntax.pair != usize::MAX) {
                auto close                       = syntax.tokens[body].syntax.pair;
                syntax.statements[id].body_open  = body;
                syntax.statements[id].body_begin = body + usize(1);
                syntax.statements[id].body_end   = close;
                end                              = close + usize(1);
                parse_statements(syntax, body + usize(1), close, depth + 1);
            } else {
                auto child                       = parse_statement(syntax, body, limit, depth + 1);
                end                              = syntax.statements[child].extent.end;
                syntax.statements[id].body_begin = body;
                syntax.statements[id].body_end   = end;
                syntax.statements[id].simple     = syntax.statements[child].simple;
            }
            auto following = end;
            while (following < limit && syntax.tokens[following].source.comment) ++following;
            if (kind == ControlKind::If && syntax.is(following, "else"_str)) {
                auto otherwise = parse_statement(syntax, following, limit, depth + 1);
                syntax.statements[id].else_statement = otherwise;
                syntax.statements[id].has_else       = true;
                end                                  = syntax.statements[otherwise].extent.end;
            }
            syntax.statements[id].extent.end = end;
            for (auto index = begin; index < end; ++index)
                if (syntax.tokens[index].syntax.statement_id == usize::MAX)
                    syntax.tokens[index].syntax.statement_id = id;
            return id;
        }
    }
    if (syntax.is(begin, "case"_str) || syntax.is(begin, "default"_str)) {
        auto     colon = begin + usize(1);
        unsigned questions {};
        while (colon < limit) {
            if (syntax.is(colon, "?"_str)) ++questions;
            if (syntax.is(colon, ":"_str)) {
                if (questions == 0) break;
                --questions;
            }
            auto pair = syntax.tokens[colon].syntax.pair;
            if (pair != usize::MAX && pair > colon) colon = pair;
            ++colon;
        }
        if (colon < limit) {
            syntax.tokens[colon].syntax.colon_role = ColonRole::Case;
            auto end                               = colon + usize(1);
            while (end < limit && ! syntax.is(end, "case"_str) && ! syntax.is(end, "default"_str)) {
                auto pair = syntax.tokens[end].syntax.pair;
                if (pair != usize::MAX && pair > end) end = pair;
                ++end;
            }
            syntax.statements[id].kind       = StatementKind::CaseLabel;
            syntax.statements[id].extent.end = colon + usize(1);
            syntax.statements[id].body_begin = colon + usize(1);
            syntax.statements[id].body_end   = end;
            syntax.statements[id].simple     = statement_simple(syntax, colon + usize(1), end);
            for (auto index = begin; index <= colon; ++index)
                syntax.tokens[index].syntax.statement_id = id;
            parse_statements(syntax, colon + usize(1), end, depth + 1);
            return id;
        }
    }
    auto end  = begin;
    auto body = usize::MAX;
    while (end < limit) {
        auto& token = syntax.tokens[end];
        if (syntax.is(end, ";"_str)) {
            ++end;
            break;
        }
        if (syntax.is(end, "{"_str) && token.syntax.block != BlockKind::None &&
            token.syntax.block != BlockKind::Initializer) {
            body = end;
            end  = token.syntax.pair + usize(1);
            break;
        }
        auto pair = token.syntax.pair;
        if (pair != usize::MAX && pair > end) end = pair;
        ++end;
    }
    if (end > limit) end = limit;
    syntax.statements[id].extent.end = end;
    if (syntax.is(begin, "return"_str) || syntax.is(begin, "co_return"_str))
        syntax.statements[id].kind = StatementKind::Return;
    if (body != usize::MAX) {
        auto block                       = syntax.tokens[body].syntax.block;
        syntax.statements[id].kind       = block == BlockKind::Function ? StatementKind::Function
                                           : block == BlockKind::Lambda ? StatementKind::Lambda
                                                                        : StatementKind::Unknown;
        syntax.statements[id].body_open  = body;
        syntax.statements[id].body_begin = body + usize(1);
        syntax.statements[id].body_end   = syntax.tokens[body].syntax.pair;
        syntax.statements[id].inline_record_scope = syntax.tokens[body].syntax.inline_record_scope;
    }
    declaration_facts(syntax, syntax.statements[id], body == usize::MAX ? end : body);
    syntax.statements[id].simple = body == usize::MAX && statement_simple(syntax, begin, end);
    auto header_end              = body == usize::MAX ? end : body + usize(1);
    for (auto index = begin; index < header_end; ++index)
        if (syntax.tokens[index].syntax.statement_id == usize::MAX)
            syntax.tokens[index].syntax.statement_id = id;
    if (body != usize::MAX) {
        syntax.tokens[syntax.tokens[body].syntax.pair].syntax.statement_id = id;
        parse_statements(syntax, body + usize(1), syntax.tokens[body].syntax.pair, depth + 1);
    }
    return id;
}

auto annotate_nested_lambdas(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (! syntax.is(index, "{"_str) || token.syntax.block != BlockKind::Lambda) continue;
        auto current = token.syntax.statement_id;
        if (current != usize::MAX && syntax.statements[current].kind == StatementKind::Lambda &&
            syntax.statements[current].body_open == index)
            continue;
        auto begin = token.syntax.header_begin;
        auto close = token.syntax.pair;
        if (begin == usize::MAX || close == usize::MAX) continue;
        auto id = syntax.statements.len();
        syntax.statements.push(FormatStatement { .kind       = StatementKind::Lambda,
                                                 .extent     = { begin, close + usize(1) },
                                                 .scope_id   = token.syntax.scope_id,
                                                 .body_begin = index + usize(1),
                                                 .body_open  = index,
                                                 .body_end   = close });
        declaration_facts(syntax, syntax.statements[id], index);
        for (auto header = begin; header <= index; ++header)
            syntax.tokens[header].syntax.statement_id = id;
        syntax.tokens[close].syntax.statement_id = id;
        parse_statements(syntax, index + usize(1), close, 0);
    }
}

auto annotate_groups_and_expressions(FormatSyntax& syntax) -> void {
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (token.source.comment || token.syntax.protected_text) continue;
        if (syntax.is(index, "("_str) && token.syntax.pair != usize::MAX &&
            token.syntax.group_id == usize::MAX && token.syntax.paren_role == ParenRole::Call)
            (void)add_group(syntax, GroupKind::Arguments, index, token.syntax.pair);
        if (syntax.is(index, "!"_str) &&
            ! syntax.is(syntax.previous_token(index), "operator"_str)) {
            token.syntax.logical_not   = true;
            token.syntax.operator_role = OperatorRole::Unary;
        }
    }
    auto count = syntax.groups.len();
    for (auto group_id = usize {}; group_id < count; ++group_id) {
        if (syntax.groups[group_id].kind != GroupKind::LambdaCapture) continue;
        auto open = syntax.next_token(syntax.groups[group_id].close);
        if (! syntax.is(open, "("_str) || syntax.tokens[open].syntax.pair == usize::MAX) continue;
        auto group = syntax.tokens[open].syntax.group_id;
        if (group == usize::MAX)
            (void)add_group(syntax, GroupKind::Parameters, open, syntax.tokens[open].syntax.pair);
        else
            syntax.groups[group].kind = GroupKind::Parameters;
    }
    count = syntax.groups.len();
    for (auto group_id = usize {}; group_id < count; ++group_id) {
        if (syntax.groups[group_id].kind != GroupKind::Parameters) continue;
        auto item_count = syntax.groups[group_id].items.len();
        for (auto item_id = usize {}; item_id < item_count; ++item_id) {
            auto item      = syntax.groups[group_id].items[item_id];
            auto parameter = FormatStatement { .extent   = { item.begin, item.end },
                                               .scope_id = syntax.groups[group_id].scope_id };
            declaration_facts(syntax, parameter, item.end, true);
        }
    }
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& token = syntax.tokens[index];
        if (token.source.comment || token.syntax.protected_text ||
            token.syntax.operator_role != OperatorRole::Unknown ||
            (syntax.text(index) != "*"_str && syntax.text(index) != "&"_str &&
             syntax.text(index) != "&&"_str))
            continue;
        auto previous = syntax.previous_token(index);
        auto next     = syntax.next_token(index);
        if (previous == usize::MAX || next == syntax.tokens.len()) continue;
        auto id         = token.syntax.statement_id;
        auto expression = id < syntax.statements.len() &&
                          (syntax.statements[id].kind == StatementKind::Return ||
                           syntax.statements[id].kind == StatementKind::Assignment ||
                           syntax.statements[id].kind == StatementKind::Control ||
                           (syntax.statements[id].assignment_index != usize::MAX &&
                            index > syntax.statements[id].assignment_index));
        auto scope      = token.syntax.scope_id;
        expression |=
            scope < syntax.tokens.len() && syntax.tokens[scope].syntax.block == BlockKind::Control;
        if (! expression) continue;
        auto left_operand  = (syntax.tokens[previous].syntax.word_like &&
                              ! syntax.tokens[previous].syntax.expression_prefix) ||
                             syntax.is(previous, ")"_str) || syntax.is(previous, "]"_str);
        auto right_operand = syntax.tokens[next].syntax.word_like || syntax.is(next, "("_str);
        token.syntax.operator_role =
            left_operand && right_operand ? OperatorRole::Binary : OperatorRole::Unary;
    }
    for (const auto& statement : syntax.statements) {
        if (! statement.simple) continue;
        auto chain =
            ExpressionChain { .extent        = statement.extent,
                              .scope_id      = statement.scope_id,
                              .first_operand = statement.assignment_index == usize::MAX
                                                   ? statement.extent.begin
                                                   : statement.assignment_index + usize(1) };
        if (statement.kind == StatementKind::Return)
            chain.first_operand = syntax.next_token(statement.extent.begin);
        for (auto index = chain.first_operand; index < chain.extent.end; ++index) {
            if (syntax.tokens[index].syntax.operator_role == OperatorRole::Binary)
                chain.operators.push(usize(index));
            auto pair = syntax.tokens[index].syntax.pair;
            if (pair != usize::MAX && pair > index) index = pair;
        }
        if (! chain.operators.is_empty()) syntax.expressions.push(rstd::move(chain));
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
        annotate_delimiters(syntax);
        annotate_lambda_blocks(syntax);
        classify_blocks(syntax);
        normalize_ranges(syntax.protected_ranges);
        annotate_roles(syntax);
        annotate_casts(syntax);
        normalize_ranges(syntax.protected_ranges);
        annotate_module_statements(syntax);
        annotate_initializer_groups(syntax);
        annotate_scopes(syntax);
        annotate_simple_blocks(syntax);
        parse_statements(syntax, usize {}, syntax.tokens.len(), 0);
        annotate_nested_lambdas(syntax);
        annotate_groups_and_expressions(syntax);
    }
    annotate_directives(syntax);
    return Ok(rstd::move(syntax));
}

} // namespace lito::frontend::format
