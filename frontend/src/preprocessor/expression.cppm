export module lito.frontend.preprocessor:expression;

import rstd;
import lito.frontend.lexical;
import :traits;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace lito::frontend::lexical;

export namespace lito::frontend::preprocessor
{
struct ExpressionInteger {
    i64  value {};
    bool is_unsigned {};

    ExpressionInteger() = default;
    ExpressionInteger(i64 number, bool unsigned_value = false)
        : value(number), is_unsigned(unsigned_value) {}

    auto bits() const -> u64 { return as_cast<u64>(value); }
    auto negative() const -> bool { return ! is_unsigned && value < i64 {}; }
    auto operator==(ExpressionInteger other) const -> bool { return value == other.value; }
    auto operator<(ExpressionInteger other) const -> bool {
        return is_unsigned || other.is_unsigned ? bits() < other.bits() : value < other.value;
    }
    auto operator>(ExpressionInteger other) const -> bool { return other < *this; }
    auto operator<=(ExpressionInteger other) const -> bool { return ! (other < *this); }
    auto operator>=(ExpressionInteger other) const -> bool { return ! (*this < other); }
    auto operator~() const -> ExpressionInteger { return { ~value, is_unsigned }; }
    auto operator|(ExpressionInteger other) const -> ExpressionInteger {
        return { value | other.value, is_unsigned || other.is_unsigned };
    }
    auto operator^(ExpressionInteger other) const -> ExpressionInteger {
        return { value ^ other.value, is_unsigned || other.is_unsigned };
    }
    auto operator&(ExpressionInteger other) const -> ExpressionInteger {
        return { value & other.value, is_unsigned || other.is_unsigned };
    }
    auto wrapping_add(ExpressionInteger other) const -> ExpressionInteger {
        return { value.wrapping_add(other.value), is_unsigned || other.is_unsigned };
    }
    auto wrapping_sub(ExpressionInteger other) const -> ExpressionInteger {
        return { value.wrapping_sub(other.value), is_unsigned || other.is_unsigned };
    }
    auto wrapping_mul(ExpressionInteger other) const -> ExpressionInteger {
        return { value.wrapping_mul(other.value), is_unsigned || other.is_unsigned };
    }
    auto wrapping_div(ExpressionInteger other) const -> ExpressionInteger {
        if (is_unsigned || other.is_unsigned) return { as_cast<i64>(bits() / other.bits()), true };
        return { value.wrapping_div(other.value) };
    }
    auto wrapping_rem(ExpressionInteger other) const -> ExpressionInteger {
        if (is_unsigned || other.is_unsigned) return { as_cast<i64>(bits() % other.bits()), true };
        return { value.wrapping_rem(other.value) };
    }
    auto wrapping_neg() const -> ExpressionInteger { return { value.wrapping_neg(), is_unsigned }; }
    auto wrapping_shl(u64 count) const -> ExpressionInteger {
        return { value.wrapping_shl(count), is_unsigned };
    }
    auto wrapping_shr(u64 count) const -> ExpressionInteger {
        return { is_unsigned ? as_cast<i64>(bits().wrapping_shr(count)) : value.wrapping_shr(count),
                 is_unsigned };
    }
};
} // namespace lito::frontend::preprocessor

namespace lito::frontend::preprocessor
{

class ExpressionParser {
public:
    explicit ExpressionParser(slice<Token> tokens): tokens_(tokens) {}

    auto parse() -> Result<ExpressionInteger> {
        auto result = conditional();
        if (result.is_err()) return result;
        if (index_ != tokens_.len())
            return failure("unexpected token in preprocessor expression"_str);
        return result;
    }

private:
    auto failure(ref<str> message) -> Result<ExpressionInteger> {
        if (index_ < tokens_.len()) {
            return Err(Error::at(
                rstd::format("{} before '{}'", message, tokens_[index_].text.display().as_str()),
                tokens_[index_].expansion));
        }
        if (! tokens_.is_empty()) {
            return Err(Error::at(message.into(), tokens_[tokens_.len() - usize(1)].expansion));
        }
        return Err(Error::make(message));
    }

    auto match(ref<str> value) -> bool {
        if (index_ >= tokens_.len() || tokens_[index_].text != value) return false;
        ++index_;
        return true;
    }

    auto conditional() -> Result<ExpressionInteger> {
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
        auto selected        = *condition != i64 {} ? *when_true : *when_false;
        selected.is_unsigned = when_true->is_unsigned || when_false->is_unsigned;
        if (! outer) selected.value = i64 {};
        return Ok(selected);
    }

    auto logical_or() -> Result<ExpressionInteger> {
        auto left = logical_and();
        while (left.is_ok() && match("||"_str)) {
            auto outer  = evaluating_;
            evaluating_ = outer && *left == i64 {};
            auto right  = logical_and();
            evaluating_ = outer;
            if (right.is_err()) return right;
            left = Ok(ExpressionInteger(i64(outer && (*left != i64 {} || *right != i64 {}))));
        }
        return left;
    }

    auto logical_and() -> Result<ExpressionInteger> {
        auto left = bit_or();
        while (left.is_ok() && match("&&"_str)) {
            auto outer  = evaluating_;
            evaluating_ = outer && *left != i64 {};
            auto right  = bit_or();
            evaluating_ = outer;
            if (right.is_err()) return right;
            left = Ok(ExpressionInteger(i64(outer && *left != i64 {} && *right != i64 {})));
        }
        return left;
    }

    auto bit_or() -> Result<ExpressionInteger> {
        auto left = bit_xor();
        while (left.is_ok() && match("|"_str)) {
            auto right = bit_xor();
            if (right.is_err()) return right;
            left = Ok(*left | *right);
        }
        return left;
    }

    auto bit_xor() -> Result<ExpressionInteger> {
        auto left = bit_and();
        while (left.is_ok() && match("^"_str)) {
            auto right = bit_and();
            if (right.is_err()) return right;
            left = Ok(*left ^ *right);
        }
        return left;
    }

    auto bit_and() -> Result<ExpressionInteger> {
        auto left = equality();
        while (left.is_ok() && match("&"_str)) {
            auto right = equality();
            if (right.is_err()) return right;
            left = Ok(*left & *right);
        }
        return left;
    }

    auto equality() -> Result<ExpressionInteger> {
        auto left = relational();
        while (left.is_ok()) {
            if (match("=="_str)) {
                auto right = relational();
                if (right.is_err()) return right;
                left = Ok(ExpressionInteger(i64(*left == *right)));
            } else if (match("!="_str)) {
                auto right = relational();
                if (right.is_err()) return right;
                left = Ok(ExpressionInteger(i64(*left != *right)));
            } else {
                break;
            }
        }
        return left;
    }

    auto relational() -> Result<ExpressionInteger> {
        auto left = shift();
        while (left.is_ok()) {
            if (match("<"_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(ExpressionInteger(i64(*left < *right)));
            } else if (match(">"_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(ExpressionInteger(i64(*left > *right)));
            } else if (match("<="_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(ExpressionInteger(i64(*left <= *right)));
            } else if (match(">="_str)) {
                auto right = shift();
                if (right.is_err()) return right;
                left = Ok(ExpressionInteger(i64(*left >= *right)));
            } else {
                break;
            }
        }
        return left;
    }

    auto shift() -> Result<ExpressionInteger> {
        auto left = additive();
        while (left.is_ok()) {
            if (match("<<"_str)) {
                auto right = additive();
                if (right.is_err()) return right;
                if (evaluating_ && (*right < i64 {} || *right >= i64(64))) {
                    return failure("invalid left shift"_str);
                }
                if (! evaluating_) {
                    left->value = i64 {};
                    continue;
                }
                left = Ok(left->wrapping_shl(right->bits()));
            } else if (match(">>"_str)) {
                auto right = additive();
                if (right.is_err()) return right;
                if (evaluating_ && (*right < i64 {} || *right >= i64(64))) {
                    return failure("invalid right shift"_str);
                }
                if (! evaluating_) {
                    left->value = i64 {};
                    continue;
                }
                left = Ok(left->wrapping_shr(right->bits()));
            } else {
                break;
            }
        }
        return left;
    }

    auto additive() -> Result<ExpressionInteger> {
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

    auto multiplicative() -> Result<ExpressionInteger> {
        auto left = unary();
        while (left.is_ok()) {
            if (match("*"_str)) {
                auto right = unary();
                if (right.is_err()) return right;
                left = Ok(left->wrapping_mul(*right));
            } else if (match("/"_str) || match("%"_str)) {
                auto remainder = tokens_[index_ - usize(1)].text == "%"_str;
                auto right     = unary();
                if (right.is_err()) return right;
                if (evaluating_ && *right == i64 {}) {
                    return failure("division by zero in preprocessor expression"_str);
                }
                left = evaluating_
                           ? Ok(remainder ? left->wrapping_rem(*right) : left->wrapping_div(*right))
                           : Ok(ExpressionInteger(i64 {}, left->is_unsigned || right->is_unsigned));
            } else {
                break;
            }
        }
        return left;
    }

    auto unary() -> Result<ExpressionInteger> {
        if (match("!"_str)) {
            auto value = unary();
            if (value.is_err()) return value;
            return Ok(ExpressionInteger(i64(*value == i64 {})));
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

    auto primary() -> Result<ExpressionInteger> {
        if (match("("_str)) {
            auto value = conditional();
            if (value.is_err()) return value;
            if (! match(")"_str)) return failure("expected ')' in preprocessor expression"_str);
            return value;
        }
        if (index_ >= tokens_.len()) return failure("expected preprocessor expression"_str);
        const auto& token = tokens_[index_++];
        if ((token.kind == TokenKind::Identifier || token.kind == TokenKind::PpNumber) &&
            token.text.utf8().is_err()) {
            return Err(Error::at("invalid UTF-8 preprocessing token"_Str, token.expansion));
        }
        if (token.kind == TokenKind::Identifier) return Ok(ExpressionInteger {});
        if (token.kind == TokenKind::CharacterLiteral) {
            auto bytes = token.text.as_bytes();
            if (bytes.len() >= usize(3))
                return Ok(ExpressionInteger(i64(bytes[bytes.len() - usize(2)].to_primitive())));
            return failure("invalid character constant"_str);
        }
        if (token.kind != TokenKind::PpNumber) return failure("expected integer constant"_str);
        auto bytes  = token.text.as_bytes();
        auto base   = uint32_t(10);
        auto cursor = usize {};
        auto value  = u64 {};
        auto digits = false;
        if (bytes.len() > usize(1) && bytes[usize {}] == u8('0')) {
            base   = 8;
            cursor = usize(1);
            digits = true;
            if (cursor < bytes.len() && (bytes[cursor] == u8('x') || bytes[cursor] == u8('X'))) {
                base   = 16;
                digits = false;
                ++cursor;
            } else if (cursor < bytes.len() &&
                       (bytes[cursor] == u8('b') || bytes[cursor] == u8('B'))) {
                base   = 2;
                digits = false;
                ++cursor;
            }
        }
        while (cursor < bytes.len()) {
            auto byte = bytes[cursor];
            if (byte == u8('\'')) {
                ++cursor;
                continue;
            }
            auto digit = uint32_t(99);
            if (byte >= u8('0') && byte <= u8('9'))
                digit = byte.to_primitive() - '0';
            else if (byte >= u8('a') && byte <= u8('f'))
                digit = byte.to_primitive() - 'a' + 10;
            else if (byte >= u8('A') && byte <= u8('F'))
                digit = byte.to_primitive() - 'A' + 10;
            if (digit >= base) break;
            if (value > (u64::MAX - u64(digit)) / u64(base))
                return failure("integer constant exceeds 64-bit range"_str);
            value  = value * u64(base) + u64(digit);
            digits = true;
            ++cursor;
        }
        if (! digits && token.text != "0"_str) {
            return Err(Error::at(
                rstd::format("invalid integer constant '{}'", token.text.display().as_str()),
                token.expansion));
        }
        auto       suffix          = token.text.utf8().unwrap().get(cursor, bytes.len()).unwrap();
        const bool unsigned_suffix = suffix.contains("u"_str) || suffix.contains("U"_str);
        auto       normalized      = String::make();
        for (auto byte : suffix.as_bytes()) {
            normalized.push_ascii(byte == u8('U')   ? 'u'
                                  : byte == u8('L') ? 'l'
                                  : byte == u8('Z') ? 'z'
                                                    : byte.to_primitive());
        }
        auto text = normalized.as_str();
        if (! (text.is_empty() || text == "u"_str || text == "l"_str || text == "ll"_str ||
               text == "ul"_str || text == "lu"_str || text == "ull"_str || text == "llu"_str ||
               text == "z"_str || text == "uz"_str || text == "zu"_str))
            return failure("invalid integer suffix"_str);
        return Ok(ExpressionInteger(as_cast<i64>(value),
                                    unsigned_suffix || value > as_cast<u64>(i64::MAX)));
    }

    slice<Token> tokens_;
    usize        index_ {};
    bool         evaluating_ { true };
};

} // namespace lito::frontend::preprocessor

export namespace lito::frontend::preprocessor
{

auto evaluate_expression(slice<Token> tokens) -> Result<ExpressionInteger> {
    return ExpressionParser(tokens).parse();
}

} // namespace lito::frontend::preprocessor
