#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

class Lock : public ProjectFixture {
protected:
    auto project(ref<str> name, ref<str> lock)
        -> lito::SourceTreeResult<lito::SourceMaterialization> {
        const ProjectFile files[] = {
            {
                "lito.toml"_str,
                R"toml([package]
name = "fixture-lock"
version = "1.0.0"

[[bin]]
link-stdlib = false
name = "fixture-lock"
sources = ["main.cpp"]
)toml"_str,
            },
            { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
            { "lito.lock"_str, lock },
        };
        return materialize(name, files);
    }

    auto project_without_lock(ref<str> name)
        -> lito::SourceTreeResult<lito::SourceMaterialization> {
        const ProjectFile files[] = {
            {
                "lito.toml"_str,
                R"toml([package]
name = "fixture-lock"
version = "1.0.0"

[[bin]]
link-stdlib = false
name = "fixture-lock"
sources = ["main.cpp"]
)toml"_str,
            },
            { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        };
        return materialize(name, files);
    }

    auto current(ref<rstd::path::Path> directory) -> bool {
        auto session = lito::load_lock_session(directory, true);
        if (session.is_err()) return false;
        auto options = session->take_resolution_options();
        auto graph   = lito::resolve_package_graph(directory, rstd::move(options));
        if (graph.is_err()) return false;
        return lito::sync_lock(*graph, rstd::move(session).unwrap()).is_ok();
    }
};

TEST_F(Lock, OnlyCurrentLockVersionIsAcceptedByLockStore) {
    constexpr auto current_lock = R"json({
  "externals": [],
  "packages": [{
    "dependencies": [],
    "manifest": "lito.toml",
    "name": "fixture-lock",
    "runtime-dependencies": [],
    "source": { "kind": "path", "path": "." },
    "version": "1.0.0"
  }],
  "version": 1
})json"_str;
    auto           valid        = project("current"_str, current_lock);
    ASSERT_TRUE(valid.is_ok());
    EXPECT_TRUE(current(valid->root.as_path()));

    struct InvalidLockCase {
        ref<str> name;
        ref<str> contents;
    };
    constexpr InvalidLockCase invalid[] = {
        { "invalid"_str, "{"_str },
        {
            "stale"_str,
            R"json({"externals":[],"packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock-outdated","runtime-dependencies":[],"source":{"kind":"path","path":"."},"version":"1.0.0"}],"version":1})json"_str,
        },
        {
            "future-version"_str,
            R"json({"externals":[],"packages":[],"version":2})json"_str,
        },
        {
            "git-reference-mismatch"_str,
            R"json({"externals":[],"packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"commit":"0000000000000000000000000000000000000001","kind":"git","reference":{"kind":"commit","value":"1111111111111111111111111111111111111111"},"url":"https://example.invalid/repository.git"}}],"version":1})json"_str,
        },
        {
            "dangling-dependency"_str,
            R"json({"externals":[],"packages":[{"dependencies":["missing-package"],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":1})json"_str,
        },
        {
            "duplicate-package"_str,
            R"json({"externals":[],"packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}},{"dependencies":[],"manifest":"other/lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"other"}}],"version":1})json"_str,
        },
        {
            "duplicate-external"_str,
            R"json({"externals":[{"alias":"archive","package":"fixture-lock","provider":"cmake","source":{"kind":"archive","sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://example.invalid/archive.tar.gz"}},{"alias":"archive","package":"fixture-lock","provider":"cmake","source":{"kind":"archive","sha256":"1111111111111111111111111111111111111111111111111111111111111111","url":"https://example.invalid/other.tar.gz"}}],"packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":1})json"_str,
        },
        {
            "missing-package-external"_str,
            R"json({"externals":[{"alias":"shader","package":"missing","provider":"cmake","source":{"kind":"package","path":"shaders"}}],"packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":1})json"_str,
        },
        {
            "unsafe-package-external"_str,
            R"json({"externals":[{"alias":"shader","package":"fixture-lock","provider":"cmake","source":{"kind":"package","path":"../shaders"}}],"packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":1})json"_str,
        },
        {
            "unknown-field"_str,
            R"json({"externals":[],"id":"legacy-lock-id","packages":[{"dependencies":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":1})json"_str,
        },
    };
    for (const auto& item : invalid) {
        SCOPED_TRACE(item.name);
        auto fixture = project(item.name, item.contents);
        ASSERT_TRUE(fixture.is_ok());
        EXPECT_FALSE(current(fixture->root.as_path()));
    }

    auto missing = project_without_lock("missing"_str);
    ASSERT_TRUE(missing.is_ok());
    EXPECT_FALSE(current(missing->root.as_path()));

    auto future_version = project("future-version-error"_str, invalid[2].contents);
    ASSERT_TRUE(future_version.is_ok());
    auto loaded = lito::load_lock_session(future_version->root.as_path(), false);
    ASSERT_TRUE(loaded.is_err());
    auto version_error = rstd::move(loaded).unwrap_err();
    ASSERT_TRUE(version_error.is_Schema());
    EXPECT_TRUE(version_error.as_Schema().message.as_str().contains("supports version 1"_str));
}

TEST_F(Lock, FutureLockCannotBeDowngradedByUpdate) {
    constexpr auto future_lock = R"json({
  "externals": [],
  "packages": [],
  "version": 2
})json"_str;
    auto           fixture     = project("future-version"_str, future_lock);
    ASSERT_TRUE(fixture.is_ok());
    auto loading = lito::load_lock_session(fixture->root.as_path(), false);
    ASSERT_TRUE(loading.is_err());
    auto update = lito::load_lock_session(
        fixture->root.as_path(), lito::LockConfig {}, false, lito::GitResolutionMode::Refresh);
    ASSERT_TRUE(update.is_err());
    auto locked = lito::load_lock_session(fixture->root.as_path(), true);
    ASSERT_TRUE(locked.is_err());
    auto loading_error = rstd::move(loading).unwrap_err();
    ASSERT_TRUE(loading_error.is_Schema());
    EXPECT_TRUE(loading_error.as_Schema().message.as_str().contains("supports version 1"_str));
    auto update_error = rstd::move(update).unwrap_err();
    ASSERT_TRUE(update_error.is_Schema());
    EXPECT_TRUE(update_error.as_Schema().message.as_str().contains("supports version 1"_str));
    auto locked_error = rstd::move(locked).unwrap_err();
    ASSERT_TRUE(locked_error.is_Schema());
    EXPECT_TRUE(locked_error.as_Schema().message.as_str().contains("supports version 1"_str));
}
