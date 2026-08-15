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

TEST(ModuleDiscovery, DiscoveryAndModuleConventionsBuildExpectedCases) {
    auto output = output_root("module-discovery"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    for (const auto path : VALID_BUILD_CASES) {
        auto directory = fixture_path(path);
        auto built =
            lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_err()) rstd::io::eprintln("unexpected build failure: {}", path);
        EXPECT_TRUE(built.is_ok());
    }
    for (const auto path : INVALID_BUILD_CASES) {
        auto directory = fixture_path(path);
        auto built =
            lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_ok()) rstd::io::eprintln("unexpected build success: {}", path);
        EXPECT_TRUE(built.is_err());
    }
    EXPECT_TRUE(clear_output(output.as_path()));
}
