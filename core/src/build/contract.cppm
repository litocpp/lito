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

using namespace rstd::prelude;

export namespace lito
{

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

} // namespace lito
