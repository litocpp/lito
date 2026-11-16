export module lito.frontend.format:model;

import rstd;
import lito.frontend.lexical;

using namespace rstd::prelude;

export namespace lito::frontend::format
{

enum class BlockKind
{
    None,
    Function,
    Control,
    Namespace,
    Class,
    Struct,
    Union,
    Enum,
    Lambda,
    Initializer
};
enum class OperatorRole
{
    Unknown,
    Binary,
    Assignment,
    PointerReference,
    Unary
};
enum class ParenRole
{
    Unknown,
    Control,
    Call,
    Expression
};
enum class ArrowRole
{
    None,
    Member,
    TrailingReturn
};
enum class ParenSpacing
{
    Unknown,
    Other,
    Conditional,
    Cast
};
enum class ControlKind
{
    None,
    If,
    For,
    While,
    Switch,
    Catch,
    Else,
    Do,
    Try
};
enum class ColonRole
{
    Unknown,
    Constructor,
    Inheritance,
    RangeFor,
    Case,
    Conditional
};
enum class SquareRole
{
    Unknown,
    Subscript,
    Capture,
    Attribute,
    Binding
};
enum class GroupKind
{
    Arguments,
    Parameters,
    TemplateParameters,
    TemplateArguments,
    Initializer,
    ConstructorInitializers,
    Inheritance,
    Attribute,
    LambdaCapture
};
enum class StatementKind
{
    Unknown,
    Declaration,
    Assignment,
    Function,
    Lambda,
    Control,
    CaseLabel,
    Return,
    Expression
};
enum class WhitespaceSlotKind
{
    Leading,
    AfterMarker,
    MacroReplacement,
    Continuation
};

struct TextRange {
    usize begin;
    usize end;
};
// Token ranges are half-open. Text ranges and editable slots use byte offsets.
struct TokenRange {
    usize begin;
    usize end;
};

struct TokenSource {
    usize              begin;
    usize              end;
    lexical::TokenKind kind;
    bool               comment {};
    bool               line_comment {};
    bool               start_of_line {};
};

struct TokenSyntax {
    usize        pair { usize::MAX };
    usize        header_begin { usize::MAX };
    usize        scope_id { usize::MAX };
    usize        statement_id { usize::MAX };
    usize        group_id { usize::MAX };
    BlockKind    block { BlockKind::None };
    ControlKind  control { ControlKind::None };
    OperatorRole operator_role { OperatorRole::Unknown };
    ParenRole    paren_role { ParenRole::Unknown };
    ParenSpacing paren_spacing { ParenSpacing::Unknown };
    ArrowRole    arrow_role { ArrowRole::None };
    ColonRole    colon_role { ColonRole::Unknown };
    SquareRole   square_role { SquareRole::Unknown };
    bool         protected_text {};
    bool         module_statement {};
    bool         word_like {};
    bool         expression_prefix {};
    bool         block_continuation {};
    bool         brace_continuation {};
    bool         else_continuation {};
    bool         header_multiline {};
    bool         access_label {};
    bool         access_colon {};
    bool         access_end {};
    bool         enum_separator {};
    bool         double_paren {};
    bool         cast_operand {};
    bool         simple_block {};
    unsigned     block_statements {};
    bool         template_open {};
    unsigned     template_closes {};
    bool         template_declaration_end {};
    bool         attribute_end {};
    bool         logical_not {};
    bool         inline_record_scope {};
    bool         initializer_open {};
    bool         initializer_close {};
};

struct FormatToken {
    TokenSource source;
    TokenSyntax syntax;
};

struct FormatGroup {
    GroupKind       kind { GroupKind::Arguments };
    usize           open { usize::MAX };
    usize           close { usize::MAX };
    usize           scope_id { usize::MAX };
    Vec<TokenRange> items;
    Vec<usize>      separators;
    bool            safe { true };
};

struct FormatStatement {
    StatementKind kind { StatementKind::Unknown };
    TokenRange    extent;
    usize         scope_id { usize::MAX };
    usize         name_index { usize::MAX };
    usize         type_end { usize::MAX };
    usize         assignment_index { usize::MAX };
    usize         parameters_group { usize::MAX };
    usize         return_type_end { usize::MAX };
    usize         body_begin { usize::MAX };
    usize         body_open { usize::MAX };
    usize         body_end { usize::MAX };
    usize         else_statement { usize::MAX };
    ControlKind   control { ControlKind::None };
    bool          simple {};
    bool          inline_record_scope {};
    bool          compound_assignment {};
    bool          has_else {};
};

struct ExpressionChain {
    TokenRange extent;
    usize      scope_id { usize::MAX };
    usize      first_operand { usize::MAX };
    Vec<usize> operators;
};

struct WhitespaceSlot {
    WhitespaceSlotKind kind { WhitespaceSlotKind::Leading };
    TextRange          span;
    usize              line_begin {};
    bool               editable {};
};

struct FormatDirective {
    usize                  begin;
    usize                  end;
    lexical::DirectiveKind kind { lexical::DirectiveKind::Empty };
    usize                  marker_index { usize::MAX };
    usize                  keyword_index { usize::MAX };
    usize                  name_index { usize::MAX };
    usize                  prefix_end { usize::MAX };
    usize                  replacement_begin { usize::MAX };
    Vec<WhitespaceSlot>    safe_slots;
    bool                   function_like {};
    bool                   has_stringize {};
    bool                   has_paste {};
    bool                   locked {};
};

struct FormatSyntax {
    lexical::SharedSourceSnapshot snapshot;
    Vec<FormatToken>              tokens;
    Vec<TextRange>                protected_ranges;
    Vec<TextRange>                locked_ranges;
    Vec<FormatDirective>          directives;
    Vec<String>                   macros;
    Vec<FormatStatement>          statements;
    Vec<FormatGroup>              groups;
    Vec<ExpressionChain>          expressions;
    bool                          preserve_all {};

    auto bytes() const -> slice<u8> { return snapshot->contents.as_bytes(); }
    auto text(usize begin, usize end) const -> ref<str> {
        return rstd::str_::from_utf8(
                   slice<u8>::from_raw_parts(bytes().as_ptr() + begin.to_primitive(), end - begin))
            .unwrap();
    }
    auto text(usize index) const -> ref<str> {
        return text(tokens[index].source.begin, tokens[index].source.end);
    }
    auto is(usize index, ref<str> spelling) const -> bool {
        return index < tokens.len() && text(index) == spelling;
    }
    auto next_token(usize index, usize end = usize::MAX) const -> usize {
        if (index < tokens.len()) ++index;
        while (index < tokens.len() && tokens[index].source.comment) ++index;
        return index < tokens.len() && tokens[index].source.begin < end ? index : tokens.len();
    }
    auto previous_token(usize index) const -> usize {
        while (index != usize {}) {
            --index;
            if (! tokens[index].source.comment) return index;
        }
        return usize::MAX;
    }
    auto line_begin(usize offset) const -> usize {
        return lexical::physical_line_begin(bytes(), offset);
    }
    auto line_end(usize offset) const -> usize {
        return lexical::physical_line_end(bytes(), offset);
    }
    auto overlaps(usize begin, usize end) const -> bool {
        for (const auto& range : protected_ranges)
            if (range.begin < end && begin < range.end) return true;
        return false;
    }
    auto directive(usize offset) const -> bool {
        for (const auto& range : directives) {
            if (range.begin > offset) break;
            if (offset < range.end) return true;
        }
        return false;
    }
};

} // namespace lito::frontend::format
