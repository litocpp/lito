module;
#include <rstd/enum.hpp>

export module lito.driver:build.layout_error;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

class BuildLayoutError {
    RSTD_ENUM(BuildLayoutError,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using BuildLayoutResult = Result<T, BuildLayoutError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::BuildLayoutError> : ImplBase<lito::BuildLayoutError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_str(
                rstd::format("cannot {} '{}'", value.operation.as_str(), value.path.as_path())
                    .as_str());
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::BuildLayoutError> : ImplBase<lito::BuildLayoutError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::BuildLayoutError> : ImplBase<lito::BuildLayoutError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
