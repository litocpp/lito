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

struct LayoutPlan {
    Vec<PlannedGap>     gaps;
    Vec<PlannedLine>    lines;
    Vec<usize>          token_columns;
    Vec<usize>          token_lines;
    Vec<WhitespaceEdit> extra_edits;
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
    if (group.kind == GroupKind::Arguments) return ! style.bin_pack_arguments;
    if (group.kind == GroupKind::Parameters)
        return style.bin_pack_parameters != ParameterPacking::BinPack;
    if (group.kind == GroupKind::ConstructorInitializers)
        return style.pack_constructor_initializers != ConstructorPacking::BinPack;
    return group.kind == GroupKind::Initializer &&
           style.braced_list_style == BracedListStyle::Block;
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
    auto all_next = group.kind == GroupKind::Parameters &&
                    style.allow_all_parameters_of_declaration_on_next_line;
    all_next |= group.kind == GroupKind::ConstructorInitializers &&
                style.pack_constructor_initializers == ConstructorPacking::NextLine;
    if (all_next && group.items[usize {}].begin < plan.gaps.len()) {
        auto first = group.items[usize {}].begin;
        auto end   = group.close < syntax.tokens.len()
                         ? group.close
                         : group.items[group.items.len() - usize(1)].end - usize(1);
        auto width = usize {};
        for (auto index = first; index <= end; ++index) {
            if (index != first) width += usize(plan.gaps[index].flat_spaces);
            width = advance_columns(syntax.text(index), width, style.tab_width);
        }
        auto constructor = group.kind == GroupKind::ConstructorInitializers;
        auto indent      = constructor ? plan.token_columns[first] : plan.gaps[first].indent;
        if (style.column_limit != 0 && indent + width <= usize(style.column_limit)) {
            if (plan.gaps[first].editable) {
                if (constructor)
                    set_gap_spaces(plan, first, 1);
                else
                    set_gap_break(plan, first, indent, style);
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
        rebuild_lines(syntax, plan, style);
        return;
    }
    for (auto pass = usize {}; pass < syntax.tokens.len(); ++pass) {
        rebuild_lines(syntax, plan, style);
        auto chosen = usize::MAX;
        for (const auto& line : plan.lines) {
            if (line.width <= usize(style.column_limit)) continue;
            for (auto index = line.extent.begin + usize(1); index < line.extent.end; ++index) {
                const auto& gap = plan.gaps[index];
                if (gap.editable && ! gap.broken && gap.breaking == GapBreak::Soft &&
                    plan.token_columns[index] <= usize(style.column_limit))
                    chosen = index;
            }
            if (chosen != usize::MAX) break;
        }
        if (chosen == usize::MAX) break;
        auto constructor = usize::MAX;
        if (style.pack_constructor_initializers != ConstructorPacking::BinPack)
            for (auto index = usize {}; index < syntax.groups.len(); ++index) {
                const auto& group = syntax.groups[index];
                if (group.safe && group.kind == GroupKind::ConstructorInitializers &&
                    group.open <= chosen && chosen < group.close &&
                    plan.gaps[group.open].editable && ! plan.gaps[group.open].broken)
                    constructor = index;
            }
        if (constructor != usize::MAX) {
            break_group(syntax, plan, constructor, chosen, style);
            continue;
        }
        auto group = plan.gaps[chosen].group;
        set_gap_break(plan, chosen, continuation_column(plan, chosen), style);
        break_group(syntax, plan, group, chosen, style);
    }
    rebuild_lines(syntax, plan, style);
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
