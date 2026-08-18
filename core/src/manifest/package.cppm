export module lito.core:manifest.package;

import rstd;
import lito.system;
import :manifest.profile;
import :manifest.language;
import :manifest.target;
import :manifest.dependency;
import :manifest.build_tool;
import :manifest.conditional;
import :dependency.usage;
import :dependency.cmake;
import :dependency.pkg_config;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito::manifest
{

enum class PackageVersionSource
{
    Unspecified,
    Explicit,
    Workspace,
};

struct PackageVersion {
    PackageVersionSource source { PackageVersionSource::Unspecified };
    Option<String>       value;
};

enum class PackageLicenseSource
{
    Unspecified,
    Explicit,
    Workspace,
};

struct PackageLicense {
    PackageLicenseSource source { PackageLicenseSource::Unspecified };
    Option<String>       value;
};

struct PackageManifest {
    String                                             name;
    PackageVersion                                     version;
    PackageLicense                                     license;
    Option<PackageLanguageRequirement>                 language;
    PathBuf                                            root;
    PathBuf                                            source_root;
    PathBuf                                            manifest_path;
    Option<PathBuf>                                    install_script;
    Option<ProjectProfile>                             profile;
    Vec<BuildToolRequirement>                          build_tools;
    Vec<PackageExternalSourceDeclaration>              external_sources;
    Vec<WorkspaceExternalSourceReference>              workspace_external_sources;
    Vec<SourceGroupManifest>                           source_groups;
    Vec<PackageTargetManifest>                         targets;
    TargetPredicate                                    target;
    Vec<CompileTestCase>                               compile_tests;
    lito::dependency::DeclaredUsageRequirements        usage;
    Vec<ConditionalConfiguration>                      conditions;
    Vec<FeatureDeclaration>                            features;
    Vec<DeclaredDependency>                            dependencies;
    Vec<DeclaredDependency>                            dev_dependencies;
    Vec<DeclaredRuntimeDependency>                     runtime_dependencies;
    Vec<WorkspaceDependencyReference>                  workspace_dependencies;
    Vec<WorkspaceDependencyReference>                  workspace_dev_dependencies;
    Vec<WorkspaceRuntimeDependencyReference>           workspace_runtime_dependencies;
    Vec<lito::dependency::PkgConfigExternalDependency> pkg_config_external_dependencies;
    Vec<WorkspacePkgConfigExternalDependencyReference> workspace_pkg_config_external_dependencies;
    Vec<lito::dependency::CMakeDependencyRequirement>  cmake_external_dependencies;
    Vec<WorkspaceCMakeExternalDependencyReference>     workspace_cmake_external_dependencies;
};

} // namespace lito::manifest
