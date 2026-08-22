module;
#include <rstd/enum.hpp>

export module lito.core:lock.error;

import rstd;
import rstd.serde;
import rstd.toml;
import :parse.error;

using namespace rstd::prelude;

export namespace lito::lock
{

class LockError {
    RSTD_ENUM(LockError,
              (Schema, (String message;)),
              (Parse, (lito::parse::Error source;)),
              (Data, (rstd::serde::Error source;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)),
              (Toml, (rstd::path::PathBuf path; rstd::toml::Error source;)))
};

template<typename T>
using LockResult = Result<T, LockError>;

} // namespace lito::lock

export namespace rstd
{

template<>
struct Impl<convert::From<lito::parse::Error>, lito::lock::LockError> {
    static auto from(lito::parse::Error error) -> lito::lock::LockError {
        return lito::lock::LockError::Parse(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::lock::LockError> : ImplBase<lito::lock::LockError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Schema()) return formatter.write_str(error.as_Schema().message.as_str());
        if (error.is_Parse()) return as<fmt::Display>(error.as_Parse().source).fmt(formatter);
        if (error.is_Data()) {
            return formatter.write_raw("lock data is invalid", sizeof("lock data is invalid") - 1);
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} lock '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_fmt(
            fmt::Arguments::make("cannot parse lock '{}'", error.as_Toml().path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::lock::LockError> : ImplBase<lito::lock::LockError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::lock::LockError> : ImplBase<lito::lock::LockError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Parse()) return Some(dyn<error::Error>::from_ref(error.as_Parse().source));
        if (error.is_Data()) return Some(dyn<error::Error>::from_ref(error.as_Data().source));
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Toml()) return Some(dyn<error::Error>::from_ref(error.as_Toml().source));
        return None();
    }
};

} // namespace rstd
