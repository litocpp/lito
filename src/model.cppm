export module tenon.model;

import rstd;

export namespace tenon
{

using String  = rstd::string::String;
using PathBuf = rstd::path::PathBuf;

template<typename T>
using Vec = rstd::vec::Vec<T>;

using TargetId = rstd::usize;
using UnitId   = rstd::usize;

enum class ErrorKind
{
    InvalidRequest,
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

    static auto make(ErrorKind kind, rstd::ref<rstd::str> message) -> Error {
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

enum class DependencyVisibility
{
    Public,
    Private,
    Runtime,
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

struct ToolchainSpec {
    PathBuf compiler;
    PathBuf scanner;
    PathBuf archiver;
};

struct ProfileSpec {
    String          name;
    StandardLibrary standard_library { StandardLibrary::Libstdcxx };
    BmiMode         bmi_mode { BmiMode::Reduced };
    String          language_standard;
    Vec<String>     options;
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
};

struct DeclaredDependency {
    String               alias;
    PathBuf              manifest;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct PackageManifest {
    rstd::u64             manifest_version { 1 };
    String                name;
    String                version;
    rstd::Option<String>  root_module;
    PathBuf               root;
    PathBuf               manifest_path;
    ArtifactKind          artifact_kind { ArtifactKind::StaticLibrary };
    String                artifact_name;
    SourceDiscoveryMode   discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>          declared_sources;
    UsageRequirements     usage;
    Vec<DeclaredDependency> dependencies;
};

struct ResolvedSource {
    PathBuf              relative_path;
    PathBuf              canonical_path;
    SourceOrigin         origin { SourceOrigin::Explicit };
    rstd::Option<String> expected_module;
};

struct ResolvedSourceSet {
    Vec<ResolvedSource> sources;
};

struct ResolvedDependency {
    String               alias;
    String               package_id;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct ResolvedPackage {
    String                  id;
    PathBuf                 source_directory;
    String                  source_manifest;
    PackageManifest         manifest;
    Vec<ResolvedDependency> dependencies;
};

struct ResolvedPackageGraph {
    String               root_id;
    PathBuf              root_directory;
    Vec<ResolvedPackage> packages;
};

enum class LockStatus
{
    Unchanged,
    Updated,
};

struct BuildConfiguration {
    String          profile_name;
    ToolchainSpec   toolchain;
    StandardLibrary standard_library { StandardLibrary::Libstdcxx };
    BmiMode         bmi_mode { BmiMode::Reduced };
    String          language_standard;
    Vec<String>     options;
};

struct ModuleExpectation {
    PathBuf source;
    String  logical_name;
};

struct TargetSpec {
    String                  name;
    ArtifactKind            artifact_kind { ArtifactKind::StaticLibrary };
    String                  artifact_name;
    rstd::Option<String>    module_affiliation;
    PathBuf                 root;
    Vec<PathBuf>            sources;
    Vec<ModuleExpectation>  module_expectations;
    Vec<DependencySpec>     dependencies;
    UsageRequirements       usage;
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

struct PackagePlan {
    const PackageSpec*      package {};
    const ProfileSpec*      profile {};
    Vec<TargetId>           target_order;
    Vec<CompileContext>     contexts;
    Vec<Vec<TargetId>>      visible_targets;
    Vec<Vec<TargetId>>      link_dependencies;
};

struct UnitSpec {
    UnitId                  id {};
    TargetId                target {};
    PathBuf                 source;
    PathBuf                 object;
    PathBuf                 depfile;
    PathBuf                 fingerprint;
    rstd::Option<PathBuf>   bmi;
    const CompileContext*   context {};
};

struct PreparedUnit {
    UnitSpec unit;
    PathBuf  working_directory;
};

struct ProvidedModule {
    String logical_name;
    bool   is_interface { false };
};

struct ScanResult {
    UnitId                       unit {};
    rstd::Option<ProvidedModule> provided;
    Vec<String>                  required_modules;
    Vec<PathBuf>                 header_inputs;
};

struct ResolvedModuleArtifact {
    String  logical_name;
    UnitId  provider {};
    PathBuf bmi;
};

struct ModulePlan {
    Vec<UnitId>                             compile_order;
    Vec<Vec<ResolvedModuleArtifact>>        direct_inputs;
    Vec<Vec<ResolvedModuleArtifact>>        transitive_inputs;
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
    BuildEventKind kind { BuildEventKind::Scan };
    rstd::ref<rstd::str> target;
    rstd::ref<rstd::path::Path> path;
};

struct BuildObserver {
    void* context {};
    void (*notify)(void*, const BuildEvent&) noexcept {};
};

struct BuildRequest {
    PathBuf                    manifest;
    String                     profile;
    Vec<String>                targets;
    PathBuf                    output;
    BuildConfiguration         configuration;
    bool                       locked { false };
    rstd::Option<BuildObserver> observer;
};

struct BuildSummary {
    String       package;
    String       profile;
    PathBuf      output;
    rstd::usize  scanned {};
    rstd::usize  compiled {};
    rstd::usize  reused {};
    Vec<PathBuf> archives;
    Vec<PathBuf> executables;
};

} // namespace tenon
