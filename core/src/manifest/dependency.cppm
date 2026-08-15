export module lito.core:manifest.dependency;

import rstd;
import :source.requirement;
import :dependency.visibility;
import :dependency.cmake;
import :dependency.pkg_config;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

struct DeclaredDependency {
    String                   name;
    PackageSourceRequirement source;
    DependencyVisibility     visibility { DependencyVisibility::Private };
    Option<PathBuf>          declaration_root;
};

struct WorkspaceDependencyReference {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct DeclaredRuntimeDependency {
    String                   name;
    PackageSourceRequirement source;
    Option<PathBuf>          declaration_root;
};

struct WorkspaceRuntimeDependencyReference {
    String name;
};

struct WorkspacePkgConfigExternalDependencyReference {
    String               alias;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct WorkspaceCMakeExternalDependencyReference {
    String                      alias;
    Vec<CMakeTargetRequirement> targets;
};

struct WorkspaceDependencyDefinition {
    String                   name;
    PackageSourceRequirement source;
};

struct WorkspacePkgConfigExternalDependencyDefinition {
    String                         alias;
    PkgConfigDependencyRequirement requirement;
};

struct WorkspaceCMakeExternalDependencyDefinition {
    String                alias;
    String                package;
    CMakeDependencySource source;
    CMakeIntegration      integration { CMakeIntegration::Install };
    Option<PathBuf>       adapter;
    Option<PathBuf>       config_directory;
    Vec<CMakeCacheEntry>  cache;
};

} // namespace lito
