module;
#include <rstd/enum.hpp>

export module lito.manifest.contract;

import rstd;
import lito.error;
import lito.cpp;
import lito.build.profile_contract;
import lito.platform.contract;
import lito.source.contract;
import lito.dependency.contract;
import lito.package.identity;

using namespace rstd::prelude;

export namespace lito
{

enum class SourceDiscoveryMode
{
    Explicit,
    Module,
};

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

struct WorkspacePkgConfigExternalDependencyReference {
    String               alias;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct WorkspaceCMakeExternalDependencyReference {
    String                      alias;
    Vec<CMakeTargetRequirement> targets;
};

struct ConditionalSourceGroup {
    TargetPredicate predicate;
    Vec<PathBuf>    sources;
};

struct TestAttachmentManifest {
    String                      package;
    Vec<PathBuf>                sources;
    Vec<ConditionalSourceGroup> conditional_source_groups;
};

struct TargetSourceManifest {
    Option<String>              module;
    SourceDiscoveryMode         discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>                declared_sources;
    Vec<ConditionalSourceGroup> conditional_source_groups;
};

class PackageTargetManifest {
    RSTD_ENUM(PackageTargetManifest,
              (Library, (String name; String archive; TargetSourceManifest source;)),
              (Binary, (String name; TargetSourceManifest source; bool link_stdlib;)),
              (Test,
               (String name; TargetSourceManifest source; bool link_stdlib;
                Vec<TestAttachmentManifest>                    attachments;)),
              (Benchmark, (String name; TargetSourceManifest source; bool link_stdlib;)))
};

auto package_target_kind(const PackageTargetManifest& target) noexcept -> PackageTargetKind {
    if (target.is_Library()) return PackageTargetKind::Library;
    if (target.is_Binary()) return PackageTargetKind::Binary;
    if (target.is_Test()) return PackageTargetKind::Test;
    return PackageTargetKind::Benchmark;
}

auto package_target_name(const PackageTargetManifest& target) noexcept -> ref<str> {
    if (target.is_Library()) return target.as_Library().name.as_str();
    if (target.is_Binary()) return target.as_Binary().name.as_str();
    if (target.is_Test()) return target.as_Test().name.as_str();
    return target.as_Benchmark().name.as_str();
}

auto package_target_source(const PackageTargetManifest& target) noexcept
    -> const TargetSourceManifest& {
    if (target.is_Library()) return target.as_Library().source;
    if (target.is_Binary()) return target.as_Binary().source;
    if (target.is_Test()) return target.as_Test().source;
    return target.as_Benchmark().source;
}

auto package_target_source(PackageTargetManifest& target) noexcept -> TargetSourceManifest& {
    if (target.is_Library()) return target.as_Library().source;
    if (target.is_Binary()) return target.as_Binary().source;
    if (target.is_Test()) return target.as_Test().source;
    return target.as_Benchmark().source;
}

auto package_target_artifact_name(const PackageTargetManifest& target) noexcept -> ref<str> {
    if (target.is_Library()) return target.as_Library().archive.as_str();
    return package_target_name(target);
}

auto package_target_links_stdlib(const PackageTargetManifest& target) noexcept -> bool {
    if (target.is_Binary()) return target.as_Binary().link_stdlib;
    if (target.is_Test()) return target.as_Test().link_stdlib;
    if (target.is_Benchmark()) return target.as_Benchmark().link_stdlib;
    return true;
}

auto package_target_attachments(const PackageTargetManifest& target) noexcept
    -> Option<ref<Vec<TestAttachmentManifest>>> {
    if (! target.is_Test()) return None();
    return Some(ref<Vec<TestAttachmentManifest>>::from_raw_parts(
        rstd::addressof(target.as_Test().attachments)));
}

enum class CompileTestOutcome
{
    Success,
    Failure,
};

struct CompileTestCase {
    String             name;
    PathBuf            source;
    CompileTestOutcome outcome { CompileTestOutcome::Failure };
    Vec<String>        options;
    CppArgumentLayer   arguments;
    Vec<String>        diagnostic_contains;
    Vec<String>        diagnostic_contains_any;
};

struct PackageManifest {
    String                                             name;
    PackageVersion                                     version;
    PathBuf                                            root;
    PathBuf                                            source_root;
    PathBuf                                            manifest_path;
    Option<ProjectProfile>                             profile;
    Vec<PackageTargetManifest>                         targets;
    TargetPredicate                                    target;
    Vec<CompileTestCase>                               compile_tests;
    UsageRequirements                                  usage;
    Vec<DeclaredDependency>                            dependencies;
    Vec<DeclaredDependency>                            dev_dependencies;
    Vec<WorkspaceDependencyReference>                  workspace_dependencies;
    Vec<WorkspaceDependencyReference>                  workspace_dev_dependencies;
    Vec<PkgConfigExternalDependency>                   pkg_config_external_dependencies;
    Vec<WorkspacePkgConfigExternalDependencyReference> workspace_pkg_config_external_dependencies;
    Vec<CMakeDependencyRequirement>                    cmake_external_dependencies;
    Vec<WorkspaceCMakeExternalDependencyReference>     workspace_cmake_external_dependencies;
};

struct WorkspacePackageDefaults {
    Option<String> version;
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

struct WorkspaceManifest {
    String                                              name;
    PathBuf                                             root;
    PathBuf                                             manifest_path;
    Option<ProjectProfile>                              profile;
    Vec<PathBuf>                                        members;
    Vec<PathBuf>                                        default_members;
    WorkspacePackageDefaults                            package;
    Vec<WorkspaceDependencyDefinition>                  dependencies;
    Vec<WorkspacePkgConfigExternalDependencyDefinition> pkg_config_external_dependencies;
    Vec<WorkspaceCMakeExternalDependencyDefinition>     cmake_external_dependencies;
};

enum class ManifestKind
{
    Package,
    Workspace,
};

struct ManifestDocument {
    ManifestKind              kind { ManifestKind::Package };
    Option<PackageManifest>   package;
    Option<WorkspaceManifest> workspace;
};

struct ManifestLocation {
    PathBuf directory;
    PathBuf manifest;
};

} // namespace lito
