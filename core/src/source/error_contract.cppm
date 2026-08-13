module;
#include <rstd/enum.hpp>

export module lito.source.error_contract;

import rstd;
import lito.error;
import lito.manifest.contract;
import lito.workspace.contract;
import lito.system.error_contract;

using namespace rstd::literals;

export namespace lito
{

class SourceError {
    RSTD_ENUM(SourceError,
              (Manifest, (ManifestError source;)),
              (Workspace, (WorkspaceError source;)),
              (System, (String operation; SystemError source;)),
              (Io,
               (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using SourceResult = rstd::Result<T, SourceError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::ManifestError>, lito::SourceError> {
    static auto from(lito::ManifestError error) -> lito::SourceError {
        return lito::SourceError::Manifest(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::WorkspaceError>, lito::SourceError> {
    static auto from(lito::WorkspaceError error) -> lito::SourceError {
        return lito::SourceError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::SystemError>, lito::SourceError> {
    static auto from(lito::SystemError error) -> lito::SourceError {
        return lito::SourceError::System(rstd::string::String::make("source operation"_str),
                                         rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::SourceError> : ImplBase<lito::SourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Manifest()) {
            return formatter.write_raw("package source manifest failed",
                                       sizeof("package source manifest failed") - 1);
        }
        if (error.is_Workspace()) {
            return formatter.write_raw("package source workspace resolution failed",
                                       sizeof("package source workspace resolution failed") - 1);
        }
        if (error.is_System()) return formatter.write_str(error.as_System().operation.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} source '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::SourceError> : ImplBase<lito::SourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::SourceError> : ImplBase<lito::SourceError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Manifest()) {
            return Some(dyn<error::Error>::from_ref(error.as_Manifest().source));
        }
        if (error.is_Workspace()) {
            return Some(dyn<error::Error>::from_ref(error.as_Workspace().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
