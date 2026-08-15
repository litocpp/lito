#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
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
    EXPECT_EQ(loaded->standard_library, lito::StandardLibrary::Libstdcxx);

    auto legacy = lito::load_project_config(fixture_path("config/toolchain-legacy"_str).as_path());
    EXPECT_TRUE(legacy.is_err());

    auto defaults = lito::load_project_config(fixture_path("config"_str).as_path());
    ASSERT_TRUE(defaults.is_ok());
    EXPECT_EQ(defaults->standard_library, lito::StandardLibrary::Libcxx);
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

TEST(Config, ProjectConfigResolvesLitodocSourcePath) {
    auto overrides = Vec<String>::make();
    overrides.push(String::make("doc.litodoc-path=."_str));
    auto loaded = lito::load_project_config(
        fixture_path("config"_str).as_path(),
        lito::ProjectConfigRequest { .overrides = rstd::move(overrides) });
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->doc.litodoc_path.is_some());
    EXPECT_EQ(loaded->doc.litodoc_path->as_path(), fixture_path("config"_str).as_path());
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

TEST(Config, RuntimeOverridesShareOneSchemaDecode) {
    auto directory = output_root("config-runtime-overrides"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(config.as_path(), "[toolchain]\ncxx = 7\n"_str.as_bytes()).is_ok());

    auto overrides = Vec<String>::make();
    overrides.push(String::make("toolchain.cxx=generic-cxx"_str));
    overrides.push(String::make("toolchain.cc=generic-cc"_str));
    overrides.push(String::make("toolchain.stdlib=libstdc++"_str));
    overrides.push(String::make("build.options=[\"-pthread\"]"_str));
    auto loaded = lito::load_project_config(
        directory.as_path(),
        lito::ProjectConfigRequest {
            .overrides = rstd::move(overrides),
            .toolchain =
                lito::ToolchainOverride {
                    .cxx = Some(PathBuf::from("dedicated-cxx"_str)),
                },
            .toolchain_standard_library = Some(String::make("libc++"_str)),
        });
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("generic-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("dedicated-cxx"_str).as_path());
    EXPECT_EQ(loaded->standard_library, lito::StandardLibrary::Libcxx);
    ASSERT_EQ(loaded->build_options.len(), usize(1));
    EXPECT_EQ(loaded->build_options[usize {}].as_str(), "-pthread"_str);

    auto disabled_overrides = Vec<String>::make();
    disabled_overrides.push(String::make("toolchain.cxx=no-config-cxx"_str));
    disabled_overrides.push(String::make("toolchain.stdlib=libstdc++"_str));
    auto disabled = lito::load_project_config(directory.as_path(),
                                              lito::ProjectConfigRequest {
                                                  .mode      = lito::ConfigLoadMode::Disabled,
                                                  .overrides = rstd::move(disabled_overrides),
                                              });
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_EQ(disabled->toolchain.cxx.as_path(), PathBuf::from("no-config-cxx"_str).as_path());
    EXPECT_EQ(disabled->standard_library, lito::StandardLibrary::Libstdcxx);

    auto invalid_standard_library = Vec<String>::make();
    invalid_standard_library.push(String::make("toolchain.stdlib=unknown"_str));
    auto invalid = lito::load_project_config(directory.as_path(),
                                             lito::ProjectConfigRequest {
                                                 .mode      = lito::ConfigLoadMode::Disabled,
                                                 .overrides = rstd::move(invalid_standard_library),
                                             });
    EXPECT_TRUE(invalid.is_err());

    auto patch_directory = directory.join(PathBuf::from("rstd-patch"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(patch_directory.as_path()).is_ok());
    auto patch_overrides = Vec<String>::make();
    patch_overrides.push(rstd::format("patch.\"https://example.com/source?a=b\".path={}",
                                      patch_directory.as_path()));
    auto patched = lito::load_project_config(directory.as_path(),
                                             lito::ProjectConfigRequest {
                                                 .mode      = lito::ConfigLoadMode::Disabled,
                                                 .overrides = rstd::move(patch_overrides),
                                             });
    ASSERT_TRUE(patched.is_ok());
    ASSERT_EQ(patched->sources.patches.len(), usize(1));
    EXPECT_EQ(patched->sources.patches[usize {}].git.as_str(),
              "https://example.com/source?a=b"_str);
    EXPECT_EQ(patched->sources.patches[usize {}].path.as_path(), patch_directory.as_path());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Config, RuntimeOverridesRejectKeyStructureConflicts) {
    auto directory = output_root("config-runtime-conflict"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(config.as_path(), "toolchain = \"scalar\"\n"_str.as_bytes()).is_ok());
    auto overrides = Vec<String>::make();
    overrides.push(String::make("toolchain.cxx=clang++"_str));
    auto loaded = lito::load_project_config(directory.as_path(),
                                            lito::ProjectConfigRequest {
                                                .overrides = rstd::move(overrides),
                                            });
    EXPECT_TRUE(loaded.is_err());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Config, PersistedConfigSetGetUnsetIsAtomicAndValidated) {
    auto directory = output_root("config-persistence"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());

    auto path = lito::project_config_path(directory.as_path());
    ASSERT_TRUE(path.is_ok());
    EXPECT_EQ(path->as_path(),
              directory.join(PathBuf::from(".lito/config.toml"_str).as_path()).as_path());

    auto set =
        lito::set_persisted_config(directory.as_path(), "lock.path"_str, ".lito/lito.lock"_str);
    ASSERT_TRUE(set.is_ok());
    EXPECT_EQ(set->key.as_str(), "lock.path"_str);

    auto get = lito::get_persisted_config(directory.as_path(), Some(String::make("lock.path"_str)));
    ASSERT_TRUE(get.is_ok());
    EXPECT_EQ(get->output.as_str(), "\".lito/lito.lock\"\n"_str);

    auto before_invalid = rstd::fs::read_to_string(path->as_path());
    ASSERT_TRUE(before_invalid.is_ok());
    auto invalid = lito::set_persisted_config(directory.as_path(), "unknown"_str, "value"_str);
    EXPECT_TRUE(invalid.is_err());
    auto after_invalid = rstd::fs::read_to_string(path->as_path());
    ASSERT_TRUE(after_invalid.is_ok());
    EXPECT_EQ(after_invalid->as_str(), before_invalid->as_str());

    ASSERT_TRUE(rstd::fs::write(path->as_path(), "[lock]\npath = 7\n"_str.as_bytes()).is_ok());
    auto repaired =
        lito::set_persisted_config(directory.as_path(), "lock.path"_str, ".lito/lito.lock"_str);
    ASSERT_TRUE(repaired.is_ok());

    auto unset = lito::unset_persisted_config(directory.as_path(), "lock.path"_str);
    ASSERT_TRUE(unset.is_ok());
    auto whole = lito::get_persisted_config(directory.as_path(), None());
    ASSERT_TRUE(whole.is_ok());
    EXPECT_EQ(whole->output.as_str(), "\n"_str);

    ASSERT_TRUE(rstd::fs::write(path->as_path(), "unknown = 1\n"_str.as_bytes()).is_ok());
    auto repaired_unknown = lito::unset_persisted_config(directory.as_path(), "unknown"_str);
    ASSERT_TRUE(repaired_unknown.is_ok());
    whole = lito::get_persisted_config(directory.as_path(), None());
    ASSERT_TRUE(whole.is_ok());
    EXPECT_EQ(whole->output.as_str(), "\n"_str);

    auto missing =
        lito::get_persisted_config(directory.as_path(), Some(String::make("lock.path"_str)));
    EXPECT_TRUE(missing.is_err());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Config, PersistedConfigRejectsSymlinkFiles) {
    auto directory = output_root("config-symlink"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto target = directory.join(PathBuf::from("target.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(target.as_path(), "[lock]\npath = \"safe.lock\"\n"_str.as_bytes()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::soft_link(target.as_path(), config.as_path()).is_ok());

    EXPECT_TRUE(lito::get_persisted_config(directory.as_path(), None()).is_err());
    EXPECT_TRUE(
        lito::set_persisted_config(directory.as_path(), "lock.path"_str, ".lito/lito.lock"_str)
            .is_err());
    auto unchanged = rstd::fs::read_to_string(target.as_path());
    ASSERT_TRUE(unchanged.is_ok());
    EXPECT_EQ(unchanged->as_str(), "[lock]\npath = \"safe.lock\"\n"_str);
    EXPECT_TRUE(clear_output(directory.as_path()));
}
