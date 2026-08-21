module;
#include <rstd/enum.hpp>

export module lito.toolchain.common:error;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import lito.frontend.lexical;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace lito::system;

export namespace lito
{

struct StandardLibraryModuleErrorContext {
    config::StandardLibrary family { config::StandardLibrary::Libstdcxx };
    String                  target;
    rstd::path::PathBuf     artifact;
};

class StandardLibraryModuleError {
    RSTD_ENUM(
        StandardLibraryModuleError,
        (Missing, (StandardLibraryModuleErrorContext context; Vec<rstd::path::PathBuf> searched;)),
        (Ambiguous,
         (StandardLibraryModuleErrorContext context; rstd::path::PathBuf first;
          rstd::path::PathBuf                                            second;)),
        (Manifest,
         (StandardLibraryModuleErrorContext context; rstd::path::PathBuf manifest;
          Option<String>                                                 entry;
          String                                                         message;)),
        (Io,
         (StandardLibraryModuleErrorContext context; String operation; rstd::path::PathBuf path;
          Option<rstd::path::PathBuf>                                                      manifest;
          Option<String>                                                                   entry;
          rstd::io::error::Error source;)))
};

class ToolchainError {
    RSTD_ENUM(ToolchainError,
              (System, (SystemError source;)),
              (Tools, (lito::tools::ToolError source;)),
              (Frontend, (frontend::lexical::Error source;)),
              (Cpp, (cpp::CppOptionError source;)),
              (StandardLibraryModule, (StandardLibraryModuleError source;)),
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
    static auto from(lito::system::SystemError error) -> lito::ToolchainError;
};

template<>
struct Impl<convert::From<lito::tools::ToolError>, lito::ToolchainError> {
    static auto from(lito::tools::ToolError error) -> lito::ToolchainError;
};

template<>
struct Impl<convert::From<lito::frontend::lexical::Error>, lito::ToolchainError> {
    static auto from(lito::frontend::lexical::Error error) -> lito::ToolchainError;
};

template<>
struct Impl<convert::From<lito::cpp::CppOptionError>, lito::ToolchainError> {
    static auto from(lito::cpp::CppOptionError error) -> lito::ToolchainError;
};

template<>
struct Impl<convert::From<lito::system::PlatformError>, lito::ToolchainError> {
    static auto from(lito::system::PlatformError error) -> lito::ToolchainError;
};

template<>
struct Impl<fmt::Display, lito::StandardLibraryModuleError>
    : ImplBase<lito::StandardLibraryModuleError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::StandardLibraryModuleError>
    : ImplBase<lito::StandardLibraryModuleError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::StandardLibraryModuleError>
    : ImplBase<lito::StandardLibraryModuleError> {
    auto source() const noexcept -> Option<error::ErrorRef>;
};

template<>
struct Impl<fmt::Display, lito::ToolchainError> : ImplBase<lito::ToolchainError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::ToolchainError> : ImplBase<lito::ToolchainError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::ToolchainError> : ImplBase<lito::ToolchainError> {
    auto source() const noexcept -> Option<error::ErrorRef>;
};

} // namespace rstd
