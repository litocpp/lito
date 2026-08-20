#include <rstd/test/gtest.hpp>

import rstd;
import lito.tools;
import rstd.test;
import lito.driver;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

class BuildEnvironment : public ProjectFixture {};

TEST_F(BuildEnvironment, EnvironmentIsSharedWithinBuild) {
    const ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-environment-cache"
version = "0.1.0"

[lib]
name = "fixture-environment-cache"
module = "fixture.environment"
archive = "fixture_environment_cache"
sources = ["one.cpp", "two.cpp"]
)toml"_str,
        },
        { "shared.hpp"_str, "inline constexpr int fixture_environment_shared = 0;\n"_str },
        {
            "one.cpp"_str,
            R"cpp(#include "shared.hpp"

extern "C" auto fixture_environment_one() -> int {
    return 1 + fixture_environment_shared;
}
)cpp"_str,
        },
        {
            "two.cpp"_str,
            R"cpp(#include "shared.hpp"

extern "C" auto fixture_environment_two() -> int {
    return 2 + fixture_environment_shared;
}
)cpp"_str,
        },
    };
    auto project = materialize("environment"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto root   = project->root.clone();
    auto output = build_root("environment"_str);
    auto request =
        build_request(root.as_path(), output.as_path(), strings("fixture-environment-cache"_str));
    request.configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-DFIXTURE_CPP=1"_str),
        .source    = String::make("config.build.options"_str),
    });
    request.configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("-DFIXTURE_C=1"_str),
        .source    = String::make("CFLAGS"_str),
    });
    request.configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-Wl,--as-needed"_str),
        .source    = String::make("config.build.linker-options"_str),
    });
    request.execution.scan.jobs    = Some(usize(2));
    request.execution.compile.jobs = Some(usize(2));
    auto progress                  = CompileProgressCapture {};
    request.observer               = Some(lito::BuildEventSink {
        .context = rstd::addressof(progress),
        .notify  = capture_compile_progress,
    });
    request.setup_reporter         = Some(lito::BuildSetupReportSink {
        .context = rstd::addressof(progress),
        .notify  = capture_build_setup,
    });
    request.tool_reporter          = Some(lito::tools::HostToolResolutionSink {
        .context = rstd::addressof(progress),
        .notify  = capture_host_tool,
    });
    auto summary                   = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    ASSERT_EQ(progress.requested_tools.len(), usize(4));
    ASSERT_EQ(progress.resolved_tools.len(), usize(4));
    EXPECT_TRUE(progress.host_tools.is_empty());
    for (const auto& path : progress.requested_tools) EXPECT_FALSE(path.is_empty());
    for (const auto& path : progress.resolved_tools) EXPECT_TRUE(path.as_path().is_absolute());
    ASSERT_EQ(progress.option_domains.len(), usize(3));
    EXPECT_EQ(progress.option_domains[usize {}], lito::BuildOptionReportDomain::Cpp);
    EXPECT_EQ(progress.option_domains[usize(1)], lito::BuildOptionReportDomain::C);
    EXPECT_EQ(progress.option_domains[usize(2)], lito::BuildOptionReportDomain::Link);
    EXPECT_EQ(progress.option_sources[usize {}].as_str(), "config.build.options"_str);
    EXPECT_EQ(progress.option_sources[usize(1)].as_str(), "CFLAGS"_str);
    EXPECT_EQ(progress.option_sources[usize(2)].as_str(), "config.build.linker-options"_str);
    EXPECT_EQ(progress.profile.as_str(), "debug"_str);
    ASSERT_EQ(progress.profile_values.len(), usize(11));
    EXPECT_FALSE(progress.missing);
    ASSERT_EQ(progress.values.len(), usize(2));
    EXPECT_EQ(progress.values[usize {}].current, usize(1));
    EXPECT_EQ(progress.values[usize(1)].current, usize(2));
    EXPECT_EQ(progress.values[usize {}].total, usize(2));
    EXPECT_EQ(progress.values[usize(1)].total, usize(2));
    EXPECT_EQ(summary->toolchain.preprocessor_environment_entries, usize(1));
    EXPECT_EQ(summary->toolchain.preprocessor_environment_queries, usize(1));
    EXPECT_GE(summary->toolchain.preprocessor_environment_hits, usize(1));
    EXPECT_EQ(summary->scan_profile.execution().jobs, usize(2));
    EXPECT_EQ(summary->scan_profile.execution().tasks, usize(2));
    EXPECT_EQ(summary->scan_profile.execution().max_active, usize(2));
    EXPECT_FALSE(summary->scan_profile.execution().task_work.is_zero());
    EXPECT_FALSE(summary->scan_profile.execution().completion_wait.is_zero());
    EXPECT_EQ(summary->compile_execution.jobs, usize(2));
    EXPECT_EQ(summary->compile_execution.tasks, usize(2));
    EXPECT_EQ(summary->compile_execution.max_active, usize(2));
    EXPECT_FALSE(summary->compile_execution.task_work.is_zero());
    EXPECT_FALSE(summary->compile_execution.wall.is_zero());
    EXPECT_EQ(summary->frontend.source_requests, usize(4));
    EXPECT_EQ(summary->frontend.source_reads, usize(3));
    EXPECT_EQ(summary->frontend.lex_builds, usize(3));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_requests, usize(4));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_hits, usize(1));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_builds, usize(3));
    auto report  = output.join(PathBuf::from("timing.txt"_str).as_path());
    auto emitted = lito::timing_output::emit(*summary,
                                             lito::timing_output::OutputOptions {
                                                 .file = Some(report.clone()),
                                             });
    ASSERT_TRUE(emitted.is_ok());
    auto contents = rstd::fs::read_to_string(report.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_TRUE(contents->as_str().contains("frontend"_str));
    EXPECT_TRUE(contents->as_str().contains("compile execution"_str));
    EXPECT_TRUE(contents->as_str().contains("build.compile"_str));
    EXPECT_TRUE(contents->as_str().contains("aggregate timing"_str));
}
