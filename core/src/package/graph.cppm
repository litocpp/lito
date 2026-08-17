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

export namespace lito::package
{

enum class ProjectRootRole
{
    PrimaryPackage,
    WorkspaceMember,
    AssociatedTest,
};

struct ResolvedDependency {
    String                                 name;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    Vec<String> features;
    bool        default_features { true };
};

struct ResolvedRuntimeDependency {
    String name;
};

struct ResolvedFeature {
    String      name;
    String      macro_name;
    bool        enabled { false };
    Vec<String> activation_sources;
};

struct ResolvedPackage {
    String                              source_identity;
    lito::source::ResolvedPackageSource source;
    PathBuf                             source_manifest;
    lito::manifest::PackageManifest     manifest;
    Vec<ResolvedDependency>             dependencies;
    Vec<ResolvedDependency>             dev_dependencies;
    Vec<ResolvedRuntimeDependency>      runtime_dependencies;
    Vec<ResolvedFeature>                features;
};

struct ResolvedProjectRoot {
    String          name;
    String          source_identity;
    ProjectRootRole role { ProjectRootRole::PrimaryPackage };
};

struct ResolvedPackageGraph {
    String                                              name;
    Vec<ResolvedProjectRoot>                            roots;
    PathBuf                                             root_directory;
    PathBuf                                             manifest_path;
    bool                                                root_is_workspace { false };
    lito::manifest::ProjectProfile                      profile;
    Vec<lito::source::ResolvedPackageSource>            sources;
    Vec<ResolvedPackage>                                packages;
    Vec<lito::dependency::ResolvedExternalSourceRecord> externals;
};

} // namespace lito::package

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::package::ProjectRootRole>
    : ImplBase<lito::package::ProjectRootRole> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::package::ProjectRootRole::PrimaryPackage: name = "primary package"_str; break;
        case lito::package::ProjectRootRole::WorkspaceMember: name = "workspace member"_str; break;
        case lito::package::ProjectRootRole::AssociatedTest: name = "test"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
