export module lito.frontend.format:layout;

import rstd;
import lito.frontend.lexical;
import :syntax;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

struct TokenLayout {
    unsigned indent {};
    unsigned parens {};
};

class LayoutCursor {
    TokenLayout next_;

public:
    auto advance(const FormatSyntax& syntax, usize index) -> TokenLayout {
        const auto& token = syntax.tokens[index];
        if (token.source.comment || token.syntax.protected_text) return next_;
        auto text = syntax.text(index);
        if (text == "}"_str && token.syntax.block != BlockKind::None && next_.indent != 0)
            --next_.indent;
        if ((text == ")"_str || text == "]"_str) && next_.parens != 0) --next_.parens;
        auto current = next_;
        if (text == "{"_str && token.syntax.block != BlockKind::None) ++next_.indent;
        if (text == "("_str || text == "["_str) ++next_.parens;
        return current;
    }
};

auto gap_spaces(const FormatSyntax& syntax, usize index) -> Option<unsigned> {
    const auto& left  = syntax.tokens[index - usize(1)];
    const auto& right = syntax.tokens[index];
    auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
    if (! left.source.comment && ! right.source.comment &&
        left.source.kind == lexical::TokenKind::Punctuation &&
        right.source.kind == lexical::TokenKind::Punctuation) {
        auto structural = a == "("_str || (a == "["_str && b != "["_str) || b == ")"_str ||
                          b == "]"_str || b == ";"_str || b == ","_str || a == ","_str ||
                          a == ";"_str || right.syntax.arrow_role == ArrowRole::TrailingReturn ||
                          (right.syntax.block != BlockKind::None && b == "{"_str);
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
    if (a == ";"_str || a == ","_str) return Some(1u);
    if (b == ";"_str || b == ","_str || b == ")"_str || b == "]"_str) return Some(0u);
    if (a == "("_str || (a == "["_str && b != "["_str)) return Some(0u);
    if (b == "("_str) {
        if (right.syntax.paren_role == ParenRole::Control ||
            right.syntax.paren_role == ParenRole::Expression)
            return Some(1u);
        if (right.syntax.paren_role == ParenRole::Call) return Some(0u);
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
    if (left.syntax.operator_role == OperatorRole::Binary ||
        right.syntax.operator_role == OperatorRole::Binary)
        return Some(1u);
    return None();
}

auto layout_source(const FormatSyntax& syntax) -> lexical::Result<String> {
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
    LayoutCursor cursor;
    TokenLayout  previous;
    auto         previous_end = usize {};
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        const auto& token   = syntax.tokens[index];
        auto        current = cursor.advance(syntax, index);
        auto        begin   = previous_end;
        auto        gap     = syntax.text(begin, token.source.begin);
        bool        plain   = true;
        unsigned    newlines {};
        for (auto value : gap.as_bytes()) {
            if (! whitespace(value)) plain = false;
            if (value == u8('\n') || (value == u8('\r') && newline == "\r"_str)) ++newlines;
        }
        const auto protected_gap =
            token.syntax.protected_text || syntax.overlaps(begin, token.source.begin) ||
            (index != usize {} && syntax.tokens[index - usize(1)].syntax.protected_text);
        if (! plain || protected_gap)
            output.push_str(gap);
        else if (index == usize {}) {
            for (unsigned count {}; count < current.indent * 4; ++count) output.push_ascii(' ');
        } else {
            const auto& left = syntax.tokens[index - usize(1)];
            auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
            auto        force_newline =
                left.source.line_comment ||
                (a == "{"_str && left.syntax.block != BlockKind::None) ||
                (b == "}"_str && token.syntax.block != BlockKind::None) ||
                (a == ";"_str && previous.parens == 0) ||
                (a == "}"_str && left.syntax.block != BlockKind::None && b != ";"_str &&
                 b != ","_str && b != ")"_str && ! token.syntax.block_continuation);
            if (force_newline || newlines != 0) {
                if (! force_newline && current.parens != 0)
                    output.push_str(gap);
                else {
                    if (newlines == 0) newlines = 1;
                    for (unsigned count {}; count < newlines; ++count) output.push_str(newline);
                    for (unsigned count {}; count < current.indent * 4; ++count)
                        output.push_ascii(' ');
                }
            } else {
                auto spaces = gap_spaces(syntax, index);
                if (spaces.is_none())
                    output.push_str(gap);
                else
                    for (unsigned count {}; count < *spaces; ++count) output.push_ascii(' ');
            }
        }
        output.push_str(syntax.text(index));
        previous_end = token.source.end;
        previous     = current;
    }
    auto suffix = syntax.text(previous_end, syntax.bytes().len());
    if (syntax.tokens[syntax.tokens.len() - usize(1)].syntax.protected_text ||
        syntax.overlaps(previous_end, syntax.bytes().len()))
        output.push_str(suffix);
    else
        output.push_str(newline);
    return Ok(rstd::move(output));
}

} // namespace lito::frontend::format
