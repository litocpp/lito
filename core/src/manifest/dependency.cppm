export module lito.core:manifest.dependency;

import rstd;
import :source.requirement;
import :dependency.visibility;
import :dependency.cargo;
import :dependency.cmake;
import :dependency.pkg_config;
import :dependency.source;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

struct DeclaredDependency {
    String                                         name;
    lito::source::PackageSourceRequirement         source;
    Option<lito::dependency::DependencyVisibility> visibility;
    Option<Vec<String>>                            features;
    Option<bool>                                   default_features;
    Option<PathBuf>                                declaration_root;
};

struct WorkspaceDependencyReference {
    String                                         name;
    Option<lito::dependency::DependencyVisibility> visibility;
    Option<Vec<String>>                            features;
    Option<bool>                                   default_features;
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
    Option<lito::dependency::ExternalDependencyCondition> condition;
};

struct WorkspaceCMakeExternalDependencyReference {
    String                                                alias;
    Vec<lito::dependency::CMakeTargetRequirement>         targets;
    Option<lito::dependency::ExternalDependencyCondition> condition;
};

struct WorkspaceCargoExternalDependencyReference {
    String                                       alias;
    lito::dependency::CargoDependencyConsumption consumption;
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
    String                                          alias;
    String                                          package;
    Vec<String>                                     components;
    Option<String>                                  source;
    Option<PathBuf>                                 adapter;
    Option<PathBuf>                                 config_directory;
    Vec<lito::dependency::CMakeCacheEntry>          cache;
    Vec<lito::dependency::CMakeHostToolRequirement> host_tools;
};

struct WorkspaceCargoExternalDependencyDefinition {
    String                                  alias;
    lito::dependency::CargoDependencyRecipe recipe;
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
