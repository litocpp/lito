export module lito.frontend.format:layout;

import rstd;
import lito.frontend.lexical;
import :syntax;
import :style;
import :lines;
import :alignment;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

struct TokenLayout {
    usize    columns {};
    unsigned parens {};
};

auto wrap_before(const TokenSyntax& token, const BraceWrapping& wrapping) -> bool {
    if (token.case_body) return wrapping.after_case_label;
    if (token.block == BlockKind::Lambda) return wrapping.before_lambda_body;
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
        if (text == "}"_str && token.syntax.block != BlockKind::None &&
            token.syntax.block != BlockKind::Initializer) {
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
        if (token.syntax.label_name) {
            current.columns =
                style_.indent_goto_labels && current.columns >= usize(style_.indent_width)
                    ? current.columns - usize(style_.indent_width)
                    : usize {};
        }
        if (text == "{"_str && token.syntax.block != BlockKind::None &&
            token.syntax.block != BlockKind::Initializer) {
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

auto block_initializer_style(const FormatSyntax& syntax, usize index, const FormatStyle& style)
    -> bool {
    if (style.braced_list_style == BracedListStyle::Block) return true;
    if (style.braced_list_style != BracedListStyle::AlignFirstComment) return false;
    for (const auto& group : syntax.groups)
        if (group.kind == GroupKind::Initializer && (group.open == index || group.close == index) &&
            ! group.items.is_empty() && syntax.tokens[group.items[usize {}].begin].source.comment)
            return true;
    return false;
}

auto pointer_alignment(const TokenSyntax& token, const FormatStyle& style) -> PointerAlignment {
    if (token.pointer_kind == PointerKind::LValueReference ||
        token.pointer_kind == PointerKind::RValueReference) {
        if (style.reference_alignment == ReferenceAlignment::Left) return PointerAlignment::Left;
        if (style.reference_alignment == ReferenceAlignment::Right) return PointerAlignment::Right;
        if (style.reference_alignment == ReferenceAlignment::Middle)
            return PointerAlignment::Middle;
    }
    return style.pointer_alignment;
}

auto empty_brace_spaces(const FormatSyntax& syntax, usize open, const FormatStyle& style)
    -> unsigned {
    if (syntax.tokens[open].syntax.block == BlockKind::Initializer) {
        if (! block_initializer_style(syntax, open, style))
            return style.effective_parens_options().in_empty_parentheses ? 1u : 0u;
        return style.space_in_empty_braces == EmptyBraceSpacing::Always ? 1u : 0u;
    }
    return style.space_in_empty_braces == EmptyBraceSpacing::Never ? 0u : 1u;
}

auto gap_spaces(const FormatSyntax& syntax, usize index, const FormatStyle& style)
    -> Option<unsigned> {
    const auto& left  = syntax.tokens[index - usize(1)];
    const auto& right = syntax.tokens[index];
    auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
    if (a == "{"_str && b == "}"_str && left.syntax.pair == index)
        return Some(empty_brace_spaces(syntax, index - usize(1), style));
    if (right.syntax.access_colon) return Some(0u);
    if (right.syntax.cast_operand) return Some(style.space_after_c_style_cast ? 1u : 0u);
    if (left.syntax.logical_not) return Some(style.space_after_logical_not ? 1u : 0u);
    if (left.syntax.operator_role == OperatorRole::Conditional ||
        right.syntax.operator_role == OperatorRole::Conditional)
        return Some(1u);
    if (right.syntax.operator_role == OperatorRole::PointerReference)
        return Some(
            left.syntax.pointer_qualifier &&
                    (style.space_around_pointer_qualifiers == PointerQualifierSpacing::After ||
                     style.space_around_pointer_qualifiers == PointerQualifierSpacing::Both)
                ? 1u
            : left.syntax.operator_role == OperatorRole::PointerReference ||
                    pointer_alignment(right.syntax, style) == PointerAlignment::Left
                ? 0u
                : 1u);
    if (left.syntax.operator_role == OperatorRole::PointerReference)
        return Some(
            right.syntax.pointer_qualifier &&
                    (style.space_around_pointer_qualifiers == PointerQualifierSpacing::Before ||
                     style.space_around_pointer_qualifiers == PointerQualifierSpacing::Both)
                ? 1u
            : pointer_alignment(left.syntax, style) == PointerAlignment::Right ? 0u
                                                                               : 1u);
    if (left.syntax.operator_role == OperatorRole::Unary) return Some(0u);
    if (left.syntax.operator_role == OperatorRole::Binary &&
        right.syntax.operator_role == OperatorRole::Unary)
        return Some(1u);
    if (right.syntax.template_open) {
        auto group = right.syntax.group_id;
        if (group < syntax.groups.len() &&
            syntax.groups[group].kind == GroupKind::TemplateParameters)
            return Some(style.space_after_template_keyword ? 1u : 0u);
        return Some(0u);
    }
    if (left.syntax.template_open || right.syntax.template_closes != 0) {
        if (style.spaces_in_angles == AnglesSpacing::Leave) return None();
        return Some(style.spaces_in_angles == AnglesSpacing::Always ? 1u : 0u);
    }
    if (right.syntax.colon_role != ColonRole::Unknown) {
        if (right.syntax.colon_role == ColonRole::Case)
            return Some(style.space_before_case_colon ? 1u : 0u);
        if (right.syntax.colon_role == ColonRole::Label) return Some(0u);
        if (right.syntax.colon_role == ColonRole::BitField)
            return Some(style.bit_field_colon_spacing == BitFieldColonSpacing::Both ||
                                style.bit_field_colon_spacing == BitFieldColonSpacing::Before
                            ? 1u
                            : 0u);
        auto spaced = right.syntax.colon_role == ColonRole::Constructor
                          ? style.space_before_ctor_initializer_colon
                      : right.syntax.colon_role == ColonRole::Inheritance
                          ? style.space_before_inheritance_colon
                      : right.syntax.colon_role == ColonRole::RangeFor
                          ? style.space_before_range_based_for_loop_colon
                          : true;
        return Some(spaced ? 1u : 0u);
    }
    if (left.syntax.colon_role == ColonRole::BitField)
        return Some(style.bit_field_colon_spacing == BitFieldColonSpacing::Both ||
                            style.bit_field_colon_spacing == BitFieldColonSpacing::After
                        ? 1u
                        : 0u);
    if (left.syntax.colon_role != ColonRole::Unknown) return Some(1u);
    if (b == "["_str && right.syntax.square_role == SquareRole::Subscript)
        return Some(style.space_before_square_brackets && right.syntax.square_chain_first ? 1u
                                                                                          : 0u);
    if ((a == "["_str && left.syntax.square_role == SquareRole::Subscript) ||
        (b == "]"_str && right.syntax.square_role == SquareRole::Subscript))
        return Some(a == "["_str && b == "]"_str ? 0u : style.spaces_in_square_brackets ? 1u : 0u);
    if (right.syntax.initializer_open ||
        (right.syntax.block == BlockKind::Initializer && b == "{"_str))
        return Some(left.syntax.operator_role == OperatorRole::Assignment ||
                            style.space_before_cpp11_braced_list
                        ? 1u
                        : 0u);
    if (left.syntax.initializer_open || right.syntax.initializer_close ||
        (a == "{"_str && left.syntax.block == BlockKind::Initializer) ||
        (b == "}"_str && right.syntax.block == BlockKind::Initializer))
        return Some(a == "{"_str && b == "}"_str ? 0u
                    : block_initializer_style(
                          syntax, left.syntax.initializer_open ? index - usize(1) : index, style)
                        ? 1u
                        : 0u);
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
    if (token.syntax.block == BlockKind::Enum && syntax.is(index, "{"_str)) {
        if (! style.short_enums) return false;
        for (const auto& body : syntax.enums)
            if (body.open == index) return body.safe;
        return false;
    }
    if (! syntax.is(index, "{"_str) || ! token.syntax.simple_block ||
        token.syntax.block_statements > 1)
        return false;
    auto empty = token.syntax.pair == index + usize(1);
    if (token.syntax.block == BlockKind::Function)
        return style.short_functions == ShortFunctionStyle::All ||
               (empty && (style.short_functions == ShortFunctionStyle::Empty ||
                          style.short_functions == ShortFunctionStyle::Inline)) ||
               (token.syntax.inline_record_scope &&
                (style.short_functions == ShortFunctionStyle::Inline ||
                 style.short_functions == ShortFunctionStyle::InlineOnly));
    if (token.syntax.block == BlockKind::Lambda) {
        if (style.short_lambdas == ShortLambdaStyle::All ||
            (empty && style.short_lambdas == ShortLambdaStyle::Empty))
            return true;
        if (style.short_lambdas == ShortLambdaStyle::Inline)
            for (const auto& group : syntax.groups)
                if (group.kind == GroupKind::Arguments && group.open < index && index < group.close)
                    return true;
    }
    if (token.syntax.block == BlockKind::Control)
        return style.short_blocks == ShortBlockStyle::Always ||
               (empty && style.short_blocks == ShortBlockStyle::Empty);
    return false;
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

auto make_layout_plan(const FormatSyntax& syntax, const FormatStyle& style)
    -> lexical::Result<LayoutPlan> {
    if (syntax.preserve_all || syntax.tokens.is_empty()) return Ok(LayoutPlan {});
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
    LayoutPlan   plan { .newline = newline };
    LayoutCursor cursor(style);
    auto         wrapping = style.effective_brace_wrapping();
    TokenLayout  previous;
    auto         previous_end  = usize {};
    auto         compact_close = usize::MAX;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto        output         = String::make();
        auto        required_break = false;
        const auto& token          = syntax.tokens[index];
        auto        current        = cursor.advance(syntax, index);
        auto        begin          = previous_end;
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
            begin = usize(3);
        }
        auto gap      = syntax.text(begin, token.source.begin);
        bool plain    = true;
        auto newlines = layout_newlines(gap);
        for (auto value : gap.as_bytes())
            if (! whitespace(value)) plain = false;
        const auto protected_gap =
            token.syntax.protected_text || syntax.overlaps(begin, token.source.begin) ||
            (index != usize {} && syntax.tokens[index - usize(1)].syntax.protected_text);
        if (! plain || protected_gap) {
        } else if (index == usize {}) {
            if (style.keep_empty_lines.at_start_of_file)
                append_newlines(output, limited_newlines(newlines, style, false), newline);
            append_layout_indent(output, current.columns, style);
        } else {
            const auto& left = syntax.tokens[index - usize(1)];
            auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
            auto        opening    = b == "{"_str && token.syntax.block != BlockKind::None &&
                                     token.syntax.block != BlockKind::Initializer;
            auto        after_open = a == "{"_str && left.syntax.block != BlockKind::None &&
                                     left.syntax.block != BlockKind::Initializer;
            auto        closing    = b == "}"_str && token.syntax.block != BlockKind::None &&
                                     token.syntax.block != BlockKind::Initializer;
            auto        continuation =
                a == "}"_str && (token.syntax.brace_continuation || token.syntax.do_while_tail);
            auto wrap_opening = opening && wrap_before(token.syntax, wrapping);
            auto wrap_continuation =
                continuation && (token.syntax.do_while_tail       ? wrapping.before_while
                                 : token.syntax.else_continuation ? wrapping.before_else
                                                                  : wrapping.before_catch);
            if (compact_close != usize::MAX && index <= compact_close) {
                auto spaces = gap_spaces(syntax, index, style);
                if (after_open && closing && left.syntax.pair == index)
                    spaces = Some(empty_brace_spaces(syntax, index - usize(1), style));
                else if (opening || after_open || closing)
                    spaces = Some(1u);
                auto count = spaces.is_some() ? *spaces : gap.is_empty() ? 0u : 1u;
                for (unsigned column {}; column < count; ++column) output.push_ascii(' ');
            } else {
                auto attached =
                    ((opening && ! wrap_opening) || (continuation && ! wrap_continuation)) &&
                    ! left.source.line_comment && ! left.syntax.label_end;
                auto force_newline =
                    left.source.line_comment || after_open || closing || wrap_opening ||
                    wrap_continuation || token.syntax.access_label || token.syntax.label_name ||
                    left.syntax.label_end || (left.syntax.access_end && ! token.source.comment) ||
                    left.syntax.enum_separator || (a == ";"_str && previous.parens == 0) ||
                    (a == "}"_str && left.syntax.block != BlockKind::None &&
                     left.syntax.block != BlockKind::Initializer && b != ";"_str && b != ","_str &&
                     b != ")"_str && ! token.syntax.block_continuation);
                if (token.source.comment && newlines == 0 && ! after_open) force_newline = false;
                if (attached)
                    output.push_ascii(' ');
                else if (force_newline || newlines != 0) {
                    required_break         = force_newline;
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
                            append_layout_indent(output, current.columns, style);
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
        auto editable    = plain && ! protected_gap;
        auto broken      = layout_newlines(output.as_str()) != 0;
        auto flat_spaces = broken ? 1u : static_cast<unsigned>(output.len().to_primitive());
        plan.gaps.push(PlannedGap {
            .span        = { begin, token.source.begin },
            .replacement = editable ? Some(rstd::move(output)) : Option<String> {},
            .breaking    = required_break ? GapBreak::Hard : GapBreak::Never,
            .indent      = current.columns,
            .flat_spaces = flat_spaces,
            .editable    = editable,
            .broken      = broken,
        });
        previous_end = token.source.end;
        previous     = current;
        if (compact_empty) compact_close = token.syntax.pair;
        if (index == compact_close) compact_close = usize::MAX;
    }
    auto output           = String::make();
    auto suffix           = syntax.text(previous_end, syntax.bytes().len());
    auto protected_suffix = syntax.tokens[syntax.tokens.len() - usize(1)].syntax.protected_text ||
                            syntax.overlaps(previous_end, syntax.bytes().len());
    if (! protected_suffix) {
        auto count = style.keep_empty_lines.at_end_of_file ? layout_newlines(suffix) : 1u;
        if (count == 0) count = 1;
        append_newlines(output, limited_newlines(count, style, true), newline);
    }
    plan.gaps.push(PlannedGap {
        .span        = { previous_end, syntax.bytes().len() },
        .replacement = protected_suffix ? Option<String> {} : Some(rstd::move(output)),
        .editable    = ! protected_suffix,
    });
    return Ok(rstd::move(plan));
}

auto change_indent(LayoutPlan& plan, usize begin, usize end, int levels, const FormatStyle& style)
    -> void {
    for (auto index = begin; index < end && index < plan.gaps.len(); ++index) {
        auto& gap      = plan.gaps[index];
        auto  distance = usize(static_cast<rstd::size_t>(levels < 0 ? -levels : levels)) *
                         usize(style.indent_width);
        if (levels < 0) {
            if (distance <= gap.indent) gap.indent -= distance;
        } else
            gap.indent += distance;
        if (gap.editable && gap.broken) {
            auto count =
                gap.replacement.is_some() ? layout_newlines(gap.replacement->as_str()) : 1u;
            set_gap_break(plan, index, gap.indent, style, count);
        }
    }
}

auto short_control(const FormatSyntax& syntax, usize statement_index, const FormatStyle& style)
    -> bool {
    const auto& statement = syntax.statements[statement_index];
    if (! statement.simple || statement.body_open != usize::MAX) return false;
    if (statement.control == ControlKind::For || statement.control == ControlKind::While ||
        statement.control == ControlKind::Do)
        return style.short_loops;
    auto else_branch = false;
    for (const auto& parent : syntax.statements)
        if (parent.else_statement == statement_index) else_branch = true;
    if (statement.control == ControlKind::If)
        return style.short_if != ShortIfStyle::Never &&
               (style.short_if != ShortIfStyle::WithoutElse ||
                (! statement.has_else && ! else_branch)) &&
               (style.short_if != ShortIfStyle::OnlyFirstIf || ! else_branch);
    return statement.control == ControlKind::Else && style.short_if == ShortIfStyle::AllIfsAndElse;
}

auto flat_range_width(const FormatSyntax& syntax,
                      const LayoutPlan&   plan,
                      usize               begin,
                      usize               end,
                      const FormatStyle&  style) -> usize {
    auto width = usize {};
    for (auto index = begin; index < end && index < syntax.tokens.len(); ++index) {
        if (index != begin) width += usize(plan.gaps[index].flat_spaces);
        width = advance_columns(syntax.text(index), width, style.tab_width);
    }
    return width;
}

auto flatten_range(const FormatSyntax& syntax,
                   LayoutPlan&         plan,
                   usize               begin,
                   usize               end,
                   const FormatStyle&  style) -> void {
    for (auto index = begin; index < end && index < syntax.tokens.len(); ++index) {
        auto& gap = plan.gaps[index];
        if (! gap.editable || index == usize {}) continue;
        auto spaces = gap_spaces(syntax, index, style);
        if (spaces.is_some())
            set_gap_spaces(plan, index, *spaces);
        else
            set_gap_spaces(plan, index, gap.flat_spaces);
        gap.breaking = GapBreak::Never;
    }
}

auto bracket_breaks(const FormatGroup& group, bool block_initializer, const FormatStyle& style)
    -> PlannedGroup {
    PlannedGroup result;
    if (group.kind == GroupKind::Arguments || group.kind == GroupKind::Parameters) {
        result.after_open   = style.break_after_open_bracket_function;
        result.before_close = style.break_before_close_bracket_function;
    } else if (group.kind == GroupKind::Initializer && ! block_initializer) {
        result.after_open   = style.break_after_open_bracket_braced_list;
        result.before_close = style.break_before_close_bracket_braced_list;
    } else if (group.kind == GroupKind::Control) {
        if (group.control == ControlKind::If) {
            result.after_open   = style.break_after_open_bracket_if;
            result.before_close = style.break_before_close_bracket_if;
        } else if (group.control == ControlKind::For || group.control == ControlKind::While ||
                   group.control == ControlKind::Do) {
            result.after_open   = style.break_after_open_bracket_loop;
            result.before_close = style.break_before_close_bracket_loop;
        } else if (group.control == ControlKind::Switch) {
            result.after_open   = style.break_after_open_bracket_switch;
            result.before_close = style.break_before_close_bracket_switch;
        }
    }
    if (block_initializer) result.before_close = true;
    return result;
}

auto expression_break(const FormatSyntax& syntax,
                      LayoutPlan&         plan,
                      usize               op,
                      bool                before,
                      usize               base,
                      usize               anchor,
                      usize               indent_anchor,
                      usize               indent_offset,
                      const FormatStyle&  style) -> usize {
    auto selected = before ? op : op + usize(1);
    auto opposite = before ? op + usize(1) : op;
    if (selected >= syntax.tokens.len()) return usize::MAX;
    if (opposite < syntax.tokens.len()) {
        auto& gap = plan.gaps[opposite];
        if (gap.editable && gap.breaking != GapBreak::Hard &&
            (! gap.broken || style.column_limit != 0) && ! syntax.tokens[opposite].source.comment &&
            ! syntax.tokens[opposite - usize(1)].source.line_comment) {
            auto spaces = gap_spaces(syntax, opposite, style);
            set_gap_spaces(plan, opposite, spaces.is_some() ? *spaces : gap.flat_spaces);
            gap.breaking      = GapBreak::Never;
            gap.indent_anchor = usize::MAX;
        }
    }
    auto& gap = plan.gaps[selected];
    if (! gap.editable || gap.breaking == GapBreak::Hard) return usize::MAX;
    gap.breaking      = GapBreak::Soft;
    gap.indent        = base;
    gap.anchor        = anchor;
    gap.indent_anchor = anchor == usize::MAX ? indent_anchor : usize::MAX;
    gap.indent_offset = indent_offset;
    return selected;
}

auto expression_indent_anchor(const FormatSyntax& syntax, TokenRange extent) -> usize {
    auto anchor = extent.begin;
    if (extent.begin < syntax.tokens.len()) {
        auto statement = syntax.tokens[extent.begin].syntax.statement_id;
        if (statement < syntax.statements.len()) anchor = syntax.statements[statement].extent.begin;
    }
    auto group_open = usize::MAX;
    for (const auto& group : syntax.groups) {
        if (! group.safe || group.open >= extent.begin || extent.end > group.close ||
            (group.kind != GroupKind::Arguments && group.kind != GroupKind::Parameters &&
             group.kind != GroupKind::Control && group.kind != GroupKind::Initializer))
            continue;
        if (group_open == usize::MAX || group.open > group_open) group_open = group.open;
    }
    return group_open != usize::MAX ? group_open : anchor;
}

auto annotate_list(const FormatSyntax& syntax,
                   LayoutPlan&         plan,
                   usize               group_index,
                   const FormatStyle&  style) -> void {
    const auto& group         = syntax.groups[group_index];
    auto&       state         = plan.groups[group_index];
    auto        constructor   = group.kind == GroupKind::ConstructorInitializers;
    auto        base          = plan.gaps[group.open].indent;
    auto        indent        = base + usize(style.constructor_initializer_indent_width);
    state.base_indent         = base;
    state.continuation_indent = indent;
    auto after_colon          = after_list_colon(group, style);
    auto before_comma         = before_list_comma(group, style);
    auto after_comma =
        ! constructor && style.break_inheritance_list == InheritanceBreak::AfterComma;
    auto& colon = plan.gaps[group.open];
    if (colon.editable && colon.breaking != GapBreak::Hard) {
        set_gap_spaces(plan,
                       group.open,
                       constructor ? style.space_before_ctor_initializer_colon ? 1u : 0u
                       : style.space_before_inheritance_colon ? 1u
                                                              : 0u);
        colon.breaking = after_colon || after_comma ? GapBreak::Never : GapBreak::Soft;
        colon.indent   = indent;
        colon.group    = group_index;
    }
    auto first = group.items[usize {}].begin;
    for (auto item_index = usize {}; item_index < group.items.len(); ++item_index) {
        auto index = group.items[item_index].begin;
        if (index >= syntax.tokens.len() || ! plan.gaps[index].editable) continue;
        auto& gap  = plan.gaps[index];
        gap.indent = indent;
        gap.anchor = first;
        gap.group  = group_index;
        if (gap.breaking == GapBreak::Hard || syntax.tokens[index].source.comment) continue;
        set_gap_spaces(plan, index, 1);
        gap.breaking = item_index == usize {} ? after_colon ? GapBreak::Soft : GapBreak::Never
                       : before_comma         ? GapBreak::Never
                                              : GapBreak::Soft;
    }
    if (before_comma)
        for (auto separator : group.separators) {
            auto& gap = plan.gaps[separator];
            if (! gap.editable || gap.breaking == GapBreak::Hard) continue;
            set_gap_spaces(plan, separator, 0);
            gap.breaking = GapBreak::Soft;
            gap.indent   = indent;
            gap.anchor   = group.open;
            gap.group    = group_index;
        }
    if (group.close < syntax.tokens.len()) plan.gaps[group.close].indent = base;
}

auto annotate_plan(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style) -> void {
    for (auto statement_index = usize {}; statement_index < syntax.statements.len();
         ++statement_index) {
        const auto& statement = syntax.statements[statement_index];
        if (statement.kind == StatementKind::Control && statement.body_open == usize::MAX &&
            statement.body_begin < statement.body_end) {
            change_indent(plan, statement.body_begin, statement.body_end, 1, style);
            auto compact = short_control(syntax, statement_index, style);
            if (compact && style.column_limit != 0)
                compact =
                    plan.gaps[statement.extent.begin].indent +
                        flat_range_width(
                            syntax, plan, statement.extent.begin, statement.body_end, style) <=
                    usize(style.column_limit);
            auto body = statement.body_begin;
            if (plan.gaps[body].editable) {
                if (compact) {
                    flatten_range(syntax, plan, body, statement.body_end, style);
                    set_gap_spaces(plan, body, 1);
                } else {
                    set_gap_break(plan, body, plan.gaps[body].indent, style);
                    plan.gaps[body].breaking = GapBreak::Hard;
                }
            }
            if (statement.body_end < syntax.tokens.len() &&
                plan.gaps[statement.body_end].editable) {
                set_gap_break(
                    plan, statement.body_end, plan.gaps[statement.body_end].indent, style);
                plan.gaps[statement.body_end].breaking = GapBreak::Hard;
            }
        }
        if (statement.kind == StatementKind::CaseLabel) {
            auto braced = statement.body_open < syntax.tokens.len();
            if (! style.indent_case_labels) {
                change_indent(plan, statement.extent.begin, statement.extent.end, -1, style);
                if (braced)
                    change_indent(plan,
                                  statement.body_open,
                                  syntax.tokens[statement.body_open].syntax.pair + usize(1),
                                  -1,
                                  style);
            } else if (! braced)
                change_indent(plan, statement.body_begin, statement.body_end, 1, style);
            else {
                auto end = syntax.tokens[statement.body_open].syntax.pair + usize(1);
                if (end < statement.body_end)
                    change_indent(plan, end, statement.body_end, 1, style);
            }
            if (statement.extent.begin < syntax.tokens.len() &&
                plan.gaps[statement.extent.begin].editable) {
                set_gap_break(
                    plan, statement.extent.begin, plan.gaps[statement.extent.begin].indent, style);
                plan.gaps[statement.extent.begin].breaking = GapBreak::Hard;
            }
            auto body = statement.body_begin;
            if (body < statement.body_end && body < syntax.tokens.len() &&
                plan.gaps[body].editable) {
                if (braced && body == statement.body_open) {
                    if (style.effective_brace_wrapping().after_case_label ||
                        (body != usize {} && syntax.tokens[body - usize(1)].source.line_comment)) {
                        set_gap_break(plan, body, plan.gaps[body].indent, style);
                        plan.gaps[body].breaking = GapBreak::Hard;
                    } else {
                        set_gap_spaces(plan, body, 1);
                        plan.gaps[body].breaking = GapBreak::Never;
                    }
                    continue;
                }
                auto compact = style.short_case_labels && statement.simple;
                if (compact && style.column_limit != 0)
                    compact =
                        plan.gaps[statement.extent.begin].indent +
                            flat_range_width(
                                syntax, plan, statement.extent.begin, statement.body_end, style) <=
                        usize(style.column_limit);
                if (compact)
                    flatten_range(syntax, plan, body, statement.body_end, style);
                else {
                    set_gap_break(plan, body, plan.gaps[body].indent, style);
                    plan.gaps[body].breaking = GapBreak::Hard;
                }
            }
        }
        if (statement.kind == StatementKind::Function &&
            statement.name_index < syntax.tokens.len() && statement.return_type_end != usize::MAX &&
            statement.return_type_end > statement.extent.begin) {
            auto index = statement.name_index;
            if (plan.gaps[index].editable) {
                plan.gaps[index].breaking =
                    style.break_after_return_type == ReturnTypeBreakStyle::All ? GapBreak::Hard
                                                                               : GapBreak::Soft;
                if (style.break_after_return_type == ReturnTypeBreakStyle::All)
                    set_gap_break(plan, index, plan.gaps[index].indent, style);
                else {
                    auto spaces = gap_spaces(syntax, index, style);
                    set_gap_spaces(plan, index, spaces.is_some() ? *spaces : 1u);
                }
                if (style.indent_wrapped_function_names)
                    plan.gaps[index].indent += usize(style.continuation_indent_width);
            }
        }
    }
    for (auto index = usize(1); index < syntax.tokens.len(); ++index) {
        auto& gap = plan.gaps[index];
        if (! gap.editable) continue;
        const auto& previous = syntax.tokens[index - usize(1)].syntax;
        if (previous.template_declaration_end) {
            if (style.break_template_declarations == TemplateBreakStyle::Yes) {
                set_gap_break(plan, index, gap.indent, style);
                gap.breaking = GapBreak::Hard;
            } else if (style.break_template_declarations != TemplateBreakStyle::Leave) {
                set_gap_spaces(plan, index, 1);
                gap.breaking = GapBreak::Soft;
            }
        }
        if (previous.attribute_end) {
            if (style.break_after_attributes == AttributeBreakStyle::Always) {
                set_gap_break(plan, index, gap.indent, style);
                gap.breaking = GapBreak::Hard;
            } else if (style.break_after_attributes == AttributeBreakStyle::Never) {
                set_gap_spaces(plan, index, 1);
                gap.breaking = GapBreak::Soft;
            }
        }
    }
    for (auto group_index = usize {}; group_index < syntax.groups.len(); ++group_index) {
        const auto& group = syntax.groups[group_index];
        plan.groups.push(PlannedGroup {});
        if (! group.safe || group.items.is_empty() || group.open >= syntax.tokens.len()) continue;
        if (group.kind == GroupKind::Attribute || group.kind == GroupKind::LambdaCapture ||
            group.kind == GroupKind::Enumerators)
            continue;
        if (group.kind == GroupKind::ConstructorInitializers ||
            group.kind == GroupKind::Inheritance) {
            annotate_list(syntax, plan, group_index, style);
            continue;
        }
        auto first = group.items[usize {}].begin;
        auto base  = plan.gaps[group.open].indent;
        auto block_initializer =
            group.kind == GroupKind::Initializer &&
            (style.braced_list_style == BracedListStyle::Block ||
             (style.braced_list_style == BracedListStyle::AlignFirstComment &&
              first < syntax.tokens.len() && syntax.tokens[first].source.comment));
        auto indent =
            base + usize(block_initializer ? style.indent_width : style.continuation_indent_width);
        auto& group_plan               = plan.groups[group_index];
        group_plan                     = bracket_breaks(group, block_initializer, style);
        group_plan.base_indent         = base;
        group_plan.continuation_indent = indent;
        for (const auto& item : group.items) {
            auto index = item.begin;
            if (index >= syntax.tokens.len() || ! plan.gaps[index].editable) continue;
            auto& gap  = plan.gaps[index];
            gap.indent = indent;
            if (gap.breaking == GapBreak::Hard || syntax.tokens[index].source.comment) {
                if (gap.broken)
                    set_gap_break(plan,
                                  index,
                                  indent,
                                  style,
                                  gap.replacement.is_some()
                                      ? layout_newlines(gap.replacement->as_str())
                                      : 1u);
                continue;
            }
            auto spaces = gap_spaces(syntax, index, style);
            set_gap_spaces(plan, index, spaces.is_some() ? *spaces : gap.flat_spaces);
            gap.breaking = GapBreak::Soft;
            gap.anchor = style.align_after_open_bracket && ! block_initializer ? first : usize::MAX;
            gap.group  = group_index;
        }
        if (group.close < syntax.tokens.len() && plan.gaps[group.close].editable) {
            plan.gaps[group.close].indent = base;
            if (block_initializer && plan.gaps[group.close].broken)
                set_gap_break(plan, group.close, base, style);
            else if (! block_initializer && plan.gaps[group.close].breaking != GapBreak::Hard &&
                     ! syntax.tokens[group.close - usize(1)].source.comment) {
                auto spaces = gap_spaces(syntax, group.close, style);
                if (spaces.is_some()) set_gap_spaces(plan, group.close, *spaces);
            }
        }
    }
    for (const auto& expression : syntax.expressions) {
        if (expression.first_operand >= syntax.tokens.len()) continue;
        for (auto op : expression.operators) {
            auto assignment = syntax.tokens[op].syntax.operator_role == OperatorRole::Assignment;
            auto before     = style.break_before_binary_operators == BinaryOperatorBreak::All ||
                              (! assignment && style.break_before_binary_operators ==
                                                   BinaryOperatorBreak::NonAssignment);
            expression_break(syntax,
                             plan,
                             op,
                             before,
                             plan.gaps[expression.extent.begin].indent +
                                 usize(style.continuation_indent_width),
                             ! assignment && style.align_operands == OperandAlignment::Align
                                 ? expression.first_operand
                                 : usize::MAX,
                             expression_indent_anchor(syntax, expression.extent),
                             usize(style.continuation_indent_width),
                             style);
        }
    }
    for (const auto& conditional : syntax.conditionals) {
        auto base   = plan.gaps[conditional.extent.begin].indent +
                      usize(style.continuation_indent_width) * usize(conditional.depth + 1u);
        auto anchor = style.align_operands == OperandAlignment::Align ? conditional.condition_begin
                                                                      : usize::MAX;
        auto indent_extent = conditional.extent;
        auto parent        = conditional.parent;
        while (parent < syntax.conditionals.len()) {
            indent_extent = syntax.conditionals[parent].extent;
            parent        = syntax.conditionals[parent].parent;
        }
        auto indent_anchor = expression_indent_anchor(syntax, indent_extent);
        auto offset        = usize(style.continuation_indent_width) * usize(conditional.depth + 1u);
        auto question      = expression_break(syntax,
                                              plan,
                                              conditional.question,
                                              style.break_before_ternary_operators,
                                              base,
                                              anchor,
                                              indent_anchor,
                                              offset,
                                              style);
        auto colon         = expression_break(syntax,
                                              plan,
                                              conditional.colon,
                                              style.break_before_ternary_operators,
                                              base,
                                              anchor,
                                              indent_anchor,
                                              offset,
                                              style);
        if (question < syntax.tokens.len() && colon < syntax.tokens.len()) {
            plan.gaps[question].paired = colon;
            plan.gaps[colon].paired    = question;
        }
    }
    rebuild_lines(syntax, plan, style);
    if (style.column_limit != 0)
        for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
            const auto& token = syntax.tokens[index];
            if (! compact_block(syntax, index, style) || token.syntax.pair >= syntax.tokens.len())
                continue;
            auto header = token.syntax.header_begin < index ? token.syntax.header_begin : index;
            auto close  = token.syntax.pair;
            if (plan.gaps[header].indent +
                    flat_range_width(syntax, plan, header, close + usize(1), style) <=
                usize(style.column_limit))
                continue;
            if (index + usize(1) < close && plan.gaps[index + usize(1)].editable) {
                set_gap_break(plan, index + usize(1), plan.gaps[index + usize(1)].indent, style);
                plan.gaps[index + usize(1)].breaking = GapBreak::Hard;
            }
            if (plan.gaps[close].editable) {
                set_gap_break(plan, close, plan.gaps[close].indent, style);
                plan.gaps[close].breaking = GapBreak::Hard;
            }
        }
}

auto annotate_enums(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style)
    -> void {
    auto wrapping = style.effective_brace_wrapping();
    for (const auto& body : syntax.enums) {
        if (body.group_index >= syntax.groups.len() || body.close >= syntax.tokens.len()) continue;
        auto compact = style.short_enums && body.safe;
        if (compact && style.column_limit != 0)
            compact = plan.gaps[body.header_begin].indent +
                          flat_range_width(
                              syntax, plan, body.header_begin, body.close + usize(1), style) <=
                      usize(style.column_limit);
        if (compact) continue;
        auto base = plan.gaps[body.open].indent;
        if (plan.gaps[body.open].editable) {
            if (wrap_before(syntax.tokens[body.open].syntax, wrapping)) {
                set_gap_break(plan, body.open, base, style);
                plan.gaps[body.open].breaking = GapBreak::Hard;
            } else if (body.open != usize {} &&
                       ! syntax.tokens[body.open - usize(1)].source.line_comment) {
                set_gap_spaces(plan, body.open, 1);
                plan.gaps[body.open].breaking = GapBreak::Never;
            }
        }
        if (body.safe)
            for (const auto& item : syntax.groups[body.group_index].items) {
                if (item.begin >= body.close || ! plan.gaps[item.begin].editable) continue;
                auto count = layout_newlines(gap_text(syntax, plan.gaps[item.begin]));
                set_gap_break(plan,
                              item.begin,
                              base + usize(style.indent_width),
                              style,
                              count == 0 ? 1 : count);
                plan.gaps[item.begin].breaking = GapBreak::Hard;
            }
        if (plan.gaps[body.close].editable) {
            auto count = layout_newlines(gap_text(syntax, plan.gaps[body.close]));
            set_gap_break(plan, body.close, base, style, count == 0 ? 1 : count);
            plan.gaps[body.close].breaking = GapBreak::Hard;
        }
    }
}

auto annotate_lambdas(const FormatSyntax& syntax, LayoutPlan& plan) -> void {
    for (const auto& lambda : syntax.lambdas) {
        if (lambda.body_open >= syntax.tokens.len() || lambda.body_close >= syntax.tokens.len())
            continue;
        auto base = plan.gaps[lambda.body_open].indent;
        auto scope_base =
            lambda.scope < syntax.tokens.len() ? plan.gaps[lambda.scope].indent : usize {};
        plan.lambdas.push(PlannedLambda {
            .signature    = lambda.signature.begin,
            .open         = lambda.body_open,
            .close        = lambda.body_close,
            .scope        = lambda.scope,
            .outer_offset = base >= scope_base ? base - scope_base : usize {},
        });
    }
    rstd::slice_::sort_unstable_by(plan.lambdas.as_mut_slice().as_mut_ref(),
                                   [](const PlannedLambda& a, const PlannedLambda& b) {
                                       return a.open < b.open;
                                   });
}

auto sync_multiline_control_braces(const FormatSyntax& syntax,
                                   LayoutPlan&         plan,
                                   const FormatStyle&  style) -> bool {
    if (style.effective_brace_wrapping().after_control_statement != ControlBraceStyle::MultiLine)
        return false;
    auto changed = false;
    for (const auto& statement : syntax.statements) {
        auto body = statement.body_open;
        if (statement.kind != StatementKind::Control || body >= syntax.tokens.len() ||
            ! plan.gaps[body].editable || statement.extent.begin >= body)
            continue;
        auto header_end = body - usize(1);
        for (const auto& group : syntax.groups)
            if (group.kind == GroupKind::Control && group.control == statement.control &&
                group.scope_id == statement.scope_id && statement.extent.begin <= group.open &&
                group.close < body)
                header_end = group.close;
        auto  wrap = plan.token_lines[statement.extent.begin] != plan.token_lines[header_end] ||
                     syntax.tokens[body - usize(1)].source.line_comment;
        auto& gap  = plan.gaps[body];
        if (wrap == gap.broken) continue;
        if (wrap) {
            set_gap_break(plan, body, gap.indent, style);
            gap.breaking = GapBreak::Hard;
        } else {
            set_gap_spaces(plan, body, 1);
            gap.breaking = GapBreak::Never;
        }
        changed = true;
    }
    if (changed) rebuild_lines(syntax, plan, style);
    return changed;
}

auto apply_access_spacing(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style)
    -> void {
    for (const auto& label : syntax.access_labels) {
        if (label.name_index < syntax.tokens.len() && plan.gaps[label.name_index].editable) {
            auto& gap   = plan.gaps[label.name_index];
            auto  count = layout_newlines(gap_text(syntax, gap));
            if (count == 0) count = 1;
            if (style.empty_line_before_access_modifier == EmptyLineBeforeAccess::Never)
                count = 1;
            else if (style.empty_line_before_access_modifier == EmptyLineBeforeAccess::Always &&
                     ! label.at_record_start && count < 2)
                count = 2;
            else if (style.empty_line_before_access_modifier ==
                     EmptyLineBeforeAccess::LogicalBlock) {
                if (label.previous_access)
                    count = 1;
                else if (label.completed_member && ! label.comment_before && count < 2)
                    count = 2;
            }
            set_gap_break(plan, label.name_index, gap.indent, style, count);
            gap.breaking = GapBreak::Hard;
        }
        if (label.next_access || label.after_index >= syntax.tokens.len() ||
            ! plan.gaps[label.after_index].editable)
            continue;
        auto& gap   = plan.gaps[label.after_index];
        auto  count = layout_newlines(gap_text(syntax, gap));
        if (count == 0) count = 1;
        if (style.empty_line_after_access_modifier == EmptyLineAfterAccess::Never ||
            (style.empty_line_after_access_modifier == EmptyLineAfterAccess::Always &&
             label.at_record_end))
            count = 1;
        else if (style.empty_line_after_access_modifier == EmptyLineAfterAccess::Always &&
                 count < 2)
            count = 2;
        set_gap_break(plan, label.after_index, gap.indent, style, count);
        gap.breaking = GapBreak::Hard;
    }
    rebuild_lines(syntax, plan, style);
}

auto layout_source(const FormatSyntax& syntax, const FormatStyle& requested_style)
    -> lexical::Result<String> {
    auto style = requested_style;
    if (style.derive_pointer_alignment) {
        auto left  = usize {};
        auto right = usize {};
        for (const auto& chain : syntax.pointer_chains) {
            if (chain.alignment == PointerEvidence::Left)
                ++left;
            else if (chain.alignment == PointerEvidence::Right)
                ++right;
        }
        if (left > right)
            style.pointer_alignment = PointerAlignment::Left;
        else if (right > left)
            style.pointer_alignment = PointerAlignment::Right;
        style.reference_alignment = ReferenceAlignment::Pointer;
    }
    if (style.tab_width == 0)
        return Err(lexical::Error::make("TabWidth must be greater than zero"_str));
    if (style.spaces_in_angles == AnglesSpacing::Always)
        return Err(lexical::Error::make("SpacesInAngles: Always is not supported"_str));
    if (syntax.preserve_all || syntax.tokens.is_empty())
        return Ok(String::make(syntax.text(usize {}, syntax.bytes().len())));
    auto plan = make_layout_plan(syntax, style);
    if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
    annotate_plan(syntax, *plan, style);
    annotate_enums(syntax, *plan, style);
    annotate_lambdas(syntax, *plan);
    apply_access_spacing(syntax, *plan, style);
    sync_multiline_control_braces(syntax, *plan, style);
    wrap_lines(syntax, *plan, style);
    if (sync_multiline_control_braces(syntax, *plan, style)) wrap_lines(syntax, *plan, style);
    if (style.break_template_declarations == TemplateBreakStyle::MultiLine) {
        auto changed = false;
        for (auto index = usize(1); index < syntax.tokens.len(); ++index) {
            if (! syntax.tokens[index - usize(1)].syntax.template_declaration_end ||
                ! plan->gaps[index].editable || plan->gaps[index].broken)
                continue;
            auto statement_index = syntax.tokens[index].syntax.statement_id;
            if (statement_index >= syntax.statements.len()) continue;
            const auto& statement = syntax.statements[statement_index];
            auto        end = statement.body_open < syntax.tokens.len() ? statement.body_open
                              : statement.extent.end == usize {} ? index
                                                                 : statement.extent.end - usize(1);
            if (end < plan->token_lines.len() &&
                plan->token_lines[end] != plan->token_lines[index]) {
                set_gap_break(*plan, index, plan->gaps[index].indent, style);
                plan->gaps[index].breaking = GapBreak::Hard;
                changed                    = true;
            }
        }
        if (changed) wrap_lines(syntax, *plan, style);
    }
    align_layout(syntax, *plan, style);
    return emit_layout(syntax, *plan);
}

} // namespace lito::frontend::format
