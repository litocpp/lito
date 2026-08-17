module;
#include <rstd/enum.hpp>

export module lito.core:template_.configure;

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
    static auto from_string(String value) -> ConfigureValue;
    static auto from_integer(i64 value) -> ConfigureValue;
    static auto from_boolean(bool value) -> ConfigureValue;

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

auto configure_placeholder_name_is_valid(ref<str> name) noexcept -> bool;

auto render_configure_template(ref<str>               input,
                               const ConfigureValues& values,
                               ref<rstd::path::Path>  source) -> TemplateResult<String>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::TemplateError> : ImplBase<lito::TemplateError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::TemplateError> : ImplBase<lito::TemplateError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::TemplateError> : DefaultInImpl<error::Error, lito::TemplateError> {
};

} // namespace rstd
