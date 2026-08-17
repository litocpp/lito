module;
#include <rstd/macro.hpp>

export module lito.core:condition;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::condition
{

enum class ValueKind
{
    Boolean,
    String,
};

struct Value {
    ValueKind kind { ValueKind::Boolean };
    bool      boolean {};
    String    string;

    static auto boolean_value(bool value) -> Value {
        return Value { .kind = ValueKind::Boolean, .boolean = value };
    }

    static auto string_value(String value) -> Value {
        return Value { .kind = ValueKind::String, .string = rstd::move(value) };
    }
};

enum class ExpressionKind
{
    Boolean,
    String,
    Key,
    Equal,
    NotEqual,
    Not,
    And,
    Or,
};

struct Expression {
    ExpressionKind          kind { ExpressionKind::Boolean };
    usize                   position {};
    bool                    boolean {};
    String                  text;
    Option<Box<Expression>> left;
    Option<Box<Expression>> right;

    auto clone() const -> Expression {
        auto result = Expression {
            .kind     = kind,
            .position = position,
            .boolean  = boolean,
            .text     = text.clone(),
        };
        if (left.is_some()) result.left = Some(Box<Expression>::make((**left).clone()));
        if (right.is_some()) result.right = Some(Box<Expression>::make((**right).clone()));
        return result;
    }
};

struct ParseError {
    String expression;
    String message;
    usize  position {};
};

template<typename T>
using ParseResult = Result<T, ParseError>;

class Context {
    rstd::collections::BTreeMap<String, Value> values_ {
        rstd::collections::BTreeMap<String, Value>::make()
    };

public:
    auto set_bool(String key, bool value) -> void {
        values_.insert(rstd::move(key), Value::boolean_value(value));
    }

    auto set_string(String key, String value) -> void {
        values_.insert(rstd::move(key), Value::string_value(rstd::move(value)));
    }

    auto get(ref<str> key) const noexcept -> Option<ref<Value>> { return values_.get(key); }
};

struct EvaluationError {
    String message;
    usize  position {};
};

using EvaluationResult = Result<bool, EvaluationError>;

auto parse(ref<str> expression) -> ParseResult<Expression>;
auto evaluate(const Expression& expression, const Context& context) -> EvaluationResult;

} // namespace lito::condition

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::condition::ParseError> : ImplBase<lito::condition::ParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error   = this->self();
        auto        message = rstd::format("{} at byte {} in condition '{}'",
                                           error.message.as_str(),
                                           error.position,
                                           error.expression.as_str());
        return formatter.write_str(message.as_str());
    }
};

template<>
struct Impl<fmt::Display, lito::condition::EvaluationError>
    : ImplBase<lito::condition::EvaluationError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error   = this->self();
        auto        message = rstd::format("{} at byte {}", error.message.as_str(), error.position);
        return formatter.write_str(message.as_str());
    }
};

} // namespace rstd

namespace lito::condition
{

enum class TokenKind
{
    End,
    Identifier,
    String,
    Boolean,
    Equal,
    NotEqual,
    Not,
    And,
    Or,
    LeftParen,
    RightParen,
};

struct Token {
    TokenKind kind { TokenKind::End };
    usize     position {};
    String    text;
    bool      boolean {};
};

class Parser {
    ref<str> input_;
    usize    cursor_ {};
    Token    current_;

    auto failure(ref<str> message, usize position) const -> ParseError {
        return ParseError {
            .expression = String::make(input_),
            .message    = String::make(message),
            .position   = position,
        };
    }

    auto failure(String message, usize position) const -> ParseError {
        return ParseError {
            .expression = String::make(input_),
            .message    = rstd::move(message),
            .position   = position,
        };
    }

    static auto identifier_start(u8 value) -> bool {
        return (value >= u8('a') && value <= u8('z')) || (value >= u8('A') && value <= u8('Z')) ||
               value == u8('_');
    }

    static auto identifier_continue(u8 value) -> bool {
        return identifier_start(value) || (value >= u8('0') && value <= u8('9')) ||
               value == u8('-');
    }

    auto next_token() -> ParseResult<Token> {
        const auto bytes = input_.as_bytes();
        while (cursor_ < bytes.len() &&
               (bytes[cursor_] == u8(' ') || bytes[cursor_] == u8('\t') ||
                bytes[cursor_] == u8('\n') || bytes[cursor_] == u8('\r'))) {
            ++cursor_;
        }
        const auto position = cursor_;
        if (cursor_ >= bytes.len()) return Ok(Token { .position = position });
        const auto value = bytes[cursor_++];
        if (value == u8('('))
            return Ok(Token { .kind = TokenKind::LeftParen, .position = position });
        if (value == u8(')'))
            return Ok(Token { .kind = TokenKind::RightParen, .position = position });
        if (value == u8('!')) {
            if (cursor_ < bytes.len() && bytes[cursor_] == u8('=')) {
                ++cursor_;
                return Ok(Token { .kind = TokenKind::NotEqual, .position = position });
            }
            return Ok(Token { .kind = TokenKind::Not, .position = position });
        }
        if (value == u8('=') && cursor_ < bytes.len() && bytes[cursor_] == u8('=')) {
            ++cursor_;
            return Ok(Token { .kind = TokenKind::Equal, .position = position });
        }
        if (value == u8('&') && cursor_ < bytes.len() && bytes[cursor_] == u8('&')) {
            ++cursor_;
            return Ok(Token { .kind = TokenKind::And, .position = position });
        }
        if (value == u8('|') && cursor_ < bytes.len() && bytes[cursor_] == u8('|')) {
            ++cursor_;
            return Ok(Token { .kind = TokenKind::Or, .position = position });
        }
        if (value == u8('"')) {
            auto text = String::make();
            while (cursor_ < bytes.len() && bytes[cursor_] != u8('"')) {
                auto byte = bytes[cursor_++];
                if (byte == u8('\\')) {
                    if (cursor_ >= bytes.len())
                        return Err(failure("unterminated escape"_str, position));
                    const auto escaped = bytes[cursor_++];
                    if (escaped != u8('\\') && escaped != u8('"')) {
                        return Err(failure("unsupported string escape"_str, cursor_ - usize(2)));
                    }
                    byte = escaped;
                }
                text.push_ascii(byte);
            }
            if (cursor_ >= bytes.len()) return Err(failure("unterminated string"_str, position));
            ++cursor_;
            return Ok(Token {
                .kind     = TokenKind::String,
                .position = position,
                .text     = rstd::move(text),
            });
        }
        if (identifier_start(value)) {
            auto text = String::make();
            text.push_ascii(value);
            while (cursor_ < bytes.len()) {
                if (identifier_continue(bytes[cursor_])) {
                    text.push_ascii(bytes[cursor_++]);
                    continue;
                }
                if (bytes[cursor_] == u8('.') && cursor_ + usize(1) < bytes.len() &&
                    identifier_start(bytes[cursor_ + usize(1)])) {
                    text.push_ascii(bytes[cursor_++]);
                    continue;
                }
                break;
            }
            if (text.as_str() == "true"_str || text.as_str() == "false"_str) {
                return Ok(Token {
                    .kind     = TokenKind::Boolean,
                    .position = position,
                    .boolean  = text.as_str() == "true"_str,
                });
            }
            return Ok(Token {
                .kind     = TokenKind::Identifier,
                .position = position,
                .text     = rstd::move(text),
            });
        }
        return Err(failure(rstd::format("unexpected character '{}'", char(value.to_primitive())),
                           position));
    }

    auto advance() -> ParseResult<empty> {
        auto token = next_token();
        if (token.is_err()) return Err(rstd::move(token).unwrap_err());
        current_ = rstd::move(token).unwrap();
        return Ok(empty {});
    }

    static auto unary(ExpressionKind kind, usize position, Expression operand) -> Expression {
        return Expression {
            .kind     = kind,
            .position = position,
            .left     = Some(Box<Expression>::make(rstd::move(operand))),
        };
    }

    static auto binary(ExpressionKind kind, usize position, Expression left, Expression right)
        -> Expression {
        return Expression {
            .kind     = kind,
            .position = position,
            .left     = Some(Box<Expression>::make(rstd::move(left))),
            .right    = Some(Box<Expression>::make(rstd::move(right))),
        };
    }

    auto primary() -> ParseResult<Expression> {
        const auto position = current_.position;
        if (current_.kind == TokenKind::Boolean) {
            const auto value = current_.boolean;
            rstd_try(advance());
            return Ok(Expression {
                .kind = ExpressionKind::Boolean, .position = position, .boolean = value });
        }
        if (current_.kind == TokenKind::String) {
            auto value = rstd::move(current_.text);
            rstd_try(advance());
            return Ok(Expression {
                .kind = ExpressionKind::String, .position = position, .text = rstd::move(value) });
        }
        if (current_.kind == TokenKind::Identifier) {
            auto value = rstd::move(current_.text);
            rstd_try(advance());
            return Ok(Expression {
                .kind = ExpressionKind::Key, .position = position, .text = rstd::move(value) });
        }
        if (current_.kind == TokenKind::LeftParen) {
            rstd_try(advance());
            auto value = rstd_try(or_expression());
            if (current_.kind != TokenKind::RightParen) {
                return Err(failure("expected ')'"_str, current_.position));
            }
            rstd_try(advance());
            return Ok(rstd::move(value));
        }
        return Err(failure("expected condition operand"_str, position));
    }

    auto unary_expression() -> ParseResult<Expression> {
        if (current_.kind != TokenKind::Not) return primary();
        const auto position = current_.position;
        rstd_try(advance());
        return Ok(unary(ExpressionKind::Not, position, rstd_try(unary_expression())));
    }

    auto equality_expression() -> ParseResult<Expression> {
        auto left = rstd_try(unary_expression());
        while (current_.kind == TokenKind::Equal || current_.kind == TokenKind::NotEqual) {
            const auto kind     = current_.kind == TokenKind::Equal ? ExpressionKind::Equal
                                                                    : ExpressionKind::NotEqual;
            const auto position = current_.position;
            rstd_try(advance());
            left = binary(kind, position, rstd::move(left), rstd_try(unary_expression()));
        }
        return Ok(rstd::move(left));
    }

    auto and_expression() -> ParseResult<Expression> {
        auto left = rstd_try(equality_expression());
        while (current_.kind == TokenKind::And) {
            const auto position = current_.position;
            rstd_try(advance());
            left = binary(
                ExpressionKind::And, position, rstd::move(left), rstd_try(equality_expression()));
        }
        return Ok(rstd::move(left));
    }

    auto or_expression() -> ParseResult<Expression> {
        auto left = rstd_try(and_expression());
        while (current_.kind == TokenKind::Or) {
            const auto position = current_.position;
            rstd_try(advance());
            left =
                binary(ExpressionKind::Or, position, rstd::move(left), rstd_try(and_expression()));
        }
        return Ok(rstd::move(left));
    }

public:
    explicit Parser(ref<str> input): input_(input) {}

    auto run() -> ParseResult<Expression> {
        rstd_try(advance());
        auto expression = rstd_try(or_expression());
        if (current_.kind != TokenKind::End) {
            return Err(failure("unexpected token"_str, current_.position));
        }
        return Ok(rstd::move(expression));
    }
};

struct ResolvedValue {
    ValueKind kind { ValueKind::Boolean };
    bool      boolean {};
    ref<str>  string;
};

auto evaluation_failure(ref<str> message, usize position) -> EvaluationError {
    return EvaluationError { .message = String::make(message), .position = position };
}

auto evaluation_failure(String message, usize position) -> EvaluationError {
    return EvaluationError { .message = rstd::move(message), .position = position };
}

auto resolve_value(const Expression& expression, const Context& context)
    -> Result<ResolvedValue, EvaluationError> {
    if (expression.kind == ExpressionKind::Boolean) {
        return Ok(ResolvedValue { .kind = ValueKind::Boolean, .boolean = expression.boolean });
    }
    if (expression.kind == ExpressionKind::String) {
        return Ok(ResolvedValue { .kind = ValueKind::String, .string = expression.text.as_str() });
    }
    if (expression.kind == ExpressionKind::Key) {
        auto value = context.get(expression.text.as_str());
        if (value.is_none()) {
            return Err(evaluation_failure(
                rstd::format("unknown condition key '{}'", expression.text.as_str()),
                expression.position));
        }
        if ((**value).kind == ValueKind::Boolean) {
            return Ok(ResolvedValue { .kind = ValueKind::Boolean, .boolean = (**value).boolean });
        }
        return Ok(ResolvedValue { .kind = ValueKind::String, .string = (**value).string.as_str() });
    }
    auto result = evaluate(expression, context);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    return Ok(ResolvedValue { .kind = ValueKind::Boolean, .boolean = *result });
}

auto require_boolean(const Expression& expression, const Context& context)
    -> Result<bool, EvaluationError> {
    auto value = resolve_value(expression, context);
    if (value.is_err()) return Err(rstd::move(value).unwrap_err());
    if (value->kind != ValueKind::Boolean) {
        return Err(
            evaluation_failure("condition operand must be boolean"_str, expression.position));
    }
    return Ok(value->boolean);
}

auto parse(ref<str> expression) -> ParseResult<Expression> {
    return Parser(expression).run();
}

auto evaluate(const Expression& expression, const Context& context) -> EvaluationResult {
    if (expression.kind == ExpressionKind::Boolean || expression.kind == ExpressionKind::Key ||
        expression.kind == ExpressionKind::String) {
        return require_boolean(expression, context);
    }
    if (expression.kind == ExpressionKind::Not) {
        auto value = require_boolean(**expression.left, context);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        return Ok(! *value);
    }
    if (expression.kind == ExpressionKind::And) {
        auto left = require_boolean(**expression.left, context);
        if (left.is_err()) return Err(rstd::move(left).unwrap_err());
        if (! *left) return Ok(false);
        return require_boolean(**expression.right, context);
    }
    if (expression.kind == ExpressionKind::Or) {
        auto left = require_boolean(**expression.left, context);
        if (left.is_err()) return Err(rstd::move(left).unwrap_err());
        if (*left) return Ok(true);
        return require_boolean(**expression.right, context);
    }
    auto left = resolve_value(**expression.left, context);
    if (left.is_err()) return Err(rstd::move(left).unwrap_err());
    auto right = resolve_value(**expression.right, context);
    if (right.is_err()) return Err(rstd::move(right).unwrap_err());
    if (left->kind != right->kind) {
        return Err(evaluation_failure("condition comparison operands have different types"_str,
                                      expression.position));
    }
    auto equal = left->kind == ValueKind::Boolean ? left->boolean == right->boolean
                                                  : left->string == right->string;
    return Ok(expression.kind == ExpressionKind::Equal ? equal : ! equal);
}

} // namespace lito::condition
