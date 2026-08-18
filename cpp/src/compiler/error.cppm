module;
#include <rstd/enum.hpp>

export module lito.cpp:compiler.error;

import rstd;
import :compiler.argument;

using namespace rstd::prelude;

export namespace lito::cpp
{

class CompilerOptionError {
    RSTD_ENUM(CompilerOptionError,
              (Argument, (CompilerArgumentError source; String context;)),
              (Message, (String message;)))
};

template<typename T>
using CompilerOptionResult = Result<T, CompilerOptionError>;

using CppOptionError = CompilerOptionError;

template<typename T>
using CppOptionResult = CompilerOptionResult<T>;

} // namespace lito::cpp

export namespace rstd
{

template<>
struct Impl<convert::From<lito::cpp::CompilerArgumentError>, lito::cpp::CompilerOptionError> {
    static auto from(lito::cpp::CompilerArgumentError error) -> lito::cpp::CompilerOptionError {
        return lito::cpp::CompilerOptionError::Argument(rstd::move(error), String::make());
    }
};

template<>
struct Impl<fmt::Display, lito::cpp::CompilerOptionError>
    : ImplBase<lito::cpp::CompilerOptionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Argument()) {
            if (! error.as_Argument().context.is_empty()) {
                return formatter.write_fmt(
                    fmt::Arguments::make("compiler arguments from '{}' are invalid",
                                         error.as_Argument().context.as_str()));
            }
            return formatter.write_raw("compiler argument is invalid",
                                       sizeof("compiler argument is invalid") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::cpp::CompilerOptionError> : ImplBase<lito::cpp::CompilerOptionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::cpp::CompilerOptionError>
    : ImplBase<lito::cpp::CompilerOptionError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Argument()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Argument().source));
    }
};

} // namespace rstd
