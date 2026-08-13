module;
#include <rstd/enum.hpp>

export module lito.system.error_contract;

import rstd;

export namespace lito
{

class SystemError {
    RSTD_ENUM(SystemError,
              (InvalidCommand, (rstd::string::String message;)),
              (Environment, (rstd::string::String message;)),
              (Storage, (rstd::string::String message;)),
              (Io,
               (rstd::string::String operation; rstd::path::PathBuf path;
                rstd::io::error::Error source;)),
              (Utf8,
               (rstd::string::String context; alloc::string::FromUtf8Error source;)),
              (PathJoin, (rstd::env::JoinPathsError source;)),
              (Fragment,
               (rstd::string::String context; rstd::string::String issue;)))
};

template<typename T>
using SystemResult = rstd::Result<T, SystemError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::SystemError> : ImplBase<lito::SystemError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_InvalidCommand()) {
            return formatter.write_str(error.as_InvalidCommand().message.as_str());
        }
        if (error.is_Environment()) {
            return formatter.write_str(error.as_Environment().message.as_str());
        }
        if (error.is_Storage()) return formatter.write_str(error.as_Storage().message.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            if (value.path.is_empty()) return formatter.write_str(value.operation.as_str());
            return formatter.write_fmt(
                fmt::Arguments::make("{} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_Utf8()) {
            return formatter.write_fmt(
                fmt::Arguments::make("{} is not valid UTF-8", error.as_Utf8().context));
        }
        if (error.is_PathJoin()) {
            return formatter.write_raw("cannot materialize effective PATH",
                                       sizeof("cannot materialize effective PATH") - 1);
        }
        const auto& fragment = error.as_Fragment();
        return formatter.write_fmt(
            fmt::Arguments::make("{} {}", fragment.context, fragment.issue));
    }
};

template<>
struct Impl<fmt::Debug, lito::SystemError> : ImplBase<lito::SystemError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::SystemError> : ImplBase<lito::SystemError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Utf8()) return Some(dyn<error::Error>::from_ref(error.as_Utf8().source));
        if (error.is_PathJoin()) {
            return Some(dyn<error::Error>::from_ref(error.as_PathJoin().source));
        }
        return None();
    }
};

} // namespace rstd
