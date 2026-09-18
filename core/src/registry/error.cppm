module;
#include <rstd/enum.hpp>

export module lito.core:registry.error;

import rstd;

using namespace rstd::prelude;

export namespace lito::registry
{

class RegistryValueError {
    RSTD_ENUM(RegistryValueError, (Message, (String message;)))
};

template<typename T>
using RegistryValueResult = Result<T, RegistryValueError>;

} // namespace lito::registry

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::registry::RegistryValueError>
    : ImplBase<lito::registry::RegistryValueError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::registry::RegistryValueError>
    : ImplBase<lito::registry::RegistryValueError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::registry::RegistryValueError>
    : DefaultInImpl<error::Error, lito::registry::RegistryValueError> {};

} // namespace rstd
