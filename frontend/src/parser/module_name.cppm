export module lito.frontend.parser:module_name;

import rstd;
import rstd.parse.core;
import lito.frontend.lexical;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::frontend::parser
{

namespace parse = rstd::parse;

struct TokenSpelling {
    ref<str> value;
    auto     operator()(const lexical::Token& token) const noexcept -> bool {
        return token.text == value;
    }
};

struct TokenParseAdapter {
    using error_type = lexical::Error;
    slice<lexical::Token> tokens;

    auto error(String message, parse::Span span) const -> error_type {
        if (tokens.is_empty()) return lexical::Error::make(rstd::move(message));
        auto        index = span.begin < tokens.len() ? span.begin : tokens.len() - usize(1);
        const auto& token = tokens[index];
        return lexical::Error {
            .message  = rstd::move(message),
            .location = Some(lexical::SourceLocation(token.expansion)),
            .path     = token.presumed_path.is_some()
                            ? Some(rstd::path::PathBuf::from(token.presumed_path->as_path()))
                            : None(),
        };
    }

    auto expected(parse::RuleId rule, parse::Span span) const -> error_type {
        return error(rstd::format("expected {}", rule.name()), span);
    }
    auto stalled(parse::RuleId rule, parse::Span span) const -> error_type {
        return error(rstd::format("{} did not consume a token", rule.name()), span);
    }
    auto capacity(parse::RuleId rule, parse::Span span) const -> error_type {
        return error(rstd::format("{} exceeded parse capacity", rule.name()), span);
    }
};

struct ModuleNameRule {
    using value_type               = parse::Span;
    static constexpr bool nullable = false;
    auto id() const noexcept -> parse::RuleId { return parse::RuleId("module name"_str); }

    template<typename Adapter, typename Observer>
    auto match(parse::Driver<lexical::Token, Adapter, Observer>& driver)
        -> parse::Match<parse::Span, typename Adapter::error_type> {
        auto& cursor = driver.cursor();
        auto  start  = cursor.checkpoint();
        (void)parse::consume_if(cursor, TokenSpelling { ":"_str });
        auto identifier =
            parse::atomic(parse::RuleId("identifier"_str), [](const lexical::Token& token) {
                return token.kind == lexical::TokenKind::Identifier;
            });
        auto first = driver.apply(identifier);
        if (first.is_err()) return Err(rstd::move(first).unwrap_err());
        if (first->is_none()) return Ok(None());
        auto separator =
            parse::atomic(parse::RuleId("module separator"_str), [](const lexical::Token& token) {
                return token.text == "."_str || token.text == ":"_str;
            });
        auto component = parse::seq(
            parse::RuleId("module component"_str), rstd::move(separator), rstd::move(identifier));
        for (;;) {
            auto next = driver.apply(component);
            if (next.is_err()) return Err(rstd::move(next).unwrap_err());
            if (next->is_none()) break;
        }
        return Ok(Some(cursor.span_from(start)));
    }
};

auto parse_module_name(slice<lexical::Token> tokens, ref<str> context)
    -> lexical::Result<Option<String>> {
    if (tokens.is_empty()) return Ok(None());
    TokenParseAdapter adapter { tokens };
    const auto&       first = tokens[usize()];
    if (first.text == "<"_str || first.kind == lexical::TokenKind::StringLiteral ||
        first.kind == lexical::TokenKind::HeaderName) {
        return Err(adapter.error(rstd::format("{} uses an unsupported header unit at line {}",
                                              context,
                                              first.expansion.line),
                                 parse::Span {}));
    }
    auto rule =
        parse::seq(parse::RuleId("module name and terminator"_str),
                   ModuleNameRule {},
                   parse::atomic(parse::RuleId("semicolon"_str), TokenSpelling { ";"_str }));
    parse::NoopObserver                                                   observer;
    parse::Driver<lexical::Token, TokenParseAdapter, parse::NoopObserver> driver(
        parse::Input<lexical::Token>(tokens), adapter, observer);
    auto matched = driver.apply(rule);
    if (matched.is_err()) return Err(rstd::move(matched).unwrap_err());
    if (matched->is_none()) return Ok(None());
    auto name   = matched->unwrap().template get<0>();
    auto result = String::make();
    for (const auto& token : driver.cursor().view(name))
        result.push_str(token.text.utf8().unwrap());
    return Ok(Some(rstd::move(result)));
}

} // namespace lito::frontend::parser
