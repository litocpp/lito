export module lito.frontend.format:alignment;

import rstd;
import lito.frontend.lexical;
import :model;
import :style;
import :lines;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

enum class AlignmentKind
{
    Declaration,
    Assignment,
    TrailingComment
};

auto alignment_candidate(const FormatSyntax& syntax, const PlannedLine& line, AlignmentKind kind)
    -> usize {
    if (line.protected_text) return usize::MAX;
    if (kind == AlignmentKind::TrailingComment) {
        if (line.extent.end <= line.extent.begin + usize(1)) return usize::MAX;
        auto last = line.extent.end - usize(1);
        return syntax.tokens[last].source.line_comment ? last : usize::MAX;
    }
    for (const auto& statement : syntax.statements) {
        if (statement.extent.begin != line.extent.begin || statement.extent.end > line.extent.end)
            continue;
        if (kind == AlignmentKind::Assignment && ! statement.compound_assignment)
            return statement.assignment_index;
        if (kind == AlignmentKind::Declaration &&
            (statement.kind == StatementKind::Declaration ||
             statement.kind == StatementKind::Function) &&
            statement.name_index > statement.extent.begin && statement.name_index < line.extent.end)
            return statement.name_index;
    }
    return usize::MAX;
}

auto align_token_group(const FormatSyntax& syntax,
                       LayoutPlan&         plan,
                       const Vec<usize>&   group,
                       const FormatStyle&  style) -> void {
    if (group.len() < usize(2)) return;
    auto target = usize {};
    for (auto index : group)
        if (plan.token_columns[index] > target) target = plan.token_columns[index];
    for (auto index : group) {
        auto delta = target - plan.token_columns[index];
        if (style.column_limit != 0 &&
            plan.lines[plan.token_lines[index]].width + delta > usize(style.column_limit))
            return;
    }
    for (auto index : group) {
        auto delta    = target - plan.token_columns[index];
        auto left_end = plan.token_columns[index - usize(1)] +
                        advance_columns(syntax.text(index - usize(1)), usize {}, style.tab_width);
        auto spaces   = plan.token_columns[index] - left_end + delta;
        set_gap_spaces(plan, index, static_cast<unsigned>(spaces.to_primitive()));
    }
}

auto align_token_lines(const FormatSyntax& syntax,
                       LayoutPlan&         plan,
                       const FormatStyle&  style,
                       AlignmentKind       kind) -> void {
    rebuild_lines(syntax, plan, style);
    auto group = Vec<usize>::make();
    auto scope = usize::MAX;
    for (const auto& line : plan.lines) {
        auto candidate = alignment_candidate(syntax, line, kind);
        auto eligible  = candidate < syntax.tokens.len() && candidate > line.extent.begin &&
                         plan.gaps[candidate].editable && ! plan.gaps[candidate].broken;
        if (! eligible || line.newlines_before > 1 ||
            (! group.is_empty() && line.scope_id != scope)) {
            align_token_group(syntax, plan, group, style);
            group.clear();
        }
        if (eligible) {
            scope = line.scope_id;
            group.push(usize(candidate));
        }
    }
    align_token_group(syntax, plan, group, style);
    rebuild_lines(syntax, plan, style);
}

auto set_slot(const FormatSyntax& syntax, LayoutPlan& plan, TextRange span, String text) -> void {
    for (auto& edit : plan.extra_edits)
        if (edit.span.begin == span.begin && edit.span.end == span.end) {
            edit.replacement = rstd::move(text);
            return;
        }
    if (text.as_str() != syntax.text(span.begin, span.end))
        plan.extra_edits.push(WhitespaceEdit { .span = span, .replacement = rstd::move(text) });
}

auto slot_column(const FormatSyntax& syntax,
                 const LayoutPlan&   plan,
                 usize               begin,
                 usize               end,
                 const FormatStyle&  style) -> usize {
    auto                  column      = usize {};
    auto                  cursor      = begin;
    const WhitespaceEdit* last_insert = nullptr;
    for (;;) {
        const WhitespaceEdit* next = nullptr;
        for (const auto& edit : plan.extra_edits)
            if (rstd::addressof(edit) != last_insert && edit.span.begin >= cursor &&
                edit.span.end <= end && (next == nullptr || edit.span.begin < next->span.begin))
                next = rstd::addressof(edit);
        if (next == nullptr) break;
        column = advance_columns(syntax.text(cursor, next->span.begin), column, style.tab_width);
        column = advance_columns(next->replacement.as_str(), column, style.tab_width);
        if (next->span.end == cursor && next->span.begin == cursor) {
            last_insert = next;
        } else
            cursor = next->span.end;
    }
    return advance_columns(syntax.text(cursor, end), column, style.tab_width);
}

auto padding(usize count) -> String {
    auto text = String::make();
    for (auto index = usize {}; index < count; ++index) text.push_ascii(' ');
    return text;
}

auto macro_slot(const FormatDirective& directive) -> const WhitespaceSlot* {
    if (directive.locked) return nullptr;
    for (const auto& slot : directive.safe_slots)
        if (slot.editable && slot.kind == WhitespaceSlotKind::MacroReplacement)
            return rstd::addressof(slot);
    return nullptr;
}

auto align_macro_group(const FormatSyntax& syntax,
                       LayoutPlan&         plan,
                       const Vec<usize>&   group,
                       const FormatStyle&  style) -> void {
    if (group.len() < usize(2)) return;
    auto target = usize {};
    for (auto index : group) {
        const auto& directive = syntax.directives[index];
        auto        slot      = macro_slot(directive);
        auto        column =
            slot_column(syntax, plan, slot->line_begin, slot->span.begin, style) + usize(1);
        if (column > target) target = column;
    }
    for (auto index : group) {
        auto slot   = macro_slot(syntax.directives[index]);
        auto column = slot_column(syntax, plan, slot->line_begin, slot->span.begin, style);
        set_slot(syntax, plan, slot->span, padding(target - column));
    }
}

auto align_directives(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style)
    -> void {
    if (style.indent_pp_directives == PreprocessorIndent::None)
        for (const auto& directive : syntax.directives)
            if (! directive.locked)
                for (const auto& slot : directive.safe_slots)
                    if (slot.editable && (slot.kind == WhitespaceSlotKind::Leading ||
                                          slot.kind == WhitespaceSlotKind::AfterMarker))
                        set_slot(syntax, plan, slot.span, String::make());
    if (style.align_consecutive_macros) {
        auto group        = Vec<usize>::make();
        auto previous_end = usize::MAX;
        for (auto index = usize {}; index < syntax.directives.len(); ++index) {
            const auto& directive = syntax.directives[index];
            auto        slot      = macro_slot(directive);
            if (slot == nullptr || directive.kind != lexical::DirectiveKind::Define ||
                (! group.is_empty() && previous_end != directive.begin)) {
                align_macro_group(syntax, plan, group, style);
                group.clear();
            }
            if (slot != nullptr && directive.kind == lexical::DirectiveKind::Define) {
                group.push(usize(index));
                previous_end = directive.end;
            }
        }
        align_macro_group(syntax, plan, group, style);
    }
    if (style.align_escaped_newlines != EscapedNewlineAlignment::DontAlign)
        for (const auto& directive : syntax.directives) {
            if (directive.locked) continue;
            auto target = usize {};
            for (const auto& slot : directive.safe_slots)
                if (slot.editable && slot.kind == WhitespaceSlotKind::Continuation &&
                    slot.span.begin != slot.span.end) {
                    auto column =
                        slot_column(syntax, plan, slot.line_begin, slot.span.begin, style) +
                        usize(1);
                    if (column > target) target = column;
                }
            if (target == usize {}) continue;
            if (style.align_escaped_newlines == EscapedNewlineAlignment::Right &&
                style.column_limit != 0 && usize(style.column_limit) > target)
                target = usize(style.column_limit - 1);
            for (const auto& slot : directive.safe_slots)
                if (slot.editable && slot.kind == WhitespaceSlotKind::Continuation &&
                    slot.span.begin != slot.span.end) {
                    auto column =
                        slot_column(syntax, plan, slot.line_begin, slot.span.begin, style);
                    set_slot(syntax, plan, slot.span, padding(target - column));
                }
        }
}

auto align_layout(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style) -> void {
    if (style.align_consecutive_declarations)
        align_token_lines(syntax, plan, style, AlignmentKind::Declaration);
    if (style.align_consecutive_assignments)
        align_token_lines(syntax, plan, style, AlignmentKind::Assignment);
    rebuild_lines(syntax, plan, style);
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& gap = plan.gaps[index];
        if (gap.editable && gap.broken && gap.breaking == GapBreak::Soft && gap.anchor != index &&
            gap.anchor < plan.token_columns.len())
            set_gap_break(plan, index, plan.token_columns[gap.anchor], style);
    }
    if (style.align_trailing_comments == TrailingCommentAlignment::Always)
        align_token_lines(syntax, plan, style, AlignmentKind::TrailingComment);
    align_directives(syntax, plan, style);
}

} // namespace lito::frontend::format
