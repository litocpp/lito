module;
#include <rstd/enum.hpp>

export module tenon.model;

import rstd;

using namespace rstd::prelude;

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

enum class StandardLibrary
{
    Libstdcxx,
    Libcxx,
};

enum class BmiMode
{
    Reduced,
    Full,
};

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
    Executable,
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
    String          name;
    StandardLibrary standard_library { StandardLibrary::Libstdcxx };
    BmiMode         bmi_mode { BmiMode::Reduced };
    String          language_standard;
    Vec<String>     options;
    Vec<String>     linker_options;
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

struct PackageManifest {
    String                  name;
    PackageVersion          version;
    Option<String>          root_module;
    PathBuf                 root;
    PathBuf                 manifest_path;
    ArtifactKind            artifact_kind { ArtifactKind::StaticLibrary };
    String                  artifact_name;
    SourceDiscoveryMode     discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>            declared_sources;
    UsageRequirements       usage;
    Vec<DeclaredDependency> dependencies;
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

struct ProvidedModule {
    String logical_name;
    bool   is_interface { false };
};

struct SourceLocation {
    PathBuf path;
    usize   line {};
};

struct ModuleImport {
    String         logical_name;
    SourceLocation location;
};

struct PreprocessedModuleFacts {
    PathBuf                source;
    Option<ProvidedModule> provided;
    Option<String>         implementation_module;
    Vec<ModuleImport>      imports;
    Vec<PathBuf>           header_inputs;
    usize                  output_bytes {};
};

struct ResolvedSource {
    PathBuf                         relative_path;
    PathBuf                         canonical_path;
    SourceOrigin                    origin { SourceOrigin::Explicit };
    Option<String>                  expected_module;
    Option<PreprocessedModuleFacts> preprocessed;
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
    BuildProfile    profile { BuildProfile::Debug };
    ToolchainSpec   toolchain;
    StandardLibrary standard_library { StandardLibrary::Libstdcxx };
    BmiMode         bmi_mode { BmiMode::Reduced };
    String          language_standard;
    Vec<String>     options;
    Vec<String>     linker_options;
};

struct TargetSource {
    PathBuf                         relative_path;
    PathBuf                         path;
    Option<String>                  expected_module;
    Option<PreprocessedModuleFacts> preprocessed;
};

struct TargetMetadata {
    PackageManifest     manifest;
    Vec<DependencySpec> dependencies;
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
    String              name;
    ArtifactKind        artifact_kind { ArtifactKind::StaticLibrary };
    String              artifact_name;
    Option<String>      module_affiliation;
    PathBuf             root;
    Vec<TargetSource>   sources;
    Vec<DependencySpec> dependencies;
    UsageRequirements   usage;
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
    String          id;
    StandardLibrary standard_library { StandardLibrary::Libstdcxx };
    BmiMode         bmi_mode { BmiMode::Reduced };
    String          language_standard;
    Vec<PathBuf>    include_directories;
    Vec<String>     definitions;
    Vec<String>     options;
};

struct CompilerIdentity {
    PathBuf path;
    String  version;
    String  target;
    PathBuf resource_directory;
    u64     size {};
    i64     modified_seconds {};
    u32     modified_nanoseconds {};
};

struct CompileInvocation {
    Vec<String> arguments;
    PathBuf     working_directory;
    String      identity;
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
    UnitId                id {};
    TargetId              target {};
    PathBuf               relative_source;
    PathBuf               source;
    PathBuf               object;
    PathBuf               scan_depfile;
    PathBuf               cache_record;
    Option<PathBuf>       bmi;
    const CompileContext* context {};
};

struct PreparedUnit {
    UnitSpec                       unit;
    PathBuf                        working_directory;
    const PreprocessedModuleFacts* preprocessed {};
};

struct ScanResult {
    UnitId                 unit {};
    Option<ProvidedModule> provided;
    Option<String>         implementation_module;
    Vec<String>            required_modules;
    Vec<PathBuf>           header_inputs;
};

struct ModulePlan {
    Vec<UnitId>      compile_order;
    Vec<Vec<UnitId>> direct_inputs;
};

enum class BuildEventKind
{
    Scan,
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

struct BuildRequest {
    PackageSelection      selection;
    Vec<String>           targets;
    PathBuf               output;
    BuildConfiguration    configuration;
    PackageSourceConfig   sources;
    bool                  locked { false };
    Option<BuildObserver> observer;
};

struct BuildSummary {
    String       package;
    String       profile;
    PathBuf      output;
    usize        scanned {};
    usize        compiled {};
    usize        reused {};
    Vec<PathBuf> archives;
    Vec<PathBuf> executables;
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

} // namespace tenon
