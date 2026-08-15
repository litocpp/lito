module;
#include <rstd/enum.hpp>

export module lito.driver:install.materialize_error;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

class InstallMaterializeError {
    RSTD_ENUM(InstallMaterializeError, (Message, (String message;)))
};

template<typename T>
using InstallMaterializeResult = Result<T, InstallMaterializeError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::InstallMaterializeError> : ImplBase<lito::InstallMaterializeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::InstallMaterializeError> : ImplBase<lito::InstallMaterializeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::InstallMaterializeError>
    : DefaultInImpl<error::Error, lito::InstallMaterializeError> {};

} // namespace rstd
