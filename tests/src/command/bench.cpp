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

class BenchCommand : public ProjectFixture {};

auto bench_command_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"bench([workspace]
name = "bench-command"
members = ["multi-target", "multi-consumer"]

[workspace.package]
version = "0.1.0"

[profile]
exceptions = false
rtti = false
)bench"_str },
        { "multi-target/lito.toml"_str, R"bench([package]
name = "fixture-multi-target"
version = { workspace = true }

[lib]
name = "shared"
module = "fixture.multi"
archive = "fixture_multi"
sources = ["src/lib.cppm"]

[[bin]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]

[[bin]]
name = "tool"
sources = ["src/main.cpp"]
link-stdlib = false

[[test]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]

[[test]]
name = "unit"
sources = ["src/main.cpp"]
link-stdlib = false

[[bench]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]

[[bench]]
name = "speed"
sources = ["src/main.cpp"]
link-stdlib = false
)bench"_str },
        { "multi-target/marker.txt"_str, R"bench(benchmark working directory marker
)bench"_str },
        { "multi-target/src/lib.cppm"_str, R"bench(export module fixture.multi;

export namespace fixture::multi
{

auto answer() -> int {
    return 42;
}

} // namespace fixture::multi
)bench"_str },
        { "multi-target/src/main.cpp"_str, R"bench(#include <cstdio>
#include <cstring>

import fixture.multi;

int main(int argc, char** argv) {
    if (fixture::multi::answer() != 42) return 1;
    if (argc != 2 || std::strcmp(argv[1], "expected-benchmark") != 0) return 2;
    auto* marker = std::fopen("marker.txt", "r");
    if (marker == nullptr) return 3;
    std::fclose(marker);
    return 0;
}
)bench"_str },
        { "multi-consumer/lito.toml"_str, R"bench([package]
name = "fixture-multi-consumer"
version = { workspace = true }

[[bin]]
link-stdlib = false
name = "fixture-multi-consumer"
sources = ["src/main.cpp"]

[dependencies.fixture-multi-target]
path = "../multi-target"
visibility = "private"
)bench"_str },
        { "multi-consumer/src/main.cpp"_str, R"bench(import fixture.multi;

int main() {
    return fixture::multi::answer() == 42 ? 0 : 1;
}
)bench"_str },
    };
    return source_tree(files);
}

TEST_F(BenchCommand, PackageTargetsSelectTypedArtifactsAndRunBenchmarks) {
    auto tree = bench_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("multi-target"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root   = project->root.clone();
    auto output = build_root("multi-target"_str);

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::Executable), usize(2));
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::TestExecutable), usize {});
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::BenchmarkExecutable), usize {});

    auto consumed = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-consumer"_str)));
    ASSERT_TRUE(consumed.is_ok());
    EXPECT_EQ(artifact_count(*consumed, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*consumed, lito::cpp::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*consumed, lito::cpp::ArtifactKind::BenchmarkExecutable), usize {});

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
    EXPECT_EQ(artifact_count(*typed, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*typed, lito::cpp::ArtifactKind::Executable), usize(1));

    auto test_request = lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str)),
        .no_run = true,
    };
    auto tested = lito::test(rstd::move(test_request));
    ASSERT_TRUE(tested.is_ok());
    EXPECT_EQ(artifact_count(tested->build, lito::cpp::ArtifactKind::TestExecutable), usize(2));
    EXPECT_EQ(artifact_count(tested->build, lito::cpp::ArtifactKind::BenchmarkExecutable),
              usize {});

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
    EXPECT_EQ(artifact_count(benchmarked->build, lito::cpp::ArtifactKind::BenchmarkExecutable),
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
}
