module;
#include <rstd/enum.hpp>

export module lito.driver:project.error;

import rstd;
import lito.core;
import lito.system;
import lito.toolchain;
import lito.cpp;
import :build.layout_error;

using namespace rstd::prelude;

using namespace lito::system;

export namespace lito
{

class ProjectError {
    RSTD_ENUM(ProjectError,
              (Workspace, (lito::package::PackageSelectionError source;)),
              (Package, (lito::package::PackageError source;)),
              (Dependency, (lito::dependency::DependencyError source;)),
              (System, (SystemError source;)),
              (Toolchain, (ToolchainError source;)),
              (AndroidNdk, (AndroidNdkError source;)),
              (Lock, (lito::lock::LockError source;)),
              (Platform, (PlatformError source;)),
              (Profile, (lito::manifest::BuildProfileError source;)),
              (Layout, (BuildLayoutError source;)),
              (Message, (String message;)))
};

template<typename T>
using ProjectResult = Result<T, ProjectError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::package::PackageSelectionError>, lito::ProjectError> {
    static auto from(lito::package::PackageSelectionError error) -> lito::ProjectError {
        return lito::ProjectError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::package::PackageError>, lito::ProjectError> {
    static auto from(lito::package::PackageError error) -> lito::ProjectError {
        return lito::ProjectError::Package(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::dependency::DependencyError>, lito::ProjectError> {
    static auto from(lito::dependency::DependencyError error) -> lito::ProjectError {
        return lito::ProjectError::Dependency(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::ProjectError> {
    static auto from(lito::system::SystemError error) -> lito::ProjectError {
        return lito::ProjectError::System(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ToolchainError>, lito::ProjectError> {
    static auto from(lito::ToolchainError error) -> lito::ProjectError {
        return lito::ProjectError::Toolchain(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::AndroidNdkError>, lito::ProjectError> {
    static auto from(lito::AndroidNdkError error) -> lito::ProjectError {
        return lito::ProjectError::AndroidNdk(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::lock::LockError>, lito::ProjectError> {
    static auto from(lito::lock::LockError error) -> lito::ProjectError {
        return lito::ProjectError::Lock(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::PlatformError>, lito::ProjectError> {
    static auto from(lito::system::PlatformError error) -> lito::ProjectError {
        return lito::ProjectError::Platform(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::manifest::BuildProfileError>, lito::ProjectError> {
    static auto from(lito::manifest::BuildProfileError error) -> lito::ProjectError {
        return lito::ProjectError::Profile(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildLayoutError>, lito::ProjectError> {
    static auto from(lito::BuildLayoutError error) -> lito::ProjectError {
        return lito::ProjectError::Layout(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::ProjectError> : ImplBase<lito::ProjectError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Workspace()) {
            return formatter.write_raw("project workspace resolution failed",
                                       sizeof("project workspace resolution failed") - 1);
        }
        if (error.is_Package()) {
            return formatter.write_raw("project package resolution failed",
                                       sizeof("project package resolution failed") - 1);
        }
        if (error.is_Dependency()) {
            return formatter.write_raw("project external dependency resolution failed",
                                       sizeof("project external dependency resolution failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("project environment resolution failed",
                                       sizeof("project environment resolution failed") - 1);
        }
        if (error.is_Toolchain()) {
            return formatter.write_raw("project toolchain resolution failed",
                                       sizeof("project toolchain resolution failed") - 1);
        }
        if (error.is_AndroidNdk()) {
            return formatter.write_raw("Android NDK resolution failed",
                                       sizeof("Android NDK resolution failed") - 1);
        }
        if (error.is_Lock()) {
            return formatter.write_raw("project lock operation failed",
                                       sizeof("project lock operation failed") - 1);
        }
        if (error.is_Platform()) {
            return formatter.write_raw("project target platform resolution failed",
                                       sizeof("project target platform resolution failed") - 1);
        }
        if (error.is_Profile()) {
            return formatter.write_raw("project build profile resolution failed",
                                       sizeof("project build profile resolution failed") - 1);
        }
        if (error.is_Layout()) {
            return formatter.write_raw("project build layout resolution failed",
                                       sizeof("project build layout resolution failed") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::ProjectError> : ImplBase<lito::ProjectError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ProjectError> : ImplBase<lito::ProjectError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Workspace()) {
            return Some(dyn<error::Error>::from_ref(error.as_Workspace().source));
        }
        if (error.is_Package()) {
            return Some(dyn<error::Error>::from_ref(error.as_Package().source));
        }
        if (error.is_Dependency()) {
            return Some(dyn<error::Error>::from_ref(error.as_Dependency().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Toolchain()) {
            return Some(dyn<error::Error>::from_ref(error.as_Toolchain().source));
        }
        if (error.is_AndroidNdk()) {
            return Some(lito::android_ndk_error_ref(error.as_AndroidNdk().source));
        }
        if (error.is_Lock()) {
            return Some(dyn<error::Error>::from_ref(error.as_Lock().source));
        }
        if (error.is_Platform()) {
            return Some(dyn<error::Error>::from_ref(error.as_Platform().source));
        }
        if (error.is_Profile()) {
            return Some(dyn<error::Error>::from_ref(error.as_Profile().source));
        }
        if (error.is_Layout()) {
            return Some(dyn<error::Error>::from_ref(error.as_Layout().source));
        }
        return None();
    }
};

} // namespace rstd
