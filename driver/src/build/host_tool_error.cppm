module;
#include <rstd/enum.hpp>

export module lito.driver:build.host_tool_error;

import rstd;
import lito.core;
import lito.system;

using namespace rstd::prelude;

using namespace rstd::literals;
using namespace lito::system;

export namespace lito
{

class HostBuildToolError {
    RSTD_ENUM(HostBuildToolError,
              (Source, (lito::source::SourceError source;)),
              (System, (SystemError source;)),
              (DuplicateAlias, (String alias;)),
              (UnsupportedHost, (String alias; String os; String architecture;)),
              (MissingExecutable, (String alias; PathBuf path;)),
              (Version, (String alias; String expected; String actual; PathBuf executable;)))
};

template<typename T>
using HostBuildToolResult = Result<T, HostBuildToolError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::source::SourceError>, lito::HostBuildToolError> {
    static auto from(lito::source::SourceError error) -> lito::HostBuildToolError {
        return lito::HostBuildToolError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::HostBuildToolError> {
    static auto from(lito::system::SystemError error) -> lito::HostBuildToolError {
        return lito::HostBuildToolError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::HostBuildToolError> : ImplBase<lito::HostBuildToolError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source())
            return formatter.write_str("host build-tool source acquisition failed"_str);
        if (error.is_System()) return formatter.write_str("host build-tool process failed"_str);
        if (error.is_DuplicateAlias()) {
            return formatter.write_str(
                rstd::format("selected build-script packages repeat build-tool alias '{}'",
                             error.as_DuplicateAlias().alias.as_str())
                    .as_str());
        }
        if (error.is_UnsupportedHost()) {
            const auto& value = error.as_UnsupportedHost();
            return formatter.write_str(rstd::format("build-tool '{}' has no archive for host {}-{}",
                                                    value.alias.as_str(),
                                                    value.os.as_str(),
                                                    value.architecture.as_str())
                                           .as_str());
        }
        if (error.is_MissingExecutable()) {
            const auto& value = error.as_MissingExecutable();
            return formatter.write_str(
                rstd::format("build-tool '{}' executable '{}' is missing or invalid",
                             value.alias.as_str(),
                             value.path.as_path())
                    .as_str());
        }
        const auto& value = error.as_Version();
        return formatter.write_str(
            rstd::format("build-tool '{}' expected version '{}', received '{}' from '{}'",
                         value.alias.as_str(),
                         value.expected.as_str(),
                         value.actual.as_str(),
                         value.executable.as_path())
                .as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::HostBuildToolError> : ImplBase<lito::HostBuildToolError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::HostBuildToolError> : ImplBase<lito::HostBuildToolError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Source()) return Some(dyn<error::Error>::from_ref(error.as_Source().source));
        if (error.is_System()) return Some(dyn<error::Error>::from_ref(error.as_System().source));
        return None();
    }
};

} // namespace rstd
