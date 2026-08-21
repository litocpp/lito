export module lito.core:manifest.workspace;

import rstd;
import :manifest.profile;
import :manifest.dependency;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

struct WorkspacePackageDefaults {
    Option<String>      version;
    Option<String>      license;
    Option<Vec<String>> authors;
};

struct WorkspaceManifest {
    String                                              name;
    PathBuf                                             root;
    PathBuf                                             manifest_path;
    Option<ProjectProfile>                              profile;
    Vec<PathBuf>                                        members;
    Vec<PathBuf>                                        default_members;
    WorkspacePackageDefaults                            package;
    Vec<WorkspaceDependencyDefinition>                  dependencies;
    Vec<WorkspaceExternalSourceDefinition>              external_sources;
    Vec<WorkspacePkgConfigExternalDependencyDefinition> pkg_config_external_dependencies;
    Vec<WorkspaceCMakeExternalDependencyDefinition>     cmake_external_dependencies;
};

} // namespace lito::manifest
