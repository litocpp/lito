export module lito.core:manifest.dependency;

import rstd;
import :source.requirement;
import :dependency.visibility;
import :dependency.cmake;
import :dependency.pkg_config;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

struct DeclaredDependency {
    String                                 name;
    lito::source::PackageSourceRequirement source;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    Option<PathBuf> declaration_root;
};

struct WorkspaceDependencyReference {
    String                                 name;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
};

struct DeclaredRuntimeDependency {
    String                                 name;
    lito::source::PackageSourceRequirement source;
    Option<PathBuf>                        declaration_root;
};

struct WorkspaceRuntimeDependencyReference {
    String name;
};

struct WorkspacePkgConfigExternalDependencyReference {
    String                                 alias;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
};

struct WorkspaceCMakeExternalDependencyReference {
    String                                        alias;
    Vec<lito::dependency::CMakeTargetRequirement> targets;
};

struct WorkspaceDependencyDefinition {
    String                                 name;
    lito::source::PackageSourceRequirement source;
};

struct WorkspacePkgConfigExternalDependencyDefinition {
    String                                           alias;
    lito::dependency::PkgConfigDependencyRequirement requirement;
};

struct WorkspaceCMakeExternalDependencyDefinition {
    String                                  alias;
    String                                  package;
    lito::dependency::CMakeDependencySource source;
    lito::dependency::CMakeIntegration integration { lito::dependency::CMakeIntegration::Install };
    bool                               add_subdirectory { true };
    Option<PathBuf>                    adapter;
    Option<PathBuf>                    config_directory;
    Vec<lito::dependency::CMakeCacheEntry> cache;
};

} // namespace lito::manifest
