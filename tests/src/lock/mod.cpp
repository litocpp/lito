#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

class Lock : public ProjectFixture {
protected:
    auto project(ref<str> name, ref<str> lock)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
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
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
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
        auto session = lito::lock::load_lock_session(directory, true);
        if (session.is_err()) return false;
        auto options = session->take_resolution_options();
        auto graph   = lito::package::resolve_package_graph(directory, rstd::move(options));
        if (graph.is_err()) return false;
        return lito::lock::sync_lock(*graph, rstd::move(session).unwrap()).is_ok();
    }
};

TEST_F(Lock, VersionTwoUsesPackageOwnedExternalSources) {
    constexpr auto current_lock = R"json({
  "packages": [{
    "dependencies": [],
    "externals": [],
    "manifest": "lito.toml",
    "name": "fixture-lock",
    "runtime-dependencies": [],
    "source": { "kind": "path", "path": "." },
    "version": "1.0.0"
  }],
  "version": 2
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
        { "stale"_str,
          R"json({"packages":[{"dependencies":[],"externals":[],"manifest":"lito.toml","name":"fixture-lock-outdated","runtime-dependencies":[],"source":{"kind":"path","path":"."},"version":"1.0.0"}],"version":2})json"_str },
        { "future-version"_str, R"json({"packages":[],"version":4})json"_str },
        { "git-reference-mismatch"_str,
          R"json({"packages":[{"dependencies":[],"externals":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"commit":"0000000000000000000000000000000000000001","kind":"git","reference":{"kind":"commit","value":"1111111111111111111111111111111111111111"},"url":"https://example.invalid/repository.git"}}],"version":2})json"_str },
        { "dangling-dependency"_str,
          R"json({"packages":[{"dependencies":["missing-package"],"externals":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":2})json"_str },
        { "duplicate-package"_str,
          R"json({"packages":[{"dependencies":[],"externals":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}},{"dependencies":[],"externals":[],"manifest":"other/lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"other"}}],"version":2})json"_str },
        { "duplicate-external"_str,
          R"json({"packages":[{"dependencies":[],"externals":[{"name":"archive","source":{"kind":"archive","sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://example.invalid/archive.tar.gz"}},{"name":"archive","source":{"kind":"archive","sha256":"1111111111111111111111111111111111111111111111111111111111111111","url":"https://example.invalid/other.tar.gz"}}],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":2})json"_str },
        { "unsafe-package-external"_str,
          R"json({"packages":[{"dependencies":[],"externals":[{"name":"shader","source":{"kind":"package","path":"../shaders"}}],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":2})json"_str },
        { "root-externals"_str, R"json({"externals":[],"packages":[{"dependencies":[],"externals":[],"manifest":"lito.toml","name":"fixture-lock","runtime-dependencies":[],"source":{"kind":"path","path":"."}}],"version":2})json"_str },
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
    auto loaded = lito::lock::load_lock_session(future_version->root.as_path(), false);
    ASSERT_TRUE(loaded.is_err());
    auto version_error = rstd::move(loaded).unwrap_err();
    ASSERT_TRUE(version_error.is_Schema());
    EXPECT_TRUE(version_error.as_Schema().message.as_str().contains("supports version 3"_str));
}

TEST_F(Lock, VersionOneIsRebuiltUnlessLockIsRequired) {
    constexpr auto version_one = R"json({
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
    auto           fixture     = project("version-one"_str, version_one);
    ASSERT_TRUE(fixture.is_ok());

    auto strict = lito::lock::load_lock_session(fixture->root.as_path(), true);
    ASSERT_TRUE(strict.is_err());
    auto strict_error = rstd::move(strict).unwrap_err();
    ASSERT_TRUE(strict_error.is_Schema());
    EXPECT_TRUE(strict_error.as_Schema().message.as_str().contains("run 'lito update'"_str));

    auto session = lito::lock::load_lock_session(fixture->root.as_path(), false);
    ASSERT_TRUE(session.is_ok());
    auto options = session->take_resolution_options();
    EXPECT_TRUE(options.git_sources.is_empty());
    auto graph = lito::package::resolve_package_graph(fixture->root.as_path(), rstd::move(options));
    ASSERT_TRUE(graph.is_ok());
    auto synchronized = lito::lock::sync_lock(*graph, rstd::move(session).unwrap());
    ASSERT_TRUE(synchronized.is_ok());
    EXPECT_EQ(*synchronized, lito::lock::LockStatus::Updated);
    auto rebuilt = lito::lock::load_locked_project(fixture->root.as_path());
    ASSERT_TRUE(rebuilt.is_ok());
    ASSERT_EQ(rebuilt->packages.len(), usize(1));
    EXPECT_TRUE(rebuilt->packages[usize {}].externals.is_empty());
    EXPECT_TRUE(lito::lock::load_lock_session(fixture->root.as_path(), true).is_ok());
}

TEST_F(Lock, FutureLockCannotBeDowngradedByUpdate) {
    constexpr auto future_lock = R"json({"packages":[],"version":4})json"_str;
    auto           fixture     = project("future-version"_str, future_lock);
    ASSERT_TRUE(fixture.is_ok());
    auto loading = lito::lock::load_lock_session(fixture->root.as_path(), false);
    ASSERT_TRUE(loading.is_err());
    auto update = lito::lock::load_lock_session(fixture->root.as_path(),
                                                lito::lock::LockConfig {},
                                                false,
                                                lito::source::GitResolutionMode::Refresh,
                                                lito::lock::InvalidLockPolicy::Replace);
    ASSERT_TRUE(update.is_err());
    auto locked = lito::lock::load_lock_session(fixture->root.as_path(), true);
    ASSERT_TRUE(locked.is_err());
    auto loading_error = rstd::move(loading).unwrap_err();
    ASSERT_TRUE(loading_error.is_Schema());
    EXPECT_TRUE(loading_error.as_Schema().message.as_str().contains("supports version 3"_str));
    auto update_error = rstd::move(update).unwrap_err();
    ASSERT_TRUE(update_error.is_Schema());
    EXPECT_TRUE(update_error.as_Schema().message.as_str().contains("supports version 3"_str));
    auto locked_error = rstd::move(locked).unwrap_err();
    ASSERT_TRUE(locked_error.is_Schema());
    EXPECT_TRUE(locked_error.as_Schema().message.as_str().contains("supports version 3"_str));
}
