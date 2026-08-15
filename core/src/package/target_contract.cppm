export module lito.package.target_contract;

import rstd;
import lito.error;
import lito.frontend;
import lito.cpp;
import lito.cpp.bmi;
import lito.build.profile_contract;
import lito.toolchain.spec;
import lito.dependency.contract;
import lito.manifest.contract;
import lito.package.identity;

using namespace rstd::prelude;

export namespace lito
{

enum class ArtifactKind
{
    StaticLibrary,
    TestAttachmentArchive,
    Executable,
    TestExecutable,
    BenchmarkExecutable,
    CompileTest,
};

struct DependencySpec {
    PackageTargetId      target;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct TargetSource {
    PathBuf                            relative_path;
    PathBuf                            path;
    Option<String>                     expected_module;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct TestAttachmentTarget {
    PackageTargetId test_target;
    PackageTargetId library_target;
};

struct ResolvedTarget {
    PackageTargetId                 id;
    ArtifactKind                    artifact_kind { ArtifactKind::StaticLibrary };
    String                          artifact_name;
    bool                            link_stdlib { true };
    TargetSourceManifest            source;
    PathBuf                         root;
    PathBuf                         source_root;
    UsageRequirements               usage;
    Vec<CompileTestCase>            compile_tests;
    Vec<TestAttachmentManifest>     attachments;
    Vec<RuntimeResourceManifest>    runtime_resources;
    Vec<DependencySpec>             dependencies;
    Vec<ResolvedExternalDependency> external_dependencies;
    Option<TestAttachmentTarget>    test_attachment;
};

struct PackageBuildToolRequirement {
    String               package;
    PathBuf              root;
    BuildToolRequirement requirement;
};

struct SelectedPackageMetadata {
    String         name;
    Option<String> version;
    String         source_identity;
    PathBuf        root;
};

struct PackageMetadata {
    String                       name;
    PathBuf                      root;
    PathBuf                      manifest_path;
    Vec<String>                  build_script_packages;
    String                       default_profile;
    Vec<PackageTargetId>         default_targets;
    Vec<SelectedPackageMetadata> selected_packages;
    Vec<PackageBuildToolRequirement> build_tools;
    ToolchainSpec                toolchain;
    Vec<ProfileSpec>             profiles;
    Vec<ResolvedTarget>          targets;
};

struct TargetSpec {
    PackageTargetId                 id;
    ArtifactKind                    artifact_kind { ArtifactKind::StaticLibrary };
    String                          artifact_name;
    bool                            link_stdlib { true };
    String                          archive_stem;
    Option<String>                  module_affiliation;
    PathBuf                         root;
    PathBuf                         source_root;
    Vec<TargetSource>               sources;
    Vec<DependencySpec>             dependencies;
    Vec<ResolvedExternalDependency> external_dependencies;
    UsageRequirements               usage;
    Vec<CompileTestCase>            compile_tests;
    Option<TestAttachmentTarget>    test_attachment;
};

struct PackageSpec {
    String               name;
    PathBuf              root;
    PathBuf              manifest_path;
    String               default_profile;
    Vec<PackageTargetId> default_targets;
    ToolchainSpec        toolchain;
    Vec<ProfileSpec>     profiles;
    Vec<TargetSpec>      targets;
};

struct CompileContext {
    String                id;
    String                scan_id;
    BmiRequest            bmi;
    CppCompileOptions     cpp;
    CppPublicRequirements public_requirements;
    Vec<String>           external_identities;
};

} // namespace lito
