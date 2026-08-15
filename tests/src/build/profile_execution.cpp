#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(BuildProfile, BuildProfileOwnsOptimizationAndDebugDefinitions) {
    auto directory = fixture_path("build/profile"_str);
    auto output    = output_root("profile"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto debug =
        lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(debug.is_ok());
    auto debug_executable = executable(*debug);
    ASSERT_TRUE(debug_executable.is_some());
    auto debug_status = rstd::process::Command::make((*debug_executable).as_os_str())
                            .current_dir(directory.as_path())
                            .status();
    ASSERT_TRUE(debug_status.is_ok());
    ASSERT_TRUE(debug_status->code().is_some());
    EXPECT_EQ(*debug_status->code(), i32(1));

    auto release = lito::build(build_request(
        directory.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str)));
    ASSERT_TRUE(release.is_ok());
    auto release_executable = executable(*release);
    ASSERT_TRUE(release_executable.is_some());
    auto release_status = rstd::process::Command::make((*release_executable).as_os_str())
                              .current_dir(directory.as_path())
                              .status();
    ASSERT_TRUE(release_status.is_ok());
    EXPECT_TRUE(release_status->success());
    EXPECT_TRUE(clear_output(output.as_path()));
}
