module;
#include <rstd/enum.hpp>

export module lito.driver:build.product_error;

import rstd;
import rstd.json;

using namespace rstd::prelude;

export namespace lito
{

class BuildProductError {
    RSTD_ENUM(BuildProductError,
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)),
              (Json, (rstd::path::PathBuf path; rstd::json::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using BuildProductResult = Result<T, BuildProductError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::BuildProductError> : ImplBase<lito::BuildProductError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot parse build product '{}'",
                                                            error.as_Json().path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::BuildProductError> : ImplBase<lito::BuildProductError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::BuildProductError> : ImplBase<lito::BuildProductError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        return None();
    }
};

} // namespace rstd
