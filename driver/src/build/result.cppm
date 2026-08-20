export module lito.driver:build.result;

import rstd;
import lito.core;
import lito.frontend;
import :build.artifact;
import :build.documentation;
import :build.profiling;
import :dependency.preparation;
import lito.cpp;
import lito.toolchain.common;

using namespace rstd::prelude;

export namespace lito
{

struct BuildSummary {
    String                              package;
    String                              profile;
    String                              target;
    lito::system::BuildPlatform         platform;
    String                              language_standard;
    PathBuf                             output;
    usize                               scanned {};
    usize                               compiled {};
    usize                               reused {};
    Vec<BuiltArtifact>                  artifacts;
    Vec<BuiltRuntimeResource>           runtime_resources;
    Vec<BuiltTargetRuntime>             target_runtimes;
    Vec<lito::package::PackageTargetId> selected_targets;
    Vec<cpp::SelectedPackageMetadata>   selected_packages;
    frontend::FrontendStatistics        frontend;
    ToolchainStatistics                 toolchain;
    ScanProfileReport                   scan_profile;
    CompileExecutionStatistics          compile_execution;
    ExternalPreparationTimingReport     external_preparation;
    BuildTimingReport                   build_timing;
    Vec<CompileTestExecution>           compile_tests;
    BuildScriptReport                   script;
    ExternalAssetCatalog                external_assets;
    Vec<ExternalSourceProvenance>       external_source_provenance;
    CompilerIdentity                    compiler;
    Vec<DocumentationBuildUnit>         documentation_units;
};

} // namespace lito
