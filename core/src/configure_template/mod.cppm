module;
#include <rstd/enum.hpp>

export module lito.core:configure_template;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class ConfigureValueKind
{
    String,
    Integer,
    Boolean,
};

class ConfigureValue {
public:
    static auto from_string(String value) -> ConfigureValue {
        auto result    = ConfigureValue {};
        result.kind_   = ConfigureValueKind::String;
        result.string_ = rstd::move(value);
        return result;
    }

    static auto from_integer(i64 value) -> ConfigureValue {
        auto result     = ConfigureValue {};
        result.kind_    = ConfigureValueKind::Integer;
        result.integer_ = value;
        return result;
    }

    static auto from_boolean(bool value) -> ConfigureValue {
        auto result     = ConfigureValue {};
        result.kind_    = ConfigureValueKind::Boolean;
        result.boolean_ = value;
        return result;
    }

    auto kind() const noexcept -> ConfigureValueKind { return kind_; }
    auto string() const noexcept -> ref<str> { return string_.as_str(); }
    auto integer() const noexcept -> i64 { return integer_; }
    auto boolean() const noexcept -> bool { return boolean_; }

private:
    ConfigureValueKind kind_ { ConfigureValueKind::String };
    String             string_;
    i64                integer_ {};
    bool               boolean_ { false };
};

using ConfigureValues = rstd::collections::BTreeMap<String, ConfigureValue>;

class TemplateError {
    RSTD_ENUM(TemplateError, (Message, (String message;)))
};

template<typename T>
using TemplateResult = Result<T, TemplateError>;

auto configure_placeholder_name_is_valid(ref<str> name) noexcept -> bool {
    if (name.is_empty()) return false;
    auto first = name[usize()].to_primitive();
    auto alpha = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
    if (! (alpha || first == '_')) return false;
    for (auto index = usize(1); index < name.len(); ++index) {
        auto value  = name[index].to_primitive();
        auto letter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        auto digit  = value >= '0' && value <= '9';
        if (! (letter || digit || value == '_')) return false;
    }
    return true;
}

auto render_configure_template(ref<str>               input,
                               const ConfigureValues& values,
                               ref<rstd::path::Path>  source) -> TemplateResult<String> {
    auto output        = String::make();
    auto used          = rstd::collections::BTreeMap<String, empty>::make();
    auto literal_begin = usize();
    auto index         = usize();
    while (index < input.len()) {
        if (input[index] != u8('@')) {
            ++index;
            continue;
        }
        auto literal = input.get(literal_begin, index);
        if (literal.is_some()) output.push_str(*literal);
        if (index + usize(1) < input.len() && input[index + usize(1)] == u8('@')) {
            output.push_ascii('@');
            index += usize(2);
            literal_begin = index;
            continue;
        }

        auto end = index + usize(1);
        while (end < input.len() && input[end] != u8('@')) ++end;
        if (end == input.len()) {
            return Err(TemplateError::Message(rstd::format(
                "template '{}' has an unclosed placeholder at byte {}", source, index)));
        }
        auto name = input.get(index + usize(1), end);
        if (name.is_none() || ! configure_placeholder_name_is_valid(*name)) {
            return Err(TemplateError::Message(rstd::format(
                "template '{}' has an invalid placeholder at byte {}", source, index)));
        }
        auto value = values.get(*name);
        if (value.is_none()) {
            return Err(TemplateError::Message(
                rstd::format("template '{}' is missing value '{}'", source, *name)));
        }
        switch ((**value).kind()) {
        case ConfigureValueKind::String: output.push_str((**value).string()); break;
        case ConfigureValueKind::Integer: {
            auto text = rstd::format("{}", (**value).integer());
            output.push_str(text.as_str());
            break;
        }
        case ConfigureValueKind::Boolean:
            output.push_str((**value).boolean() ? "true"_str : "false"_str);
            break;
        }
        used.insert(String::make(*name), empty {});
        index         = end + usize(1);
        literal_begin = index;
    }
    auto tail = input.get(literal_begin, input.len());
    if (tail.is_some()) output.push_str(*tail);

    auto keys = values.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (used.contains_key((**key).as_str())) continue;
        return Err(TemplateError::Message(
            rstd::format("template '{}' does not use value '{}'", source, (**key).as_str())));
    }
    return Ok(rstd::move(output));
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::TemplateError> : ImplBase<lito::TemplateError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::TemplateError> : ImplBase<lito::TemplateError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::TemplateError> : DefaultInImpl<error::Error, lito::TemplateError> {
};

} // namespace rstd
