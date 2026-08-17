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

auto build_command_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
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
    request.purpose = lito::package::PackageSelectionPurpose::Documentation;
    auto summary    = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::Executable), usize {});
    ASSERT_FALSE(summary->documentation_units.is_empty());
    for (const auto& unit : summary->documentation_units) {
        EXPECT_EQ(unit.target.kind, lito::package::PackageTargetKind::Library);
    }
}

TEST_F(BuildCommand, FeatureChangesInvalidateDiscoveryAndCompileCaches) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-feature-build"
version = "0.1.0"

[lib]
name = "fixture-feature-build"
module = "fixture.feature"
archive = "fixture_feature"

[features.optional]
default = false

[[when]]
condition = "feature.optional"

[when.usage]
private-definitions = ["FIXTURE_FEATURE_CONDITION=1"]
)toml"_str },
        { "src/lib.cppm"_str, R"cpp(export module fixture.feature;

#if FIXTURE_FEATURE_BUILD_FEATURE_OPTIONAL
export import :optional;
#endif
)cpp"_str },
        { "src/optional.cppm"_str, R"cpp(module;

#if FIXTURE_FEATURE_BUILD_FEATURE_OPTIONAL
#ifndef FIXTURE_FEATURE_CONDITION
#error feature condition did not contribute to the scan context
#endif

export module fixture.feature:optional;
#endif
)cpp"_str },
    };
    auto project = materialize("feature-build"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output = build_root("feature-build"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-feature-build"_str));

    auto disabled = lito::build(request);
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_EQ(disabled->compiled, usize(1));

    request.selection.features.enabled.push(String::make("optional"_str));
    auto enabled = lito::build(request);
    if (enabled.is_err()) {
        auto message = error_chain_text(enabled.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(enabled->compiled, usize(2));
    EXPECT_EQ(enabled->scanned, usize(2));

    request.selection.features.enabled.clear();
    auto disabled_again = lito::build(request);
    if (disabled_again.is_err()) {
        auto message = error_chain_text(disabled_again.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(disabled_again->compiled, usize(1));
}

TEST_F(BuildCommand, ConditionalConflictsReportBothSources) {
    const ProjectFile definition_files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-condition-definition-conflict"
version = "0.1.0"

[lib]
name = "fixture-condition-definition-conflict"
module = "fixture.condition.definition_conflict"
archive = "fixture_condition_definition_conflict"

[[when]]
condition = "true"

[when.usage]
private-definitions = ["FIXTURE_CONFLICT=1"]

[[when]]
condition = 'target.family == "unix"'

[when.usage]
private-definitions = ["FIXTURE_CONFLICT=2"]
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.condition.definition_conflict;\n"_str },
    };
    auto definition_project = materialize("condition-definition-conflict"_str, definition_files);
    ASSERT_TRUE(definition_project.is_ok());
    auto definition_request = project_build_request(
        "condition-definition-conflict"_str,
        definition_project->root.as_path(),
        strings("fixture-condition-definition-conflict"_str));
    auto definition_result = lito::build(definition_request);
    ASSERT_TRUE(definition_result.is_err());
    auto definition_error = error_chain_text(definition_result.unwrap_err());
    EXPECT_TRUE(definition_error.as_str().contains("condition 'true'"_str));
    EXPECT_TRUE(
        definition_error.as_str().contains(R"(condition 'target.family == "unix"')"_str));

    const ProjectFile scalar_files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-condition-scalar-conflict"
version = "0.1.0"

[lib]
name = "fixture-condition-scalar-conflict"
module = "fixture.condition.scalar_conflict"
archive = "fixture_condition_scalar_conflict"

[[when]]
condition = "true"

[when.usage]
threads = true

[[when]]
condition = 'target.family == "unix"'

[when.usage]
threads = false
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.condition.scalar_conflict;\n"_str },
    };
    auto scalar_project = materialize("condition-scalar-conflict"_str, scalar_files);
    ASSERT_TRUE(scalar_project.is_ok());
    auto scalar_request =
        project_build_request("condition-scalar-conflict"_str,
                              scalar_project->root.as_path(),
                              strings("fixture-condition-scalar-conflict"_str));
    auto scalar_result = lito::build(scalar_request);
    ASSERT_TRUE(scalar_result.is_err());
    auto scalar_error = error_chain_text(scalar_result.unwrap_err());
    EXPECT_TRUE(scalar_error.as_str().contains("condition 'true'"_str));
    EXPECT_TRUE(scalar_error.as_str().contains(R"(condition 'target.family == "unix"')"_str));
}

TEST_F(BuildCommand, DependencyFeaturesPropagatePublicMacros) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "fixture-dependency-features"
members = ["provider", "consumer"]

[workspace.package]
version = "0.1.0"
)toml"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-feature-provider"
version.workspace = true

[lib]
name = "fixture-feature-provider"
module = "fixture.feature.provider"
archive = "fixture_feature_provider"

[features.api]
default = false
)toml"_str },
        { "provider/src/lib.cppm"_str, "export module fixture.feature.provider;\n"_str },
        { "provider/src/api.cppm"_str, R"cpp(export module fixture.feature.provider.api;

export constexpr auto fixture_feature_value() -> int {
    return FIXTURE_FEATURE_PROVIDER_FEATURE_API;
}
)cpp"_str },
        { "consumer/lito.toml"_str, R"toml([package]
name = "fixture-feature-consumer"
version.workspace = true

[[bin]]
name = "fixture-feature-consumer"
link-stdlib = false
sources = ["src/main.cpp"]

[dependencies.fixture-feature-provider]
path = "../provider"
visibility = "private"
features = ["api"]
default-features = false
)toml"_str },
        { "consumer/src/main.cpp"_str, R"cpp(import fixture.feature.provider.api;

#if FIXTURE_FEATURE_PROVIDER_FEATURE_API != 1
#error dependency feature macro did not propagate
#endif

auto main() -> int {
    return fixture_feature_value() == 1 ? 0 : 1;
}
)cpp"_str },
    };
    auto project = materialize("dependency-features"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = project_build_request("dependency-features"_str,
                                         project->root.as_path(),
                                         strings("fixture-feature-consumer"_str));
    auto result = lito::build(request);
    if (result.is_err()) {
        auto message = error_chain_text(result.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::Executable), usize(1));
}
