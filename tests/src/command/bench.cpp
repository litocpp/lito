#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.manifest;
import lito.source;
import lito.test.support;
import lito.workspace.contract;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

TEST(BenchCommand, PackageTargetsSelectTypedArtifactsAndRunBenchmarks) {
    auto root   = project_root();
    auto output = output_root("multi-target"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::Executable), usize(2));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::TestExecutable), usize {});
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::BenchmarkExecutable), usize {});

    auto consumed = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-consumer"_str)));
    ASSERT_TRUE(consumed.is_ok());
    EXPECT_EQ(artifact_count(*consumed, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*consumed, lito::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*consumed, lito::ArtifactKind::BenchmarkExecutable), usize {});

    auto ambiguous_request =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    ambiguous_request.targets = strings("shared"_str);
    auto ambiguous            = lito::build(rstd::move(ambiguous_request));
    ASSERT_TRUE(ambiguous.is_err());
    auto ambiguous_error = rstd::move(ambiguous).unwrap_err();
    ASSERT_TRUE(ambiguous_error.is_Package());
    ASSERT_TRUE(ambiguous_error.as_Package().source.is_Message());
    EXPECT_TRUE(ambiguous_error.as_Package().source.as_Message().message.as_str().contains(
        "ambiguous"_str));

    auto typed_request =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    typed_request.targets = strings("bin:shared"_str);
    auto typed            = lito::build(rstd::move(typed_request));
    ASSERT_TRUE(typed.is_ok());
    EXPECT_EQ(artifact_count(*typed, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*typed, lito::ArtifactKind::Executable), usize(1));

    auto test_request = lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str)),
        .no_run = true,
    };
    auto tested = lito::test(rstd::move(test_request));
    ASSERT_TRUE(tested.is_ok());
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::TestExecutable), usize(2));
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::BenchmarkExecutable), usize {});

    auto bench_build =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    bench_build.profile = None();
    auto benchmarked    = lito::bench(lito::BenchRequest {
        .build     = rstd::move(bench_build),
        .arguments = strings("expected-benchmark"_str),
    });
    ASSERT_TRUE(benchmarked.is_ok());
    EXPECT_TRUE(benchmarked->success());
    EXPECT_EQ(benchmarked->build.profile.as_str(), "release"_str);
    EXPECT_EQ(artifact_count(benchmarked->build, lito::ArtifactKind::BenchmarkExecutable),
              usize(2));
    ASSERT_EQ(benchmarked->executions.len(), usize(2));
    EXPECT_EQ(benchmarked->executions[usize {}].target.name.as_str(), "shared"_str);
    EXPECT_EQ(benchmarked->executions[usize(1)].target.name.as_str(), "speed"_str);
    auto package_root = root.join(PathBuf::from("multi-target"_str).as_path());
    for (const auto& execution : benchmarked->executions) {
        EXPECT_EQ(execution.working_directory.as_path(), package_root.as_path());
    }

    auto debug_build =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    auto debug_bench = lito::bench(lito::BenchRequest {
        .build  = rstd::move(debug_build),
        .no_run = true,
    });
    ASSERT_TRUE(debug_bench.is_ok());
    EXPECT_EQ(debug_bench->build.profile.as_str(), "debug"_str);
    EXPECT_TRUE(debug_bench->executions.is_empty());

    ASSERT_TRUE(clear_output(output.as_path()));
}
