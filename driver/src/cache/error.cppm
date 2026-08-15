module;
#include <rstd/enum.hpp>

export module lito.driver:cache.error;

import rstd;

using namespace rstd::prelude;

export namespace lito
{

class CacheError {
    RSTD_ENUM(CacheError,
              (Record, (String message;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)),
              (SharedIo,
               (String operation; rstd::path::PathBuf   path;
                rstd::sync::Arc<rstd::io::error::Error> source;)))
};

template<typename T>
using CacheResult = Result<T, CacheError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::CacheError> : ImplBase<lito::CacheError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Record()) return formatter.write_str(error.as_Record().message.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} cache path '{}'", value.operation, value.path.as_path()));
        }
        const auto& value = error.as_SharedIo();
        return formatter.write_fmt(fmt::Arguments::make(
            "cannot {} cache path '{}'", value.operation, value.path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::CacheError> : ImplBase<lito::CacheError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::CacheError> : ImplBase<lito::CacheError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_SharedIo()) {
            const auto& source = *error.as_SharedIo().source;
            return Some(dyn<error::Error>::from_ref(source));
        }
        return None();
    }
};

} // namespace rstd
