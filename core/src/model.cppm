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
    Script,
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

struct BuildProfileName {
    String value { String::make("debug"_str) };

    auto as_str() const noexcept -> ref<str> { return value.as_str(); }

    auto clone() const -> BuildProfileName { return BuildProfileName { .value = value.clone() }; }

    auto operator==(const BuildProfileName& other) const noexcept -> bool {
        return value == other.value;
    }
};

enum class BuildProfileFamily
{
    Debug,
    Release,
};

enum class StripMode
{
    None,
    DebugInfo,
    Symbols,
};

struct BuildProfileDefinition {
    BuildProfileName         name;
    Option<BuildProfileName> inherits;
    Option<CppOptimization>  optimization;
    Option<CppDebugInfo>     debug_info;
    Option<StripMode>        strip;
    Option<CppLto>           lto;

    auto clone() const -> BuildProfileDefinition {
        auto result = BuildProfileDefinition {
            .name         = name.clone(),
            .optimization = optimization,
            .debug_info   = debug_info,
            .strip        = strip,
            .lto          = lto,
        };
        if (inherits.is_some()) result.inherits = Some(inherits->clone());
        return result;
    }
};

struct ResolvedBuildProfile {
    BuildProfileName   name;
    BuildProfileFamily family { BuildProfileFamily::Debug };
    CppOptimization    optimization { CppOptimization::None };
    CppDebugInfo       debug_info { CppDebugInfo::Full };
    StripMode          strip { StripMode::None };
    CppLto             lto { CppLto::Off };
    bool               ndebug { false };
};

struct ProjectProfile {
    bool                        exceptions { true };
    bool                        rtti { true };
    Vec<BuildProfileDefinition> build_profiles;

    auto clone() const -> ProjectProfile {
        auto profiles = Vec<BuildProfileDefinition>::with_capacity(build_profiles.len());
        for (const auto& profile : build_profiles) profiles.push(profile.clone());
        return ProjectProfile {
            .exceptions     = exceptions,
            .rtti           = rtti,
            .build_profiles = rstd::move(profiles),
        };
    }
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

struct Architecture {
    String name;

    auto as_str() const noexcept -> ref<str> { return name.as_str(); }

    auto clone() const -> Architecture { return Architecture { .name = name.clone() }; }

    auto operator==(const Architecture& other) const noexcept -> bool { return name == other.name; }
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

struct CMakeArchiveVariant {
    Architecture architecture;
    String       url;
    String       sha256;
};

class CMakeDependencySource {
    RSTD_ENUM(CMakeDependencySource,
              (Installed),
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<CMakeArchiveVariant> variants;)))

public:
    auto clone() const -> CMakeDependencySource {
        if (is_Path()) return CMakeDependencySource::Path(as_Path().path.clone());
        if (is_Git()) {
            return CMakeDependencySource::Git(as_Git().url.clone(),
                                              GitReference {
                                                  .kind  = as_Git().reference.kind,
                                                  .value = as_Git().reference.value.clone(),
                                              });
        }
        if (is_Archive()) {
            return CMakeDependencySource::Archive(as_Archive().url.clone(),
                                                  as_Archive().sha256.clone());
        }
        if (is_ArchitectureArchives()) {
            auto variants =
                Vec<CMakeArchiveVariant>::with_capacity(as_ArchitectureArchives().variants.len());
            for (const auto& variant : as_ArchitectureArchives().variants) {
                variants.push(CMakeArchiveVariant {
                    .architecture = variant.architecture.clone(),
                    .url          = variant.url.clone(),
                    .sha256       = variant.sha256.clone(),
                });
            }
            return CMakeDependencySource::ArchitectureArchives(rstd::move(variants));
        }
        return CMakeDependencySource::Installed();
    }
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
    BenchmarkExecutable,
    CompileTest,
};

enum class PackageTargetKind
{
    Library,
    Binary,
    Test,
    Benchmark,
    TestAttachment,
    CompileTest,
};

auto package_target_kind_name(PackageTargetKind kind) noexcept -> ref<str> {
    switch (kind) {
    case PackageTargetKind::Library: return "lib"_str;
    case PackageTargetKind::Binary: return "bin"_str;
    case PackageTargetKind::Test: return "test"_str;
    case PackageTargetKind::Benchmark: return "bench"_str;
    case PackageTargetKind::TestAttachment: return "test-attachment"_str;
    case PackageTargetKind::CompileTest: return "compile-test"_str;
    }
    return "unknown"_str;
}

struct PackageTargetId {
    String            package;
    PackageTargetKind kind { PackageTargetKind::Library };
    String            name;

    auto clone() const -> PackageTargetId {
        return PackageTargetId {
            .package = package.clone(),
            .kind    = kind,
            .name    = name.clone(),
        };
    }

    auto operator==(const PackageTargetId& other) const noexcept -> bool {
        return package == other.package && kind == other.kind && name == other.name;
    }
};

auto package_target_id_text(const PackageTargetId& id) -> String {
    return rstd::format(
        "{}::{}::{}", id.package.as_str(), package_target_kind_name(id.kind), id.name.as_str());
}

enum class PackageSelectionPurpose
{
    All,
    Production,
    Test,
    Benchmark,
};

enum class ProjectRootRole
{
    PrimaryPackage,
    WorkspaceMember,
    AssociatedTest,
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

struct ToolchainSpec {
    PathBuf compiler;
    PathBuf c_compiler { PathBuf::from("clang"_str) };
    PathBuf linker { PathBuf::from("ld.lld"_str) };
    PathBuf archiver;
    PathBuf formatter;
    PathBuf stripper;

    auto clone() const -> ToolchainSpec {
        return ToolchainSpec {
            .compiler   = compiler.clone(),
            .c_compiler = c_compiler.clone(),
            .linker     = linker.clone(),
            .archiver   = archiver.clone(),
            .formatter  = formatter.clone(),
            .stripper   = stripper.clone(),
        };
    }
};

struct ProcessEnvironmentSpec {
    Vec<PathBuf> append_path;

    auto clone() const -> ProcessEnvironmentSpec {
        return ProcessEnvironmentSpec {
            .append_path = as<rstd::clone::Clone>(append_path).clone(),
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
    PathBuf      executable;
    String       generator;
    String       identity;
    Vec<PathBuf> search_paths;

    auto clone() const -> CMakeProviderConfig {
        return CMakeProviderConfig {
            .executable   = executable.clone(),
            .generator    = generator.clone(),
            .identity     = identity.clone(),
            .search_paths = as<rstd::clone::Clone>(search_paths).clone(),
        };
    }
};

struct ProjectConfig {
    PathBuf                 root;
    ProcessEnvironmentSpec  environment;
    ToolchainSpec           toolchain;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
};

struct ProfileSpec {
    String             name;
    BuildProfileFamily family { BuildProfileFamily::Debug };
    BmiRequest         bmi;
    CppCompileOptions  cpp;
    StripMode          strip { StripMode::None };
    Vec<String>        linker_options;
};

struct DependencySpec {
    PackageTargetId      target;
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

enum class IncludeDirectoryRoot
{
    Package,
    Generated,
};

struct IncludeDirectoryRequirement {
    IncludeDirectoryRoot root { IncludeDirectoryRoot::Package };
    PathBuf              path;

    auto clone() const -> IncludeDirectoryRequirement {
        return IncludeDirectoryRequirement { .root = root, .path = path.clone() };
    }
};

struct UsageRequirements {
    Vec<PathBuf>                     public_include_directories;
    Vec<PathBuf>                     private_include_directories;
    Vec<String>                      public_definitions;
    Vec<String>                      private_definitions;
    Vec<String>                      public_options;
    Vec<String>                      private_options;
    CppArgumentLayer                 public_arguments;
    CppArgumentLayer                 private_arguments;
    Vec<String>                      private_linker_options;
    Vec<IncludeDirectoryRequirement> private_include_directory_requirements;
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
    Architecture architecture;
    String       os;
    TargetFamily family { TargetFamily::Unknown };

    auto clone() const -> TargetInfo {
        return TargetInfo {
            .triple       = triple.clone(),
            .architecture = architecture.clone(),
            .os           = os.clone(),
            .family       = family,
        };
    }

    auto family_name() const noexcept -> ref<str> {
        switch (family) {
        case TargetFamily::Unix: return "unix"_str;
        case TargetFamily::Windows: return "windows"_str;
        case TargetFamily::Unknown: return "unknown"_str;
        }
        return "unknown"_str;
    }
};

struct HostInfo {
    Architecture architecture;
    String       os;

    auto clone() const -> HostInfo {
        return HostInfo {
            .architecture = architecture.clone(),
            .os           = os.clone(),
        };
    }
};

enum class BuildTargetIntent
{
    Native,
    ExplicitTarget,
};

struct BuildPlatform {
    HostInfo          host;
    TargetInfo        compiler_default;
    TargetInfo        effective_target;
    BuildTargetIntent intent { BuildTargetIntent::Native };
    bool              cross { false };
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

class PreparedCMakeDependencySource {
    RSTD_ENUM(PreparedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<CMakeArchiveVariant> variants;)))
};

struct PreparedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    PreparedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    Option<PathBuf>               adapter;
    String                        adapter_identity;
    Option<PathBuf>               config_directory;
    Vec<CMakeCacheEntry>          cache;
    Vec<CMakeTargetRequirement>   targets;
};

class ResolvedCMakeDependencySource {
    RSTD_ENUM(ResolvedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity; bool add_subdirectory; bool cacheable;)),
              (Archive, (String url; String sha256;)))
};

struct ResolvedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    ResolvedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    Option<PathBuf>               adapter;
    String                        adapter_identity;
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
    Vec<ResolvedDependency>                 dev_dependencies;
    Vec<PreparedCMakeDependencyRequirement> cmake_external_dependencies;
};

struct ResolvedProjectRoot {
    String          name;
    String          source_identity;
    ProjectRootRole role { ProjectRootRole::PrimaryPackage };
};

struct ResolvedPackageGraph {
    String                     name;
    Vec<ResolvedProjectRoot>   roots;
    PathBuf                    root_directory;
    PathBuf                    manifest_path;
    bool                       root_is_workspace { false };
    ProjectProfile             profile;
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
    Vec<PackageTargetId> selected_targets;
};

struct ResolvedTargetSources {
    PackageTargetId   target;
    ResolvedSourceSet sources;
};

enum class LockStatus
{
    Unchanged,
    Updated,
};

struct BuildConfiguration {
    ToolchainSpec            toolchain;
    StandardLibrary          standard_library { StandardLibrary::Libstdcxx };
    BmiMode                  bmi_mode { BmiMode::Reduced };
    BmiSourceEmbeddingPolicy bmi_source_embedding { BmiSourceEmbeddingPolicy::ExternalSources };
    String                   language_standard;
    Vec<String>              options;
    Vec<String>              linker_options;

    auto clone() const -> BuildConfiguration {
        return BuildConfiguration {
            .toolchain            = toolchain.clone(),
            .standard_library     = standard_library,
            .bmi_mode             = bmi_mode,
            .bmi_source_embedding = bmi_source_embedding,
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
    Vec<DependencySpec>             dependencies;
    Vec<ResolvedExternalDependency> external_dependencies;
    Option<TestAttachmentTarget>    test_attachment;
};

struct PackageMetadata {
    String               name;
    PathBuf              root;
    PathBuf              manifest_path;
    Vec<String>          build_script_packages;
    String               default_profile;
    Vec<PackageTargetId> default_targets;
    ToolchainSpec        toolchain;
    Vec<ProfileSpec>     profiles;
    Vec<ResolvedTarget>  targets;
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
    Vec<PackageTargetId>       target_identities;
    Vec<TargetId>              target_order;
    Vec<CompileContext>        contexts;
    Vec<Vec<TargetId>>         visible_targets;
    Vec<Vec<PlannedLinkInput>> link_inputs;
    Vec<Vec<String>>           linker_options;
};

struct SourceTargetSelection {
    usize         profile {};
    Vec<TargetId> selected_targets;
    Vec<TargetId> target_order;
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
    Strip,
    Configure,
    ConfigureReuse,
    CMakeConfigure,
    CMakeBuild,
    CMakeInstall,
    CMakeQuery,
    CMakeQueryBuild,
    CMakeSnapshot,
    CMakeReuse,
};

struct BuildEvent {
    BuildEventKind        kind { BuildEventKind::Scan };
    ref<str>              target;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct BuildObserver {
    void* context {};
    void (*notify)(void*, const BuildEvent&) noexcept {};
};

struct ScanExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct CompileExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct BuildExecutionPolicy {
    ScanExecutionPolicy    scan;
    CompileExecutionPolicy compile;
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
    PackageSelection         selection;
    Vec<String>              targets;
    PathBuf                  output;
    ProcessEnvironmentSpec   environment;
    BuildConfiguration       configuration;
    Option<BuildProfileName> profile;
    PackageSourceConfig      sources;
    PkgConfigProviderConfig  pkg_config;
    CMakeProviderConfig      cmake;
    PackageSelectionPurpose  purpose { PackageSelectionPurpose::Production };
    bool                     locked { false };
    BuildExecutionPolicy     execution;
    Option<BuildObserver>    observer;
};

struct BuiltArtifact {
    PackageTargetId target;
    ArtifactKind    kind { ArtifactKind::StaticLibrary };
    PathBuf         path;
    PathBuf         package_root;
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

struct ConfiguredFile {
    PathBuf                output;
    rstd::fs::WriteOutcome write { rstd::fs::WriteOutcome::Unchanged };
};

struct BuildScriptReport {
    bool                 executed { false };
    rstd::time::Duration elapsed;
    usize                created {};
    usize                replaced {};
    usize                unchanged {};
    usize                stale_removed {};
    Vec<ConfiguredFile>  files;
};

struct BuildSummary {
    String                          package;
    String                          profile;
    PathBuf                         output;
    usize                           scanned {};
    usize                           compiled {};
    usize                           reused {};
    Vec<BuiltArtifact>              artifacts;
    frontend::FrontendStatistics    frontend;
    ToolchainStatistics             toolchain;
    ScanProfileReport               scan_profile;
    CompileExecutionStatistics      compile_execution;
    ExternalPreparationTimingReport external_preparation;
    BuildTimingReport               build_timing;
    Vec<CompileTestExecution>       compile_tests;
    BuildScriptReport               script;
};

struct ScanRequest {
    PackageSelection         selection;
    Vec<String>              targets;
    PathBuf                  source;
    ProcessEnvironmentSpec   environment;
    BuildConfiguration       configuration;
    Option<BuildProfileName> profile;
    PackageSourceConfig      sources;
    PkgConfigProviderConfig  pkg_config;
    CMakeProviderConfig      cmake;
    bool                     locked { false };
    Option<BuildObserver>    observer;
};

struct ScanReport {
    String         target;
    String         profile;
    PathBuf        primary_output;
    FrontendResult result;
};

struct FormatRequest {
    PackageSelection       selection;
    ProcessEnvironmentSpec environment;
    ToolchainSpec          toolchain;
    PackageSourceConfig    sources;
};

struct UpdateRequest {
    PathBuf                root;
    ProcessEnvironmentSpec environment;
    PackageSourceConfig    sources;
};

struct FormatSummary {
    usize packages {};
    usize files {};
};

} // namespace lito
