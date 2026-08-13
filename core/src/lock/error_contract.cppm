module;
#include <rstd/enum.hpp>

export module lito.lock.error_contract;

import rstd;
import rstd.json;

export namespace lito
{

class LockError {
    RSTD_ENUM(LockError,
              (Schema, (rstd::string::String message;)),
              (Io,
               (rstd::string::String operation; rstd::path::PathBuf path;
                rstd::io::error::Error source;)),
              (Json, (rstd::path::PathBuf path; rstd::json::Error source;)))
};

template<typename T>
using LockResult = rstd::Result<T, LockError>;

}

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::LockError> : ImplBase<lito::LockError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Schema()) return formatter.write_str(error.as_Schema().message.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} lock '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_fmt(
            fmt::Arguments::make("cannot parse lock '{}'", error.as_Json().path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::LockError> : ImplBase<lito::LockError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::LockError> : ImplBase<lito::LockError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        return None();
    }
};

}
