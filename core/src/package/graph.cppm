module;
#include <rstd/enum.hpp>

export module lito.core:package.graph;

import rstd;
import :manifest.profile;
import :source.requirement;
import :dependency.visibility;
import :dependency.source;
import :manifest.package;
import :package.identity;
import :source.tree;

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

struct ResolvedCppDependency {
    String                                 name;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    Vec<String> features;
    bool        default_features { true };
};

struct ResolvedScriptDependency {
    String                              name;
    String                              require_name;
    String                              source_identity;
    Vec<lito::manifest::ScriptHostKind> supports;
};

struct ResolvedScriptPackageView {
    String                              name;
    String                              require_name;
    String                              source_identity;
    Vec<lito::manifest::ScriptHostKind> supports;
    PathBuf                             root;
    Option<lito::source::SourceTree>    embedded_source;
    Vec<String>                         dependencies;
};

class ResolvedRequiredDependency {
    RSTD_ENUM(ResolvedRequiredDependency,
              (Cpp, (ResolvedCppDependency value;)),
              (Script, (ResolvedScriptDependency value;)))
};

auto resolved_dependency_name_value(const ResolvedRequiredDependency& dependency) noexcept
    -> const String& {
    if (dependency.is_Cpp()) return dependency.as_Cpp().value.name;
    return dependency.as_Script().value.name;
}

auto resolved_dependency_name(const ResolvedRequiredDependency& dependency) noexcept -> ref<str> {
    return resolved_dependency_name_value(dependency).as_str();
}

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
    String                                              source_identity;
    lito::source::ResolvedPackageSource                 source;
    PathBuf                                             source_manifest;
    lito::manifest::PackageManifest                     manifest;
    Option<lito::source::SourceTree>                    embedded_source;
    Vec<ResolvedRequiredDependency>                     dependencies;
    Vec<ResolvedCppDependency>                          dev_dependencies;
    Vec<ResolvedRuntimeDependency>                      runtime_dependencies;
    Vec<ResolvedFeature>                                features;
    Vec<lito::dependency::ResolvedExternalSourceRecord> externals;
};

struct ResolvedProjectRoot {
    String          name;
    String          source_identity;
    ProjectRootRole role { ProjectRootRole::PrimaryPackage };
};

struct ResolvedPackageGraph {
    String                                   name;
    Vec<ResolvedProjectRoot>                 roots;
    Vec<String>                              default_roots;
    PathBuf                                  root_directory;
    PathBuf                                  manifest_path;
    bool                                     root_is_workspace { false };
    lito::manifest::ProjectProfile           profile;
    Vec<lito::source::ResolvedPackageSource> sources;
    Vec<ResolvedPackage>                     packages;
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
