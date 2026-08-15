export module lito.core:package.graph;

import rstd;
import :manifest.profile;
import :source.requirement;
import :dependency.visibility;
import :dependency.source;
import :manifest.package;
import :package.identity;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito
{

enum class ProjectRootRole
{
    PrimaryPackage,
    WorkspaceMember,
    AssociatedTest,
};

struct ResolvedDependency {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct ResolvedRuntimeDependency {
    String name;
};

struct ResolvedPackage {
    String                         source_identity;
    ResolvedPackageSource          source;
    PathBuf                        source_manifest;
    PackageManifest                manifest;
    Vec<ResolvedDependency>        dependencies;
    Vec<ResolvedDependency>        dev_dependencies;
    Vec<ResolvedRuntimeDependency> runtime_dependencies;
};

struct ResolvedProjectRoot {
    String          name;
    String          source_identity;
    ProjectRootRole role { ProjectRootRole::PrimaryPackage };
};

struct ResolvedPackageGraph {
    String                            name;
    Vec<ResolvedProjectRoot>          roots;
    PathBuf                           root_directory;
    PathBuf                           manifest_path;
    bool                              root_is_workspace { false };
    ProjectProfile                    profile;
    Vec<ResolvedPackageSource>        sources;
    Vec<ResolvedPackage>              packages;
    Vec<ResolvedExternalSourceRecord> externals;
};

} // namespace lito

export namespace rstd
{

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

} // namespace rstd
