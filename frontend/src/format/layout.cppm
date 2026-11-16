export module lito.frontend.format:layout;

import rstd;
import lito.frontend.lexical;
import :syntax;
import :style;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

struct TokenLayout {
    usize    columns {};
    unsigned parens {};
};

auto wrap_before(const TokenSyntax& token, const BraceWrapping& wrapping) -> bool {
    if (token.block == BlockKind::Function) return wrapping.after_function;
    if (token.block == BlockKind::Class) return wrapping.after_class;
    if (token.block == BlockKind::Struct) return wrapping.after_struct;
    if (token.block == BlockKind::Union) return wrapping.after_union;
    if (token.block == BlockKind::Enum) return wrapping.after_enum;
    if (token.block == BlockKind::Namespace) return wrapping.after_namespace;
    if (token.block == BlockKind::Control)
        return wrapping.after_control_statement == ControlBraceStyle::Always ||
               (wrapping.after_control_statement == ControlBraceStyle::MultiLine &&
                token.header_multiline);
    return false;
}

class LayoutCursor {
    TokenLayout        next_;
    unsigned           namespaces_ {};
    const FormatStyle& style_;
    BraceWrapping      wrapping_;

    auto block_width(const TokenSyntax& token) const -> usize {
        auto levels = 1u;
        if (record(token.block) && style_.indent_access_modifiers) levels = 2;
        if (token.block == BlockKind::Control && wrapping_.indent_braces) ++levels;
        if (token.block == BlockKind::Namespace &&
            (style_.namespace_indentation == NamespaceIndentation::None ||
             (style_.namespace_indentation == NamespaceIndentation::Inner && namespaces_ == 0)))
            levels = 0;
        return usize(levels) * usize(style_.indent_width);
    }

public:
    explicit LayoutCursor(const FormatStyle& style)
        : style_(style), wrapping_(style.effective_brace_wrapping()) {}

    auto advance(const FormatSyntax& syntax, usize index) -> TokenLayout {
        const auto& token = syntax.tokens[index];
        if (token.source.comment || token.syntax.protected_text) return next_;
        auto text = syntax.text(index);
        if (text == "}"_str && token.syntax.block != BlockKind::None) {
            if (token.syntax.block == BlockKind::Namespace && namespaces_ != 0) --namespaces_;
            auto width = block_width(token.syntax);
            if (width <= next_.columns) next_.columns -= width;
        }
        if ((text == ")"_str || text == "]"_str) && next_.parens != 0) --next_.parens;
        auto current = next_;
        if ((text == "{"_str || text == "}"_str) && token.syntax.block == BlockKind::Control &&
            wrapping_.indent_braces)
            current.columns += usize(style_.indent_width);
        if (token.syntax.access_label) {
            if (style_.indent_access_modifiers) {
                if (current.columns >= usize(style_.indent_width))
                    current.columns -= usize(style_.indent_width);
            } else if (style_.access_modifier_offset >= 0)
                current.columns += usize(static_cast<rstd::size_t>(style_.access_modifier_offset));
            else {
                auto distance = usize(static_cast<rstd::size_t>(
                    -static_cast<rstd::int64_t>(style_.access_modifier_offset)));
                if (distance <= current.columns) current.columns -= distance;
            }
        }
        if (text == "{"_str && token.syntax.block != BlockKind::None) {
            next_.columns += block_width(token.syntax);
            if (token.syntax.block == BlockKind::Namespace) ++namespaces_;
        }
        if (text == "("_str || text == "["_str) ++next_.parens;
        return current;
    }
};

auto paren_space(ParenSpacing role, const SpacesInParensOptions& options) -> Option<unsigned> {
    if (role == ParenSpacing::Unknown) return None();
    auto spaced = role == ParenSpacing::Cast          ? options.in_c_style_casts
                  : role == ParenSpacing::Conditional ? options.in_conditional_statements
                                                      : options.other;
    return Some(spaced ? 1u : 0u);
}

auto gap_spaces(const FormatSyntax& syntax, usize index, const FormatStyle& style)
    -> Option<unsigned> {
    const auto& left  = syntax.tokens[index - usize(1)];
    const auto& right = syntax.tokens[index];
    auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
    if (right.syntax.access_colon || right.syntax.cast_operand) return Some(0u);
    if (! left.source.comment && ! right.source.comment &&
        left.source.kind == lexical::TokenKind::Punctuation &&
        right.source.kind == lexical::TokenKind::Punctuation) {
        auto structural = a == "("_str || (a == "["_str && b != "["_str) || b == ")"_str ||
                          b == "]"_str || b == ";"_str || b == ","_str || a == ","_str ||
                          a == ";"_str || right.syntax.arrow_role == ArrowRole::TrailingReturn ||
                          (right.syntax.block != BlockKind::None && b == "{"_str) ||
                          (b == "("_str && right.syntax.paren_role != ParenRole::Unknown) ||
                          left.syntax.operator_role == OperatorRole::Assignment ||
                          (right.syntax.operator_role == OperatorRole::Assignment &&
                           (a == ")"_str || a == "]"_str));
        if (! structural) return None();
    }
    if (left.source.end == right.source.begin &&
        ((left.source.kind == lexical::TokenKind::StringLiteral ||
          left.source.kind == lexical::TokenKind::CharacterLiteral ||
          left.source.kind == lexical::TokenKind::PpNumber) &&
         right.source.kind == lexical::TokenKind::Identifier))
        return None();
    if (left.source.kind == lexical::TokenKind::PpNumber && b == "."_str) return None();
    if (left.source.comment || right.source.comment) return Some(1u);
    auto parens = style.effective_parens_options();
    if (a == "("_str && b == ")"_str && left.syntax.pair == index)
        return Some(parens.in_empty_parentheses ? 1u : 0u);
    if (parens.except_double_parentheses &&
        ((a == "("_str && b == "("_str && left.syntax.double_paren) ||
         (a == ")"_str && b == ")"_str && right.syntax.double_paren)))
        return Some(0u);
    if (a == "("_str) return paren_space(left.syntax.paren_spacing, parens);
    if (b == ")"_str) return paren_space(right.syntax.paren_spacing, parens);
    if (a == ";"_str || a == ","_str) return Some(1u);
    if (b == ";"_str || b == ","_str || b == ")"_str || b == "]"_str) return Some(0u);
    if (a == "["_str && b != "["_str) return Some(0u);
    if (b == "("_str) {
        if (right.syntax.paren_role == ParenRole::Expression) return Some(1u);
        if (right.syntax.paren_role != ParenRole::Unknown)
            return Some(style.space_before_parens == SpaceBeforeParens::Always ||
                                (right.syntax.paren_role == ParenRole::Control &&
                                 style.space_before_parens == SpaceBeforeParens::ControlStatements)
                            ? 1u
                            : 0u);
    }
    if (a == "::"_str || b == "::"_str || a == "."_str || b == "."_str ||
        left.syntax.arrow_role != ArrowRole::None || right.syntax.arrow_role != ArrowRole::None) {
        if (left.syntax.arrow_role == ArrowRole::TrailingReturn ||
            right.syntax.arrow_role == ArrowRole::TrailingReturn)
            return Some(1u);
        return Some(0u);
    }
    if ((a == ":"_str || b == ":"_str) &&
        (left.syntax.module_statement || right.syntax.module_statement))
        return Some(a == "import"_str || a == "module"_str ? 1u : 0u);
    if (right.syntax.block != BlockKind::None && b == "{"_str) return Some(1u);
    if (a == "}"_str && right.syntax.block_continuation) return Some(1u);
    if (left.syntax.expression_prefix) return Some(1u);
    if (left.syntax.word_like && right.syntax.word_like) return Some(1u);
    if (right.syntax.operator_role == OperatorRole::Assignment)
        return Some(style.space_before_assignment_operators ? 1u : 0u);
    if (left.syntax.operator_role == OperatorRole::Assignment) return Some(1u);
    if (left.syntax.operator_role == OperatorRole::Binary ||
        right.syntax.operator_role == OperatorRole::Binary)
        return Some(1u);
    return None();
}

auto compact_block(const FormatSyntax& syntax, usize index, const FormatStyle& style) -> bool {
    const auto& token = syntax.tokens[index];
    if (! syntax.is(index, "{"_str) || ! token.syntax.simple_block ||
        token.syntax.block_statements > 1)
        return false;
    auto empty = token.syntax.pair == index + usize(1);
    if (token.syntax.block == BlockKind::Function)
        return style.short_functions == ShortFunctionStyle::All ||
               (empty && style.short_functions == ShortFunctionStyle::Empty);
    if (token.syntax.block == BlockKind::Control)
        return style.short_blocks == ShortBlockStyle::Always ||
               (empty && style.short_blocks == ShortBlockStyle::Empty);
    return false;
}

auto append_indent(String& output, usize columns, const FormatStyle& style) -> void {
    if (style.use_tab == UseTab::ForIndentation) {
        auto tabs = columns / usize(style.tab_width);
        for (auto count = usize {}; count < tabs; ++count) output.push_ascii('\t');
        columns = columns % usize(style.tab_width);
    }
    for (auto count = usize {}; count < columns; ++count) output.push_ascii(' ');
}

auto count_newlines(ref<str> text) -> unsigned {
    auto     bytes = text.as_bytes();
    unsigned count {};
    for (auto index = usize {}; index < bytes.len(); ++index) {
        if (bytes[index] == u8('\r')) {
            ++count;
            if (index + usize(1) < bytes.len() && bytes[index + usize(1)] == u8('\n')) ++index;
        } else if (bytes[index] == u8('\n'))
            ++count;
    }
    return count;
}

auto limited_newlines(unsigned count, const FormatStyle& style, bool separator) -> unsigned {
    auto limit = style.max_empty_lines_to_keep;
    if (limit == unsigned(-1)) return count;
    if (separator) ++limit;
    return count > limit ? limit : count;
}

auto append_newlines(String& output, unsigned count, ref<str> newline) -> void {
    for (unsigned index {}; index < count; ++index) output.push_str(newline);
}

auto layout_source(const FormatSyntax& syntax, const FormatStyle& style)
    -> lexical::Result<String> {
    if (style.tab_width == 0)
        return Err(lexical::Error::make("TabWidth must be greater than zero"_str));
    if (syntax.preserve_all || syntax.tokens.is_empty())
        return Ok(String::make(syntax.text(usize {}, syntax.bytes().len())));
    auto newline = "\n"_str;
    for (auto index = usize {}; index < syntax.bytes().len(); ++index) {
        if (syntax.bytes()[index] == u8('\r')) {
            newline = index + usize(1) < syntax.bytes().len() &&
                              syntax.bytes()[index + usize(1)] == u8('\n')
                          ? "\r\n"_str
                          : "\r"_str;
            break;
        }
        if (syntax.bytes()[index] == u8('\n')) break;
    }
    String       output;
    LayoutCursor cursor(style);
    auto         wrapping = style.effective_brace_wrapping();
    TokenLayout  previous;
    auto         previous_end  = usize {};
    auto         compact_close = usize::MAX;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        const auto& token   = syntax.tokens[index];
        auto        current = cursor.advance(syntax, index);
        auto        begin   = previous_end;
        if (compact_block(syntax, index, style)) compact_close = token.syntax.pair;
        auto compact_empty = false;
        if (syntax.is(index, "{"_str) && token.syntax.pair == index + usize(1) &&
            compact_close == usize::MAX && wrap_before(token.syntax, wrapping) &&
            ! syntax.overlaps(token.source.begin, syntax.tokens[token.syntax.pair].source.end)) {
            if (token.syntax.block == BlockKind::Function)
                compact_empty = ! wrapping.split_empty_function;
            else if (record(token.syntax.block))
                compact_empty = ! wrapping.split_empty_record;
            else if (token.syntax.block == BlockKind::Namespace)
                compact_empty = ! wrapping.split_empty_namespace;
        }
        if (index == usize {} && token.source.begin >= usize(3) &&
            syntax.bytes()[usize {}] == u8(0xef) && syntax.bytes()[usize(1)] == u8(0xbb) &&
            syntax.bytes()[usize(2)] == u8(0xbf)) {
            output.push_str(syntax.text(usize {}, usize(3)));
            begin = usize(3);
        }
        auto gap      = syntax.text(begin, token.source.begin);
        bool plain    = true;
        auto newlines = count_newlines(gap);
        for (auto value : gap.as_bytes())
            if (! whitespace(value)) plain = false;
        const auto protected_gap =
            token.syntax.protected_text || syntax.overlaps(begin, token.source.begin) ||
            (index != usize {} && syntax.tokens[index - usize(1)].syntax.protected_text);
        if (! plain || protected_gap)
            output.push_str(gap);
        else if (index == usize {}) {
            if (style.keep_empty_lines.at_start_of_file)
                append_newlines(output, limited_newlines(newlines, style, false), newline);
            append_indent(output, current.columns, style);
        } else {
            const auto& left = syntax.tokens[index - usize(1)];
            auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
            auto        opening      = b == "{"_str && token.syntax.block != BlockKind::None;
            auto        after_open   = a == "{"_str && left.syntax.block != BlockKind::None;
            auto        closing      = b == "}"_str && token.syntax.block != BlockKind::None;
            auto        continuation = a == "}"_str && token.syntax.brace_continuation;
            auto        wrap_opening = opening && wrap_before(token.syntax, wrapping);
            auto        wrap_continuation =
                continuation &&
                (token.syntax.else_continuation ? wrapping.before_else : wrapping.before_catch);
            if (compact_close != usize::MAX && index <= compact_close) {
                auto spaces = gap_spaces(syntax, index, style);
                if (after_open && closing && left.syntax.pair == index)
                    spaces = Some(0u);
                else if (opening || after_open || closing)
                    spaces = Some(1u);
                auto count = spaces.is_some() ? *spaces : gap.is_empty() ? 0u : 1u;
                for (unsigned column {}; column < count; ++column) output.push_ascii(' ');
            } else {
                auto attached =
                    ((opening && ! wrap_opening) || (continuation && ! wrap_continuation)) &&
                    ! left.source.line_comment;
                auto force_newline =
                    left.source.line_comment || after_open || closing || wrap_opening ||
                    wrap_continuation || token.syntax.access_label ||
                    (left.syntax.access_end && ! token.source.comment) ||
                    left.syntax.enum_separator || (a == ";"_str && previous.parens == 0) ||
                    (a == "}"_str && left.syntax.block != BlockKind::None && b != ";"_str &&
                     b != ","_str && b != ")"_str && ! token.syntax.block_continuation);
                if (attached)
                    output.push_ascii(' ');
                else if (force_newline || newlines != 0) {
                    auto original_newlines = newlines;
                    if (newlines == 0) newlines = 1;
                    newlines = limited_newlines(newlines, style, true);
                    if ((after_open && ! style.keep_empty_lines.at_start_of_block) ||
                        (closing && ! style.keep_empty_lines.at_end_of_block))
                        newlines = 1;
                    if (! force_newline && current.parens != 0 && newlines == original_newlines)
                        output.push_str(gap);
                    else {
                        append_newlines(output, newlines, newline);
                        if (! force_newline && current.parens != 0) {
                            auto tail = gap.len();
                            while (tail != usize {} &&
                                   gap.as_bytes()[tail - usize(1)] != u8('\r') &&
                                   gap.as_bytes()[tail - usize(1)] != u8('\n'))
                                --tail;
                            output.push_str(gap.get(tail, gap.len()).unwrap());
                        } else
                            append_indent(output, current.columns, style);
                    }
                } else {
                    auto spaces = gap_spaces(syntax, index, style);
                    if (spaces.is_none())
                        output.push_str(gap);
                    else
                        for (unsigned count {}; count < *spaces; ++count) output.push_ascii(' ');
                }
            }
        }
        output.push_str(syntax.text(index));
        previous_end = token.source.end;
        previous     = current;
        if (compact_empty) compact_close = token.syntax.pair;
        if (index == compact_close) compact_close = usize::MAX;
    }
    auto suffix = syntax.text(previous_end, syntax.bytes().len());
    if (syntax.tokens[syntax.tokens.len() - usize(1)].syntax.protected_text ||
        syntax.overlaps(previous_end, syntax.bytes().len()))
        output.push_str(suffix);
    else {
        auto count = style.keep_empty_lines.at_end_of_file ? count_newlines(suffix) : 1u;
        if (count == 0) count = 1;
        append_newlines(output, limited_newlines(count, style, true), newline);
    }
    return Ok(rstd::move(output));
}

} // namespace lito::frontend::format
