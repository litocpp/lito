module;
#include <rstd/enum.hpp>

export module lito.workspace.resolution_error;

import rstd;
import lito.error;
import lito.workspace.contract;
import lito.package.error_contract;
import lito.system.error_contract;

export namespace lito
{

class WorkspaceResolutionError {
    RSTD_ENUM(WorkspaceResolutionError,
              (Workspace, (WorkspaceError source;)),
              (Package, (PackageError source;)),
              (System, (SystemError source;)),
              (Message, (String message;)))
};

template<typename T>
using WorkspaceResolutionResult = rstd::Result<T, WorkspaceResolutionError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::WorkspaceError>, lito::WorkspaceResolutionError> {
    static auto from(lito::WorkspaceError error) -> lito::WorkspaceResolutionError {
        return lito::WorkspaceResolutionError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::PackageError>, lito::WorkspaceResolutionError> {
    static auto from(lito::PackageError error) -> lito::WorkspaceResolutionError {
        return lito::WorkspaceResolutionError::Package(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::SystemError>, lito::WorkspaceResolutionError> {
    static auto from(lito::SystemError error) -> lito::WorkspaceResolutionError {
        return lito::WorkspaceResolutionError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::WorkspaceResolutionError>
    : ImplBase<lito::WorkspaceResolutionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Workspace()) {
            return formatter.write_raw("workspace resolution failed",
                                       sizeof("workspace resolution failed") - 1);
        }
        if (error.is_Package()) {
            return formatter.write_raw("package graph resolution failed",
                                       sizeof("package graph resolution failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("workspace environment resolution failed",
                                       sizeof("workspace environment resolution failed") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::WorkspaceResolutionError>
    : ImplBase<lito::WorkspaceResolutionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::WorkspaceResolutionError>
    : ImplBase<lito::WorkspaceResolutionError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Workspace()) {
            return Some(dyn<error::Error>::from_ref(error.as_Workspace().source));
        }
        if (error.is_Package()) {
            return Some(dyn<error::Error>::from_ref(error.as_Package().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        return None();
    }
};

} // namespace rstd
