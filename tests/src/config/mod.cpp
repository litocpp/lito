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

TEST(Config, RemovedConfigFieldsAreRejectedByConfigOwner) {
    auto loaded = lito::load_project_config(fixture_path("config/removed-scanner"_str).as_path());
    EXPECT_TRUE(loaded.is_err());
}

TEST(Config, ToolchainConfigurationUsesCommandLineNames) {
    auto loaded = lito::load_project_config(fixture_path("config/toolchain"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("custom-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("custom-cxx"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ld.as_path(), PathBuf::from("custom-ld"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ar.as_path(), PathBuf::from("custom-ar"_str).as_path());
    EXPECT_EQ(loaded->toolchain.strip.as_path(), PathBuf::from("custom-strip"_str).as_path());
    EXPECT_EQ(loaded->toolchain.format.as_path(), PathBuf::from("custom-format"_str).as_path());

    auto legacy = lito::load_project_config(fixture_path("config/toolchain-legacy"_str).as_path());
    EXPECT_TRUE(legacy.is_err());
}

TEST(Config, EnvironmentAppendPathBelongsToProjectConfig) {
    auto loaded = lito::load_project_config(fixture_path("config/environment-valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->environment.append_path.len(), usize(2));
    EXPECT_EQ(loaded->environment.append_path[usize {}].as_path(),
              fixture_path("config/environment-valid"_str).as_path());
    EXPECT_EQ(loaded->environment.append_path[usize(1)].as_path(),
              fixture_path("config"_str).as_path());

    auto empty = lito::load_project_config(fixture_path("config/environment-empty"_str).as_path());
    ASSERT_TRUE(empty.is_ok());
    EXPECT_TRUE(empty->environment.append_path.is_empty());

    auto unconfigured = lito::load_project_config(fixture_path("config"_str).as_path());
    ASSERT_TRUE(unconfigured.is_ok());
    EXPECT_TRUE(unconfigured->environment.append_path.is_empty());
}

TEST(Config, LockPathBelongsToProjectConfig) {
    auto root_path = fixture_path("config/lock-local"_str);
    auto loaded    = lito::load_project_config(root_path.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->lock.path.as_path(),
              root_path.join(rstd::path::PathBuf::from(".lito/lito.lock"_str).as_path()).as_path());

    auto disabled = lito::load_project_config(root_path.as_path(), lito::ConfigLoadMode::Disabled);
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_EQ(disabled->lock.path.as_path(),
              root_path.join(rstd::path::PathBuf::from("lito.lock"_str).as_path()).as_path());

    auto invalid_root = fixture_path("config/lock-missing-path"_str);
    auto ignored =
        lito::load_project_config(invalid_root.as_path(), lito::ConfigLoadMode::Disabled);
    ASSERT_TRUE(ignored.is_ok());
    EXPECT_EQ(ignored->lock.path.as_path(),
              invalid_root.join(rstd::path::PathBuf::from("lito.lock"_str).as_path()).as_path());
}

TEST(Config, ProjectConfigParsesInstallRootRelativeToProject) {
    auto directory = output_root("install-config"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(config.as_path(), "[install]\nroot = \"tools\"\n"_str.as_bytes()).is_ok());
    auto loaded = lito::load_project_config(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->install.root.is_some());
    EXPECT_EQ(loaded->install.root->as_path(),
              directory.join(PathBuf::from("tools"_str).as_path()).as_path());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Config, InvalidLockPathsAreRejectedByConfigOwner) {
    constexpr ref<str> cases[] = {
        "config/lock-empty"_str,
        "config/lock-missing-path"_str,
        "config/lock-missing-parent"_str,
        "config/lock-directory"_str,
    };
    for (const auto path : cases) {
        EXPECT_TRUE(lito::load_project_config(fixture_path(path).as_path()).is_err());
    }
}

TEST(Config, InvalidEnvironmentAppendPathIsRejectedByConfigOwner) {
    constexpr ref<str> cases[] = {
        "config/environment-wrong-type"_str, "config/environment-empty-entry"_str,
        "config/environment-missing"_str,    "config/environment-file"_str,
        "config/environment-unknown"_str,
    };
    for (const auto path : cases) {
        EXPECT_TRUE(lito::load_project_config(fixture_path(path).as_path()).is_err());
    }
}

TEST(Config, PkgConfigProviderConfigurationBelongsToProjectConfig) {
    auto loaded = lito::load_project_config(fixture_path("config/pkg-config"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->pkg_config.target_configured);
    EXPECT_EQ(loaded->pkg_config.executable.as_path().to_str().unwrap(), "pkg-config"_str);
    EXPECT_EQ(loaded->pkg_config.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->pkg_config.library_paths.len(), usize(1));
    EXPECT_TRUE(loaded->pkg_config.sysroot.is_some());

    auto search_only =
        lito::load_project_config(fixture_path("config/pkg-config-search-only"_str).as_path());
    ASSERT_TRUE(search_only.is_ok());
    EXPECT_FALSE(search_only->pkg_config.target_configured);
}

TEST(Config, CMakeProviderConfigurationBelongsToProjectConfig) {
    auto loaded = lito::load_project_config(fixture_path("config/cmake"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->cmake.executable.as_path().to_str().unwrap(), "custom-cmake"_str);
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Unix Makefiles"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(),
              fixture_path("config/cmake"_str).as_path());
}
