module;
#include <rstd/enum.hpp>

export module tenon.model;

import rstd;
import rstd.bench;
import tenon.frontend;
import tenon.profiling;
export import tenon.cpp;
export import tenon.bmi;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace tenon
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
};

struct GitReference {
    GitReferenceKind kind { GitReferenceKind::DefaultBranch };
    String           value;
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
    Documentation,
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

struct ProjectConfig {
    PathBuf             root;
    ToolchainSpec       toolchain;
    PackageSourceConfig sources;
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

struct UsageRequirements {
    Vec<PathBuf> public_include_directories;
    Vec<PathBuf> private_include_directories;
    Vec<String>  public_definitions;
    Vec<String>  private_definitions;
    Vec<String>  public_options;
    Vec<String>  private_options;
    Vec<String>  private_linker_options;
};

struct DeclaredDependency {
    String                   name;
    PackageSourceRequirement source;
    DependencyVisibility     visibility { DependencyVisibility::Private };
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
    Vec<String>        diagnostic_contains;
    Vec<String>        diagnostic_contains_any;
};

struct PackageManifest {
    String                      name;
    PackageVersion              version;
    Option<String>              root_module;
    PathBuf                     root;
    PathBuf                     manifest_path;
    ArtifactKind                artifact_kind { ArtifactKind::StaticLibrary };
    String                      artifact_name;
    SourceDiscoveryMode         discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>                declared_sources;
    Vec<ConditionalSourceGroup> conditional_source_groups;
    Vec<TestAttachmentManifest> test_attachments;
    TargetPredicate             target;
    Vec<CompileTestCase>        compile_tests;
    UsageRequirements           usage;
    Vec<DeclaredDependency>     dependencies;
};

struct WorkspacePackageDefaults {
    Option<String> version;
};

struct WorkspaceManifest {
    PathBuf                  root;
    PathBuf                  manifest_path;
    Vec<PathBuf>             members;
    Vec<PathBuf>             default_members;
    WorkspacePackageDefaults package;
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
    PathBuf                             relative_path;
    PathBuf                             canonical_path;
    SourceOrigin                        origin { SourceOrigin::Explicit };
    Option<String>                      expected_module;
    Option<frontend::FrontendAnalysis>  frontend_analysis;
    Option<frontend::DocumentationUnit> documentation;
};

struct ResolvedSourceSet {
    Vec<ResolvedSource> sources;
};

struct ResolvedDependency {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
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
    Vec<LockedGitSource> git_sources;
    PackageSourceConfig  sources;
};

struct ResolvedPackage {
    String                  source_identity;
    PathBuf                 source_manifest;
    PackageManifest         manifest;
    Vec<ResolvedDependency> dependencies;
};

struct ResolvedPackageGraph {
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
};

struct TargetSource {
    PathBuf                             relative_path;
    PathBuf                             path;
    Option<String>                      expected_module;
    Option<frontend::FrontendAnalysis>  frontend_analysis;
    Option<frontend::DocumentationUnit> documentation;
};

struct TestAttachmentTarget {
    String test_target;
    String library_target;
};

struct TargetMetadata {
    PackageManifest              manifest;
    Vec<DependencySpec>          dependencies;
    Option<TestAttachmentTarget> test_attachment;
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
    String                       name;
    ArtifactKind                 artifact_kind { ArtifactKind::StaticLibrary };
    String                       artifact_name;
    String                       archive_stem;
    Option<String>               module_affiliation;
    PathBuf                      root;
    Vec<TargetSource>            sources;
    Vec<DependencySpec>          dependencies;
    UsageRequirements            usage;
    Vec<CompileTestCase>         compile_tests;
    Option<TestAttachmentTarget> test_attachment;
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

struct SourceDiscoveryPlan {
    usize               profile {};
    Vec<String>         target_names;
    Vec<TargetId>       target_order;
    Vec<CompileContext> contexts;
    Vec<Vec<TargetId>>  visible_targets;
    Vec<Vec<TargetId>>  link_dependencies;
    Vec<Vec<String>>    linker_options;
};

struct PackagePlan {
    const PackageSpec*  package {};
    const ProfileSpec*  profile {};
    Vec<TargetId>       target_order;
    Vec<CompileContext> contexts;
    Vec<Vec<TargetId>>  visible_targets;
    Vec<Vec<TargetId>>  link_dependencies;
    Vec<Vec<String>>    linker_options;
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
    PackageSelectionPurpose purpose { PackageSelectionPurpose::Production };
    bool                    locked { false };
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
    PackageSelection    selection;
    Vec<String>         targets;
    PathBuf             source;
    BuildConfiguration  configuration;
    PackageSourceConfig sources;
    bool                locked { false };
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

struct FormatSummary {
    usize packages {};
    usize files {};
};

struct DocRequest {
    PackageSelection    selection;
    Vec<String>         targets;
    PathBuf             output;
    BuildConfiguration  configuration;
    PackageSourceConfig sources;
    bool                locked { false };
};

enum class DocDiagnosticSeverity
{
    Warning,
    Error,
};

struct DocDiagnosticSummary {
    DocDiagnosticSeverity severity { DocDiagnosticSeverity::Warning };
    String                code;
    String                message;
    PathBuf               path;
    usize                 line {};
};

struct DocPackageSummary {
    String                    name;
    PathBuf                   directory;
    PathBuf                   json;
    PathBuf                   index;
    usize                     symbols {};
    usize                     documented {};
    usize                     undocumented {};
    usize                     unsupported {};
    usize                     diagnostics {};
    Vec<DocDiagnosticSummary> diagnostic_details;
};

struct DocSummary {
    String                       profile;
    PathBuf                      output;
    PathBuf                      index;
    Vec<DocPackageSummary>       packages;
    frontend::FrontendStatistics frontend;
    ToolchainStatistics          toolchain;
};

} // namespace tenon
