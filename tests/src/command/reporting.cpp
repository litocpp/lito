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
    EXPECT_EQ(rstd::format("{}", lito::BuildEventKind::CacheCheck).as_str(), "cache-check"_str);
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

TEST(Reporting, CacheCheckOnlyAppendsToFixedWidth) {
    auto progress = lito::CacheCheckProgress {};
    auto path     = PathBuf::from("/build/cache"_str);
    auto event = lito::BuildEvent { .kind     = lito::BuildEventKind::CacheCheck,
                                    .target   = "test"_str,
                                    .path     = path.as_path(),
                                    .progress = Some(lito::BuildProgress { usize {}, usize(10) }) };
    auto start = progress.update(event, true, usize(80));
    EXPECT_EQ(start.as_str(), "[cache-check 1] ["_str);
    event.progress->current = usize(5);
    auto middle             = progress.update(event, true, usize(80));
    EXPECT_EQ(middle.as_str(), "=========="_str);
    EXPECT_TRUE(progress.update(event, true, usize(80)).is_empty());
    event.progress->current = usize(10);
    event.completed         = true;
    auto end                = progress.update(event, true, usize(120));
    EXPECT_EQ(end.as_str(), "==========]\n"_str);
    start.push_str(middle.as_str());
    start.push_str(end.as_str());
    EXPECT_EQ(start.as_str(), "[cache-check 1] [====================]\n"_str);
    EXPECT_FALSE(start.as_str().contains("\r"_str));
    EXPECT_FALSE(start.as_str().contains("\x1b"_str));
    EXPECT_TRUE(progress.interrupt().is_empty());
}

TEST(Reporting, CacheCheckRedirectFailureAndNarrowTerminal) {
    auto progress = lito::CacheCheckProgress {};
    auto path     = PathBuf::from("/build/cache"_str);
    auto event = lito::BuildEvent { .kind     = lito::BuildEventKind::CacheCheck,
                                    .target   = "test"_str,
                                    .path     = path.as_path(),
                                    .progress = Some(lito::BuildProgress { usize {}, usize(10) }) };
    auto start = progress.update(event, false, usize {});
    EXPECT_TRUE(start.as_str().contains("checking"_str));
    EXPECT_FALSE(start.as_str().contains("\r"_str));
    event.progress->current = usize(4);
    EXPECT_TRUE(progress.update(event, false, usize {}).is_empty());
    event.completed = true;
    event.failed    = true;
    auto end        = progress.update(event, false, usize {});
    EXPECT_TRUE(end.as_str().contains("4/10 failed"_str));
    EXPECT_FALSE(end.as_str().contains("\r"_str));
    event.completed         = false;
    event.failed            = false;
    event.progress->current = usize {};
    auto narrow             = progress.update(event, true, usize(12));
    EXPECT_TRUE(narrow.len() < usize(12));
    EXPECT_EQ(progress.interrupt().as_str(), "]\n"_str);
    EXPECT_TRUE(progress.interrupt().is_empty());
    event.progress->current = usize(4);
    event.completed         = true;
    event.failed            = true;
    auto failed             = progress.update(event, true, usize(12));
    EXPECT_EQ(failed.as_str(), "[] failed\n"_str);
    EXPECT_TRUE(failed.len() <= usize(12));
    event.progress->total = usize {};
    EXPECT_TRUE(progress.update(event, true, usize(80)).is_empty());
}

TEST(Reporting, CacheCheckDoesNotFillBeforeSuccessfulCompletion) {
    auto progress = lito::CacheCheckProgress {};
    auto path     = PathBuf::from("/build/cache"_str);
    auto event = lito::BuildEvent { .kind     = lito::BuildEventKind::CacheCheck,
                                    .target   = "test"_str,
                                    .path     = path.as_path(),
                                    .progress = Some(lito::BuildProgress { usize {}, usize(1) }) };
    auto text  = progress.update(event, true, usize(80));
    event.progress->current = usize(1);
    text.push_str(progress.update(event, true, usize(80)).as_str());
    event.completed = true;
    event.failed    = true;
    text.push_str(progress.update(event, true, usize(80)).as_str());
    EXPECT_EQ(text.as_str(), "[cache-check 1] [===================] failed\n"_str);
    EXPECT_TRUE(progress.interrupt().is_empty());
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
