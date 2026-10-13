#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
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

TEST(Reporting, UserFacingEnumsImplementDisplay) {
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::Scan).as_str(), "scan"_str);
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::Fetch).as_str(), "fetch"_str);
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::Extract).as_str(), "extract"_str);
    EXPECT_EQ(rstd::to_string(lito::BuildEventKind::CMakeQueryBuild).as_str(),
              "cmake-query-build"_str);
    EXPECT_EQ(rstd::to_string(lito::BuildEventKind::ProductFinalize).as_str(),
              "product-finalize"_str);
    EXPECT_EQ(rstd::to_string(lito::BuildEventKind::ProductPublish).as_str(),
              "product-publish"_str);
    EXPECT_EQ(rstd::format("{}", lito::package::PackageSelectionPurpose::Benchmark).as_str(),
              "benchmark"_str);
    EXPECT_EQ(rstd::format("{}", lito::package::PackageSelectionPurpose::Install).as_str(),
              "install"_str);
    EXPECT_EQ(rstd::format("{}", lito::package::ProjectRootRole::AssociatedTest).as_str(),
              "test"_str);
    EXPECT_EQ(rstd::format("{}", lito::CMakePackageOperation::WriteQuery).as_str(),
              "query materialization"_str);
    EXPECT_EQ(rstd::format("{}", lito::cpp::BmiCompatibilityField::TargetFeatures).as_str(),
              "target features"_str);
}

TEST(Reporting, BuildOutputsUseProcessedFilesAndPublicRoles) {
    auto product            = lito::CompletedBuildProduct {};
    auto script             = lito::BuildScriptReport {};
    product.profile         = "release"_Str;
    product.target          = "wasm32-unknown-unknown"_Str;
    product.build_directory = PathBuf::from("/tmp/public-build"_str);
    auto artifact           = lito::BuiltArtifact {};
    artifact.target         = { "sample-package"_Str,
                                lito::package::PackageTargetKind::Example,
                                "sample"_Str };
    artifact.primary.role   = lito::ArtifactFileRole::Runtime;
    artifact.primary.path   = PathBuf::from("/tmp/processed/sample.wasm"_str);
    artifact.companions.push(lito::BuiltArtifactFile {
        .role = lito::ArtifactFileRole::JavaScriptModule,
        .path = PathBuf::from("/tmp/processed/sample.js"_str),
    });
    product.artifacts.push(rstd::move(artifact));
    auto report = lito::build_output::render(product, script);
    ASSERT_TRUE(report.is_ok());
    EXPECT_TRUE(report->as_str().contains("lito-build-outputs"_str));
    EXPECT_TRUE(report->as_str().contains("javascript-module"_str));
    EXPECT_TRUE(report->as_str().contains("/tmp/processed/sample.wasm"_str));
    EXPECT_TRUE(report->as_str().contains("/tmp/processed/sample.js"_str));
    EXPECT_FALSE(report->as_str().contains("raw"_str));
}

TEST(Reporting, BuildOutputsUseOwnedGeneratedDirectoriesInsteadOfScriptDefaults) {
    auto product            = lito::CompletedBuildProduct {};
    product.profile         = "release"_Str;
    product.target          = "x86_64-unknown-linux-gnu"_Str;
    product.build_directory = PathBuf::from("/unused-build-directory"_str);
    auto script             = lito::BuildScriptReport {};
    script.executions.push(lito::BuildScriptExecution { .owner = "workspace"_Str });
    script.executions.push(lito::BuildScriptExecution { .package = Some("no-outputs"_Str) });
    script.generated_packages.push(lito::GeneratedPackageDirectory {
        .package   = "assets"_Str,
        .directory = PathBuf::from("/actual-generated/assets"_str),
    });
    auto report = lito::build_output::render(product, script);
    ASSERT_TRUE(report.is_ok());
    auto        document = rstd::json::from_str(report->as_str()).unwrap();
    const auto& packages = *document["generated-packages"_str].as_array().unwrap();
    ASSERT_EQ(packages.len(), usize(1));
    EXPECT_EQ(packages[usize {}]["package"_str].as_str().unwrap(), "assets"_str);
    EXPECT_EQ(packages[usize {}]["directory"_str].as_str().unwrap(),
              "/actual-generated/assets"_str);
}
