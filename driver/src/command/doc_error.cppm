module;
#include <rstd/enum.hpp>

export module lito.driver:command.doc_error;

import rstd;
import rstd.json;
import lito.core;
import :build.error;
import lito.system;
import lito.toolchain.common;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

class DocError {
    RSTD_ENUM(DocError,
              (Source, (SourceError source;)),
              (Build, (BuildError source;)),
              (System, (SystemError source;)),
              (Toolchain, (ToolchainError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Execution,
               (String operation; PathBuf executable; i32 exit_code; String standard_output;
                String                                                      standard_error;)),
              (Protocol, (PathBuf path; String message;)),
              (Message, (String message;)))
};

template<typename T>
using DocResult = Result<T, DocError>;

} // namespace lito

namespace lito
{

auto doc_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error error)
    -> DocError {
    return DocError::Io(String::make(operation), PathBuf::from(path), rstd::move(error));
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::SourceError>, lito::DocError> {
    static auto from(lito::SourceError error) -> lito::DocError {
        return lito::DocError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildError>, lito::DocError> {
    static auto from(lito::BuildError error) -> lito::DocError {
        return lito::DocError::Build(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::DocError> {
    static auto from(lito::system::SystemError error) -> lito::DocError {
        return lito::DocError::System(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ToolchainError>, lito::DocError> {
    static auto from(lito::ToolchainError error) -> lito::DocError {
        return lito::DocError::Toolchain(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::DocError> : ImplBase<lito::DocError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source())
            return formatter.write_str("documentation tool source acquisition failed"_str);
        if (error.is_Build()) return formatter.write_str("documentation build failed"_str);
        if (error.is_System())
            return formatter.write_str("documentation process operation failed"_str);
        if (error.is_Toolchain())
            return formatter.write_str("documentation Clang SDK resolution failed"_str);
        if (error.is_Io()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot {} documentation path '{}'",
                                                            error.as_Io().operation,
                                                            error.as_Io().path.as_path()));
        }
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot parse documentation JSON '{}'",
                                                            error.as_Json().path.as_path()));
        }
        if (error.is_Execution()) {
            return formatter.write_fmt(
                fmt::Arguments::make("{} failed with exit code {} for '{}'",
                                     error.as_Execution().operation,
                                     error.as_Execution().exit_code,
                                     error.as_Execution().executable.as_path()));
        }
        if (error.is_Protocol()) {
            return formatter.write_fmt(
                fmt::Arguments::make("invalid documentation protocol artifact '{}': {}",
                                     error.as_Protocol().path.as_path(),
                                     error.as_Protocol().message));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::DocError> : ImplBase<lito::DocError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::DocError> : ImplBase<lito::DocError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Source()) return Some(dyn<error::Error>::from_ref(value.as_Source().source));
        if (value.is_Build()) return Some(dyn<error::Error>::from_ref(value.as_Build().source));
        if (value.is_System()) return Some(dyn<error::Error>::from_ref(value.as_System().source));
        if (value.is_Toolchain())
            return Some(dyn<error::Error>::from_ref(value.as_Toolchain().source));
        if (value.is_Io()) return Some(dyn<error::Error>::from_ref(value.as_Io().source));
        if (value.is_Json()) return Some(dyn<error::Error>::from_ref(value.as_Json().source));
        return None();
    }
};

} // namespace rstd
