export module lito.frontend.preprocessor:expression;

import rstd;
import lito.frontend.lexical;
import :traits;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace lito::frontend::lexical;

namespace lito::frontend::preprocessor
{

class ExpressionParser {
public:
    explicit ExpressionParser(const Vec<Token>& tokens): tokens_(tokens) {}

    auto parse() -> Result<i64> {
        auto result = conditional();
        if (result.is_err()) return result;
        if (index_ != tokens_.len())
            return failure("unexpected token in preprocessor expression"_str);
        return result;
    }

private:
    auto failure(ref<str> message) -> Result<i64> {
        if (index_ < tokens_.len()) {
            return Err(Error::at(String::make(message), tokens_[index_].expansion));
        }
        if (! tokens_.is_empty()) {
            return Err(
                Error::at(String::make(message), tokens_[tokens_.len() - usize(1)].expansion));
        }
        return Err(Error::make(message));
    }

    auto match(ref<str> value) -> bool {
        if (index_ >= tokens_.len() || tokens_[index_].text.as_str() != value) return false;
        ++index_;
        return true;
    }

    auto conditional() -> Result<i64> {
        auto condition = logical_or();
        if (condition.is_err() || ! match("?"_str)) return condition;
        auto outer     = evaluating_;
        evaluating_    = outer && *condition != i64 {};
        auto when_true = conditional();
        if (when_true.is_err()) return when_true;
        if (! match(":"_str)) return failure("expected ':' in conditional expression"_str);
        evaluating_     = outer && *condition == i64 {};
        auto when_false = conditional();
        evaluating_     = outer;
        if (when_false.is_err()) return when_false;
        if (! outer) return Ok(i64 {});
        return Ok(*condition != i64 {} ? *when_true : *when_false);
    }

    auto logical_or() -> Result<i64> {
        auto left = logical_and();
        while (left.is_ok() && match("||"_str)) {
            auto outer  = evaluating_;
            evaluating_ = outer && *left == i64 {};
            auto right  = logical_and();
            evaluating_ = outer;
            if (right.is_err()) return right;
            left = Ok(outer ? i64(*left != i64 {} || *right != i64 {}) : i64 {});
        }
        return left;
    }

    auto logical_and() -> Result<i64> {
        auto left = bit_or();
        while (left.is_ok() && match("&&"_str)) {
            auto outer  = evaluating_;
            evaluating_ = outer && *left != i64 {};
            auto right  = bit_or();
            evaluating_ = outer;
            if (right.is_err()) return right;
            left = Ok(outer ? i64(*left != i64 {} && *right != i64 {}) : i64 {});
        }
        return left;
    }

    auto bit_or() -> Result<i64> {
        auto left = bit_xor();
        while (left.is_ok() && match("|"_str)) {
            auto right = bit_xor();
            if (right.is_err()) return right;
            left = Ok(*left | *right);
        }
        return left;
    }

    auto bit_xor() -> Result<i64> {
        auto left = bit_and();
        while (left.is_ok() && match("^"_str)) {
            auto right = bit_and();
            if (right.is_err()) return right;
            left = Ok(*left ^ *right);
        }
        return left;
    }

    auto bit_and() -> Result<i64> {
        auto left = equality();
        while (left.is_ok() && match("&"_str)) {
            auto right = equality();
            if (right.is_err()) return right;
            left = Ok(*left & *right);
        }
        return left;
    }

    auto equality() -> Result<i64> {
        auto left = relational();
        while (left.is_ok()) {
            if (match("=="_str)) {
                auto right = relational();
                if (right.is_err()) return right;
                left = Ok(i64(*left == *right));
            } else if (match("!="_str)) {
                auto right = relational();
                if (right.is_err()) return right;
                left = Ok(i64(*left != *right));
            } else {
                break;
            }
        }
        return left;
    }

    auto relational() -> Result<i64> {
        auto left = shift();
        while (left.is_ok()) {
            if (match("<"_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(i64(*left < *right));
            } else if (match(">"_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(i64(*left > *right));
            } else if (match("<="_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(i64(*left <= *right));
            } else if (match(">="_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(i64(*left >= *right));
            } else {
                break;
            }
        }
        return left;
    }

    auto shift() -> Result<i64> {
        auto left = additive();
        while (left.is_ok()) {
            if (match("<<"_str)) {
                auto right = additive();
                if (right.is_err()) return right;
                if (evaluating_ && (*right < i64 {} || *right >= i64(64))) {
                    return failure("invalid left shift"_str);
                }
                if (! evaluating_) {
                    left = Ok(i64 {});
                    continue;
                }
                left = Ok(left->wrapping_shl(u64(rstd::as_cast<u64>(*right))));
            } else if (match(">>"_str)) {
                auto right = additive();
                if (right.is_err()) return right;
                if (evaluating_ && (*right < i64 {} || *right >= i64(64))) {
                    return failure("invalid right shift"_str);
                }
                if (! evaluating_) {
                    left = Ok(i64 {});
                    continue;
                }
                left = Ok(left->wrapping_shr(u64(rstd::as_cast<u64>(*right))));
            } else {
                break;
            }
        }
        return left;
    }

    auto additive() -> Result<i64> {
        auto left = multiplicative();
        while (left.is_ok()) {
            if (match("+"_str)) {
                auto right = multiplicative();
                if (right.is_err()) return right;
                left = Ok(left->wrapping_add(*right));
            } else if (match("-"_str)) {
                auto right = multiplicative();
                if (right.is_err()) return right;
                left = Ok(left->wrapping_sub(*right));
            } else {
                break;
            }
        }
        return left;
    }

    auto multiplicative() -> Result<i64> {
        auto left = unary();
        while (left.is_ok()) {
            if (match("*"_str)) {
                auto right = unary();
                if (right.is_err()) return right;
                left = Ok(left->wrapping_mul(*right));
            } else if (match("/"_str) || match("%"_str)) {
                auto remainder = tokens_[index_ - usize(1)].text.as_str() == "%"_str;
                auto right     = unary();
                if (right.is_err()) return right;
                if (evaluating_ && *right == i64 {}) {
                    return failure("division by zero in preprocessor expression"_str);
                }
                left = evaluating_
                           ? Ok(remainder ? left->wrapping_rem(*right) : left->wrapping_div(*right))
                           : Ok(i64 {});
            } else {
                break;
            }
        }
        return left;
    }

    auto unary() -> Result<i64> {
        if (match("!"_str)) {
            auto value = unary();
            if (value.is_err()) return value;
            return Ok(i64(*value == i64 {}));
        }
        if (match("~"_str)) {
            auto value = unary();
            if (value.is_err()) return value;
            return Ok(~*value);
        }
        if (match("+"_str)) return unary();
        if (match("-"_str)) {
            auto value = unary();
            if (value.is_err()) return value;
            return Ok(value->wrapping_neg());
        }
        return primary();
    }

    auto primary() -> Result<i64> {
        if (match("("_str)) {
            auto value = conditional();
            if (value.is_err()) return value;
            if (! match(")"_str)) return failure("expected ')' in preprocessor expression"_str);
            return value;
        }
        if (index_ >= tokens_.len()) return failure("expected preprocessor expression"_str);
        const auto& token = tokens_[index_++];
        if (token.kind == TokenKind::Identifier) return Ok(i64 {});
        if (token.kind == TokenKind::CharacterLiteral) {
            auto bytes = token.text.as_str().as_bytes();
            if (bytes.len() >= usize(3))
                return Ok(i64(bytes[bytes.len() - usize(2)].to_primitive()));
            return failure("invalid character constant"_str);
        }
        if (token.kind != TokenKind::PpNumber) return failure("expected integer constant"_str);
        auto bytes  = token.text.as_str().as_bytes();
        auto base   = rstd::uint32_t(10);
        auto cursor = usize {};
        if (bytes.len() > usize(1) && bytes[usize {}] == u8('0')) {
            base   = 8;
            cursor = usize(1);
            if (cursor < bytes.len() && (bytes[cursor] == u8('x') || bytes[cursor] == u8('X'))) {
                base = 16;
                ++cursor;
            } else if (cursor < bytes.len() &&
                       (bytes[cursor] == u8('b') || bytes[cursor] == u8('B'))) {
                base = 2;
                ++cursor;
            }
        }
        auto value  = i64 {};
        auto digits = false;
        while (cursor < bytes.len()) {
            auto byte = bytes[cursor];
            if (byte == u8('\'')) {
                ++cursor;
                continue;
            }
            auto digit = rstd::uint32_t(99);
            if (byte >= u8('0') && byte <= u8('9'))
                digit = byte.to_primitive() - '0';
            else if (byte >= u8('a') && byte <= u8('f'))
                digit = byte.to_primitive() - 'a' + 10;
            else if (byte >= u8('A') && byte <= u8('F'))
                digit = byte.to_primitive() - 'A' + 10;
            if (digit >= base) break;
            value  = value.wrapping_mul(i64(base)).wrapping_add(i64(digit));
            digits = true;
            ++cursor;
        }
        if (! digits && token.text.as_str() != "0"_str)
            return failure("invalid integer constant"_str);
        return Ok(value);
    }

    const Vec<Token>& tokens_;
    usize             index_ {};
    bool              evaluating_ { true };
};

} // namespace lito::frontend::preprocessor

export namespace lito::frontend::preprocessor
{

auto evaluate_expression(const Vec<Token>& tokens) -> Result<i64> {
    return ExpressionParser(tokens).parse();
}

} // namespace lito::frontend::preprocessor
