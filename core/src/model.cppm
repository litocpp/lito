module;
#include <rstd/enum.hpp>

export module lito.model;

import rstd;
import rstd.bench;
import lito.frontend;
import lito.profiling;
export import lito.cpp;
export import lito.bmi;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

using String  = rstd::string::String;
using PathBuf = rstd::path::PathBuf;

template<typename T>
using Vec = rstd::vec::Vec<T>;

using TargetId = usize;
using UnitId   = usize;

enum class ErrorKind
{
    InvalidRequest,
    Config,
    Manifest,
    Filesystem,
    Toolchain,
    Dependency,
    Lock,
    Artifact,
};

struct Error {
    ErrorKind kind { ErrorKind::InvalidRequest };
    String    message;

    static auto make(ErrorKind kind, ref<str> message) -> Error {
        return Error { kind, String::make(message) };
    }

    static auto make(ErrorKind kind, String message) -> Error {
        return Error { kind, rstd::move(message) };
    }
};

template<typename T>
using Result = rstd::Result<T, Error>;

enum class BuildProfile
{
    Debug,
    Release,
};

enum class DependencyVisibility
{
    Public,
    Private,
    Runtime,
};

enum class PackageSourceKind
{
    Path,
    Git,
};

enum class GitReferenceKind
{
    DefaultBranch,
    Branch,
    Tag,
    Rev,
    Commit,
};

struct GitReference {
    GitReferenceKind kind { GitReferenceKind::DefaultBranch };
    String           value;
};

enum class GitResolutionMode
{
    ReuseLocked,
    Refresh,
};

auto git_commit_is_valid(ref<str> value) noexcept -> bool {
    if (value.len() != usize(40)) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'f') ||
               (ascii >= 'A' && ascii <= 'F'))) {
            return false;
        }
    }
    return true;
}

enum class PkgConfigVersionOperator
{
    Equal,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

enum class PkgConfigQueryMode
{
    Shared,
    Static,
};

struct PkgConfigVersionRequirement {
    PkgConfigVersionOperator comparison { PkgConfigVersionOperator::Equal };
    String                   value;
};

struct PkgConfigDependencyRequirement {
    String                              module;
    Option<PkgConfigVersionRequirement> version;
    PkgConfigQueryMode                  mode { PkgConfigQueryMode::Shared };
};

struct CMakeCacheEntry {
    String name;
    String value;
};

class CMakeDependencySource {
    RSTD_ENUM(CMakeDependencySource,
              (Installed),
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)),
              (Archive, (String url; String sha256;)))
};

enum class CMakeIntegration
{
    Install,
    BuildTree,
};

struct CMakeTargetRequirement {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct CMakeDependencyRequirement {
    String                      alias;
    String                      package;
    CMakeDependencySource       source;
    CMakeIntegration            integration { CMakeIntegration::Install };
    Option<PathBuf>             adapter;
    Option<PathBuf>             config_directory;
    Vec<CMakeCacheEntry>        cache;
    Vec<CMakeTargetRequirement> targets;
    Option<PathBuf>             declaration_root;
    Option<PathBuf>             adapter_root;
};

struct PkgConfigExternalDependency {
    String                         alias;
    PkgConfigDependencyRequirement requirement;
    DependencyVisibility           visibility { DependencyVisibility::Private };
};

class PackageSourceRequirement {
    RSTD_ENUM(PackageSourceRequirement,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)))
};

enum class SourceDiscoveryMode
{
    Explicit,
    Module,
};

enum class SourceOrigin
{
    Explicit,
    Convention,
};

enum class ArtifactKind
{
    StaticLibrary,
    TestAttachmentArchive,
    Executable,
    TestExecutable,
    CompileTest,
};

enum class PackageSelectionPurpose
{
    All,
    Production,
    Test,
};

enum class PackageVersionSource
{
    Explicit,
    Workspace,
};

struct PackageVersion {
    PackageVersionSource source { PackageVersionSource::Explicit };
    Option<String>       value;
};

struct ToolchainSpec {
    PathBuf compiler;
    PathBuf archiver;
    PathBuf formatter;

    auto clone() const -> ToolchainSpec {
        return ToolchainSpec {
            .compiler  = compiler.clone(),
            .archiver  = archiver.clone(),
            .formatter = formatter.clone(),
        };
    }
};

struct GitSourcePatch {
    String  git;
    PathBuf path;
};

struct PackageSourceConfig {
    Vec<GitSourcePatch> patches;

    auto clone() const -> PackageSourceConfig {
        auto copied = Vec<GitSourcePatch>::with_capacity(patches.len());
        for (const auto& patch : patches) {
            copied.push(GitSourcePatch {
                .git  = patch.git.clone(),
                .path = patch.path.clone(),
            });
        }
        return PackageSourceConfig { .patches = rstd::move(copied) };
    }
};

struct PkgConfigProviderConfig {
    PathBuf         executable;
    Vec<PathBuf>    search_paths;
    Vec<PathBuf>    library_paths;
    Option<PathBuf> sysroot;
    bool            target_configured { false };

    auto clone() const -> PkgConfigProviderConfig {
        auto result = PkgConfigProviderConfig {
            .executable        = executable.clone(),
            .search_paths      = as<rstd::clone::Clone>(search_paths).clone(),
            .library_paths     = as<rstd::clone::Clone>(library_paths).clone(),
            .target_configured = target_configured,
        };
        if (sysroot.is_some()) result.sysroot = Some(sysroot->clone());
        return result;
    }
};

struct CMakeProviderConfig {
    PathBuf executable;
    String  generator;

    auto clone() const -> CMakeProviderConfig {
        return CMakeProviderConfig {
            .executable = executable.clone(),
            .generator  = generator.clone(),
        };
    }
};

struct ProjectConfig {
    PathBuf                 root;
    ToolchainSpec           toolchain;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
};

struct ProfileSpec {
    String            name;
    BmiRequest        bmi;
    CppCompileOptions cpp;
    Vec<String>       linker_options;
};

struct DependencySpec {
    String               target;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct LinkArgumentSequence {
    Vec<String> tokens;
    String      source;
    String      identity;

    auto clone() const -> LinkArgumentSequence {
        return LinkArgumentSequence {
            .tokens   = as<rstd::clone::Clone>(tokens).clone(),
            .source   = source.clone(),
            .identity = identity.clone(),
        };
    }
};

struct ResolvedExternalTargetUsage {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
    CppArgumentLayer     compile_arguments;
    String               identity;

    auto clone() const -> ResolvedExternalTargetUsage {
        return ResolvedExternalTargetUsage {
            .name              = name.clone(),
            .visibility        = visibility,
            .compile_arguments = as<rstd::clone::Clone>(compile_arguments).clone(),
            .identity          = identity.clone(),
        };
    }
};

struct ResolvedExternalDependency {
    String                           alias;
    String                           provider;
    String                           version;
    Vec<ResolvedExternalTargetUsage> targets;
    LinkArgumentSequence             link_arguments;
    String                           identity;

    auto clone() const -> ResolvedExternalDependency {
        auto copied_targets = Vec<ResolvedExternalTargetUsage>::with_capacity(targets.len());
        for (const auto& target : targets) copied_targets.push(target.clone());
        return ResolvedExternalDependency {
            .alias          = alias.clone(),
            .provider       = provider.clone(),
            .version        = version.clone(),
            .targets        = rstd::move(copied_targets),
            .link_arguments = link_arguments.clone(),
            .identity       = identity.clone(),
        };
    }
};

struct UsageRequirements {
    Vec<PathBuf>     public_include_directories;
    Vec<PathBuf>     private_include_directories;
    Vec<String>      public_definitions;
    Vec<String>      private_definitions;
    Vec<String>      public_options;
    Vec<String>      private_options;
    CppArgumentLayer public_arguments;
    CppArgumentLayer private_arguments;
    Vec<String>      private_linker_options;
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

enum class TargetFamily
{
    Unix,
    Windows,
    Unknown,
};

struct TargetInfo {
    String       triple;
    String       arch;
    String       os;
    TargetFamily family { TargetFamily::Unknown };

    auto family_name() const noexcept -> ref<str> {
        switch (family) {
        case TargetFamily::Unix: return "unix"_str;
        case TargetFamily::Windows: return "windows"_str;
        case TargetFamily::Unknown: return "unknown"_str;
        }
        return "unknown"_str;
    }
};

struct TargetPredicate {
    Vec<String> families;
    Vec<String> operating_systems;
    Vec<String> excluded_families;
    Vec<String> excluded_operating_systems;

    auto matches(const TargetInfo& target) const noexcept -> bool {
        const auto contains = [](const Vec<String>& values, ref<str> value) {
            if (values.is_empty()) return true;
            for (const auto& candidate : values) {
                if (candidate.as_str() == value) return true;
            }
            return false;
        };
        const auto excludes = [](const Vec<String>& values, ref<str> value) {
            for (const auto& candidate : values) {
                if (candidate.as_str() == value) return true;
            }
            return false;
        };
        return contains(families, target.family_name()) &&
               contains(operating_systems, target.os.as_str()) &&
               ! excludes(excluded_families, target.family_name()) &&
               ! excludes(excluded_operating_systems, target.os.as_str());
    }
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
    String                            name;
    PackageVersion                    version;
    Option<String>                    root_module;
    PathBuf                           root;
    PathBuf                           source_root;
    PathBuf                           manifest_path;
    ArtifactKind                      artifact_kind { ArtifactKind::StaticLibrary };
    String                            artifact_name;
    SourceDiscoveryMode               discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>                      declared_sources;
    Vec<ConditionalSourceGroup>       conditional_source_groups;
    Vec<TestAttachmentManifest>       test_attachments;
    TargetPredicate                   target;
    Vec<CompileTestCase>              compile_tests;
    UsageRequirements                 usage;
    Vec<DeclaredDependency>           dependencies;
    Vec<WorkspaceDependencyReference> workspace_dependencies;
    Vec<PkgConfigExternalDependency>  pkg_config_external_dependencies;
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

using ProvidedModule = frontend::ProvidedModule;
using SourceLocation = frontend::DependencyLocation;
using ModuleImport   = frontend::ModuleImport;
using FrontendResult = frontend::FrontendResult;

struct ResolvedSource {
    PathBuf                            relative_path;
    PathBuf                            canonical_path;
    SourceOrigin                       origin { SourceOrigin::Explicit };
    Option<String>                     expected_module;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct ResolvedSourceSet {
    Vec<ResolvedSource> sources;
};

struct ResolvedDependency {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

class ResolvedCMakeDependencySource {
    RSTD_ENUM(ResolvedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity;)),
              (Archive, (String url; String sha256;)))
};

struct ResolvedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    ResolvedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    Option<PathBuf>               adapter;
    Option<PathBuf>               config_directory;
    Vec<CMakeCacheEntry>          cache;
    Vec<CMakeTargetRequirement>   targets;
};

struct ResolvedPackageSource {
    String            identity;
    PackageSourceKind kind { PackageSourceKind::Path };
    PathBuf           root_directory;
    PathBuf           path;
    String            git;
    GitReference      reference;
    String            commit;
};

struct LockedGitSource {
    String       git;
    GitReference reference;
    String       commit;
};

struct PackageResolutionOptions {
    bool                 locked { false };
    GitResolutionMode    git { GitResolutionMode::ReuseLocked };
    Vec<LockedGitSource> git_sources;
    PackageSourceConfig  sources;

    auto clone() const -> PackageResolutionOptions {
        auto locked_sources = Vec<LockedGitSource>::with_capacity(git_sources.len());
        for (const auto& source : git_sources) {
            locked_sources.push(LockedGitSource {
                .git = source.git.clone(),
                .reference =
                    GitReference {
                        .kind  = source.reference.kind,
                        .value = source.reference.value.clone(),
                    },
                .commit = source.commit.clone(),
            });
        }
        return PackageResolutionOptions {
            .locked      = locked,
            .git         = git,
            .git_sources = rstd::move(locked_sources),
            .sources     = sources.clone(),
        };
    }
};

struct ResolvedPackage {
    String                                  source_identity;
    PathBuf                                 source_manifest;
    PackageManifest                         manifest;
    Vec<ResolvedDependency>                 dependencies;
    Vec<ResolvedCMakeDependencyRequirement> cmake_external_dependencies;
};

struct ResolvedPackageGraph {
    String                     name;
    Vec<String>                root_names;
    PathBuf                    root_directory;
    PathBuf                    manifest_path;
    bool                       root_is_workspace { false };
    Vec<ResolvedPackageSource> sources;
    Vec<ResolvedPackage>       packages;
};

struct PackageSelection {
    PathBuf     root;
    Vec<String> packages;
};

struct ResolvedPackageSelection {
    ResolvedPackageGraph graph;
    Vec<String>          selected_root_names;
    Vec<String>          selected_package_names;
};

struct ResolvedPackageSources {
    String            package_name;
    ResolvedSourceSet sources;
};

enum class LockStatus
{
    Unchanged,
    Updated,
};

struct BuildConfiguration {
    BuildProfile             profile { BuildProfile::Debug };
    ToolchainSpec            toolchain;
    StandardLibrary          standard_library { StandardLibrary::Libstdcxx };
    BmiMode                  bmi_mode { BmiMode::Reduced };
    BmiSourceEmbeddingPolicy bmi_source_embedding { BmiSourceEmbeddingPolicy::ExternalSources };
    bool                     exceptions { false };
    bool                     rtti { false };
    String                   language_standard;
    Vec<String>              options;
    Vec<String>              linker_options;

    auto clone() const -> BuildConfiguration {
        return BuildConfiguration {
            .profile              = profile,
            .toolchain            = toolchain.clone(),
            .standard_library     = standard_library,
            .bmi_mode             = bmi_mode,
            .bmi_source_embedding = bmi_source_embedding,
            .exceptions           = exceptions,
            .rtti                 = rtti,
            .language_standard    = language_standard.clone(),
            .options              = options.clone(),
            .linker_options       = linker_options.clone(),
        };
    }
};

struct TargetSource {
    PathBuf                            relative_path;
    PathBuf                            path;
    Option<String>                     expected_module;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct TestAttachmentTarget {
    String test_target;
    String library_target;
};

struct TargetMetadata {
    PackageManifest                 manifest;
    Vec<DependencySpec>             dependencies;
    Vec<ResolvedExternalDependency> external_dependencies;
    Option<TestAttachmentTarget>    test_attachment;
};

struct PackageMetadata {
    String              name;
    PathBuf             root;
    PathBuf             manifest_path;
    String              default_profile;
    Vec<String>         default_targets;
    ToolchainSpec       toolchain;
    Vec<ProfileSpec>    profiles;
    Vec<TargetMetadata> targets;
};

struct TargetSpec {
    String                          name;
    ArtifactKind                    artifact_kind { ArtifactKind::StaticLibrary };
    String                          artifact_name;
    String                          archive_stem;
    Option<String>                  module_affiliation;
    PathBuf                         root;
    Vec<TargetSource>               sources;
    Vec<DependencySpec>             dependencies;
    Vec<ResolvedExternalDependency> external_dependencies;
    UsageRequirements               usage;
    Vec<CompileTestCase>            compile_tests;
    Option<TestAttachmentTarget>    test_attachment;
};

struct PackageSpec {
    String           name;
    PathBuf          root;
    PathBuf          manifest_path;
    String           default_profile;
    Vec<String>      default_targets;
    ToolchainSpec    toolchain;
    Vec<ProfileSpec> profiles;
    Vec<TargetSpec>  targets;
};

struct CompileContext {
    String                id;
    BmiRequest            bmi;
    CppCompileOptions     cpp;
    CppPublicRequirements public_requirements;
    Vec<String>           external_identities;
};

struct CompilerIdentity {
    PathBuf path;
    String  version;
    String  target;
    PathBuf resource_directory;
    String  build_identity;
    u64     size {};
    i64     modified_seconds {};
    u32     modified_nanoseconds {};
};

struct CompileCommandResult {
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    rstd::time::Duration elapsed;
};

struct CompileInvocation {
    Vec<String>     arguments;
    PathBuf         working_directory;
    String          identity;
    PathBuf         staged_object;
    PathBuf         final_object;
    Option<PathBuf> staged_bmi;
    Option<PathBuf> final_bmi;
};

enum class LinkArchiveMode
{
    Normal,
    Whole,
};

struct LinkArchive {
    PathBuf         path;
    LinkArchiveMode mode { LinkArchiveMode::Normal };
};

class PlannedLinkInput {
    RSTD_ENUM(PlannedLinkInput,
              (Target, (TargetId target;)),
              (External, (LinkArgumentSequence arguments;)))
};

class ResolvedLinkInput {
    RSTD_ENUM(ResolvedLinkInput,
              (Archive, (LinkArchive archive;)),
              (External, (LinkArgumentSequence arguments;)))
};

struct SourceDiscoveryPlan {
    usize                      profile {};
    Vec<String>                target_names;
    Vec<TargetId>              target_order;
    Vec<CompileContext>        contexts;
    Vec<Vec<TargetId>>         visible_targets;
    Vec<Vec<PlannedLinkInput>> link_inputs;
    Vec<Vec<String>>           linker_options;
};

struct PackagePlan {
    const PackageSpec*         package {};
    const ProfileSpec*         profile {};
    Vec<TargetId>              target_order;
    Vec<CompileContext>        contexts;
    Vec<Vec<TargetId>>         visible_targets;
    Vec<Vec<PlannedLinkInput>> link_inputs;
    Vec<Vec<String>>           linker_options;
};

struct UnitSpec {
    UnitId                 id {};
    TargetId               target {};
    PathBuf                relative_source;
    PathBuf                source;
    PathBuf                object;
    PathBuf                cache_record;
    Option<PathBuf>        compile_test_record;
    Option<BmiArtifact>    bmi;
    const CompileContext*  context {};
    const CompileTestCase* compile_test {};
};

struct PreparedUnit {
    UnitSpec                           unit;
    PathBuf                            working_directory;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct ScanResult {
    UnitId                 unit {};
    Option<ProvidedModule> provided;
    Option<String>         implementation_module;
    Vec<String>            required_modules;
    Vec<PathBuf>           header_inputs;
    String                 preprocessor_environment;
};

struct ModulePlan {
    Vec<UnitId>      compile_order;
    Vec<Vec<UnitId>> direct_inputs;
    Vec<Vec<UnitId>> resolved_inputs;
};

enum class BuildEventKind
{
    Scan,
    ScanReuse,
    Compile,
    Reuse,
    Archive,
    Link,
};

struct BuildEvent {
    BuildEventKind        kind { BuildEventKind::Scan };
    ref<str>              target;
    ref<rstd::path::Path> path;
};

struct BuildObserver {
    void* context {};
    void (*notify)(void*, const BuildEvent&) noexcept {};
};

struct ScanExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct BuildExecutionPolicy {
    ScanExecutionPolicy scan;
};

struct ToolchainStatistics {
    usize target_queries {};
    usize preprocessor_environment_entries {};
    usize preprocessor_environment_queries {};
    usize preprocessor_environment_hits {};
    usize builtin_snapshots {};
    usize builtin_refreshes {};
    usize builtin_hits {};
    usize builtin_macro_processes {};
    usize builtin_capability_processes {};
    usize clang_macros {};
    usize native_macro_owners {};
    usize clang_capabilities {};
    usize native_capabilities {};
    usize builtin_macro_output_bytes {};
    usize builtin_capability_input_bytes {};
    usize builtin_capability_output_bytes {};
    usize ignored_builtin_options {};
};

struct BuildRequest {
    PackageSelection        selection;
    Vec<String>             targets;
    PathBuf                 output;
    BuildConfiguration      configuration;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
    PackageSelectionPurpose purpose { PackageSelectionPurpose::Production };
    bool                    locked { false };
    BuildExecutionPolicy    execution;
    Option<BuildObserver>   observer;
};

struct BuiltArtifact {
    String       package;
    String       target;
    ArtifactKind kind { ArtifactKind::StaticLibrary };
    PathBuf      path;
    PathBuf      package_root;
};

struct CompileTestExecution {
    String               package;
    String               name;
    PathBuf              source;
    CompileTestOutcome   expected { CompileTestOutcome::Failure };
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    Option<String>       mismatch;
    rstd::time::Duration elapsed;

    auto success() const noexcept -> bool { return mismatch.is_none(); }
};

struct BuildSummary {
    String                       package;
    String                       profile;
    PathBuf                      output;
    usize                        scanned {};
    usize                        compiled {};
    usize                        reused {};
    Vec<BuiltArtifact>           artifacts;
    frontend::FrontendStatistics frontend;
    ToolchainStatistics          toolchain;
    ScanProfileReport            scan_profile;
    BuildTimingReport            build_timing;
    Vec<CompileTestExecution>    compile_tests;
};

struct ScanRequest {
    PackageSelection        selection;
    Vec<String>             targets;
    PathBuf                 source;
    BuildConfiguration      configuration;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
    bool                    locked { false };
};

struct ScanReport {
    String         target;
    String         profile;
    FrontendResult result;
};

struct FormatRequest {
    PackageSelection    selection;
    ToolchainSpec       toolchain;
    PackageSourceConfig sources;
};

struct UpdateRequest {
    PathBuf             root;
    PackageSourceConfig sources;
};

struct FormatSummary {
    usize packages {};
    usize files {};
};

} // namespace lito
