export module lito.driver:build.result;

import rstd;
import lito.core;
import lito.frontend;
import :build.artifact;
import :build.product;
import :build.documentation;
import :build.profiling;
import :dependency.preparation;
import lito.cpp;
import lito.toolchain.common;

using namespace rstd::prelude;

export namespace lito
{

struct BuildSummary {
    CompletedBuildProduct               product;
    String                              package;
    Vec<lito::package::PackageTargetId> selected_targets;
    Vec<cpp::SelectedPackageMetadata>   selected_packages;
    Vec<BuiltRuntimeResource>           runtime_resources;
    Vec<ExternalSourceProvenance>       external_source_provenance;
    lito::system::BuildPlatform         platform;
    String                              language_standard;
    usize                               scanned {};
    usize                               compiled {};
    usize                               reused {};
    frontend::FrontendStatistics        frontend;
    cpp::SemanticScanGraphStatistics    scan_graph;
    ToolchainStatistics                 toolchain;
    ScanProfileReport                   scan_profile;
    CompileExecutionStatistics          compile_execution;
    BuildStageTimingReport              stage_timing;
    ExternalPreparationTimingReport     external_preparation;
    BuildTimingReport                   build_timing;
    Vec<CompileTestExecution>           compile_tests;
    BuildScriptReport                   script;
    CompilerIdentity                    compiler;
    Vec<DocumentationBuildUnit>         documentation_units;
    Vec<BuiltCompilerPlugin>            compiler_plugins;
    Vec<BuiltProcMacroProvider>         proc_macro_providers;
    Vec<BuiltProcMacroAggregate>        proc_macro_aggregates;
};

} // namespace lito
