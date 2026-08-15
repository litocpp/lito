module;
#include <rstd/enum.hpp>

export module lito.workspace.contract;

import rstd;
import lito.error;
import lito.manifest.contract;

using namespace rstd::literals;

export namespace lito
{

enum class PackageSelectionPurpose
{
    All,
    Production,
    Documentation,
    Install,
    Test,
    Benchmark,
};

enum class ProjectRootRole
{
    PrimaryPackage,
    WorkspaceMember,
    AssociatedTest,
};

struct PackageSelection {
    PathBuf     root;
    Vec<String> packages;
};

class WorkspaceError {
    RSTD_ENUM(WorkspaceError,
              (Manifest, (ManifestError source;)),
              (Io,
               (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using WorkspaceResult = rstd::Result<T, WorkspaceError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::PackageSelectionPurpose> : ImplBase<lito::PackageSelectionPurpose> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::PackageSelectionPurpose::All: name = "all"_str; break;
        case lito::PackageSelectionPurpose::Production: name = "production"_str; break;
        case lito::PackageSelectionPurpose::Documentation: name = "documentation"_str; break;
        case lito::PackageSelectionPurpose::Install: name = "install"_str; break;
        case lito::PackageSelectionPurpose::Test: name = "test"_str; break;
        case lito::PackageSelectionPurpose::Benchmark: name = "benchmark"_str; break;
        }
        return formatter.write_str(name);
    }
};

template<>
struct Impl<fmt::Display, lito::ProjectRootRole> : ImplBase<lito::ProjectRootRole> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::ProjectRootRole::PrimaryPackage: name = "primary package"_str; break;
        case lito::ProjectRootRole::WorkspaceMember: name = "workspace member"_str; break;
        case lito::ProjectRootRole::AssociatedTest: name = "test"_str; break;
        }
        return formatter.write_str(name);
    }
};

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
