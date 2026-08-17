module;
#include <rstd/enum.hpp>

export module lito.core:package.selection_error;

import rstd;
import :workspace.error;
import :package.error;
import lito.system;

using namespace rstd::prelude;

using namespace lito::system;

export namespace lito::package
{

class PackageSelectionError {
    RSTD_ENUM(PackageSelectionError,
              (Workspace, (lito::workspace::WorkspaceError source;)),
              (Package, (PackageError source;)),
              (System, (SystemError source;)),
              (Message, (String message;)))
};

template<typename T>
using PackageSelectionResult = Result<T, PackageSelectionError>;

} // namespace lito::package

export namespace rstd
{

template<>
struct Impl<convert::From<lito::workspace::WorkspaceError>, lito::package::PackageSelectionError> {
    static auto from(lito::workspace::WorkspaceError error)
        -> lito::package::PackageSelectionError {
        return lito::package::PackageSelectionError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::package::PackageError>, lito::package::PackageSelectionError> {
    static auto from(lito::package::PackageError error) -> lito::package::PackageSelectionError {
        return lito::package::PackageSelectionError::Package(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::package::PackageSelectionError> {
    static auto from(lito::system::SystemError error) -> lito::package::PackageSelectionError {
        return lito::package::PackageSelectionError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::package::PackageSelectionError>
    : ImplBase<lito::package::PackageSelectionError> {
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
struct Impl<fmt::Debug, lito::package::PackageSelectionError>
    : ImplBase<lito::package::PackageSelectionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::package::PackageSelectionError>
    : ImplBase<lito::package::PackageSelectionError> {
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
