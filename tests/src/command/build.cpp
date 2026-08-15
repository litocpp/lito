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

TEST(BuildCommand, BuildSelectsProductionArtifacts) {
    auto root   = project_root();
    auto output = output_root("build"_str);
    clear_output(output.as_path());
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
    clear_output(output.as_path());
}

TEST(BuildCommand, DocumentationSelectsOnlyLibraryArtifacts) {
    auto root   = project_root();
    auto output = output_root("build-doc"_str);
    clear_output(output.as_path());
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
    clear_output(output.as_path());
}
