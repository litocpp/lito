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

TEST(Reporting, UserFacingEnumsImplementDisplay) {
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::Toolchain).as_str(), "toolchain"_str);
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::Scan).as_str(), "scan"_str);
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::Fetch).as_str(), "fetch"_str);
    EXPECT_EQ(rstd::to_string(lito::BuildEventKind::CMakeQueryBuild).as_str(),
              "cmake-query-build"_str);
    EXPECT_EQ(rstd::format("{}", lito::PackageSelectionPurpose::Benchmark).as_str(),
              "benchmark"_str);
    EXPECT_EQ(rstd::format("{}", lito::PackageSelectionPurpose::Install).as_str(), "install"_str);
    EXPECT_EQ(rstd::format("{}", lito::ProjectRootRole::AssociatedTest).as_str(), "test"_str);
    EXPECT_EQ(rstd::format("{}", lito::CMakePackageOperation::WriteQuery).as_str(),
              "query materialization"_str);
    EXPECT_EQ(rstd::format("{}", lito::cpp::BmiCompatibilityField::TargetFeatures).as_str(),
              "target features"_str);
}
