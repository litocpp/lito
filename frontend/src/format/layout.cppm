export module lito.frontend.format:layout;

import rstd;
import lito.frontend.lexical;
import :syntax;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::format
{

auto gap_spaces(const FormatSyntax& syntax, usize index) -> Option<unsigned> {
    const auto& left  = syntax.tokens[index - usize(1)];
    const auto& right = syntax.tokens[index];
    auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
    if (! left.comment && ! right.comment && left.kind == lexical::TokenKind::Punctuation &&
        right.kind == lexical::TokenKind::Punctuation) {
        auto structural = a == "("_str || (a == "["_str && b != "["_str) || b == ")"_str ||
                          b == "]"_str || b == ";"_str || b == ","_str || a == ","_str ||
                          a == ";"_str || right.arrow_role == ArrowRole::TrailingReturn ||
                          (right.block != BlockKind::None && b == "{"_str);
        if (! structural) return None();
    }
    if (left.end == right.begin && ((left.kind == lexical::TokenKind::StringLiteral ||
                                     left.kind == lexical::TokenKind::CharacterLiteral ||
                                     left.kind == lexical::TokenKind::PpNumber) &&
                                    right.kind == lexical::TokenKind::Identifier))
        return None();
    if (left.kind == lexical::TokenKind::PpNumber && b == "."_str) return None();
    if (left.comment || right.comment) return Some(1u);
    if (a == ";"_str || a == ","_str) return Some(1u);
    if (b == ";"_str || b == ","_str || b == ")"_str || b == "]"_str) return Some(0u);
    if (a == "("_str || (a == "["_str && b != "["_str)) return Some(0u);
    if (b == "("_str) {
        if (right.paren_role == ParenRole::Control || right.paren_role == ParenRole::Expression)
            return Some(1u);
        if (right.paren_role == ParenRole::Call) return Some(0u);
    }
    if (a == "::"_str || b == "::"_str || a == "."_str || b == "."_str ||
        left.arrow_role != ArrowRole::None || right.arrow_role != ArrowRole::None) {
        if (left.arrow_role == ArrowRole::TrailingReturn ||
            right.arrow_role == ArrowRole::TrailingReturn)
            return Some(1u);
        return Some(0u);
    }
    if ((a == ":"_str || b == ":"_str) && (left.module_statement || right.module_statement))
        return Some(a == "import"_str || a == "module"_str ? 1u : 0u);
    if (right.block != BlockKind::None && b == "{"_str) return Some(1u);
    if (a == "}"_str && right.block_continuation) return Some(1u);
    if (left.expression_prefix) return Some(1u);
    if (left.word_like && right.word_like) return Some(1u);
    if (left.operator_role == OperatorRole::Binary || right.operator_role == OperatorRole::Binary)
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
    String output;
    auto   previous_end = usize {};
    for (auto index = usize {}; index < syntax.tokens.len(); ++index) {
        const auto& token = syntax.tokens[index];
        auto        begin = previous_end;
        auto        gap   = syntax.text(begin, token.begin);
        bool        plain = true;
        unsigned    newlines {};
        for (auto value : gap.as_bytes()) {
            if (! whitespace(value)) plain = false;
            if (value == u8('\n') || (value == u8('\r') && newline == "\r"_str)) ++newlines;
        }
        const auto protected_gap =
            token.protected_text || syntax.overlaps(begin, token.begin) ||
            (index != usize {} && syntax.tokens[index - usize(1)].protected_text);
        if (! plain || protected_gap)
            output.push_str(gap);
        else if (index == usize {}) {
            for (unsigned count {}; count < token.indent * 4; ++count) output.push_ascii(' ');
        } else {
            const auto& left = syntax.tokens[index - usize(1)];
            auto        a = syntax.text(index - usize(1)), b = syntax.text(index);
            auto force_newline = left.line_comment ||
                                 (a == "{"_str && left.block != BlockKind::None) ||
                                 (b == "}"_str && token.block != BlockKind::None) ||
                                 (a == ";"_str && left.parens == 0) ||
                                 (a == "}"_str && left.block != BlockKind::None && b != ";"_str &&
                                  b != ","_str && b != ")"_str && ! token.block_continuation);
            if (force_newline || newlines != 0) {
                if (! force_newline && token.parens != 0)
                    output.push_str(gap);
                else {
                    if (newlines == 0) newlines = 1;
                    for (unsigned count {}; count < newlines; ++count) output.push_str(newline);
                    for (unsigned count {}; count < token.indent * 4; ++count)
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
        previous_end = token.end;
    }
    auto suffix = syntax.text(previous_end, syntax.bytes().len());
    if (syntax.tokens[syntax.tokens.len() - usize(1)].protected_text ||
        syntax.overlaps(previous_end, syntax.bytes().len()))
        output.push_str(suffix);
    else
        output.push_str(newline);
    return Ok(rstd::move(output));
}

} // namespace lito::frontend::format
