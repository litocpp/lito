module;
#include <rstd/enum.hpp>

export module lito.core:package.graph;

import rstd;
import :manifest.profile;
import :source.requirement;
import :dependency.consumption;
import :dependency.condition;
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

struct DependencyActivation {
    Option<lito::dependency::DependencyCondition> condition;
    bool                                          target { true };
    bool                                          host { true };

    auto active(bool host_instance) const noexcept -> bool { return host_instance ? host : target; }
};

template<typename T>
struct ActiveDependencies {
    slice<T> values;
    bool     host {};

    struct Iterator {
        slice<T> values;
        usize    index {};
        bool     host {};

        auto skip() -> void {
            while (index < values.len() && ! values[index].activation.active(host)) ++index;
        }
        auto operator*() const -> const T& { return values[index]; }
        auto operator++() -> Iterator& {
            ++index;
            skip();
            return *this;
        }
        auto operator!=(const Iterator& other) const -> bool { return index != other.index; }
    };

    auto begin() const -> Iterator {
        auto result = Iterator { values, usize {}, host };
        result.skip();
        return result;
    }
    auto end() const -> Iterator { return Iterator { values, values.len(), host }; }
};

struct ResolvedCppDependency {
    String                                  name;
    lito::dependency::DependencyConsumption consumption;
    Vec<String>                             features;
    bool                                    default_features { true };
};

struct ResolvedScriptDependency {
    String                              name;
    String                              require_name;
    String                              source_identity;
    Vec<lito::manifest::ScriptHostKind> supports;
};

struct ResolvedPmacroDependency {
    String      name;
    Vec<String> features;
    bool        default_features { true };
};

struct ResolvedPluginDependency {
    String      name;
    Vec<String> features;
    bool        default_features { true };
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
              (Script, (ResolvedScriptDependency value;)),
              (Plugin, (ResolvedPluginDependency value;)),
              (Pmacro, (ResolvedPmacroDependency value;)))

    DependencyActivation activation;
};

auto resolved_dependency_name_value(const ResolvedRequiredDependency& dependency) noexcept
    -> const String& {
    if (dependency.is_Cpp()) return dependency.as_Cpp().value.name;
    if (dependency.is_Script()) return dependency.as_Script().value.name;
    if (dependency.is_Plugin()) return dependency.as_Plugin().value.name;
    return dependency.as_Pmacro().value.name;
}

auto resolved_dependency_name(const ResolvedRequiredDependency& dependency) noexcept -> ref<str> {
    return resolved_dependency_name_value(dependency).as_str();
}

struct ResolvedRuntimeDependency {
    String               name;
    DependencyActivation activation;
};

struct ResolvedFeature {
    String      name;
    String      macro_name;
    bool        enabled { false };
    Vec<String> activation_sources;
};

struct ResolvedPackage {
    String                                                     source_identity;
    lito::source::ResolvedPackageSource                        source;
    PathBuf                                                    source_manifest;
    lito::manifest::PackageManifest                            manifest;
    Option<lito::source::SourceTree>                           embedded_source;
    Vec<ResolvedRequiredDependency>                            dependencies;
    Vec<ResolvedRequiredDependency>                            dev_dependencies;
    Vec<ResolvedRuntimeDependency>                             runtime_dependencies;
    Vec<ResolvedFeature>                                       features;
    Vec<lito::dependency::ResolvedExternalSourceRecord>        externals;
    Option<Vec<lito::dependency::PkgConfigExternalDependency>> selected_pkg_config_dependencies;
    Option<Vec<lito::dependency::CMakeDependencyRequirement>>  selected_cmake_dependencies;
    Option<Vec<lito::dependency::CargoDependencyRequirement>>  selected_cargo_dependencies;
    bool                                                       host_view { false };
    auto effective_pkg_config_dependencies() const
        -> const Vec<lito::dependency::PkgConfigExternalDependency>& {
        return selected_pkg_config_dependencies.is_some()
                   ? *selected_pkg_config_dependencies
                   : manifest.pkg_config_external_dependencies;
    }
    auto effective_pkg_config_dependencies()
        -> Vec<lito::dependency::PkgConfigExternalDependency>& {
        return selected_pkg_config_dependencies.is_some()
                   ? *selected_pkg_config_dependencies
                   : manifest.pkg_config_external_dependencies;
    }

    auto effective_cmake_dependencies() const
        -> const Vec<lito::dependency::CMakeDependencyRequirement>& {
        return selected_cmake_dependencies.is_some() ? *selected_cmake_dependencies
                                                     : manifest.cmake_external_dependencies;
    }
    auto effective_cmake_dependencies() -> Vec<lito::dependency::CMakeDependencyRequirement>& {
        return selected_cmake_dependencies.is_some() ? *selected_cmake_dependencies
                                                     : manifest.cmake_external_dependencies;
    }

    auto effective_cargo_dependencies() const
        -> const Vec<lito::dependency::CargoDependencyRequirement>& {
        return selected_cargo_dependencies.is_some() ? *selected_cargo_dependencies
                                                     : manifest.cargo_external_dependencies;
    }
    auto effective_cargo_dependencies() -> Vec<lito::dependency::CargoDependencyRequirement>& {
        return selected_cargo_dependencies.is_some() ? *selected_cargo_dependencies
                                                     : manifest.cargo_external_dependencies;
    }

    auto active_dependencies() const -> ActiveDependencies<ResolvedRequiredDependency> {
        return { dependencies.as_slice(), host_view };
    }
    auto active_dev_dependencies() const -> ActiveDependencies<ResolvedRequiredDependency> {
        return { dev_dependencies.as_slice(), host_view };
    }
    auto active_runtime_dependencies() const -> ActiveDependencies<ResolvedRuntimeDependency> {
        return { runtime_dependencies.as_slice(), host_view };
    }
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
    Vec<String>                              builtin_packages;
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
