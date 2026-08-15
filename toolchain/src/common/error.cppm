module;
#include <rstd/enum.hpp>

export module lito.toolchain.common:error;

import rstd;
import lito.core;
import lito.system;
import lito.frontend.lexical;
import lito.cpp;

using namespace rstd::prelude;

using namespace lito::system;

export namespace lito
{

class ToolchainError {
    RSTD_ENUM(ToolchainError,
              (System, (SystemError source;)),
              (Frontend, (frontend::lexical::Error source;)),
              (Cpp, (cpp::CppOptionError source;)),
              (Platform, (PlatformError source;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)),
              (Execution,
               (String operation; i32 exit_code; String standard_output; String standard_error;)),
              (Message, (String message;)))

public:
    template<typename Kind>
    static auto make(Kind, ref<str> message) -> ToolchainError {
        return Message(String::make(message));
    }

    template<typename Kind>
    static auto make(Kind, String message) -> ToolchainError {
        return Message(rstd::move(message));
    }
};

template<typename T>
using ToolchainResult = Result<T, ToolchainError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::ToolchainError> {
    static auto from(lito::system::SystemError error) -> lito::ToolchainError {
        return lito::ToolchainError::System(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::frontend::lexical::Error>, lito::ToolchainError> {
    static auto from(lito::frontend::lexical::Error error) -> lito::ToolchainError {
        return lito::ToolchainError::Frontend(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::cpp::CppOptionError>, lito::ToolchainError> {
    static auto from(lito::cpp::CppOptionError error) -> lito::ToolchainError {
        return lito::ToolchainError::Cpp(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::PlatformError>, lito::ToolchainError> {
    static auto from(lito::system::PlatformError error) -> lito::ToolchainError {
        return lito::ToolchainError::Platform(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::ToolchainError> : ImplBase<lito::ToolchainError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_System()) {
            return formatter.write_raw("toolchain process operation failed",
                                       sizeof("toolchain process operation failed") - 1);
        }
        if (error.is_Frontend()) {
            return formatter.write_raw("toolchain frontend analysis failed",
                                       sizeof("toolchain frontend analysis failed") - 1);
        }
        if (error.is_Cpp()) {
            return formatter.write_raw("toolchain C++ argument schema failed",
                                       sizeof("toolchain C++ argument schema failed") - 1);
        }
        if (error.is_Platform()) {
            return formatter.write_raw("toolchain target platform is invalid",
                                       sizeof("toolchain target platform is invalid") - 1);
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("{} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_Execution()) {
            const auto& value = error.as_Execution();
            return formatter.write_fmt(fmt::Arguments::make("{} failed with exit code {}:\n{}{}",
                                                            value.operation,
                                                            value.exit_code,
                                                            value.standard_output,
                                                            value.standard_error));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::ToolchainError> : ImplBase<lito::ToolchainError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ToolchainError> : ImplBase<lito::ToolchainError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_System()) return Some(dyn<error::Error>::from_ref(error.as_System().source));
        if (error.is_Frontend()) {
            return Some(dyn<error::Error>::from_ref(error.as_Frontend().source));
        }
        if (error.is_Cpp()) return Some(dyn<error::Error>::from_ref(error.as_Cpp().source));
        if (error.is_Platform()) {
            return Some(dyn<error::Error>::from_ref(error.as_Platform().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
