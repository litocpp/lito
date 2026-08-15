module;

#include <rstd/macro.hpp>

export module lito.test.support.cases;

import rstd;
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
import lito.test.base_support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
inline constexpr ref<str> INVALID_MANIFESTS[] = {
    "manifest/empty-target-array"_str,
    "manifest/discovery-field"_str,
    "manifest/git/commit-invalid"_str,
    "manifest/git/multiple-selectors"_str,
    "manifest/git/path-and-git"_str,
    "manifest/git/url-fragment"_str,
    "manifest/build-tools/duplicate-host"_str,
    "manifest/build-tools/invalid-executable"_str,
    "manifest/build-tools/invalid-resource"_str,
    "manifest/build-tools/invalid-version"_str,
    "manifest/build-tools/unknown-field"_str,
    "manifest/legacy-target-schema"_str,
    "manifest/package-name-dot"_str,
    "manifest/package-name-empty"_str,
    "manifest/package-version-missing"_str,
    "manifest/profile/cycle"_str,
    "manifest/profile/linker-owned"_str,
    "manifest/profile/nested"_str,
    "manifest/profile/type"_str,
    "manifest/public-usage-without-lib"_str,
    "install/manifest/runtime/path-and-git"_str,
    "install/manifest/runtime/unknown-field"_str,
    "install/manifest/runtime/visibility"_str,
    "install/manifest/runtime/workspace-and-path"_str,
    "manifest/source-root-descendant"_str,
    "manifest/target-link-stdlib-type"_str,
    "manifest/test-attach-unknown-key"_str,
    "manifest/toml-explicit/dependency"_str,
    "manifest/toml-explicit/version-workspace-false"_str,
    "build/profile/owned-definition"_str,
    "workspace/cmake-definition-targets"_str,
    "workspace/dependency-definition-visibility"_str,
    "workspace/dependency-reference-mixed"_str,
    "workspace/mixed"_str,
};

inline constexpr ref<str> INVALID_PKG_CONFIG_MANIFESTS[] = {
    "dependency/pkg-config/manifest/legacy-dependency"_str,
    "dependency/pkg-config/manifest/path-version"_str,
    "dependency/pkg-config/manifest/selector"_str,
    "dependency/pkg-config/manifest/source-mix"_str,
    "dependency/pkg-config/manifest/static-type"_str,
    "dependency/pkg-config/manifest/version"_str,
};

inline constexpr ref<str> INVALID_CMAKE_MANIFESTS[] = {
    "dependency/cmake/manifest/adapter-install"_str,
    "dependency/cmake/manifest/archive-missing-sha"_str,
    "dependency/cmake/manifest/build-tree-installed"_str,
    "dependency/cmake/manifest/config-directory-parent"_str,
    "dependency/cmake/manifest/duplicate-target"_str,
    "dependency/cmake/manifest/empty-targets"_str,
    "dependency/cmake/manifest/installed-cache"_str,
    "dependency/cmake/manifest/installed-config-directory"_str,
    "dependency/cmake/manifest/legacy-dependency"_str,
    "dependency/cmake/manifest/missing-target"_str,
    "dependency/cmake/manifest/provider-mix"_str,
    "dependency/cmake/manifest/unsafe-target"_str,
};

inline constexpr ref<str> INVALID_CMAKE_ARCHITECTURE_ARCHIVE_MANIFESTS[] = {
    "dependency/cmake/manifest/archives-empty"_str,
    "dependency/cmake/manifest/archives-git-mix"_str,
    "dependency/cmake/manifest/archives-install"_str,
    "dependency/cmake/manifest/archives-invalid-sha"_str,
    "dependency/cmake/manifest/archives-invalid-url"_str,
    "dependency/cmake/manifest/archives-missing-archive"_str,
    "dependency/cmake/manifest/archives-missing-sha"_str,
    "dependency/cmake/manifest/archives-noncanonical-architecture"_str,
    "dependency/cmake/manifest/archives-path-mix"_str,
    "dependency/cmake/manifest/archives-source-mix"_str,
    "dependency/cmake/manifest/archives-unknown-field"_str,
    "dependency/cmake/manifest/archives-unsafe-architecture"_str,
};

inline constexpr ref<str> INVALID_EXPLICIT_SOURCES[] = {
    "manifest/toml-explicit/duplicate"_str,
    "manifest/toml-explicit/missing"_str,
    "manifest/toml-explicit/outside-root"_str,
    "manifest/toml-explicit/unsupported"_str,
};

inline constexpr ref<str> INVALID_GRAPHS[] = {
    "workspace/convention/test/invalid-kind"_str,
    "workspace/convention/test/invalid-name"_str,
    "workspace/convention/test/invalid-overlap"_str,
    "workspace/convention/test/invalid-profile"_str,
    "workspace/convention/test/invalid-version"_str,
    "workspace/convention/test/invalid-workspace-kind"_str,
    "package/resolver/cycle/a"_str,
    "package/resolver/missing"_str,
    "package/resolver/name-mismatch/root"_str,
    "package/resolver/runtime-cycle"_str,
    "package/resolver/same-name/root"_str,
    "workspace/default-not-member"_str,
    "workspace/duplicate-name"_str,
    "workspace/duplicate"_str,
    "workspace/inherited-dependency-missing"_str,
    "workspace/inherited-dependency-outside"_str,
    "workspace/inherited-runtime-dependency-missing"_str,
    "workspace/inherited-version-missing"_str,
    "workspace/member-profile"_str,
    "workspace/missing-member"_str,
    "workspace/nested"_str,
    "workspace/outside"_str,
};

inline constexpr ref<str> INVALID_LOCKS[] = {
    "lock/invalid"_str,
    "lock/missing"_str,
    "lock/stale"_str,
    "lock/old-version"_str,
    "lock/git-reference-mismatch"_str,
    "lock/dangling-dependency"_str,
    "lock/duplicate-package"_str,
    "lock/duplicate-external"_str,
    "lock/unknown-field"_str,
};

inline constexpr ref<str> VALID_BUILD_CASES[] = {
    "cache/long-path"_str,
    "modules/discovery/preprocess/app"_str,
    "manifest/multiple-primary-modules"_str,
    "manifest/toml-module/directory-markers"_str,
    "manifest/toml-module/multiple-implementations"_str,
    "modules/scanner/module-kinds"_str,
    "modules/scanner/stdlib-header"_str,
    "workspace/shared-source-root"_str,
};

inline constexpr ref<str> INVALID_BUILD_CASES[] = {
    "modules/discovery/import-cycle"_str,       "manifest/toml-module/logical-mismatch"_str,
    "manifest/toml-module/missing-primary"_str, "manifest/toml-module/partition-collision"_str,
    "modules/scanner/header-unit"_str,
};
} // namespace lito_test
