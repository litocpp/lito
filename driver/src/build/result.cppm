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
    CompletedBuildProduct           product;
    lito::system::BuildPlatform     platform;
    String                          language_standard;
    usize                           scanned {};
    usize                           compiled {};
    usize                           reused {};
    frontend::FrontendStatistics    frontend;
    ToolchainStatistics             toolchain;
    ScanProfileReport               scan_profile;
    CompileExecutionStatistics      compile_execution;
    ExternalPreparationTimingReport external_preparation;
    BuildTimingReport               build_timing;
    Vec<CompileTestExecution>       compile_tests;
    BuildScriptReport               script;
    CompilerIdentity                compiler;
    Vec<DocumentationBuildUnit>     documentation_units;
};

} // namespace lito
