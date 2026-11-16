export module lito.frontend.format:style;

import rstd;

using namespace rstd::prelude;

export namespace lito::frontend::format
{

enum class UseTab
{
    Never,
    ForIndentation,
};

enum class NamespaceIndentation
{
    None,
    Inner,
    All,
};

enum class BraceStyle
{
    Attach,
    Allman,
};

enum class SpaceBeforeParens
{
    Never,
    ControlStatements,
    Always,
};

enum class ShortFunctionStyle
{
    None,
    Empty,
    All,
};

enum class ShortBlockStyle
{
    Never,
    Empty,
    Always,
};

struct KeepEmptyLines {
    bool at_start_of_file {};
    bool at_end_of_file {};
    bool at_start_of_block { true };
    bool at_end_of_block { true };
};

struct FormatStyle {
    unsigned             indent_width { 4 };
    unsigned             tab_width { 4 };
    UseTab               use_tab { UseTab::Never };
    NamespaceIndentation namespace_indentation { NamespaceIndentation::All };
    BraceStyle           break_before_braces { BraceStyle::Attach };
    SpaceBeforeParens    space_before_parens { SpaceBeforeParens::ControlStatements };
    bool                 space_before_assignment_operators { true };
    bool                 spaces_in_parentheses {};
    bool                 space_in_empty_parentheses {};
    ShortFunctionStyle   short_functions { ShortFunctionStyle::None };
    ShortBlockStyle      short_blocks { ShortBlockStyle::Never };
    unsigned             max_empty_lines_to_keep { unsigned(-1) };
    KeepEmptyLines       keep_empty_lines;

    static auto llvm() -> FormatStyle {
        auto style                              = FormatStyle {};
        style.indent_width                      = 2;
        style.tab_width                         = 8;
        style.namespace_indentation             = NamespaceIndentation::None;
        style.short_functions                   = ShortFunctionStyle::All;
        style.max_empty_lines_to_keep           = 1;
        style.keep_empty_lines.at_start_of_file = true;
        style.keep_empty_lines.at_end_of_block  = false;
        return style;
    }
};

} // namespace lito::frontend::format
