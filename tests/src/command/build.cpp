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

TEST(BuildCommand, BuildSelectsProductionArtifacts) {
    auto root   = project_root();
    auto output = output_root("build"_str);
    clear_output(output.as_path());
    auto request = build_request(
        root.as_path(), output.as_path(), strings("fixture-test-lib"_str, "fixture-test-app"_str));
    auto summary = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::TestExecutable), usize {});
    clear_output(output.as_path());
}
