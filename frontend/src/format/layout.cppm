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

auto gap_spaces(const FormatSyntax& syntax, usize index, const FormatStyle& style)
    -> Option<unsigned> {
    const auto& left  = syntax.tokens[index - usize(1)];
    const auto& right = syntax.tokens[index];
    auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
    if (right.syntax.access_colon) return Some(0u);
    if (right.syntax.cast_operand) return Some(style.space_after_c_style_cast ? 1u : 0u);
    if (left.syntax.logical_not) return Some(style.space_after_logical_not ? 1u : 0u);
    if (right.syntax.operator_role == OperatorRole::PointerReference)
        return Some(left.syntax.operator_role == OperatorRole::PointerReference ||
                            style.pointer_alignment == PointerAlignment::Left
                        ? 0u
                        : 1u);
    if (left.syntax.operator_role == OperatorRole::PointerReference)
        return Some(style.pointer_alignment == PointerAlignment::Right ? 0u : 1u);
    if (left.syntax.operator_role == OperatorRole::Unary) return Some(0u);
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
        if (right.syntax.colon_role == ColonRole::Case) return Some(0u);
        auto spaced = right.syntax.colon_role == ColonRole::Constructor
                          ? style.space_before_ctor_initializer_colon
                      : right.syntax.colon_role == ColonRole::Inheritance
                          ? style.space_before_inheritance_colon
                      : right.syntax.colon_role == ColonRole::RangeFor
                          ? style.space_before_range_based_for_loop_colon
                          : true;
        return Some(spaced ? 1u : 0u);
    }
    if (left.syntax.colon_role != ColonRole::Unknown) return Some(1u);
    if ((a == "["_str && left.syntax.square_role == SquareRole::Subscript) ||
        (b == "]"_str && right.syntax.square_role == SquareRole::Subscript))
        return Some(a == "["_str && b == "]"_str ? 0u : style.spaces_in_square_brackets ? 1u : 0u);
    if (right.syntax.initializer_open ||
        (right.syntax.block == BlockKind::Initializer && b == "{"_str))
        return Some(style.space_before_cpp11_braced_list ? 1u : 0u);
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
            auto        opening      = b == "{"_str && token.syntax.block != BlockKind::None &&
                                       token.syntax.block != BlockKind::Initializer;
            auto        after_open   = a == "{"_str && left.syntax.block != BlockKind::None &&
                                       left.syntax.block != BlockKind::Initializer;
            auto        closing      = b == "}"_str && token.syntax.block != BlockKind::None &&
                                       token.syntax.block != BlockKind::Initializer;
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
            if (! style.indent_case_labels)
                change_indent(plan, statement.extent.begin, statement.extent.end, -1, style);
            else
                change_indent(plan, statement.body_begin, statement.body_end, 1, style);
            if (statement.extent.begin < syntax.tokens.len() &&
                plan.gaps[statement.extent.begin].editable) {
                set_gap_break(
                    plan, statement.extent.begin, plan.gaps[statement.extent.begin].indent, style);
                plan.gaps[statement.extent.begin].breaking = GapBreak::Hard;
            }
            auto body = statement.body_begin;
            if (body < statement.body_end && body < syntax.tokens.len() &&
                plan.gaps[body].editable) {
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
        if (! group.safe || group.items.is_empty() || group.open >= syntax.tokens.len()) continue;
        if (group.kind == GroupKind::Attribute || group.kind == GroupKind::LambdaCapture) continue;
        auto first = group.items[usize {}].begin;
        auto base  = plan.gaps[group.open].indent;
        auto block_initializer =
            group.kind == GroupKind::Initializer &&
            (style.braced_list_style == BracedListStyle::Block ||
             (style.braced_list_style == BracedListStyle::AlignFirstComment &&
              first < syntax.tokens.len() && syntax.tokens[first].source.comment));
        auto indent =
            base + usize(block_initializer ? style.indent_width : style.continuation_indent_width);
        if (group.kind == GroupKind::ConstructorInitializers) {
            indent = base + usize(style.constructor_initializer_indent_width);
            if (plan.gaps[group.open].editable) {
                plan.gaps[group.open].breaking = GapBreak::Soft;
                plan.gaps[group.open].indent   = indent;
                plan.gaps[group.open].group    = group_index;
                set_gap_spaces(
                    plan, group.open, style.space_before_ctor_initializer_colon ? 1u : 0u);
            }
        }
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
            if (group.kind == GroupKind::ConstructorInitializers) gap.anchor = first;
            gap.group = group_index;
        }
        if (group.close < syntax.tokens.len() && plan.gaps[group.close].editable) {
            plan.gaps[group.close].indent = base;
            if (block_initializer && plan.gaps[group.close].broken)
                set_gap_break(plan, group.close, base, style);
        }
    }
    for (const auto& expression : syntax.expressions) {
        if (expression.first_operand >= syntax.tokens.len()) continue;
        for (auto op : expression.operators) {
            auto index = op + usize(1);
            if (index >= syntax.tokens.len() || ! plan.gaps[index].editable ||
                plan.gaps[index].breaking == GapBreak::Hard)
                continue;
            plan.gaps[index].breaking = GapBreak::Soft;
            plan.gaps[index].indent =
                plan.gaps[expression.extent.begin].indent + usize(style.continuation_indent_width);
            if (style.align_operands == OperandAlignment::Align)
                plan.gaps[index].anchor = expression.first_operand;
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

auto layout_source(const FormatSyntax& syntax, const FormatStyle& style)
    -> lexical::Result<String> {
    if (style.tab_width == 0)
        return Err(lexical::Error::make("TabWidth must be greater than zero"_str));
    if (style.spaces_in_angles == AnglesSpacing::Always)
        return Err(lexical::Error::make("SpacesInAngles: Always is not supported"_str));
    if (syntax.preserve_all || syntax.tokens.is_empty())
        return Ok(String::make(syntax.text(usize {}, syntax.bytes().len())));
    auto plan = make_layout_plan(syntax, style);
    if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
    annotate_plan(syntax, *plan, style);
    wrap_lines(syntax, *plan, style);
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
