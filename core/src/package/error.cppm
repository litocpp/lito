module;
#include <rstd/enum.hpp>

export module lito.core:package.error;

import rstd;
import :source.error;
import :dependency.error;
import :manifest.error;
import :workspace.error;
import lito.system;

using namespace rstd::prelude;
using ErrorBox = Box<dyn<rstd::error::Error>>;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

enum class PackageDependencyKind
{
    Normal,
    Development,
    Runtime,
};

struct PackageDependencyCycleEdge {
    String                package;
    String                dependency;
    PackageDependencyKind kind { PackageDependencyKind::Normal };
};

struct PackageDependencyCycleError {
    Vec<String>                     packages;
    Vec<PackageDependencyCycleEdge> edges;
};

class PackageError {
    RSTD_ENUM(PackageError,
              (Source, (SourceError source;)),
              (Dependency, (DependencyError source;)),
              (Manifest, (ManifestError source;)),
              (Workspace, (WorkspaceError source;)),
              (System, (SystemError source;)),
              (Configuration, (ErrorBox source;)),
              (Cycle, (PackageDependencyCycleError cycle;)),
              (Message, (String message;)))
};

template<typename T>
using PackageResult = Result<T, PackageError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::SourceError>, lito::PackageError> {
    static auto from(lito::SourceError error) -> lito::PackageError {
        return lito::PackageError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::DependencyError>, lito::PackageError> {
    static auto from(lito::DependencyError error) -> lito::PackageError {
        return lito::PackageError::Dependency(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ManifestError>, lito::PackageError> {
    static auto from(lito::ManifestError error) -> lito::PackageError {
        return lito::PackageError::Manifest(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::WorkspaceError>, lito::PackageError> {
    static auto from(lito::WorkspaceError error) -> lito::PackageError {
        return lito::PackageError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::PackageError> {
    static auto from(lito::system::SystemError error) -> lito::PackageError {
        return lito::PackageError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::PackageError> : ImplBase<lito::PackageError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source()) {
            return formatter.write_raw("package source resolution failed",
                                       sizeof("package source resolution failed") - 1);
        }
        if (error.is_Dependency()) {
            return formatter.write_raw("package dependency resolution failed",
                                       sizeof("package dependency resolution failed") - 1);
        }
        if (error.is_Manifest()) {
            return formatter.write_raw("package manifest resolution failed",
                                       sizeof("package manifest resolution failed") - 1);
        }
        if (error.is_Workspace()) {
            return formatter.write_raw("package workspace resolution failed",
                                       sizeof("package workspace resolution failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("package environment resolution failed",
                                       sizeof("package environment resolution failed") - 1);
        }
        if (error.is_Configuration()) {
            return formatter.write_raw("package build configuration is invalid",
                                       sizeof("package build configuration is invalid") - 1);
        }
        if (error.is_Cycle()) {
            auto        text  = String::make("package dependency cycle: "_str);
            const auto& cycle = error.as_Cycle().cycle;
            for (usize index {}; index < cycle.packages.len(); ++index) {
                if (index != usize {}) text.push_str(" -> "_str);
                text.push_str(cycle.packages[index].as_str());
            }
            return formatter.write_str(text.as_str());
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::PackageError> : ImplBase<lito::PackageError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::PackageError> : ImplBase<lito::PackageError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Source()) {
            return Some(dyn<error::Error>::from_ref(error.as_Source().source));
        }
        if (error.is_Dependency()) {
            return Some(dyn<error::Error>::from_ref(error.as_Dependency().source));
        }
        if (error.is_Manifest()) {
            return Some(dyn<error::Error>::from_ref(error.as_Manifest().source));
        }
        if (error.is_Workspace()) {
            return Some(dyn<error::Error>::from_ref(error.as_Workspace().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Configuration()) {
            return Some(error.as_Configuration().source.as_ref());
        }
        return None();
    }
};

} // namespace rstd
