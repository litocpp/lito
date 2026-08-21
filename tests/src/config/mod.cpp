#include <rstd/test/gtest.hpp>

import rstd;
import lito.tools;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.system;
import lito.tools.cmake;
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
stdlib = "libstdc++"
stdlib-runtime = "dynamic"

[tools]
cmake = "custom-cmake"
tar = "custom-tar"
bsdtar = "custom-bsdtar"
clang-format = "custom-format"
curl = "custom-curl"
git = "custom-git"
pkg-config = "custom-pkg-config"
strip = "custom-strip"
)toml"_str;
constexpr auto toolchain_legacy_config       = "[toolchain]\ncompiler = \"custom-cxx\"\n"_str;
constexpr auto environment_valid_config      = "[environment]\nappend-path = [\".\", \"..\"]\n"_str;
constexpr auto environment_empty_config      = "[environment]\nappend-path = []\n"_str;
constexpr auto lock_local_config             = "[lock]\npath = \".lito/lito.lock\"\n"_str;
constexpr auto lock_missing_path_config      = "[lock]\n"_str;
constexpr auto pkg_config_config             = R"toml([tools.pkg-config]
executable = "custom-pkg-config"
search-path = ["."]
library-path = ["."]
sysroot = "."
)toml"_str;
constexpr auto pkg_config_search_only_config = "[tools.pkg-config]\nsearch-path = [\".\"]\n"_str;
constexpr auto cmake_config                  = R"toml([tools.cmake]
executable = "custom-cmake"
generator = "Unix Makefiles"
search-path = ["."]
)toml"_str;
constexpr auto cmake_override_config         = R"toml([tools.cmake.overrides.Eigen3]
source = "installed"
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
    struct RemovedField {
        ref<str> name;
        ref<str> contents;
    };
    constexpr RemovedField fields[] = {
        { "removed-scanner"_str, removed_scanner_config },
        { "removed-toolchain-format"_str, "[toolchain]\nformat = \"clang-format\"\n"_str },
        { "removed-toolchain-strip"_str, "[toolchain]\nstrip = \"llvm-strip\"\n"_str },
        { "removed-cmake"_str, "[cmake]\ngenerator = \"Ninja\"\n"_str },
        { "removed-pkg-config"_str, "[pkg-config]\nsearch-path = []\n"_str },
    };
    for (const auto& field : fields) {
        auto project = config(field.name, field.contents);
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::config::load_project_config(project->root.as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST_F(Config, InvalidHostToolProviderConfigurationReportsTheNestedKey) {
    struct ConfigCase {
        ref<str> name;
        ref<str> contents;
        ref<str> expected;
    };
    constexpr ConfigCase cases[] = {
        { "integer-cmake"_str,
          "[tools]\ncmake = 7\n"_str,
          "config.tools.cmake must be a table"_str },
        { "array-pkg-config"_str,
          "[tools]\npkg-config = []\n"_str,
          "config.tools.pkg-config must be a table"_str },
        { "empty-cmake-executable"_str,
          "[tools.cmake]\nexecutable = \"\"\n"_str,
          "config.tools.cmake.executable must not be empty"_str },
        { "unknown-pkg-config-field"_str,
          "[tools.pkg-config]\nargs = []\n"_str,
          "config.tools.pkg-config contains unknown field 'args'"_str },
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto project = config(item.name, item.contents);
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::config::load_project_config(project->root.as_path());
        ASSERT_TRUE(loaded.is_err());
        auto error = rstd::move(loaded).unwrap_err();
        EXPECT_TRUE(error_chain_text(error).as_str().contains(item.expected));
    }
}

TEST_F(Config, ToolchainAndToolsConfigurationUseCommandLineNames) {
    auto project = config("toolchain"_str, toolchain_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("custom-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("custom-cxx"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ld.as_path(), PathBuf::from("custom-ld"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ar.as_path(), PathBuf::from("custom-ar"_str).as_path());
    EXPECT_EQ(loaded->tools.cmake.as_path(), PathBuf::from("custom-cmake"_str).as_path());
    EXPECT_EQ(loaded->tools.tar.as_path(), PathBuf::from("custom-tar"_str).as_path());
    EXPECT_EQ(loaded->tools.bsdtar.as_path(), PathBuf::from("custom-bsdtar"_str).as_path());
    EXPECT_EQ(loaded->tools.clang_format.as_path(), PathBuf::from("custom-format"_str).as_path());
    EXPECT_EQ(loaded->tools.curl.as_path(), PathBuf::from("custom-curl"_str).as_path());
    EXPECT_EQ(loaded->tools.git.as_path(), PathBuf::from("custom-git"_str).as_path());
    EXPECT_EQ(loaded->tools.pkg_config.as_path(), PathBuf::from("custom-pkg-config"_str).as_path());
    EXPECT_EQ(loaded->tools.strip.as_path(), PathBuf::from("custom-strip"_str).as_path());
    EXPECT_TRUE(loaded->tools.explicitly_configured(lito::tools::Tool::CMake));
    EXPECT_TRUE(loaded->tools.explicitly_configured(lito::tools::Tool::PkgConfig));
    EXPECT_EQ(loaded->standard_library, lito::config::StandardLibrarySelection::Libstdcxx);
    EXPECT_EQ(loaded->standard_library_runtime, lito::config::StandardLibraryRuntime::Dynamic);

    auto legacy_project = config("toolchain-legacy"_str, toolchain_legacy_config);
    ASSERT_TRUE(legacy_project.is_ok());
    auto legacy = lito::config::load_project_config(legacy_project->root.as_path());
    EXPECT_TRUE(legacy.is_err());

    auto default_project = empty_project("defaults"_str);
    ASSERT_TRUE(default_project.is_ok());
    auto defaults = lito::config::load_project_config(default_project->root.as_path());
    ASSERT_TRUE(defaults.is_ok());
    EXPECT_EQ(defaults->standard_library, lito::config::StandardLibrarySelection::Auto);
    EXPECT_EQ(defaults->standard_library_runtime, lito::config::StandardLibraryRuntime::Dynamic);
    EXPECT_EQ(defaults->tools.cmake.as_path(), PathBuf::from("cmake"_str).as_path());
    EXPECT_EQ(defaults->tools.pkg_config.as_path(), PathBuf::from("pkg-config"_str).as_path());
    EXPECT_EQ(defaults->cmake.generator.as_str(), "Ninja"_str);
    EXPECT_FALSE(defaults->tools.explicitly_configured(lito::tools::Tool::CMake));
    EXPECT_FALSE(defaults->tools.explicitly_configured(lito::tools::Tool::PkgConfig));

    auto automatic_project = config("toolchain-auto"_str, "[toolchain]\nstdlib = \"auto\"\n"_str);
    ASSERT_TRUE(automatic_project.is_ok());
    auto automatic = lito::config::load_project_config(automatic_project->root.as_path());
    ASSERT_TRUE(automatic.is_ok());
    EXPECT_EQ(automatic->standard_library, lito::config::StandardLibrarySelection::Auto);
}

TEST_F(Config, AndroidTargetAndSdkSelectionAreTyped) {
    auto project = config("android-target"_str, R"toml([build.target]
kind = "android"
abi = "arm64-v8a"
min-api = 21

[toolchain]
stdlib-runtime = "static"

[toolchain.sdk]
kind = "android-ndk"
version = "29.0.14206865"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->build_target.is_Android());
    EXPECT_EQ(loaded->build_target.as_Android().target.abi.as_str(), "arm64-v8a"_str);
    EXPECT_EQ(loaded->build_target.as_Android().target.minimum_api, u32(21));
    EXPECT_EQ(loaded->standard_library_runtime, lito::config::StandardLibraryRuntime::Static);
    ASSERT_TRUE(loaded->toolchain.sdk.is_some());
    ASSERT_TRUE(loaded->toolchain.sdk->is_Managed());
    EXPECT_EQ(loaded->toolchain.sdk->as_Managed().kind, lito::config::SdkKind::AndroidNdk);
    EXPECT_EQ(loaded->toolchain.sdk->as_Managed().version.as_str(), "29.0.14206865"_str);

    constexpr ref<str> invalid[] = {
        "[build.target]\nkind = \"android\"\nabi = \"arm64-v8a\"\n"_str,
        "[build.target]\nkind = \"android\"\nmin-api = 21\n"_str,
        "[build.target]\nkind = \"android\"\nabi = \"arm64-v8a\"\nmin-api = \"21\"\n"_str,
        "[toolchain.sdk]\nkind = \"android-ndk\"\n"_str,
        "[toolchain.sdk]\nkind = \"android-ndk\"\nversion = \"29.0.14206865\"\npath = \"/tmp/ndk\"\n"_str,
        "[toolchain.sdk]\nkind = \"android-ndk\"\npath = \"relative/ndk\"\n"_str,
    };
    auto index = usize {};
    for (const auto contents : invalid) {
        auto invalid_project = config(rstd::format("android-invalid-{}", index).as_str(), contents);
        ASSERT_TRUE(invalid_project.is_ok());
        EXPECT_TRUE(lito::config::load_project_config(invalid_project->root.as_path()).is_err());
        ++index;
    }
}

TEST_F(Config, CallerToolDefaultsRemainBelowProjectAndRuntimeOverrides) {
    auto project = config("caller-tool-defaults"_str,
                          "[toolchain]\n"
                          "cxx = \"project-cxx\"\n"
                          "[tools]\n"
                          "git = \"project-git\"\n"_str);
    ASSERT_TRUE(project.is_ok());
    auto defaults = [] {
        auto tools         = lito::tools::ToolSpec {};
        tools.strip        = PathBuf::from("sdk-strip"_str);
        tools.clang_format = PathBuf::from("sdk-format"_str);
        tools.mark_configured(lito::tools::Tool::Strip);
        tools.mark_configured(lito::tools::Tool::ClangFormat);
        return lito::config::ProjectConfigDefaults {
            .tools = rstd::move(tools),
            .toolchain =
                lito::config::ToolchainSpec {
                    .cc  = PathBuf::from("sdk-cc"_str),
                    .cxx = PathBuf::from("sdk-cxx"_str),
                    .ld  = PathBuf::from("sdk-ld"_str),
                    .ar  = PathBuf::from("sdk-ar"_str),
                },
        };
    };

    auto loaded = lito::config::load_project_config(project->root.as_path(),
                                                    lito::config::ProjectConfigRequest {
                                                        .defaults = Some(defaults()),
                                                    });
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("sdk-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("project-cxx"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ld.as_path(), PathBuf::from("sdk-ld"_str).as_path());
    EXPECT_EQ(loaded->toolchain.ar.as_path(), PathBuf::from("sdk-ar"_str).as_path());
    EXPECT_EQ(loaded->tools.strip.as_path(), PathBuf::from("sdk-strip"_str).as_path());
    EXPECT_EQ(loaded->tools.clang_format.as_path(), PathBuf::from("sdk-format"_str).as_path());
    EXPECT_EQ(loaded->tools.git.as_path(), PathBuf::from("project-git"_str).as_path());
    EXPECT_TRUE(loaded->tools.explicitly_configured(lito::tools::Tool::Strip));

    auto overrides = Vec<String>::make();
    overrides.push(String::make("toolchain.ld=runtime-ld"_str));
    overrides.push(String::make("tools.strip=runtime-strip"_str));
    auto overridden = lito::config::load_project_config(project->root.as_path(),
                                                        lito::config::ProjectConfigRequest {
                                                            .overrides = rstd::move(overrides),
                                                            .defaults  = Some(defaults()),
                                                        });
    ASSERT_TRUE(overridden.is_ok());
    EXPECT_EQ(overridden->toolchain.ld.as_path(), PathBuf::from("runtime-ld"_str).as_path());
    EXPECT_EQ(overridden->tools.strip.as_path(), PathBuf::from("runtime-strip"_str).as_path());

    auto local_disabled =
        lito::config::load_project_config(project->root.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .defaults = Some(defaults()),
                                          });
    ASSERT_TRUE(local_disabled.is_ok());
    EXPECT_EQ(local_disabled->toolchain.cxx.as_path(), PathBuf::from("sdk-cxx"_str).as_path());
    EXPECT_EQ(local_disabled->tools.strip.as_path(), PathBuf::from("sdk-strip"_str).as_path());
}

TEST_F(Config, HostToolCommandConfigDoesNotDecodeProjectOnlyDomains) {
    auto project = config("host-tool-command"_str,
                          "[tools]\n"
                          "curl = \"sdk-curl\"\n"
                          "[toolchain]\n"
                          "ld = \"project-linker\"\n"
                          "[lock]\n"
                          "path = 7\n"_str);
    ASSERT_TRUE(project.is_ok());
    auto command = lito::config::load_host_tool_command_config(
        project->root.as_path(), lito::config::ProjectConfigRequest {});
    ASSERT_TRUE(command.is_ok());
    EXPECT_EQ(command->tools.curl.as_path(), PathBuf::from("sdk-curl"_str).as_path());
    EXPECT_EQ(command->toolchain.ld.as_path(), PathBuf::from("project-linker"_str).as_path());
    EXPECT_TRUE(command->tools.explicitly_configured(lito::tools::Tool::Curl));
    EXPECT_TRUE(lito::config::load_project_config(project->root.as_path()).is_err());

    auto overridden = lito::config::load_host_tool_command_config(
        project->root.as_path(),
        lito::config::ProjectConfigRequest {
            .overrides = strings("toolchain.ld=/usr/bin/ld"_str),
        });
    ASSERT_TRUE(overridden.is_ok());
    EXPECT_EQ(overridden->toolchain.ld.as_path(), PathBuf::from("/usr/bin/ld"_str).as_path());
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
                                "[tools.cmake]\n"
                                "generator = \"Ninja\"\n"
                                "search-path = [\".\"]\n"_str.as_bytes())
                    .is_ok());
    auto local = local_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(local.as_path(),
                                "[toolchain]\n"
                                "cxx = \"local-cxx\"\n"
                                "[tools]\n"
                                "cmake = \"local-cmake\"\n"_str.as_bytes())
                    .is_ok());

    auto loaded = lito::config::load_project_config(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("local-cxx"_str).as_path());
    EXPECT_EQ(loaded->standard_library, lito::config::StandardLibrarySelection::Libstdcxx);
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Ninja"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(), directory.as_path());
    EXPECT_EQ(loaded->tools.cmake.as_path(), PathBuf::from("local-cmake"_str).as_path());
    EXPECT_TRUE(loaded->tools.explicitly_configured(lito::tools::Tool::CMake));

    auto shared_only = lito::config::load_project_config(
        directory.as_path(), lito::config::ConfigLoadMode::LocalDisabled);
    ASSERT_TRUE(shared_only.is_ok());
    EXPECT_EQ(shared_only->toolchain.cxx.as_path(), PathBuf::from("project-cxx"_str).as_path());
    EXPECT_EQ(shared_only->standard_library, lito::config::StandardLibrarySelection::Libstdcxx);
    EXPECT_EQ(shared_only->cmake.generator.as_str(), "Ninja"_str);
    ASSERT_EQ(shared_only->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(shared_only->cmake.search_paths[usize {}].as_path(), directory.as_path());
    EXPECT_FALSE(shared_only->tools.explicitly_configured(lito::tools::Tool::CMake));
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
    EXPECT_EQ(loaded->tools.pkg_config.as_path().to_str().unwrap(), "custom-pkg-config"_str);
    EXPECT_TRUE(loaded->tools.explicitly_configured(lito::tools::Tool::PkgConfig));
    EXPECT_EQ(loaded->pkg_config.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->pkg_config.library_paths.len(), usize(1));
    EXPECT_TRUE(loaded->pkg_config.sysroot.is_some());

    auto search_project = config("pkg-config-search-only"_str, pkg_config_search_only_config);
    ASSERT_TRUE(search_project.is_ok());
    auto search_only = lito::config::load_project_config(search_project->root.as_path());
    ASSERT_TRUE(search_only.is_ok());
    EXPECT_FALSE(search_only->pkg_config.target_configured);
    EXPECT_FALSE(search_only->tools.explicitly_configured(lito::tools::Tool::PkgConfig));
}

TEST_F(Config, CMakeProviderConfigurationBelongsToProjectConfig) {
    auto project = config("cmake"_str, cmake_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->tools.cmake.as_path().to_str().unwrap(), "custom-cmake"_str);
    EXPECT_TRUE(loaded->tools.explicitly_configured(lito::tools::Tool::CMake));
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Unix Makefiles"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(), project->root.as_path());
}

TEST_F(Config, CMakeBuildOverridesBelongToLocalAndInvocationConfiguration) {
    auto project = config("cmake-build-override"_str, cmake_override_config);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_build_overrides.entries.len(), usize(1));
    EXPECT_EQ(loaded->cmake_build_overrides.entries[usize {}].package.as_str(), "Eigen3"_str);

    auto invocation_project = empty_project("cmake-build-override-invocation"_str);
    ASSERT_TRUE(invocation_project.is_ok());
    auto overrides = Vec<String>::make();
    overrides.push(String::make("tools.cmake.overrides.LitoFixture.source=\"installed\""_str));
    auto invocation =
        lito::config::load_project_config(invocation_project->root.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(overrides),
                                          });
    ASSERT_TRUE(invocation.is_ok());
    ASSERT_EQ(invocation->cmake_build_overrides.entries.len(), usize(1));
    EXPECT_EQ(invocation->cmake_build_overrides.entries[usize {}].package.as_str(),
              "LitoFixture"_str);
}

TEST_F(Config, SharedConfigurationCannotDeclareCMakeBuildOverrides) {
    auto project = empty_project("cmake-build-override-shared"_str);
    ASSERT_TRUE(project.is_ok());
    auto shared = project->root.join(PathBuf::from("lito-config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(shared.as_path(), cmake_override_config.as_bytes()).is_ok());

    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_err());
    auto error = rstd::move(loaded).unwrap_err();
    EXPECT_TRUE(
        error_chain_text(error).as_str().contains("cannot contain tools.cmake.overrides"_str));

    auto disabled = lito::config::load_project_config(project->root.as_path(),
                                                      lito::config::ConfigLoadMode::LocalDisabled);
    EXPECT_TRUE(disabled.is_err());
}

TEST_F(Config, InvalidCMakeBuildOverridesAreRejectedByConfigOwner) {
    struct ConfigCase {
        ref<str> name;
        ref<str> contents;
    };
    constexpr ConfigCase cases[] = {
        { "cmake-override-missing-source"_str, "[tools.cmake.overrides.Eigen3]\n"_str },
        { "cmake-override-unknown-source"_str,
          "[tools.cmake.overrides.Eigen3]\nsource = \"managed\"\n"_str },
        { "cmake-override-unknown-field"_str,
          "[tools.cmake.overrides.Eigen3]\nsource = \"installed\"\nfallback = true\n"_str },
        { "cmake-override-unsafe-package"_str,
          "[tools.cmake.overrides.'-Eigen3']\nsource = \"installed\"\n"_str },
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto project = config(item.name, item.contents);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::config::load_project_config(project->root.as_path()).is_err());
    }
}

TEST_F(Config, RuntimeOverridesShareOneSchemaDecode) {
    auto directory        = source_root("config-runtime-overrides"_str);
    auto config_directory = directory.join(PathBuf::from(".lito"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(config_directory.as_path()).is_ok());
    auto config = config_directory.join(PathBuf::from("config.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(config.as_path(),
                                "[toolchain]\n"
                                "cxx = 7\n"
                                "[tools.cmake]\n"
                                "generator = \"Ninja\"\n"_str.as_bytes())
                    .is_ok());

    auto overrides = Vec<String>::make();
    overrides.push(String::make("toolchain.cxx=generic-cxx"_str));
    overrides.push(String::make("toolchain.cc=generic-cc"_str));
    overrides.push(String::make("toolchain.stdlib=libstdc++"_str));
    overrides.push(String::make("toolchain.stdlib-runtime=dynamic"_str));
    overrides.push(String::make("build.options=[\"-pthread\"]"_str));
    overrides.push(String::make("build.c.options=[\"-Wstrict-prototypes\"]"_str));
    overrides.push(String::make("build.linker-options=[\"-Wl,--as-needed\"]"_str));
    overrides.push(String::make("tools.cmake=runtime-cmake"_str));
    overrides.push(String::make("tools.cmake={generator=\"Unix Makefiles\"}"_str));
    overrides.push(String::make("tools.cmake.search-path=[\".\"]"_str));
    overrides.push(String::make("tools.pkg-config.library-path=[\".\"]"_str));
    overrides.push(String::make("tools.pkg-config=runtime-pkg-config"_str));
    auto loaded = lito::config::load_project_config(directory.as_path(),
                                                    lito::config::ProjectConfigRequest {
                                                        .overrides = rstd::move(overrides),
                                                    });
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->toolchain.cc.as_path(), PathBuf::from("generic-cc"_str).as_path());
    EXPECT_EQ(loaded->toolchain.cxx.as_path(), PathBuf::from("generic-cxx"_str).as_path());
    EXPECT_EQ(loaded->standard_library, lito::config::StandardLibrarySelection::Libstdcxx);
    EXPECT_EQ(loaded->standard_library_runtime, lito::config::StandardLibraryRuntime::Dynamic);
    ASSERT_EQ(loaded->build_options.cpp.len(), usize(1));
    EXPECT_EQ(loaded->build_options.cpp[usize {}].source.as_str(), "config.build.options"_str);
    EXPECT_EQ(loaded->build_options.cpp[usize {}].arguments[usize {}].as_str(), "-pthread"_str);
    ASSERT_EQ(loaded->build_options.c.len(), usize(1));
    EXPECT_EQ(loaded->build_options.c[usize {}].arguments[usize {}].as_str(),
              "-Wstrict-prototypes"_str);
    ASSERT_EQ(loaded->build_options.linker.len(), usize(1));
    EXPECT_EQ(loaded->build_options.linker[usize {}].arguments[usize {}].as_str(),
              "-Wl,--as-needed"_str);
    EXPECT_EQ(loaded->tools.cmake.as_path(), PathBuf::from("runtime-cmake"_str).as_path());
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Unix Makefiles"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(), directory.as_path());
    EXPECT_EQ(loaded->tools.pkg_config.as_path(),
              PathBuf::from("runtime-pkg-config"_str).as_path());
    ASSERT_EQ(loaded->pkg_config.library_paths.len(), usize(1));
    EXPECT_EQ(loaded->pkg_config.library_paths[usize {}].as_path(), directory.as_path());

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
    EXPECT_EQ(disabled->standard_library, lito::config::StandardLibrarySelection::Libstdcxx);

    auto invalid_standard_library = Vec<String>::make();
    invalid_standard_library.push(String::make("toolchain.stdlib=unknown"_str));
    auto invalid =
        lito::config::load_project_config(directory.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(invalid_standard_library),
                                          });
    EXPECT_TRUE(invalid.is_err());

    auto msvc_standard_library = Vec<String>::make();
    msvc_standard_library.push(String::make("toolchain.stdlib=msvc"_str));
    auto msvc =
        lito::config::load_project_config(directory.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(msvc_standard_library),
                                          });
    ASSERT_TRUE(msvc.is_ok());
    EXPECT_EQ(msvc->standard_library, lito::config::StandardLibrarySelection::Msvc);

    auto static_runtime = Vec<String>::make();
    static_runtime.push(String::make("toolchain.stdlib-runtime=static"_str));
    auto unsupported =
        lito::config::load_project_config(directory.as_path(),
                                          lito::config::ProjectConfigRequest {
                                              .mode = lito::config::ConfigLoadMode::LocalDisabled,
                                              .overrides = rstd::move(static_runtime),
                                          });
    ASSERT_TRUE(unsupported.is_ok());
    EXPECT_EQ(unsupported->standard_library_runtime, lito::config::StandardLibraryRuntime::Static);

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

TEST_F(Config, EnvironmentFlagsAppendToTheirLanguageAndLinkDomains) {
    auto c_flags   = EnvironmentVariableGuard("CFLAGS"_str, "-DC_ENV=1"_str);
    auto cxx_flags = EnvironmentVariableGuard("CXXFLAGS"_str, "-DCPP_ENV='two words'"_str);
    auto ld_flags  = EnvironmentVariableGuard("LDFLAGS"_str, "-Wl,--gc-sections"_str);
    auto project   = config("environment-build-flags"_str, R"toml([build]
options = ["-DCPP_CONFIG=1"]
linker-options = ["-Wl,--as-needed"]

[build.c]
options = ["-DC_CONFIG=1"]
)toml"_str);
    ASSERT_TRUE(project.is_ok());

    auto ignored = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(ignored.is_ok());
    EXPECT_EQ(ignored->build_options.cpp.len(), usize(1));
    EXPECT_EQ(ignored->build_options.c.len(), usize(1));
    EXPECT_EQ(ignored->build_options.linker.len(), usize(1));

    auto appended = lito::config::load_project_config(
        project->root.as_path(),
        lito::config::ProjectConfigRequest {
            .environment_flags = lito::config::EnvironmentFlagPolicy::Append,
        });
    ASSERT_TRUE(appended.is_ok());
    ASSERT_EQ(appended->build_options.cpp.len(), usize(2));
    EXPECT_EQ(appended->build_options.cpp[usize(1)].source.as_str(), "CXXFLAGS"_str);
    ASSERT_EQ(appended->build_options.cpp[usize(1)].arguments.len(), usize(1));
    EXPECT_EQ(appended->build_options.cpp[usize(1)].arguments[usize {}].as_str(),
              "-DCPP_ENV=two words"_str);
    ASSERT_EQ(appended->build_options.c.len(), usize(2));
    EXPECT_EQ(appended->build_options.c[usize(1)].source.as_str(), "CFLAGS"_str);
    ASSERT_EQ(appended->build_options.linker.len(), usize(2));
    EXPECT_EQ(appended->build_options.linker[usize(1)].source.as_str(), "LDFLAGS"_str);
}

TEST_F(Config, InvalidBuildOptionConfigurationAndEnvironmentAreSourceAware) {
    auto unknown = config("build-options-unknown"_str, "[build.c]\nunknown = true\n"_str);
    ASSERT_TRUE(unknown.is_ok());
    auto unknown_result = lito::config::load_project_config(unknown->root.as_path());
    ASSERT_TRUE(unknown_result.is_err());
    EXPECT_TRUE(error_chain_text(unknown_result.unwrap_err())
                    .as_str()
                    .contains("config.build.c contains unknown field 'unknown'"_str));

    auto invalid = config("build-options-invalid"_str, "[build]\noptions = [\"\"]\n"_str);
    ASSERT_TRUE(invalid.is_ok());
    auto invalid_result = lito::config::load_project_config(invalid->root.as_path());
    ASSERT_TRUE(invalid_result.is_err());
    EXPECT_TRUE(error_chain_text(invalid_result.unwrap_err())
                    .as_str()
                    .contains("config.build.options entries must be non-empty strings"_str));

    auto cxx_flags   = EnvironmentVariableGuard("CXXFLAGS"_str, "-DVALUE='unterminated"_str);
    auto environment = lito::config::load_project_config(
        invalid->root.as_path(),
        lito::config::ProjectConfigRequest {
            .mode              = lito::config::ConfigLoadMode::LocalDisabled,
            .environment_flags = lito::config::EnvironmentFlagPolicy::Append,
        });
    ASSERT_TRUE(environment.is_err());
    auto message = error_chain_text(environment.unwrap_err());
    EXPECT_TRUE(message.as_str().contains("CXXFLAGS"_str));
    EXPECT_TRUE(message.as_str().contains("unclosed quote"_str));
}

TEST_F(Config, NonUtf8EnvironmentFlagsAreRejectedWithTheirVariableName) {
    auto bytes = Vec<u8>::make();
    bytes.push(u8(0xff));
    auto value   = rstd::ffi::OsString::from_encoded_bytes_unchecked(rstd::move(bytes));
    auto c_flags = EnvironmentVariableGuard("CFLAGS"_str, value.as_os_str());
    auto project = empty_project("non-utf8-environment-build-flags"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::config::load_project_config(
        project->root.as_path(),
        lito::config::ProjectConfigRequest {
            .environment_flags = lito::config::EnvironmentFlagPolicy::Append,
        });
    ASSERT_TRUE(loaded.is_err());
    auto message = error_chain_text(loaded.unwrap_err());
    EXPECT_TRUE(message.as_str().contains("CFLAGS"_str));
    EXPECT_TRUE(message.as_str().contains("not valid UTF-8"_str));
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

TEST_F(Config, PersistedHostToolShorthandOnlyUpdatesExecutable) {
    auto project =
        config("persisted-host-tool-shorthand"_str, "[tools]\ncmake = \"initial-cmake\"\n"_str);
    ASSERT_TRUE(project.is_ok());

    auto generator = lito::config::set_persisted_config(
        project->root.as_path(), "tools.cmake.generator"_str, "Ninja"_str);
    ASSERT_TRUE(generator.is_ok());
    auto executable = lito::config::set_persisted_config(
        project->root.as_path(), "tools.cmake"_str, "updated-cmake"_str);
    ASSERT_TRUE(executable.is_ok());

    auto loaded = lito::config::load_project_config(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->tools.cmake.as_path(), PathBuf::from("updated-cmake"_str).as_path());
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Ninja"_str);
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
