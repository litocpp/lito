#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

TEST(BuildEnvironment, EnvironmentIsSharedWithinBuild) {
    auto root   = project_root();
    auto output = output_root("environment"_str);
    clear_output(output.as_path());
    auto request =
        build_request(root.as_path(), output.as_path(), strings("fixture-environment-cache"_str));
    request.execution.scan.jobs    = Some(usize(2));
    request.execution.compile.jobs = Some(usize(2));
    auto progress                  = CompileProgressCapture {};
    request.observer               = Some(lito::BuildEventSink {
        .context = rstd::addressof(progress),
        .notify  = capture_compile_progress,
    });
    auto summary                   = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    ASSERT_EQ(progress.toolchain_names.len(), usize(4));
    EXPECT_EQ(progress.toolchain_names[usize {}].as_str(), "cc"_str);
    EXPECT_EQ(progress.toolchain_names[usize(1)].as_str(), "cxx"_str);
    EXPECT_EQ(progress.toolchain_names[usize(2)].as_str(), "ld"_str);
    EXPECT_EQ(progress.toolchain_names[usize(3)].as_str(), "ar"_str);
    for (const auto& path : progress.toolchain_paths) EXPECT_TRUE(path.as_path().is_absolute());
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
    clear_output(output.as_path());
}
