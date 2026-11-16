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
    Inline,
    InlineOnly,
    All,
};

enum class ParameterPacking
{
    BinPack,
    OnePerLine,
    AlwaysOnePerLine
};
enum class OperandAlignment
{
    DontAlign,
    Align
};
enum class BinaryOperatorBreak
{
    None,
    NonAssignment,
    All
};
enum class EscapedNewlineAlignment
{
    DontAlign,
    Left,
    Right
};
enum class TrailingCommentAlignment
{
    Leave,
    Never,
    Always
};
enum class PointerAlignment
{
    Left,
    Right,
    Middle
};
enum class ReferenceAlignment
{
    Pointer,
    Left,
    Right,
    Middle
};
enum class PointerQualifierSpacing
{
    Default,
    Before,
    After,
    Both
};
enum class BitFieldColonSpacing
{
    Both,
    None,
    Before,
    After
};
enum class EmptyLineBeforeAccess
{
    Never,
    Leave,
    LogicalBlock,
    Always
};
enum class EmptyLineAfterAccess
{
    Never,
    Leave,
    Always
};
enum class AnglesSpacing
{
    Never,
    Always,
    Leave
};
enum class BracedListStyle
{
    Block,
    FunctionCall,
    AlignFirstComment
};
enum class TemplateBreakStyle
{
    Leave,
    No,
    MultiLine,
    Yes
};
enum class ReturnTypeBreakStyle
{
    Automatic,
    All
};
enum class AttributeBreakStyle
{
    Leave,
    Never,
    Always
};
enum class ConstructorPacking
{
    Never,
    BinPack,
    CurrentLine,
    NextLine
};
enum class ShortIfStyle
{
    Never,
    WithoutElse,
    OnlyFirstIf,
    AllIfsAndElse
};
enum class ShortLambdaStyle
{
    None,
    Empty,
    Inline,
    All
};
enum class PreprocessorIndent
{
    Preserve,
    None
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
    unsigned                 indent_width { 4 };
    unsigned                 tab_width { 4 };
    UseTab                   use_tab { UseTab::Never };
    NamespaceIndentation     namespace_indentation { NamespaceIndentation::All };
    BraceStyle               break_before_braces { BraceStyle::Attach };
    BraceWrapping            brace_wrapping;
    SpaceBeforeParens        space_before_parens { SpaceBeforeParens::ControlStatements };
    bool                     space_before_assignment_operators { true };
    SpacesInParens           spaces_in_parens { SpacesInParens::Never };
    SpacesInParensOptions    spaces_in_parens_options;
    int                      access_modifier_offset { -2 };
    bool                     indent_access_modifiers {};
    ShortFunctionStyle       short_functions { ShortFunctionStyle::None };
    ShortBlockStyle          short_blocks { ShortBlockStyle::Never };
    ShortIfStyle             short_if { ShortIfStyle::Never };
    ShortLambdaStyle         short_lambdas { ShortLambdaStyle::None };
    bool                     short_loops {};
    bool                     short_case_labels {};
    unsigned                 column_limit {};
    unsigned                 continuation_indent_width { 4 };
    unsigned                 constructor_initializer_indent_width { 4 };
    bool                     align_after_open_bracket {};
    bool                     break_after_open_bracket_function {};
    bool                     break_after_open_bracket_braced_list {};
    bool                     break_after_open_bracket_if {};
    bool                     break_after_open_bracket_loop {};
    bool                     break_after_open_bracket_switch {};
    bool                     break_before_close_bracket_function {};
    bool                     break_before_close_bracket_braced_list {};
    bool                     break_before_close_bracket_if {};
    bool                     break_before_close_bracket_loop {};
    bool                     break_before_close_bracket_switch {};
    bool                     bin_pack_arguments { true };
    bool                     allow_all_arguments_on_next_line {};
    ParameterPacking         bin_pack_parameters { ParameterPacking::BinPack };
    bool                     allow_all_parameters_of_declaration_on_next_line { true };
    OperandAlignment         align_operands { OperandAlignment::DontAlign };
    BinaryOperatorBreak      break_before_binary_operators { BinaryOperatorBreak::None };
    bool                     break_before_ternary_operators {};
    bool                     align_consecutive_macros {};
    bool                     align_consecutive_assignments {};
    bool                     align_consecutive_declarations {};
    EscapedNewlineAlignment  align_escaped_newlines { EscapedNewlineAlignment::DontAlign };
    TrailingCommentAlignment align_trailing_comments { TrailingCommentAlignment::Leave };
    PointerAlignment         pointer_alignment { PointerAlignment::Right };
    ReferenceAlignment       reference_alignment { ReferenceAlignment::Pointer };
    bool                     derive_pointer_alignment {};
    PointerQualifierSpacing  space_around_pointer_qualifiers { PointerQualifierSpacing::Default };
    BitFieldColonSpacing     bit_field_colon_spacing { BitFieldColonSpacing::Both };
    AnglesSpacing            spaces_in_angles { AnglesSpacing::Never };
    BracedListStyle          braced_list_style { BracedListStyle::FunctionCall };
    bool                     spaces_in_square_brackets {};
    bool                     space_after_c_style_cast {};
    bool                     space_after_logical_not {};
    bool                     space_after_template_keyword { true };
    bool                     space_before_cpp11_braced_list {};
    bool                     space_before_ctor_initializer_colon { true };
    bool                     space_before_inheritance_colon { true };
    bool                     space_before_range_based_for_loop_colon { true };
    bool                     space_before_case_colon {};
    bool                     space_before_square_brackets {};
    bool                     indent_case_labels {};
    bool                     indent_goto_labels { true };
    EmptyLineBeforeAccess    empty_line_before_access_modifier { EmptyLineBeforeAccess::Leave };
    EmptyLineAfterAccess     empty_line_after_access_modifier { EmptyLineAfterAccess::Leave };
    bool                     indent_wrapped_function_names {};
    TemplateBreakStyle       break_template_declarations { TemplateBreakStyle::Leave };
    ReturnTypeBreakStyle     break_after_return_type { ReturnTypeBreakStyle::Automatic };
    AttributeBreakStyle      break_after_attributes { AttributeBreakStyle::Leave };
    ConstructorPacking       pack_constructor_initializers { ConstructorPacking::BinPack };
    PreprocessorIndent       indent_pp_directives { PreprocessorIndent::Preserve };
    unsigned                 max_empty_lines_to_keep { unsigned(-1) };
    KeepEmptyLines           keep_empty_lines;

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
        style.short_lambdas                     = ShortLambdaStyle::All;
        style.column_limit                      = 80;
        style.align_after_open_bracket          = true;
        style.allow_all_arguments_on_next_line  = true;
        style.align_operands                    = OperandAlignment::Align;
        style.break_before_ternary_operators    = true;
        style.align_escaped_newlines            = EscapedNewlineAlignment::Right;
        style.align_trailing_comments           = TrailingCommentAlignment::Always;
        style.braced_list_style                 = BracedListStyle::AlignFirstComment;
        style.break_template_declarations       = TemplateBreakStyle::MultiLine;
        style.indent_pp_directives              = PreprocessorIndent::None;
        style.empty_line_before_access_modifier = EmptyLineBeforeAccess::LogicalBlock;
        style.empty_line_after_access_modifier  = EmptyLineAfterAccess::Never;
        style.max_empty_lines_to_keep           = 1;
        style.keep_empty_lines.at_start_of_file = true;
        style.keep_empty_lines.at_end_of_block  = false;
        return style;
    }
};

} // namespace lito::frontend::format
