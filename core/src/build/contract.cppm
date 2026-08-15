export module lito.build.contract;

import rstd;
import lito.error;
import lito.frontend;
import lito.build.profiling;
import lito.build.profile_contract;
import lito.build.configuration;
import lito.toolchain.contract;
import lito.system.environment_contract;
import lito.source.contract;
import lito.dependency.contract;
import lito.manifest.contract;
import lito.package.identity;
import lito.package.target_contract;
import lito.workspace.contract;
import lito.lock.contract;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class BuildEventKind
{
    Toolchain,
    Fetch,
    Scan,
    ScanReuse,
    Compile,
    Reuse,
    Archive,
    Link,
    Strip,
    Configure,
    ConfigureReuse,
    BuildToolFetch,
    BuildToolReuse,
    BuildToolRun,
    BuildToolRunReuse,
    GeneratedResource,
    GeneratedResourceReuse,
    CMakeConfigure,
    CMakeBuild,
    CMakeInstall,
    CMakeQuery,
    CMakeQueryBuild,
    CMakeSnapshot,
    CMakeReuse,
};

struct BuildProgress {
    usize current {};
    usize total {};
};

struct BuildEvent {
    BuildEventKind        kind { BuildEventKind::Scan };
    ref<str>              target;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
    Option<BuildProgress> progress;
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

struct BuildRequest {
    PackageSelection         selection;
    Vec<String>              targets;
    Vec<PackageTargetId>     exact_targets;
    PathBuf                  output;
    ProcessEnvironmentSpec   environment;
    BuildConfiguration       configuration;
    LockConfig               lock;
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

struct BuiltRuntimeResource {
    PackageTargetId target;
    String          name;
    PathBuf         root;
    String          identity;
    Vec<PathBuf>    files;
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

enum class DocumentationUnitKind
{
    TranslationUnit,
    ModuleInterface,
    ModulePartition,
    ModuleImplementation,
};

struct DocumentationBmiDependency {
    String  logical_name;
    String  artifact_identity;
    PathBuf path;
};

struct DocumentationBuildUnit {
    PackageTargetId                 target;
    PathBuf                         package_root;
    PathBuf                         source;
    PathBuf                         relative_source;
    DocumentationUnitKind           kind { DocumentationUnitKind::TranslationUnit };
    bool                            is_interface { false };
    Option<String>                  logical_module;
    String                          source_identity;
    CompileInvocation               invocation;
    Vec<DocumentationBmiDependency> bmi_dependencies;
};

struct BuildSummary {
    String                          package;
    String                          profile;
    String                          target;
    String                          language_standard;
    PathBuf                         output;
    usize                           scanned {};
    usize                           compiled {};
    usize                           reused {};
    Vec<BuiltArtifact>              artifacts;
    Vec<BuiltRuntimeResource>       runtime_resources;
    Vec<PackageTargetId>            selected_targets;
    Vec<SelectedPackageMetadata>    selected_packages;
    frontend::FrontendStatistics    frontend;
    ToolchainStatistics             toolchain;
    ScanProfileReport               scan_profile;
    CompileExecutionStatistics      compile_execution;
    ExternalPreparationTimingReport external_preparation;
    BuildTimingReport               build_timing;
    Vec<CompileTestExecution>       compile_tests;
    BuildScriptReport               script;
    ExternalAssetCatalog            external_assets;
    CompilerIdentity                compiler;
    Vec<DocumentationBuildUnit>     documentation_units;
};

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::DocumentationUnitKind> : ImplBase<lito::DocumentationUnitKind> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        switch (this->self()) {
        case lito::DocumentationUnitKind::TranslationUnit:
            return formatter.write_str("translation-unit"_str);
        case lito::DocumentationUnitKind::ModuleInterface:
            return formatter.write_str("module-interface"_str);
        case lito::DocumentationUnitKind::ModulePartition:
            return formatter.write_str("module-partition"_str);
        case lito::DocumentationUnitKind::ModuleImplementation:
            return formatter.write_str("module-implementation"_str);
        }
        __builtin_unreachable();
    }
};

template<>
struct Impl<fmt::Display, lito::BuildEventKind> : ImplBase<lito::BuildEventKind> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::BuildEventKind::Toolchain: name = "toolchain"_str; break;
        case lito::BuildEventKind::Fetch: name = "fetch"_str; break;
        case lito::BuildEventKind::Scan: name = "scan"_str; break;
        case lito::BuildEventKind::ScanReuse: name = "scan-reuse"_str; break;
        case lito::BuildEventKind::Compile: name = "compile"_str; break;
        case lito::BuildEventKind::Reuse: name = "reuse"_str; break;
        case lito::BuildEventKind::Archive: name = "archive"_str; break;
        case lito::BuildEventKind::Link: name = "link"_str; break;
        case lito::BuildEventKind::Strip: name = "strip"_str; break;
        case lito::BuildEventKind::Configure: name = "configure"_str; break;
        case lito::BuildEventKind::ConfigureReuse: name = "configure-reuse"_str; break;
        case lito::BuildEventKind::BuildToolFetch: name = "build-tool-fetch"_str; break;
        case lito::BuildEventKind::BuildToolReuse: name = "build-tool-reuse"_str; break;
        case lito::BuildEventKind::BuildToolRun: name = "build-tool-run"_str; break;
        case lito::BuildEventKind::BuildToolRunReuse: name = "build-tool-run-reuse"_str; break;
        case lito::BuildEventKind::GeneratedResource: name = "generated-resource"_str; break;
        case lito::BuildEventKind::GeneratedResourceReuse:
            name = "generated-resource-reuse"_str;
            break;
        case lito::BuildEventKind::CMakeConfigure: name = "cmake-configure"_str; break;
        case lito::BuildEventKind::CMakeBuild: name = "cmake-build"_str; break;
        case lito::BuildEventKind::CMakeInstall: name = "cmake-install"_str; break;
        case lito::BuildEventKind::CMakeQuery: name = "cmake-query"_str; break;
        case lito::BuildEventKind::CMakeQueryBuild: name = "cmake-query-build"_str; break;
        case lito::BuildEventKind::CMakeSnapshot: name = "cmake-snapshot"_str; break;
        case lito::BuildEventKind::CMakeReuse: name = "cmake-reuse"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
