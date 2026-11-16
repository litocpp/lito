export module lito.frontend.format:lines;

import rstd;
import lito.frontend.lexical;
import :model;
import :style;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

enum class GapBreak
{
    Never,
    Soft,
    Hard
};

struct PlannedGap {
    TextRange      span;
    Option<String> replacement;
    GapBreak       breaking { GapBreak::Never };
    usize          indent {};
    usize          anchor { usize::MAX };
    usize          group { usize::MAX };
    usize          paired { usize::MAX };
    usize          indent_anchor { usize::MAX };
    usize          indent_offset {};
    unsigned       flat_spaces { 1 };
    bool           editable {};
    bool           broken {};
};

struct PlannedLine {
    TokenRange extent;
    usize      width {};
    usize      scope_id { usize::MAX };
    unsigned   newlines_before {};
    bool       protected_text {};
};

struct WhitespaceEdit {
    TextRange span;
    String    replacement;
};

struct PlannedGroup {
    usize base_indent {};
    usize continuation_indent {};
    bool  after_open {};
    bool  before_close {};
    bool  expanded {};
    bool  tried_next_line {};
};

struct LayoutPlan {
    Vec<PlannedGap>     gaps;
    Vec<PlannedLine>    lines;
    Vec<usize>          token_columns;
    Vec<usize>          token_lines;
    Vec<WhitespaceEdit> extra_edits;
    Vec<PlannedGroup>   groups;
    ref<str>            newline { "\n"_str };
};

auto layout_newlines(ref<str> text) -> unsigned {
    unsigned count {};
    for (auto index = usize {}; index < text.len(); ++index) {
        if (text[index] == u8('\r')) {
            ++count;
            if (index + usize(1) < text.len() && text[index + usize(1)] == u8('\n')) ++index;
        } else if (text[index] == u8('\n'))
            ++count;
    }
    return count;
}

auto advance_columns(ref<str> text, usize column, unsigned tab_width) -> usize {
    for (auto value : text.as_bytes()) {
        if (value == u8('\r') || value == u8('\n'))
            column = usize {};
        else if (value == u8('\t'))
            column += usize(tab_width) - column % usize(tab_width);
        else if ((value.to_primitive() & 0xc0) != 0x80)
            ++column;
    }
    return column;
}

auto append_layout_indent(String& output, usize columns, const FormatStyle& style) -> void {
    if (style.use_tab == UseTab::ForIndentation) {
        auto tabs = columns / usize(style.tab_width);
        for (auto count = usize {}; count < tabs; ++count) output.push_ascii('\t');
        columns = columns % usize(style.tab_width);
    }
    for (auto count = usize {}; count < columns; ++count) output.push_ascii(' ');
}

auto gap_text(const FormatSyntax& syntax, const PlannedGap& gap) -> ref<str> {
    return gap.replacement.is_some() ? gap.replacement->as_str()
                                     : syntax.text(gap.span.begin, gap.span.end);
}

auto set_gap_spaces(LayoutPlan& plan, usize index, unsigned spaces) -> void {
    auto text = String::make();
    for (unsigned count {}; count < spaces; ++count) text.push_ascii(' ');
    plan.gaps[index].replacement = Some(rstd::move(text));
    plan.gaps[index].broken      = false;
    plan.gaps[index].flat_spaces = spaces;
}

auto set_gap_break(LayoutPlan&        plan,
                   usize              index,
                   usize              columns,
                   const FormatStyle& style,
                   unsigned           count = 1) -> void {
    auto text = String::make();
    for (unsigned line {}; line < count; ++line) text.push_str(plan.newline);
    append_layout_indent(text, columns, style);
    plan.gaps[index].replacement = Some(rstd::move(text));
    plan.gaps[index].broken      = true;
}

auto rebuild_lines(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style) -> void {
    plan.lines.clear();
    plan.token_columns.clear();
    plan.token_lines.clear();
    auto     column = usize {};
    auto     begin  = usize {};
    unsigned preceding {};
    auto     protected_text = false;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto gap   = gap_text(syntax, plan.gaps[index]);
        auto count = layout_newlines(gap);
        if (count != 0 && index != begin) {
            plan.lines.push(PlannedLine {
                .extent          = { begin, index },
                .width           = column,
                .scope_id        = syntax.tokens[begin].syntax.scope_id,
                .newlines_before = preceding,
                .protected_text  = protected_text,
            });
            begin          = index;
            preceding      = count;
            protected_text = false;
        }
        column = advance_columns(gap, column, style.tab_width);
        plan.token_columns.push(usize(column));
        plan.token_lines.push(usize(plan.lines.len()));
        auto token_text = syntax.text(index);
        protected_text |=
            syntax.tokens[index].syntax.protected_text || layout_newlines(token_text) != 0;
        column = advance_columns(token_text, column, style.tab_width);
    }
    if (begin < syntax.tokens.len())
        plan.lines.push(PlannedLine {
            .extent          = { begin, syntax.tokens.len() },
            .width           = column,
            .scope_id        = syntax.tokens[begin].syntax.scope_id,
            .newlines_before = preceding,
            .protected_text  = protected_text,
        });
}

auto continuation_column(const LayoutPlan& plan, usize index) -> usize {
    const auto& gap = plan.gaps[index];
    return gap.anchor != index && gap.anchor < plan.token_columns.len()
               ? plan.token_columns[gap.anchor]
               : gap.indent;
}

auto group_one_per_line(const FormatGroup& group, const FormatStyle& style) -> bool {
    if (group.kind == GroupKind::Control && group.control == ControlKind::For) return true;
    if (group.kind == GroupKind::Arguments) return ! style.bin_pack_arguments;
    if (group.kind == GroupKind::Parameters)
        return style.bin_pack_parameters != ParameterPacking::BinPack;
    if (group.kind == GroupKind::ConstructorInitializers)
        return style.pack_constructor_initializers != ConstructorPacking::BinPack;
    return group.kind == GroupKind::Initializer &&
           (style.braced_list_style == BracedListStyle::Block || ! style.bin_pack_arguments);
}

auto group_next_line(const FormatGroup& group, const FormatStyle& style) -> bool {
    if (group.kind == GroupKind::Parameters)
        return style.allow_all_parameters_of_declaration_on_next_line &&
               style.bin_pack_parameters != ParameterPacking::AlwaysOnePerLine;
    if (group.kind == GroupKind::ConstructorInitializers)
        return style.pack_constructor_initializers == ConstructorPacking::NextLine;
    return (group.kind == GroupKind::Arguments || group.kind == GroupKind::Initializer) &&
           style.allow_all_arguments_on_next_line;
}

auto group_flat_width(const FormatSyntax& syntax,
                      const LayoutPlan&   plan,
                      const FormatGroup&  group,
                      const FormatStyle&  style,
                      bool                include_close = true) -> Option<usize> {
    if (group.items.is_empty() || group.close >= syntax.tokens.len()) return None();
    auto first = group.items[usize {}].begin;
    if (first >= group.close) return None();
    auto width = usize {};
    auto end   = group.close + usize(include_close ? 1 : 0);
    for (auto index = first; index < end; ++index) {
        if (syntax.tokens[index].source.comment || layout_newlines(syntax.text(index)) != 0 ||
            (index != first &&
             (plan.gaps[index].breaking == GapBreak::Hard || ! plan.gaps[index].editable)))
            return None();
        if (index != first) width += usize(plan.gaps[index].flat_spaces);
        width = advance_columns(syntax.text(index), width, style.tab_width);
    }
    return Some(width);
}

auto group_next_line_width(const FormatSyntax& syntax,
                           const LayoutPlan&   plan,
                           usize               group_index,
                           const FormatStyle&  style) -> Option<usize> {
    const auto& group   = syntax.groups[group_index];
    auto separate_close = plan.groups[group_index].before_close &&
                          group.close < syntax.tokens.len() && plan.gaps[group.close].editable;
    return group_flat_width(syntax, plan, group, style, ! separate_close);
}

auto refresh_group_indents(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style)
    -> bool {
    auto changed = false;
    for (auto index = usize {}; index < syntax.groups.len(); ++index) {
        const auto& group = syntax.groups[index];
        auto&       state = plan.groups[index];
        if (! group.safe || group.open >= plan.token_lines.len() || group.items.is_empty() ||
            (group.kind != GroupKind::Arguments && group.kind != GroupKind::Parameters &&
             group.kind != GroupKind::Control && group.kind != GroupKind::Initializer))
            continue;
        auto line  = plan.token_lines[group.open];
        auto base  = plan.token_columns[plan.lines[line].extent.begin];
        auto width = state.continuation_indent - state.base_indent;
        changed |= state.base_indent != base;
        state.base_indent         = base;
        state.continuation_indent = base + width;
        for (const auto& item : group.items) {
            if (item.begin >= group.close || item.begin >= syntax.tokens.len()) continue;
            auto& gap = plan.gaps[item.begin];
            if (gap.group != index) continue;
            gap.indent = state.continuation_indent;
            if (gap.editable && gap.broken)
                set_gap_break(plan, item.begin, continuation_column(plan, item.begin), style);
        }
        if (group.close < syntax.tokens.len()) plan.gaps[group.close].indent = base;
    }
    return changed;
}

auto refresh_expression_indents(const FormatSyntax& syntax,
                                LayoutPlan&         plan,
                                const FormatStyle&  style) -> bool {
    auto changed = false;
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        auto& gap    = plan.gaps[index];
        auto  indent = usize::MAX;
        if (gap.indent_anchor < plan.token_lines.len()) {
            auto line = plan.token_lines[gap.indent_anchor];
            indent    = plan.token_columns[plan.lines[line].extent.begin] + gap.indent_offset;
            changed |= gap.indent != indent;
            gap.indent = indent;
        } else if (gap.anchor < plan.token_columns.len())
            indent = continuation_column(plan, index);
        if (indent == usize::MAX) continue;
        auto original = gap_text(syntax, gap);
        if (gap.editable && gap.broken &&
            advance_columns(original, usize {}, style.tab_width) != indent) {
            set_gap_break(plan, index, indent, style, layout_newlines(original));
            changed = true;
            rebuild_lines(syntax, plan, style);
        }
    }
    return changed;
}

auto sync_group_closes(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style)
    -> void {
    for (auto index = usize {}; index < syntax.groups.len(); ++index) {
        const auto& group = syntax.groups[index];
        const auto& state = plan.groups[index];
        if (! group.safe || ! state.before_close || group.items.is_empty() ||
            group.close >= syntax.tokens.len())
            continue;
        auto first = group.items[usize {}].begin;
        if (first < group.close && plan.gaps[first].broken && plan.gaps[group.close].editable)
            set_gap_break(plan, group.close, state.base_indent, style);
    }
}

auto refresh_layout_indents(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style)
    -> void {
    auto passes =
        syntax.groups.len() + syntax.expressions.len() + syntax.conditionals.len() + usize(1);
    for (auto pass = usize {}; pass < passes; ++pass) {
        rebuild_lines(syntax, plan, style);
        auto changed = refresh_group_indents(syntax, plan, style);
        rebuild_lines(syntax, plan, style);
        changed |= refresh_expression_indents(syntax, plan, style);
        sync_group_closes(syntax, plan, style);
        if (! changed) break;
    }
    rebuild_lines(syntax, plan, style);
}

auto break_after_group_open(const FormatSyntax& syntax,
                            LayoutPlan&         plan,
                            usize               group_index,
                            const FormatStyle&  style) -> void {
    const auto& group = syntax.groups[group_index];
    auto&       state = plan.groups[group_index];
    auto        first = group.items[usize {}].begin;
    for (auto index = group.open + usize(1); index < group.close; ++index) {
        auto& gap = plan.gaps[index];
        if (gap.group != group_index) continue;
        gap.anchor = usize::MAX;
        gap.indent = state.continuation_indent;
        if (gap.editable && gap.broken) set_gap_break(plan, index, gap.indent, style);
    }
    set_gap_break(plan, first, state.continuation_indent, style);
    state.expanded = true;
}

auto break_group(const FormatSyntax& syntax,
                 LayoutPlan&         plan,
                 usize               group_index,
                 usize               chosen,
                 const FormatStyle&  style) -> void {
    if (group_index >= syntax.groups.len()) return;
    const auto& group = syntax.groups[group_index];
    if (! group.safe || group.items.is_empty()) return;
    if (group.kind == GroupKind::ConstructorInitializers && plan.gaps[group.open].editable) {
        set_gap_break(plan, group.open, plan.gaps[group.open].indent, style);
        rebuild_lines(syntax, plan, style);
    }
    auto& state    = plan.groups[group_index];
    auto  all_next = group_next_line(group, style) && ! state.tried_next_line;
    if (all_next && group.items[usize {}].begin < plan.gaps.len()) {
        state.tried_next_line = true;
        auto first            = group.items[usize {}].begin;
        auto width            = group_next_line_width(syntax, plan, group_index, style);
        auto constructor      = group.kind == GroupKind::ConstructorInitializers;
        auto indent =
            constructor ? plan.token_columns[first] : plan.groups[group_index].continuation_indent;
        if (width.is_some() && style.column_limit != 0 &&
            indent + *width <= usize(style.column_limit)) {
            if (plan.gaps[first].editable) {
                if (constructor)
                    set_gap_spaces(plan, first, 1);
                else
                    break_after_group_open(syntax, plan, group_index, style);
            }
            for (auto item = usize(1); item < group.items.len(); ++item) {
                auto index = group.items[item].begin;
                if (plan.gaps[index].editable && plan.gaps[index].breaking != GapBreak::Hard)
                    set_gap_spaces(plan, index, 1);
            }
            return;
        }
    }
    if (! group_one_per_line(group, style)) return;
    if (group.kind == GroupKind::Initializer && style.braced_list_style == BracedListStyle::Block) {
        auto first = group.items[usize {}].begin;
        if (plan.gaps[first].editable) set_gap_break(plan, first, plan.gaps[first].indent, style);
    }
    for (auto item = usize(1); item < group.items.len(); ++item) {
        auto index = group.items[item].begin;
        if (plan.gaps[index].editable)
            set_gap_break(plan, index, continuation_column(plan, index), style);
    }
    if ((group.kind == GroupKind::Initializer &&
         style.braced_list_style == BracedListStyle::Block) &&
        group.close < syntax.tokens.len() && plan.gaps[group.close].editable)
        set_gap_break(plan, group.close, plan.gaps[group.close].indent, style);
    (void)chosen;
}

auto pending_constructor(const FormatSyntax& syntax,
                         const LayoutPlan&   plan,
                         usize               token,
                         const FormatStyle&  style) -> usize {
    auto result = usize::MAX;
    if (style.pack_constructor_initializers != ConstructorPacking::BinPack)
        for (auto index = usize {}; index < syntax.groups.len(); ++index) {
            const auto& group = syntax.groups[index];
            if (group.safe && group.kind == GroupKind::ConstructorInitializers &&
                group.open <= token && token < group.close && plan.gaps[group.open].editable &&
                ! plan.gaps[group.open].broken)
                result = index;
        }
    return result;
}

auto wrap_lines(const FormatSyntax& syntax, LayoutPlan& plan, const FormatStyle& style) -> void {
    rebuild_lines(syntax, plan, style);
    for (auto group_index = usize {}; group_index < syntax.groups.len(); ++group_index) {
        const auto& group = syntax.groups[group_index];
        if (group.safe && group.kind == GroupKind::Parameters &&
            style.bin_pack_parameters == ParameterPacking::AlwaysOnePerLine) {
            for (auto item = usize(1); item < group.items.len(); ++item) {
                auto index = group.items[item].begin;
                if (plan.gaps[index].editable)
                    set_gap_break(plan, index, continuation_column(plan, index), style);
            }
        }
        if (group.safe && group.kind == GroupKind::ConstructorInitializers &&
            style.pack_constructor_initializers == ConstructorPacking::Never) {
            if (plan.gaps[group.open].editable)
                set_gap_break(plan, group.open, plan.gaps[group.open].indent, style);
            rebuild_lines(syntax, plan, style);
            for (auto item = usize(1); item < group.items.len(); ++item) {
                auto index = group.items[item].begin;
                if (plan.gaps[index].editable)
                    set_gap_break(plan, index, continuation_column(plan, index), style);
            }
        }
    }
    if (style.column_limit == 0) {
        refresh_layout_indents(syntax, plan, style);
        return;
    }
    auto max_passes = syntax.tokens.len() + syntax.groups.len() * usize(2);
    for (auto pass = usize {}; pass < max_passes; ++pass) {
        refresh_layout_indents(syntax, plan, style);
        auto opening = usize::MAX;
        for (auto index = usize {}; index < syntax.groups.len(); ++index) {
            const auto& group = syntax.groups[index];
            const auto& state = plan.groups[index];
            if (! group.safe || ! state.after_open || state.expanded || group.items.is_empty())
                continue;
            auto first = group.items[usize {}].begin;
            if (first >= group.close || ! plan.gaps[first].editable || plan.gaps[first].broken)
                continue;
            auto width = group_flat_width(syntax, plan, group, style);
            if (width.is_some() && plan.token_columns[first] + *width > usize(style.column_limit) &&
                (opening == usize::MAX || group.open < syntax.groups[opening].open))
                opening = index;
        }
        if (opening != usize::MAX) {
            auto constructor =
                pending_constructor(syntax, plan, syntax.groups[opening].open, style);
            if (constructor != usize::MAX) {
                break_group(syntax, plan, constructor, usize::MAX, style);
                continue;
            }
            break_after_group_open(syntax, plan, opening, style);
            break_group(syntax, plan, opening, usize::MAX, style);
            continue;
        }
        auto chosen        = usize::MAX;
        auto overflow_line = usize::MAX;
        auto line_index    = usize {};
        for (const auto& line : plan.lines) {
            if (line.width <= usize(style.column_limit)) {
                ++line_index;
                continue;
            }
            auto fallback = usize::MAX;
            for (auto index = line.extent.begin + usize(1); index < line.extent.end; ++index) {
                const auto& gap = plan.gaps[index];
                if (gap.editable && ! gap.broken && gap.breaking == GapBreak::Soft) {
                    if (fallback == usize::MAX) fallback = index;
                    if (plan.token_columns[index] <= usize(style.column_limit)) chosen = index;
                }
            }
            if (chosen == usize::MAX) chosen = fallback;
            if (chosen != usize::MAX) {
                overflow_line = line_index;
                break;
            }
            ++line_index;
        }
        if (chosen == usize::MAX) break;
        auto constructor = pending_constructor(syntax, plan, chosen, style);
        if (constructor != usize::MAX) {
            break_group(syntax, plan, constructor, chosen, style);
            continue;
        }
        auto next_line = usize::MAX;
        for (auto index = usize {}; index < syntax.groups.len(); ++index) {
            const auto& group = syntax.groups[index];
            const auto& state = plan.groups[index];
            if (! group.safe || group.items.is_empty() || state.tried_next_line ||
                ! group_next_line(group, style) || group.open >= plan.token_lines.len() ||
                plan.token_lines[group.open] != overflow_line)
                continue;
            auto first = group.items[usize {}].begin;
            if (first >= group.close || ! plan.gaps[first].editable || plan.gaps[first].broken)
                continue;
            auto width      = group_flat_width(syntax, plan, group, style);
            auto next_width = group_next_line_width(syntax, plan, index, style);
            if (width.is_some() && plan.token_columns[first] + *width > usize(style.column_limit) &&
                next_width.is_some() &&
                state.continuation_indent + *next_width <= usize(style.column_limit) &&
                (next_line == usize::MAX || group.open < syntax.groups[next_line].open))
                next_line = index;
        }
        if (next_line != usize::MAX) {
            break_group(syntax, plan, next_line, chosen, style);
            continue;
        }
        auto group = plan.gaps[chosen].group;
        set_gap_break(plan, chosen, continuation_column(plan, chosen), style);
        auto paired = plan.gaps[chosen].paired;
        if (paired < syntax.tokens.len() && plan.gaps[paired].editable)
            set_gap_break(plan, paired, continuation_column(plan, paired), style);
        break_group(syntax, plan, group, chosen, style);
    }
    refresh_layout_indents(syntax, plan, style);
}

auto emit_layout(const FormatSyntax& syntax, LayoutPlan& plan) -> lexical::Result<String> {
    auto edits = Vec<WhitespaceEdit>::make();
    for (auto& gap : plan.gaps)
        if (gap.replacement.is_some() &&
            gap.replacement->as_str() != syntax.text(gap.span.begin, gap.span.end))
            edits.push(WhitespaceEdit { .span        = gap.span,
                                        .replacement = rstd::move(gap.replacement).unwrap() });
    for (auto& edit : plan.extra_edits) edits.push(rstd::move(edit));
    rstd::slice_::sort_unstable_by(
        edits.as_mut_slice().as_mut_ref(), [](const WhitespaceEdit& a, const WhitespaceEdit& b) {
            return a.span.begin != b.span.begin ? a.span.begin < b.span.begin
                                                : a.span.end < b.span.end;
        });
    auto output          = String::make();
    auto cursor          = usize {};
    auto previous_insert = usize::MAX;
    for (const auto& edit : edits) {
        if (edit.span.begin < cursor || edit.span.end < edit.span.begin ||
            edit.span.end > syntax.bytes().len() ||
            (edit.span.begin == edit.span.end && edit.span.begin == previous_insert))
            return Err(lexical::Error::make("overlapping format whitespace edits"_str));
        auto original = syntax.text(edit.span.begin, edit.span.end);
        for (auto byte : original.as_bytes())
            if (byte != u8(' ') && byte != u8('\t') && byte != u8('\r') && byte != u8('\n') &&
                byte != u8('\f') && byte != u8('\v'))
                return Err(lexical::Error::make("format edit would change a source token"_str));
        output.push_str(syntax.text(cursor, edit.span.begin));
        output.push_str(edit.replacement.as_str());
        cursor          = edit.span.end;
        previous_insert = edit.span.begin == edit.span.end ? edit.span.begin : usize::MAX;
    }
    output.push_str(syntax.text(cursor, syntax.bytes().len()));
    return Ok(rstd::move(output));
}

} // namespace lito::frontend::format
