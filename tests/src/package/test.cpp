#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.tools.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class PackageTest : public ProjectFixture {};

TEST_F(PackageTest, TestAttachmentRequiresADirectLibraryDependency) {
    const ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-test-attach-not-direct"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-test-attach-not-direct"
sources = ["main.cpp"]

[[test.attach]]
package = "missing-library"
sources = ["attached.cppm"]
)toml"_str,
        },
    };
    auto project = materialize("test-attach-not-direct"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output = build_root("test-attach-not-direct"_str);
    auto tested = lito::test(lito::TestRequest {
        .build = lito_test::build_request(
            project->root.as_path(), output.as_path(), Vec<String>::make()),
        .no_run = true,
    });
    ASSERT_TRUE(tested.is_err());
    auto tested_error = rstd::move(tested).unwrap_err();
    ASSERT_TRUE(tested_error.is_Build());
    EXPECT_TRUE(error_chain_text(tested_error).as_str().contains("direct dependency"_str));
}
