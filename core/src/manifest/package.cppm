export module lito.core:manifest.package;

import rstd;
import lito.system;
import :manifest.profile;
import :manifest.language;
import :manifest.target;
import :manifest.dependency;
import :manifest.build_tool;
import :manifest.build_script;
import :manifest.conditional;
import :dependency.usage;
import :dependency.cargo;
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

enum class PackageAuthorsSource
{
    Unspecified,
    Explicit,
    Workspace,
};

struct PackageAuthors {
    PackageAuthorsSource source { PackageAuthorsSource::Unspecified };
    Vec<String>          values;
};

struct PackagePublish {
    Option<Vec<String>> include;
    Vec<String>         exclude;
};

struct PackageManifest {
    String                                             name;
    PackageVersion                                     version;
    PackageLicense                                     license;
    PackageAuthors                                     authors;
    PackagePublish                                     publish;
    Option<PackageStandardRequirement>                 standard;
    PathBuf                                            root;
    PathBuf                                            source_root;
    PathBuf                                            manifest_path;
    Option<PathBuf>                                    install_script;
    Option<ProjectProfile>                             profile;
    Vec<BuildToolRequirement>                          build_tools;
    Option<ScriptPackageManifest>                      script;
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
    Vec<lito::dependency::CargoDependencyRequirement>  cargo_external_dependencies;
    Vec<WorkspaceCargoExternalDependencyReference>     workspace_cargo_external_dependencies;
};

auto package_manifest_language(const PackageManifest& manifest) noexcept -> PackageLanguage {
    return manifest.standard.is_some() ? package_standard_language(*manifest.standard)
                                       : PackageLanguage::Cpp;
}

auto package_has_library_target(const PackageManifest& manifest) noexcept -> bool {
    for (const auto& target : manifest.targets) {
        if (package_target_kind(target) == lito::package::PackageTargetKind::Library) return true;
    }
    return false;
}

auto package_has_host_tool_target(const PackageManifest& manifest) noexcept -> bool {
    for (const auto& target : manifest.targets) {
        if (package_target_is_host_tool(target)) return true;
    }
    return false;
}

} // namespace lito::manifest
