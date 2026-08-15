module;
#include <rstd/enum.hpp>

export module lito.core:package.selection_error;

import rstd;
import :workspace.error;
import :package.error;
import lito.system;

using namespace rstd::prelude;

using namespace lito::system;

export namespace lito
{

class PackageSelectionError {
    RSTD_ENUM(PackageSelectionError,
              (Workspace, (WorkspaceError source;)),
              (Package, (PackageError source;)),
              (System, (SystemError source;)),
              (Message, (String message;)))
};

template<typename T>
using PackageSelectionResult = Result<T, PackageSelectionError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::WorkspaceError>, lito::PackageSelectionError> {
    static auto from(lito::WorkspaceError error) -> lito::PackageSelectionError {
        return lito::PackageSelectionError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::PackageError>, lito::PackageSelectionError> {
    static auto from(lito::PackageError error) -> lito::PackageSelectionError {
        return lito::PackageSelectionError::Package(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::PackageSelectionError> {
    static auto from(lito::system::SystemError error) -> lito::PackageSelectionError {
        return lito::PackageSelectionError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::PackageSelectionError> : ImplBase<lito::PackageSelectionError> {
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
struct Impl<fmt::Debug, lito::PackageSelectionError> : ImplBase<lito::PackageSelectionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::PackageSelectionError> : ImplBase<lito::PackageSelectionError> {
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
