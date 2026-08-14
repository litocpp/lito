#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(PackageTest, TestAttachmentRequiresADirectLibraryDependency) {
    auto directory = fixture_path("manifest/test-attach-not-direct"_str);
    auto output    = output_root("test-attach-not-direct"_str);
    auto tested    = lito::test(lito::TestRequest {
        .build  = build_request(directory.as_path(), output.as_path(), Vec<String>::make()),
        .no_run = true,
    });
    ASSERT_TRUE(tested.is_err());
    auto tested_error = rstd::move(tested).unwrap_err();
    ASSERT_TRUE(tested_error.is_Build());
    EXPECT_TRUE(error_chain_text(tested_error).as_str().contains("direct dependency"_str));
    EXPECT_TRUE(clear_output(output.as_path()));
}
