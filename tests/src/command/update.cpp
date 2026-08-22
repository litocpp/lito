#include <rstd/test/gtest.hpp>

import rstd;
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

class Update : public ProjectFixture {};

TEST_F(Update, DependencyUpdateReplacesJsonLock) {
    const ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-lock-default"
version = "1.0.0"

[[bin]]
link-stdlib = false
name = "fixture-lock-default"
sources = ["main.cpp"]
)toml"_str,
        },
        { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        {
            "lito.lock"_str,
            R"json({
  "externals": [],
  "packages": [{
    "dependencies": [],
    "manifest": "lito.toml",
    "name": "fixture-lock-default",
    "runtime-dependencies": [],
    "source": { "kind": "path", "path": "." },
    "version": "1.0.0"
  }],
  "version": 3
})json"_str,
        },
    };
    auto project = materialize("default-update"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto updated = lito::update_dependencies(lito::UpdateRequest {
        .root = project->root.clone(),
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::lock::LockStatus::Updated);
}

TEST_F(Update, DependencyUpdateReplacesInvalidCurrentLock) {
    struct InvalidLockCase {
        ref<str> name;
        ref<str> contents;
    };
    constexpr InvalidLockCase invalid[] = {
        {
            "unknown-field"_str,
            R"toml(version = 1

[[packages]]
name = "fixture-invalid-update"
version = "1.0.0"
unexpected = true
)toml"_str,
        },
        { "malformed-toml"_str, "{"_str },
    };
    for (const auto& item : invalid) {
        SCOPED_TRACE(item.name);
        const ProjectFile files[] = {
            {
                "lito.toml"_str,
                R"toml([package]
name = "fixture-invalid-update"
version = "1.0.0"

[[bin]]
link-stdlib = false
name = "fixture-invalid-update"
sources = ["main.cpp"]
)toml"_str,
            },
            { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
            { "lito.lock"_str, item.contents },
        };
        auto project = materialize(item.name, files);
        ASSERT_TRUE(project.is_ok());
        auto updated = lito::update_dependencies(lito::UpdateRequest {
            .root = project->root.clone(),
        });
        ASSERT_TRUE(updated.is_ok());
        EXPECT_EQ(*updated, lito::lock::LockStatus::Updated);
        EXPECT_TRUE(lito::lock::load_locked_project(project->root.as_path()).is_ok());
    }
}

TEST_F(Update, DependencyUpdateWritesConfiguredLocalLock) {
    const ProjectFile files[] = {
        { ".lito/config.toml"_str, "[lock]\npath = \".lito/lito.lock\"\n"_str },
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-config-lock-local"
version = "0.1.0"

[lib]
name = "fixture-config-lock-local"
module = "fixture.config.lock.local"
archive = "fixture.config.lock.local"
sources = ["source.cppm"]
)toml"_str,
        },
        { "source.cppm"_str, "export module fixture.config.lock.local;\n"_str },
    };
    auto project = materialize("config-lock-local"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();

    auto config = lito::config::load_project_config(directory.as_path());
    ASSERT_TRUE(config.is_ok());
    auto configured_lock = config->lock.path.clone();
    auto updated         = lito::update_dependencies(lito::UpdateRequest {
        .root = config->root.clone(),
        .lock = rstd::move(config->lock),
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::lock::LockStatus::Updated);
    auto local_exists = rstd::fs::exists(configured_lock.as_path());
    ASSERT_TRUE(local_exists.is_ok());
    EXPECT_TRUE(*local_exists);
    auto root_lock   = directory.join(rstd::path::PathBuf::from("lito.lock"_str).as_path());
    auto root_exists = rstd::fs::exists(root_lock.as_path());
    ASSERT_TRUE(root_exists.is_ok());
    EXPECT_FALSE(*root_exists);

    auto defaults = lito::config::load_project_config(directory.as_path(),
                                                      lito::config::ConfigLoadMode::LocalDisabled);
    ASSERT_TRUE(defaults.is_ok());
    auto repository_updated = lito::update_dependencies(lito::UpdateRequest {
        .root = defaults->root.clone(),
        .lock = rstd::move(defaults->lock),
    });
    ASSERT_TRUE(repository_updated.is_ok());
    EXPECT_EQ(*repository_updated, lito::lock::LockStatus::Updated);
    root_exists = rstd::fs::exists(root_lock.as_path());
    ASSERT_TRUE(root_exists.is_ok());
    EXPECT_TRUE(*root_exists);
}
