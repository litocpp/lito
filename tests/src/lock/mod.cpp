#include <rstd/test/gtest.hpp>

import rstd;
import rstd.serde;
import rstd.test;
import lito.core;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

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

    auto project_with_dependency(ref<str> name)
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

[dependencies.fixture-lock-dependency]
path = "dependency"
visibility = "private"
)toml"_str,
            },
            { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
            {
                "dependency/lito.toml"_str,
                R"toml([package]
name = "fixture-lock-dependency"
version = "1.0.0"

[lib]
name = "fixture-lock-dependency"
module = "fixture.lock.dependency"
archive = "fixture-lock-dependency"
sources = ["lib.cppm"]
)toml"_str,
            },
            { "dependency/lib.cppm"_str, "export module fixture.lock.dependency;\n"_str },
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

TEST_F(Lock, VersionOneUsesPackageNames) {
    auto fixture = project_with_dependency("package-names"_str);
    ASSERT_TRUE(fixture.is_ok());
    auto session = lito::lock::load_lock_session(fixture->root.as_path(), false);
    ASSERT_TRUE(session.is_ok());
    auto options = session->take_resolution_options();
    auto graph = lito::package::resolve_package_graph(fixture->root.as_path(), rstd::move(options));
    ASSERT_TRUE(graph.is_ok());
    auto synchronized = lito::lock::sync_lock(*graph, rstd::move(session).unwrap());
    ASSERT_TRUE(synchronized.is_ok());

    auto lock = rstd::fs::read_to_string(
        fixture->root.join(PathBuf::from("lito.lock"_str).as_path()).as_path());
    ASSERT_TRUE(lock.is_ok());
    EXPECT_TRUE(lock->as_str().contains("version = 1"_str));
    EXPECT_TRUE(lock->as_str().contains("\"fixture-lock-dependency\""_str));
    EXPECT_TRUE(lock->as_str().contains(
        "name = \"fixture-lock\"\nversion = \"1.0.0\"\ndependencies = ["_str));
    EXPECT_FALSE(lock->as_str().contains("lito-pkg-"_str));
    EXPECT_FALSE(lock->as_str().contains("id = "_str));
    EXPECT_FALSE(lock->as_str().contains("source = "_str));
    EXPECT_FALSE(lock->as_str().contains("manifest = "_str));
    EXPECT_FALSE(lock->as_str().contains("[[packages.externals]]"_str));
    EXPECT_FALSE(lock->as_str().contains("runtime-dependencies = "_str));

    auto loaded = lito::lock::load_locked_project(fixture->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    const lito::lock::LockedPackage* root = nullptr;
    for (const auto& package : loaded->packages) {
        if (package.name.as_str() == "fixture-lock"_str) root = rstd::addressof(package);
    }
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->dependencies.len(), usize(1));
    EXPECT_EQ(root->dependencies[usize {}].as_str(), "fixture-lock-dependency"_str);
}

TEST_F(Lock, RegistryWriterUsesIdentityOnly) {
    auto fixture = project_without_lock("registry-writer"_str);
    ASSERT_TRUE(fixture.is_ok());
    auto session = lito::lock::load_lock_session(fixture->root.as_path(), false);
    ASSERT_TRUE(session.is_ok());
    auto options = session->take_resolution_options();
    auto graph = lito::package::resolve_package_graph(fixture->root.as_path(), rstd::move(options));
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    auto& source            = graph->packages[usize {}].source;
    source.kind             = lito::source::PackageSourceKind::Registry;
    source.registry_package = Some(lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .name     = lito::registry::RegistryPackageName::parse("fixture-lock"_str).unwrap(),
    });
    source.registry_version = Some(lito::registry::SemanticVersion::parse("1.0.0"_str).unwrap());
    source.package_checksum =
        Some(lito::registry::PackageChecksum::parse(
                 "0000000000000000000000000000000000000000000000000000000000000000"_str)
                 .unwrap());
    auto synchronized = lito::lock::sync_lock(*graph, rstd::move(session).unwrap());
    ASSERT_TRUE(synchronized.is_ok());

    auto lock = rstd::fs::read_to_string(
        fixture->root.join(PathBuf::from("lito.lock"_str).as_path()).as_path());
    ASSERT_TRUE(lock.is_ok());
    EXPECT_TRUE(lock->as_str().contains(
        "name = \"fixture-lock\"\nversion = \"1.0.0\"\nsource = \"registry+https://registry.example/\"\nchecksum = \"0000000000000000000000000000000000000000000000000000000000000000\""_str));
    EXPECT_FALSE(lock->as_str().contains("fixture-lock@1.0.0"_str));

    auto loaded = lito::lock::load_locked_project(fixture->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->packages.len(), usize(1));
    ASSERT_TRUE(loaded->packages[usize {}].source.is_some());
    EXPECT_TRUE(loaded->packages[usize {}].source->is_Registry());
}

TEST_F(Lock, PackageIdIsRejected) {
    constexpr auto old_lock = R"toml(version = 1

[[packages]]
name = "fixture-lock"
id = "lito-pkg-2e9a5795046686573e28687402dff421efb01ddbb84b85ff00f26ea66732e2f6"
)toml"_str;
    auto           fixture  = project("package-id"_str, old_lock);
    ASSERT_TRUE(fixture.is_ok());
    auto loaded = lito::lock::load_lock_session(fixture->root.as_path(), false);
    ASSERT_TRUE(loaded.is_err());
    auto error = rstd::move(loaded).unwrap_err();
    ASSERT_TRUE(error.is_Data());
    EXPECT_EQ(error.as_Data().source.kind(), rstd::serde::ErrorKind::UnknownField);
}

TEST_F(Lock, VersionOneValidatesResolvedSources) {
    constexpr auto current_lock = R"toml(version = 1

[[packages]]
name = "fixture-lock"
version = "1.0.0"
)toml"_str;
    auto           valid        = project("current"_str, current_lock);
    ASSERT_TRUE(valid.is_ok());
    EXPECT_TRUE(current(valid->root.as_path()));

    constexpr auto registry_lock = R"toml(version = 1

[[packages]]
name = "fixture-lock"
version = "1.0.0"
source = "registry+https://registry.example/"
checksum = "0000000000000000000000000000000000000000000000000000000000000000"
)toml"_str;
    auto           registry      = project("registry-source"_str, registry_lock);
    ASSERT_TRUE(registry.is_ok());
    auto loaded_registry = lito::lock::load_locked_project(registry->root.as_path());
    ASSERT_TRUE(loaded_registry.is_ok());
    ASSERT_TRUE(loaded_registry->packages[usize {}].source.is_some());
    ASSERT_TRUE(loaded_registry->packages[usize {}].source->is_Registry());
    EXPECT_EQ(loaded_registry->packages[usize {}].source->as_Registry().package.name.as_str(),
              "fixture-lock"_str);

    auto registry_reused = lito::lock::load_lock_session(registry->root.as_path(),
                                                         false,
                                                         lito::source::GitResolutionMode::Refresh,
                                                         lito::lock::InvalidLockPolicy::Reject,
                                                         lito::lock::RegistryLockPolicy::Reuse);
    ASSERT_TRUE(registry_reused.is_ok());
    EXPECT_EQ(registry_reused->take_resolution_options().registry_sources.len(), usize(1));
    auto registry_ignored =
        lito::lock::load_lock_session(registry->root.as_path(),
                                      false,
                                      lito::source::GitResolutionMode::ReuseLocked,
                                      lito::lock::InvalidLockPolicy::Reject,
                                      lito::lock::RegistryLockPolicy::Ignore);
    ASSERT_TRUE(registry_ignored.is_ok());
    EXPECT_TRUE(registry_ignored->take_resolution_options().registry_sources.is_empty());

    constexpr auto legacy_registry_lock = R"toml(version = 1

[[packages]]
name = "fixture-lock"
version = "1.0.0"
source = "registry+https://registry.example/"
checksum = "sha256:0000000000000000000000000000000000000000000000000000000000000000"
)toml"_str;
    auto           legacy_registry = project("legacy-registry-checksum"_str, legacy_registry_lock);
    ASSERT_TRUE(legacy_registry.is_ok());
    EXPECT_TRUE(lito::lock::load_locked_project(legacy_registry->root.as_path()).is_ok());

    constexpr auto legacy_archive_lock = R"toml(version = 1

[[packages]]
name = "fixture-lock"
version = "1.0.0"
[[packages.externals]]
name = "archive"
source = "archive+https://example.invalid/archive.tar.gz"
checksum = "sha256:0000000000000000000000000000000000000000000000000000000000000000"
)toml"_str;
    auto           legacy_archive = project("legacy-archive-checksum"_str, legacy_archive_lock);
    ASSERT_TRUE(legacy_archive.is_ok());
    EXPECT_TRUE(lito::lock::load_locked_project(legacy_archive->root.as_path()).is_ok());

    struct InvalidLockCase {
        ref<str> name;
        ref<str> contents;
    };
    constexpr InvalidLockCase invalid[] = {
        { "invalid"_str, "{"_str },
        { "stale"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock-outdated\"\nversion = \"1.0.0\"\n"_str },
        { "future-version"_str, "version = 2\npackages = []\n"_str },
        { "invalid-git-source"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\nsource = \"git+https://example.invalid/repository.git#short\"\n"_str },
        { "dangling-dependency"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\ndependencies = [\"missing-package\"]\n"_str },
        { "duplicate-package"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\n[[packages]]\nname = \"fixture-lock\"\n"_str },
        { "duplicate-external"_str,
          R"toml(version = 1
[[packages]]
name = "fixture-lock"
[[packages.externals]]
name = "archive"
source = "archive+https://example.invalid/archive.tar.gz"
checksum = "0000000000000000000000000000000000000000000000000000000000000000"
[[packages.externals]]
name = "archive"
source = "archive+https://example.invalid/other.tar.gz"
checksum = "1111111111111111111111111111111111111111111111111111111111111111"
)toml"_str },
        { "archive-without-checksum"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\n[[packages.externals]]\nname = \"archive\"\nsource = \"archive+https://example.invalid/archive.tar.gz\"\n"_str },
        { "registry-without-checksum"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\nversion = \"1.0.0\"\nsource = \"registry+https://registry.example/\"\n"_str },
        { "old-registry-coordinate"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\nversion = \"1.0.0\"\nsource = \"registry+https://registry.example/fixture-lock@1.0.0\"\nchecksum = \"0000000000000000000000000000000000000000000000000000000000000000\"\n"_str },
        { "git-with-checksum"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\nsource = \"git+https://example.invalid/repository.git#0000000000000000000000000000000000000000\"\nchecksum = \"sha256:0000000000000000000000000000000000000000000000000000000000000000\"\n"_str },
        { "package-manifest"_str,
          "version = 1\n[[packages]]\nname = \"fixture-lock\"\nmanifest = \"lito.toml\"\n"_str },
        { "root-externals"_str,
          "version = 1\nexternals = []\n[[packages]]\nname = \"fixture-lock\"\n"_str },
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
    EXPECT_TRUE(version_error.as_Schema().message.as_str().contains("supports version 1"_str));
}

TEST_F(Lock, JsonLockIsRebuiltUnlessLockIsRequired) {
    constexpr auto json_lock = R"json({
  "externals": [],
  "packages": [{
    "dependencies": [],
    "manifest": "lito.toml",
    "name": "fixture-lock",
    "runtime-dependencies": [],
    "source": { "kind": "path", "path": "." },
    "version": "1.0.0"
  }],
  "version": 3
})json"_str;
    auto           fixture   = project("json-lock"_str, json_lock);
    ASSERT_TRUE(fixture.is_ok());

    auto strict = lito::lock::load_lock_session(fixture->root.as_path(), true);
    ASSERT_TRUE(strict.is_err());
    auto strict_error = rstd::move(strict).unwrap_err();
    ASSERT_TRUE(strict_error.is_Toml());

    auto session = lito::lock::load_lock_session(fixture->root.as_path(),
                                                 false,
                                                 lito::source::GitResolutionMode::ReuseLocked,
                                                 lito::lock::InvalidLockPolicy::Replace);
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

TEST_F(Lock, InvalidSupportedLockCanBeReplacedButNotUsedAsLocked) {
    constexpr auto invalid_lock = R"toml(version = 1
unexpected = true

[[packages]]
name = "fixture-lock"
)toml"_str;
    auto           fixture      = project("replace-invalid"_str, invalid_lock);
    ASSERT_TRUE(fixture.is_ok());

    auto locked = lito::lock::load_lock_session(fixture->root.as_path(), true);
    ASSERT_TRUE(locked.is_err());
    auto locked_error = rstd::move(locked).unwrap_err_unchecked();
    ASSERT_TRUE(locked_error.is_Data());
    const auto& data = locked_error.as_Data().source;
    EXPECT_EQ(data.kind(), rstd::serde::ErrorKind::UnknownField);
    auto path = data.path().segments();
    ASSERT_EQ(path.len(), usize(1));
    EXPECT_EQ(path[usize {}].name().unwrap(), "unexpected"_str);

    auto update = lito::lock::load_lock_session(fixture->root.as_path(),
                                                false,
                                                lito::source::GitResolutionMode::ReuseLocked,
                                                lito::lock::InvalidLockPolicy::Replace);
    ASSERT_TRUE(update.is_ok());
    auto options = update->take_resolution_options();
    auto graph = lito::package::resolve_package_graph(fixture->root.as_path(), rstd::move(options));
    ASSERT_TRUE(graph.is_ok());
    auto synchronized = lito::lock::sync_lock(*graph, rstd::move(update).unwrap_unchecked());
    ASSERT_TRUE(synchronized.is_ok());
    EXPECT_EQ(*synchronized, lito::lock::LockStatus::Updated);
    EXPECT_TRUE(lito::lock::load_lock_session(fixture->root.as_path(), true).is_ok());
}

TEST_F(Lock, FutureLockCannotBeDowngradedByUpdate) {
    constexpr auto future_lock = "version = 2\npackages = []\n"_str;
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
    EXPECT_TRUE(loading_error.as_Schema().message.as_str().contains("supports version 1"_str));
    auto update_error = rstd::move(update).unwrap_err();
    ASSERT_TRUE(update_error.is_Schema());
    EXPECT_TRUE(update_error.as_Schema().message.as_str().contains("supports version 1"_str));
    auto locked_error = rstd::move(locked).unwrap_err();
    ASSERT_TRUE(locked_error.is_Schema());
    EXPECT_TRUE(locked_error.as_Schema().message.as_str().contains("supports version 1"_str));
}
