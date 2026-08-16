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

class BuildCommand : public ProjectFixture {};

auto build_command_tree() -> lito::SourceTreeResult<lito::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"build([workspace]
name = "build-command"
members = ["test-lib", "test-app"]

[workspace.package]
version = "0.1.0"

[profile]
exceptions = false
rtti = false
)build"_str },
        { "test-lib/lito.toml"_str, R"build([package]
name = "fixture-test-lib"
version = { workspace = true }

[lib]
name = "fixture-test-lib"
module = "fixture.test.lib"
archive = "fixture_test_lib"
sources = ["src/lib.cppm"]
)build"_str },
        { "test-lib/src/lib.cppm"_str, R"build(export module fixture.test.lib;

export namespace fixture::test
{

constexpr auto answer() noexcept -> int {
    return 42;
}

} // namespace fixture::test
)build"_str },
        { "test-app/lito.toml"_str, R"build([package]
name = "fixture-test-app"
version = { workspace = true }

[[bin]]
link-stdlib = false
name = "fixture-test-app"
sources = ["src/main.cpp"]
)build"_str },
        { "test-app/src/main.cpp"_str, R"build(int main() {
    return 0;
}
)build"_str },
    };
    return source_tree(files);
}

TEST_F(BuildCommand, BuildSelectsProductionArtifacts) {
    auto tree = build_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("build"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root    = project->root.clone();
    auto output  = build_root("build"_str);
    auto request = build_request(
        root.as_path(), output.as_path(), strings("fixture-test-lib"_str, "fixture-test-app"_str));
    auto summary = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::TestExecutable), usize {});
    EXPECT_FALSE(summary->documentation_units.is_empty());
    for (const auto& unit : summary->documentation_units) {
        EXPECT_FALSE(unit.invocation.arguments.is_empty());
        EXPECT_FALSE(unit.invocation.identity.is_empty());
        auto selected = false;
        for (const auto& target : summary->selected_targets) {
            if (target == unit.target) selected = true;
        }
        EXPECT_TRUE(selected);
    }
}

TEST_F(BuildCommand, DocumentationSelectsOnlyLibraryArtifacts) {
    auto tree = build_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("build-doc"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root    = project->root.clone();
    auto output  = build_root("build-doc"_str);
    auto request = build_request(root.as_path(), output.as_path(), strings("fixture-test-lib"_str));
    request.purpose = lito::PackageSelectionPurpose::Documentation;
    auto summary    = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::Executable), usize {});
    ASSERT_FALSE(summary->documentation_units.is_empty());
    for (const auto& unit : summary->documentation_units) {
        EXPECT_EQ(unit.target.kind, lito::PackageTargetKind::Library);
    }
}
