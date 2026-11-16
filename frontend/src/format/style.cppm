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
    Custom,
};

enum class ControlBraceStyle
{
    Never,
    MultiLine,
    Always,
};

enum class SpacesInParens
{
    Never,
    Custom,
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

struct SpacesInParensOptions {
    bool except_double_parentheses {};
    bool in_conditional_statements {};
    bool in_c_style_casts {};
    bool in_empty_parentheses {};
    bool other {};
};

struct BraceWrapping {
    bool              after_class {};
    bool              after_struct {};
    bool              after_union {};
    bool              after_enum {};
    bool              after_namespace {};
    bool              after_function {};
    ControlBraceStyle after_control_statement { ControlBraceStyle::Never };
    bool              before_else {};
    bool              before_catch {};
    bool              indent_braces {};
    bool              split_empty_function { true };
    bool              split_empty_record { true };
    bool              split_empty_namespace { true };
};

struct FormatStyle {
    unsigned              indent_width { 4 };
    unsigned              tab_width { 4 };
    UseTab                use_tab { UseTab::Never };
    NamespaceIndentation  namespace_indentation { NamespaceIndentation::All };
    BraceStyle            break_before_braces { BraceStyle::Attach };
    BraceWrapping         brace_wrapping;
    SpaceBeforeParens     space_before_parens { SpaceBeforeParens::ControlStatements };
    bool                  space_before_assignment_operators { true };
    SpacesInParens        spaces_in_parens { SpacesInParens::Never };
    SpacesInParensOptions spaces_in_parens_options;
    int                   access_modifier_offset { -2 };
    bool                  indent_access_modifiers {};
    ShortFunctionStyle    short_functions { ShortFunctionStyle::None };
    ShortBlockStyle       short_blocks { ShortBlockStyle::Never };
    unsigned              max_empty_lines_to_keep { unsigned(-1) };
    KeepEmptyLines        keep_empty_lines;

    auto effective_parens_options() const -> SpacesInParensOptions {
        return spaces_in_parens == SpacesInParens::Custom ? spaces_in_parens_options
                                                          : SpacesInParensOptions {};
    }

    auto effective_brace_wrapping() const -> BraceWrapping {
        if (break_before_braces == BraceStyle::Custom) return brace_wrapping;
        auto wrapping = BraceWrapping {};
        if (break_before_braces == BraceStyle::Allman) {
            wrapping.after_class             = true;
            wrapping.after_struct            = true;
            wrapping.after_union             = true;
            wrapping.after_enum              = true;
            wrapping.after_namespace         = true;
            wrapping.after_function          = true;
            wrapping.after_control_statement = ControlBraceStyle::Always;
            wrapping.before_else             = true;
            wrapping.before_catch            = true;
        }
        return wrapping;
    }

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
