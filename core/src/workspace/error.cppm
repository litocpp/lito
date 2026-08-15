module;
#include <rstd/enum.hpp>

export module lito.core:workspace.error;

import rstd;
import :manifest.error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

class WorkspaceError {
    RSTD_ENUM(WorkspaceError,
              (Manifest, (ManifestError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using WorkspaceResult = Result<T, WorkspaceError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::ManifestError>, lito::WorkspaceError> {
    static auto from(lito::ManifestError error) -> lito::WorkspaceError {
        return lito::WorkspaceError::Manifest(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::WorkspaceError> : ImplBase<lito::WorkspaceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Manifest()) {
            return formatter.write_raw("workspace manifest failed",
                                       sizeof("workspace manifest failed") - 1);
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} workspace path '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::WorkspaceError> : ImplBase<lito::WorkspaceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::WorkspaceError> : ImplBase<lito::WorkspaceError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Manifest()) {
            return Some(dyn<error::Error>::from_ref(error.as_Manifest().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
