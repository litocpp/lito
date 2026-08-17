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

constexpr auto removed_scanner_config        = "[toolchain]\nscanner = \"clang-scan-deps\"\n"_str;
constexpr auto toolchain_config              = R"toml([toolchain]
cc = "custom-cc"
cxx = "custom-cxx"
ld = "custom-ld"
ar = "custom-ar"
strip = "custom-strip"
format = "custom-format"
stdlib = "libstdc++"
)toml"_str;
constexpr auto toolchain_legacy_config       = "[toolchain]\ncompiler = \"custom-cxx\"\n"_str;
constexpr auto environment_valid_config      = "[environment]\nappend-path = [\".\", \"..\"]\n"_str;
constexpr auto environment_empty_config      = "[environment]\nappend-path = []\n"_str;
constexpr auto lock_local_config             = "[lock]\npath = \".lito/lito.lock\"\n"_str;
constexpr auto lock_missing_path_config      = "[lock]\n"_str;
constexpr auto pkg_config_config             = R"toml([pkg-config]
executable = "pkg-config"
search-path = ["."]
library-path = ["."]
sysroot = "."
)toml"_str;
constexpr auto pkg_config_search_only_config = "[pkg-config]\nsearch-path = [\".\"]\n"_str;
constexpr auto cmake_config                  = R"toml([cmake]
executable = "custom-cmake"
generator = "Unix Makefiles"
search-path = ["."]
)toml"_str;

class Config : public ProjectFixture {
protected:
    auto config(ref<str> name, ref<str> contents)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        const ProjectFile files[] = {
            { ".lito/config.toml"_str, contents },
        };
        return materialize(name, files);
    }

    auto empty_project(ref<str> name)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        auto tree = lito::source::SourceTree::make();
        return materialize(name, tree);
    }
};

TEST_F(Config, RemovedConfigFieldsAreRejectedByConfigOwner) {
    auto project = config("removed-scanner"_str, removed_scanner_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    EXPECT_TRUE(loaded.is_err());
}

TEST_F(Config, ToolchainConfigurationUsesCommandLineNames) {
    auto project = config("toolchain"_str, toolchain_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("custom-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("custom-cxx"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ld.as_path(), PathBuf::from("custom-ld"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ar.as_path(), PathBuf::from("custom-ar"_str).as_path());
    EXPECT_EQ(loaded->toolchain.strip.as_path(), PathBuf::from("custom-strip"_str).as_path());
    EXPECT_EQ(loaded->toolchain.format.as_path(), PathBuf::from("custom-format"_str).as_path());
    EXPECT_EQ(loaded->standard_library, lito::config::StandardLibrary::Libstdcxx);

    auto legacy_project = config("toolchain-legacy"_str, toolchain_legacy_config);
    ASSERT_TRUE(legacy_project.is_ok());
    auto legacy = lito::config::load_project_config(legacy_project->root.as_path());
    EXPECT_TRUE(legacy.is_err());

    auto default_project = empty_project("defaults"_str);
    ASSERT_TRUE(default_project.is_ok());
    auto defaults = lito::config::load_project_config(default_project->root.as_path());
    ASSERT_TRUE(defaults.is_ok());
    EXPECT_EQ(defaults->standard_library, lito::config::StandardLibrary::Libcxx);
}

TEST_F(Config, SharedConfigurationIsTheBaseOfLocalConfiguration) {
    auto directory       = source_root("config-shared-layer"_str);
    auto local_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(local_directory.as_path()).is_ok());
    auto shared = directory.join(PathBuf::from("lito-config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(shared.as_path(),
                                "[toolchain]\n"
                                "cxx = \"project-cxx\"\n"
                                "stdlib = \"libstdc++\"\n"
                                "[cmake]\n"
                                "generator = \"Ninja\"\n"_str.as_bytes())
                    .is_ok());
    auto local = local_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(local.as_path(),
                                "[toolchain]\n"
                                "cxx = \"local-cxx\"\n"
                                "[cmake]\n"
                                "search-path = [\".\"]\n"_str.as_bytes())
                    .is_ok());

    auto loaded = lito::config::load_project_config(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("local-cxx"_str).as_path());
    EXPECT_EQ(loaded->standard_library, lito::config::StandardLibrary::Libstdcxx);
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Ninja"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(), directory.as_path());

    auto shared_only = lito::config::load_project_config(
        directory.as_path(), lito::config::ConfigLoadMode::LocalDisabled);
    ASSERT_TRUE(shared_only.is_ok());
    EXPECT_EQ(shared_only->toolchain.cxx.as_path(), PathBuf::from("project-cxx"_str).as_path());
    EXPECT_EQ(shared_only->standard_library, lito::config::StandardLibrary::Libstdcxx);
    EXPECT_EQ(shared_only->cmake.generator.as_str(), "Ninja"_str);
    EXPECT_TRUE(shared_only->cmake.search_paths.is_empty());
}

TEST_F(Config, EnvironmentAppendPathBelongsToProjectConfig) {
    auto valid_project = config("environment-valid"_str, environment_valid_config);
    ASSERT_TRUE(valid_project.is_ok());
    auto loaded = lito::config::load_project_config(valid_project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->environment.append_path.len(), usize(2));
    EXPECT_EQ(loaded->environment.append_path[usize {}].as_path(), valid_project->root.as_path());
    EXPECT_EQ(loaded->environment.append_path[usize(1)].as_path(),
              valid_project->root.as_path().parent().unwrap());

    auto empty_project_root = config("environment-empty"_str, environment_empty_config);
    ASSERT_TRUE(empty_project_root.is_ok());
    auto empty = lito::config::load_project_config(empty_project_root->root.as_path());
    ASSERT_TRUE(empty.is_ok());
    EXPECT_TRUE(empty->environment.append_path.is_empty());

    auto unconfigured_project = empty_project("environment-unconfigured"_str);
    ASSERT_TRUE(unconfigured_project.is_ok());
    auto unconfigured = lito::config::load_project_config(unconfigured_project->root.as_path());
    ASSERT_TRUE(unconfigured.is_ok());
    EXPECT_TRUE(unconfigured->environment.append_path.is_empty());
}

TEST_F(Config, LockPathBelongsToProjectConfig) {
    auto project = config("lock-local"_str, lock_local_config);
    ASSERT_TRUE(project.is_ok());
    auto root_path = project->root.clone();
    auto loaded    = lito::config::load_project_config(root_path.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->lock.path.as_path(),
              root_path.join(rstd::path::PathBuf::from(".lito/lito.lock"_str).as_path()).as_path());

    auto disabled = lito::config::load_project_config(root_path.as_path(),
                                                      lito::config::ConfigLoadMode::LocalDisabled);
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_EQ(disabled->lock.path.as_path(),
              root_path.join(rstd::path::PathBuf::from("lito.lock"_str).as_path()).as_path());

    auto invalid_project = config("lock-missing-path"_str, lock_missing_path_config);
    ASSERT_TRUE(invalid_project.is_ok());
    auto invalid_root = invalid_project->root.clone();
    auto ignored = lito::config::load_project_config(invalid_root.as_path(),
                                                     lito::config::ConfigLoadMode::LocalDisabled);
    ASSERT_TRUE(ignored.is_ok());
    EXPECT_EQ(ignored->lock.path.as_path(),
              invalid_root.join(rstd::path::PathBuf::from("lito.lock"_str).as_path()).as_path());
}

TEST_F(Config, ProjectConfigParsesInstallRootRelativeToProject) {
    auto directory        = source_root("install-config"_str);
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(config.as_path(), "[install]\nroot = \"tools\"\n"_str.as_bytes()).is_ok());
    auto loaded = lito::config::load_project_config(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->install.root.is_some());
    EXPECT_EQ(loaded->install.root->as_path(),
              directory.join(PathBuf::from("tools"_str).as_path()).as_path());
}

TEST_F(Config, ProjectConfigResolvesLitodocSourcePath) {
    auto project = empty_project("litodoc-source"_str);
    ASSERT_TRUE(project.is_ok());
    auto overrides = Vec<String>::make();
    overrides.push(String::make("doc.litodoc-path=."_str));
    auto loaded = lito::config::load_project_config(
        project->root.as_path(),
        lito::config::ProjectConfigRequest { .overrides = rstd::move(overrides) });
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->doc.litodoc_path.is_some());
    EXPECT_EQ(loaded->doc.litodoc_path->as_path(), project->root.as_path());
}

TEST_F(Config, InvalidLockPathsAreRejectedByConfigOwner) {
    struct ConfigCase {
        ref<str> name;
        ref<str> contents;
    };
    constexpr ConfigCase cases[] = {
        { "lock-empty"_str, "[lock]\npath = \"\"\n"_str },
        { "lock-missing-path"_str, "[lock]\n"_str },
        { "lock-missing-parent"_str, "[lock]\npath = \".local/lito.lock\"\n"_str },
        { "lock-directory"_str, "[lock]\npath = \".lito\"\n"_str },
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto project = config(item.name, item.contents);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::config::load_project_config(project->root.as_path()).is_err());
    }
}

TEST_F(Config, InvalidEnvironmentAppendPathIsRejectedByConfigOwner) {
    struct ConfigCase {
        ref<str> name;
        ref<str> contents;
    };
    constexpr ConfigCase cases[] = {
        { "environment-wrong-type"_str, "[environment]\nappend-path = \"tools\"\n"_str },
        { "environment-empty-entry"_str, "[environment]\nappend-path = [\"\"]\n"_str },
        { "environment-missing"_str, "[environment]\nappend-path = [\"missing\"]\n"_str },
        { "environment-file"_str, "[environment]\nappend-path = [\".lito/config.toml\"]\n"_str },
        { "environment-unknown"_str, "[environment]\nappend-path = []\nprepend-path = []\n"_str },
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto project = config(item.name, item.contents);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::config::load_project_config(project->root.as_path()).is_err());
    }
}

TEST_F(Config, PkgConfigProviderConfigurationBelongsToProjectConfig) {
    auto project = config("pkg-config"_str, pkg_config_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->pkg_config.target_configured);
    EXPECT_EQ(loaded->pkg_config.executable.as_path().to_str().unwrap(), "pkg-config"_str);
    EXPECT_EQ(loaded->pkg_config.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->pkg_config.library_paths.len(), usize(1));
    EXPECT_TRUE(loaded->pkg_config.sysroot.is_some());

    auto search_project = config("pkg-config-search-only"_str, pkg_config_search_only_config);
    ASSERT_TRUE(search_project.is_ok());
    auto search_only = lito::config::load_project_config(search_project->root.as_path());
    ASSERT_TRUE(search_only.is_ok());
    EXPECT_FALSE(search_only->pkg_config.target_configured);
}

TEST_F(Config, CMakeProviderConfigurationBelongsToProjectConfig) {
    auto project = config("cmake"_str, cmake_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->cmake.executable.as_path().to_str().unwrap(), "custom-cmake"_str);
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Unix Makefiles"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(), project->root.as_path());
}

TEST_F(Config, RuntimeOverridesShareOneSchemaDecode) {
    auto directory        = source_root("config-runtime-overrides"_str);
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(config.as_path(), "[toolchain]\ncxx = 7\n"_str.as_bytes()).is_ok());

    auto overrides = Vec<String>::make();
    overrides.push(String::make("toolchain.cxx=generic-cxx"_str));
    overrides.push(String::make("toolchain.cc=generic-cc"_str));
    overrides.push(String::make("toolchain.stdlib=libstdc++"_str));
    overrides.push(String::make("build.options=[\"-pthread\"]"_str));
    auto loaded = lito::config::load_project_config(
        directory.as_path(),
        lito::config::ProjectConfigRequest {
            .overrides = rstd::move(overrides),
            .toolchain =
                lito::config::ToolchainOverride {
                    .cxx = Some(PathBuf::from("dedicated-cxx"_str)),
                },
            .toolchain_standard_library = Some(String::make("libc++"_str)),
        });
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("generic-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("dedicated-cxx"_str).as_path());
    EXPECT_EQ(loaded->standard_library, lito::config::StandardLibrary::Libcxx);
    ASSERT_EQ(loaded->build_options.len(), usize(1));
    EXPECT_EQ(loaded->build_options[usize {}].as_str(), "-pthread"_str);

    auto disabled_overrides = Vec<String>::make();
    disabled_overrides.push(String::make("toolchain.cxx=no-config-cxx"_str));
    disabled_overrides.push(String::make("toolchain.stdlib=libstdc++"_str));
    auto disabled =
        lito::config::load_project_config(directory.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(disabled_overrides),
                                          });
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_EQ(disabled->toolchain.cxx.as_path(), PathBuf::from("no-config-cxx"_str).as_path());
    EXPECT_EQ(disabled->standard_library, lito::config::StandardLibrary::Libstdcxx);

    auto invalid_standard_library = Vec<String>::make();
    invalid_standard_library.push(String::make("toolchain.stdlib=unknown"_str));
    auto invalid =
        lito::config::load_project_config(directory.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(invalid_standard_library),
                                          });
    EXPECT_TRUE(invalid.is_err());

    auto patch_directory = directory.join(PathBuf::from("rstd-patch"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(patch_directory.as_path()).is_ok());
    auto patch_overrides = Vec<String>::make();
    patch_overrides.push(rstd::format("patch.\"https://example.com/source?a=b\".path={}",
                                      patch_directory.as_path()));
    auto patched =
        lito::config::load_project_config(directory.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(patch_overrides),
                                          });
    ASSERT_TRUE(patched.is_ok());
    ASSERT_EQ(patched->sources.patches.len(), usize(1));
    EXPECT_EQ(patched->sources.patches[usize {}].git.as_str(),
              "https://example.com/source?a=b"_str);
    EXPECT_EQ(patched->sources.patches[usize {}].path.as_path(), patch_directory.as_path());
}

TEST_F(Config, RuntimeOverridesRejectKeyStructureConflicts) {
    auto directory        = source_root("config-runtime-conflict"_str);
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(config.as_path(), "toolchain = \"scalar\"\n"_str.as_bytes()).is_ok());
    auto overrides = Vec<String>::make();
    overrides.push(String::make("toolchain.cxx=clang++"_str));
    auto loaded = lito::config::load_project_config(directory.as_path(),
                                                    lito::config::ProjectConfigRequest {
                                                        .overrides = rstd::move(overrides),
                                                    });
    EXPECT_TRUE(loaded.is_err());
}

TEST_F(Config, PersistedConfigSetGetUnsetIsAtomicAndValidated) {
    auto directory = source_root("config-persistence"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());

    auto path = lito::config::project_config_path(directory.as_path());
    ASSERT_TRUE(path.is_ok());
    EXPECT_EQ(path->as_path(),
              directory.join(PathBuf::from(".lito/config.toml"_str).as_path()).as_path());

    auto set = lito::config::set_persisted_config(
        directory.as_path(), "lock.path"_str, ".lito/lito.lock"_str);
    ASSERT_TRUE(set.is_ok());
    EXPECT_EQ(set->key.as_str(), "lock.path"_str);

    auto get = lito::config::get_persisted_config(directory.as_path(),
                                                  Some(String::make("lock.path"_str)));
    ASSERT_TRUE(get.is_ok());
    EXPECT_EQ(get->output.as_str(), "\".lito/lito.lock\"\n"_str);

    auto before_invalid = rstd::fs::read_to_string(path->as_path());
    ASSERT_TRUE(before_invalid.is_ok());
    auto invalid =
        lito::config::set_persisted_config(directory.as_path(), "unknown"_str, "value"_str);
    EXPECT_TRUE(invalid.is_err());
    auto after_invalid = rstd::fs::read_to_string(path->as_path());
    ASSERT_TRUE(after_invalid.is_ok());
    EXPECT_EQ(after_invalid->as_str(), before_invalid->as_str());

    ASSERT_TRUE(rstd::fs::write(path->as_path(), "[lock]\npath = 7\n"_str.as_bytes()).is_ok());
    auto repaired = lito::config::set_persisted_config(
        directory.as_path(), "lock.path"_str, ".lito/lito.lock"_str);
    ASSERT_TRUE(repaired.is_ok());

    auto unset = lito::config::unset_persisted_config(directory.as_path(), "lock.path"_str);
    ASSERT_TRUE(unset.is_ok());
    auto whole = lito::config::get_persisted_config(directory.as_path(), None());
    ASSERT_TRUE(whole.is_ok());
    EXPECT_EQ(whole->output.as_str(), "\n"_str);

    ASSERT_TRUE(rstd::fs::write(path->as_path(), "unknown = 1\n"_str.as_bytes()).is_ok());
    auto repaired_unknown =
        lito::config::unset_persisted_config(directory.as_path(), "unknown"_str);
    ASSERT_TRUE(repaired_unknown.is_ok());
    whole = lito::config::get_persisted_config(directory.as_path(), None());
    ASSERT_TRUE(whole.is_ok());
    EXPECT_EQ(whole->output.as_str(), "\n"_str);

    auto missing = lito::config::get_persisted_config(directory.as_path(),
                                                      Some(String::make("lock.path"_str)));
    EXPECT_TRUE(missing.is_err());
}

TEST_F(Config, PersistedConfigRejectsSymlinkFiles) {
    auto directory        = source_root("config-symlink"_str);
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto target = directory.join(PathBuf::from("target.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(target.as_path(), "[lock]\npath = \"safe.lock\"\n"_str.as_bytes()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::soft_link(target.as_path(), config.as_path()).is_ok());

    EXPECT_TRUE(lito::config::get_persisted_config(directory.as_path(), None()).is_err());
    EXPECT_TRUE(lito::config::set_persisted_config(
                    directory.as_path(), "lock.path"_str, ".lito/lito.lock"_str)
                    .is_err());
    auto unchanged = rstd::fs::read_to_string(target.as_path());
    ASSERT_TRUE(unchanged.is_ok());
    EXPECT_EQ(unchanged->as_str(), "[lock]\npath = \"safe.lock\"\n"_str);
}
