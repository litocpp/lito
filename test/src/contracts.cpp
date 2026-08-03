#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import tenon;
import tenon.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace tenon_test;

namespace
{

inline constexpr ref<str> INVALID_MANIFESTS[] = {
    "manifest/git/multiple-selectors"_str,
    "manifest/git/path-and-git"_str,
    "manifest/git/url-fragment"_str,
    "manifest/package-name-dot"_str,
    "manifest/package-name-empty"_str,
    "manifest/toml-explicit/dependency"_str,
    "manifest/toml-explicit/version-workspace-false"_str,
    "profile/owned-definition"_str,
    "profile/owned-option"_str,
    "workspace/mixed"_str,
};

inline constexpr ref<str> INVALID_EXPLICIT_SOURCES[] = {
    "manifest/toml-explicit/duplicate"_str,
    "manifest/toml-explicit/missing"_str,
    "manifest/toml-explicit/outside-root"_str,
    "manifest/toml-explicit/unsupported"_str,
};

inline constexpr ref<str> INVALID_GRAPHS[] = {
    "resolver/cycle/a"_str,
    "resolver/missing"_str,
    "resolver/name-mismatch/root"_str,
    "resolver/same-name/root"_str,
    "workspace/default-not-member"_str,
    "workspace/duplicate-name"_str,
    "workspace/duplicate"_str,
    "workspace/inherited-version-missing"_str,
    "workspace/missing-member"_str,
    "workspace/nested"_str,
    "workspace/outside"_str,
};

inline constexpr ref<str> INVALID_LOCKS[] = {
    "lock/invalid"_str,
    "lock/missing"_str,
    "lock/stale"_str,
    "lock/v3"_str,
    "lock/v4-dangling"_str,
    "lock/v4-duplicate-name"_str,
    "lock/v4-duplicate-source"_str,
    "lock/v4-id-field"_str,
};

inline constexpr ref<str> VALID_BUILD_CASES[] = {
    "cache/long-path"_str,
    "discovery/preprocess/app"_str,
    "manifest/toml-module/directory-markers"_str,
    "manifest/toml-module/multiple-implementations"_str,
    "scanner/module-kinds"_str,
    "scanner/stdlib-header"_str,
};

inline constexpr ref<str> INVALID_BUILD_CASES[] = {
    "discovery/import-cycle"_str,
    "manifest/toml-module/logical-mismatch"_str,
    "manifest/toml-module/missing-primary"_str,
    "manifest/toml-module/partition-collision"_str,
    "scanner/header-unit"_str,
};

auto locked_graph_is_current(ref<str> relative) -> bool {
    auto directory = root(relative);
    auto session   = tenon::load_lock_session(directory.as_path(), true);
    if (session.is_err()) return false;
    auto options = session->take_resolution_options();
    auto graph   = tenon::resolve_package_graph(directory.as_path(), rstd::move(options));
    if (graph.is_err()) return false;
    return tenon::sync_lock(*graph, rstd::move(session).unwrap()).is_ok();
}

auto executable(const tenon::BuildSummary& summary) -> Option<ref<rstd::path::Path>> {
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == tenon::ArtifactKind::Executable) {
            return Some(artifact.path.as_path());
        }
    }
    return None();
}

} // namespace

TEST(Contracts, InvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_MANIFESTS) {
        auto loaded = tenon::load_manifest_document(root(path).as_path());
        if (loaded.is_ok()) rstd::io::eprintln("unexpected valid manifest: {}", path);
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, RemovedConfigFieldsAreRejectedByConfigOwner) {
    auto loaded = tenon::load_project_config(root("config/removed-scanner"_str).as_path());
    EXPECT_TRUE(loaded.is_err());
}

TEST(Contracts, InvalidDependencyGraphsAreRejectedByResolverOwner) {
    for (const auto path : INVALID_GRAPHS) {
        auto resolved = tenon::resolve_package_graph(root(path).as_path());
        if (resolved.is_ok()) rstd::io::eprintln("unexpected valid graph: {}", path);
        EXPECT_TRUE(resolved.is_err());
    }
}

TEST(Contracts, InvalidExplicitSourcesAreRejectedByDiscoveryOwner) {
    for (const auto path : INVALID_EXPLICIT_SOURCES) {
        auto loaded = tenon::load_package_manifest(root(path).as_path());
        ASSERT_TRUE(loaded.is_ok());
        auto discovered = tenon::discover_explicit_sources(*loaded);
        if (discovered.is_ok()) rstd::io::eprintln("unexpected valid sources: {}", path);
        EXPECT_TRUE(discovered.is_err());
    }
}

TEST(Contracts, LockValidationAndMigrationAreOwnedByLockStore) {
    EXPECT_TRUE(locked_graph_is_current("lock/default-update"_str));
    for (const auto path : INVALID_LOCKS) {
        auto current = locked_graph_is_current(path);
        if (current) rstd::io::eprintln("unexpected current lock: {}", path);
        EXPECT_FALSE(current);
    }
    EXPECT_TRUE(tenon::load_lock_session(root("lock/v3"_str).as_path(), false).is_ok());
}

TEST(Contracts, DiscoveryAndModuleConventionsBuildExpectedCases) {
    auto output = output_root("contracts-build"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    for (const auto path : VALID_BUILD_CASES) {
        auto directory = root(path);
        auto built = tenon::build(
            build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_err()) rstd::io::eprintln("unexpected build failure: {}", path);
        EXPECT_TRUE(built.is_ok());
    }
    for (const auto path : INVALID_BUILD_CASES) {
        auto directory = root(path);
        auto built = tenon::build(
            build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_ok()) rstd::io::eprintln("unexpected build success: {}", path);
        EXPECT_TRUE(built.is_err());
    }
    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Contracts, BuildProfileOwnsOptimizationAndDebugDefinitions) {
    auto directory = root("profile"_str);
    auto output    = output_root("profile"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto debug = tenon::build(build_request(directory.as_path(),
                                            output.as_path(),
                                            Vec<String>::make()));
    ASSERT_TRUE(debug.is_ok());
    auto debug_executable = executable(*debug);
    ASSERT_TRUE(debug_executable.is_some());
    auto debug_status = rstd::process::Command::make((*debug_executable).as_os_str())
                            .current_dir(directory.as_path())
                            .status();
    ASSERT_TRUE(debug_status.is_ok());
    ASSERT_TRUE(debug_status->code().is_some());
    EXPECT_EQ(*debug_status->code(), i32(1));

    auto release = tenon::build(build_request(directory.as_path(),
                                              output.as_path(),
                                              Vec<String>::make(),
                                              tenon::BuildProfile::Release));
    ASSERT_TRUE(release.is_ok());
    auto release_executable = executable(*release);
    ASSERT_TRUE(release_executable.is_some());
    auto release_status = rstd::process::Command::make((*release_executable).as_os_str())
                              .current_dir(directory.as_path())
                              .status();
    ASSERT_TRUE(release_status.is_ok());
    EXPECT_TRUE(release_status->success());
    EXPECT_TRUE(clear_output(output.as_path()));
}
