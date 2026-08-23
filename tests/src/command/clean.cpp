#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using namespace lito_test;

class CleanCommand : public ProjectFixture {};

auto create_clean_file(ref<rstd::path::Path> root, ref<str> relative) -> bool {
    auto path    = PathBuf::from(root).join(PathBuf::from(relative).as_path());
    auto parent  = path.as_path().parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) return false;
    return rstd::fs::write(path.as_path(), "output"_str.as_bytes()).is_ok();
}

TEST_F(CleanCommand, RemovesAllDefaultBuildProfiles) {
    auto project = source_root("clean-all"_str);
    ASSERT_TRUE(rstd::fs::create_dir(project.as_path()).is_ok());
    ASSERT_TRUE(create_clean_file(project.as_path(), "build/debug/debug.o"_str));
    ASSERT_TRUE(create_clean_file(project.as_path(), "build/release/release.o"_str));

    auto result = lito::clean(lito::CleanRequest {
        .root   = project.clone(),
        .target = lito::CleanTarget::All(),
    });
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->removed);
    EXPECT_FALSE(
        rstd::fs::exists(project.join(PathBuf::from("build"_str).as_path()).as_path()).unwrap());
}

TEST_F(CleanCommand, RemovesOnlySelectedDefaultProfile) {
    auto project = source_root("clean-profile"_str);
    ASSERT_TRUE(rstd::fs::create_dir(project.as_path()).is_ok());
    ASSERT_TRUE(create_clean_file(project.as_path(), "build/debug/debug.o"_str));
    ASSERT_TRUE(create_clean_file(project.as_path(), "build/release/release.o"_str));

    auto result = lito::clean(lito::CleanRequest {
        .root   = project.clone(),
        .target = lito::CleanTarget::Profile(build_profile("debug"_str)),
    });
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->removed);
    EXPECT_FALSE(
        rstd::fs::exists(project.join(PathBuf::from("build/debug"_str).as_path()).as_path())
            .unwrap());
    EXPECT_TRUE(
        rstd::fs::exists(project.join(PathBuf::from("build/release"_str).as_path()).as_path())
            .unwrap());
}

TEST_F(CleanCommand, RemovesExplicitBuildDirectory) {
    auto project = source_root("clean-explicit-project"_str);
    auto output  = build_root("clean-explicit-output"_str);
    ASSERT_TRUE(rstd::fs::create_dir(project.as_path()).is_ok());
    ASSERT_TRUE(create_clean_file(output.as_path(), "artifact.o"_str));

    auto result = lito::clean(lito::CleanRequest {
        .root   = project.clone(),
        .target = lito::CleanTarget::Directory(output.clone()),
    });
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->removed);
    EXPECT_FALSE(rstd::fs::exists(output.as_path()).unwrap());
}

TEST_F(CleanCommand, MissingTargetIsSuccessful) {
    auto project = source_root("clean-missing"_str);
    ASSERT_TRUE(rstd::fs::create_dir(project.as_path()).is_ok());

    auto result = lito::clean(lito::CleanRequest {
        .root   = project.clone(),
        .target = lito::CleanTarget::All(),
    });
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result->removed);
}

TEST_F(CleanCommand, RejectsTargetContainingProjectRoot) {
    auto project = source_root("clean-project-root"_str);
    ASSERT_TRUE(rstd::fs::create_dir(project.as_path()).is_ok());

    auto result = lito::clean(lito::CleanRequest {
        .root   = project.clone(),
        .target = lito::CleanTarget::Directory(PathBuf::from(temp_root())),
    });
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(
        error_chain_text(result.unwrap_err()).as_str().contains("contains project root"_str));
    EXPECT_TRUE(rstd::fs::exists(project.as_path()).unwrap());
}
