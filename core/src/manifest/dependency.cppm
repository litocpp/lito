export module lito.core:manifest.dependency;

import rstd;
import :source.requirement;
import :dependency.visibility;
import :dependency.cmake;
import :dependency.pkg_config;
import :dependency.source;

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
    Vec<String>     features;
    bool            default_features { true };
    Option<PathBuf> declaration_root;
};

struct WorkspaceDependencyReference {
    String                                 name;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    Vec<String> features;
    bool        default_features { true };
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
    String                                 alias;
    String                                 package;
    Option<String>                         source;
    Option<PathBuf>                        adapter;
    Option<PathBuf>                        config_directory;
    Vec<lito::dependency::CMakeCacheEntry> cache;
};

struct PackageExternalSourceDeclaration {
    String                                      name;
    lito::dependency::ExternalSourceRequirement source;
    Option<PathBuf>                             declaration_root;
};

struct WorkspaceExternalSourceReference {
    String name;
};

struct WorkspaceExternalSourceDefinition {
    String                                      name;
    lito::dependency::ExternalSourceRequirement source;
};

} // namespace lito::manifest
