#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import luato;
import lito.core;
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

class PackageResolver : public ProjectFixture {};

auto pmacro_source_options(ref<rstd::path::Path> support) -> lito::source::SourceResolutionOptions {
    auto options = lito::source::SourceResolutionOptions {};
    options.sources.builtin_packages.push(lito::source::BuiltinPackageSourceEntry {
        .id     = String::make("pmacro"_str),
        .source = lito::source::BuiltinPackageSource::Path(PathBuf::from(support)),
    });
    return options;
}

using GraphFactory = lito::source::SourceTreeResult<lito::source::SourceTree> (*)();

auto workspace_convention_test_invalid_kind_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-kind-root"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-conventional-invalid-kind-root"
sources = ["main.cpp"]
)graph"_str },
        { "tests/lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-kind-test"
version = "0.1.0"

[lib]
name = "fixture-conventional-invalid-kind-test"
module = "fixture.conventional.invalid_kind"
archive = "fixture-conventional-invalid-kind"
sources = ["lib.cppm"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_convention_test_invalid_name_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-conventional-name"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-conventional-name"
sources = ["main.cpp"]
)graph"_str },
        { "tests/lito.toml"_str, R"graph([package]
name = "fixture-conventional-name"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-conventional-name-test"
sources = ["main.cpp"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_convention_test_invalid_overlap_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "library/lito.toml"_str, R"graph([package]
name = "fixture-conventional-overlap-library"
version.workspace = true

[lib]
name = "fixture-conventional-overlap-library"
module = "fixture.conventional.overlap"
archive = "fixture-conventional-overlap"
sources = ["lib.cppm"]
)graph"_str },
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-conventional-overlap-root"
members = ["library", "tests/runtime"]

[workspace.package]
version = "0.1.0"
)graph"_str },
        { "tests/lito.toml"_str, R"graph([workspace]
name = "fixture-conventional-overlap-tests"
members = ["runtime"]

[workspace.package]
version = "0.1.0"
)graph"_str },
        { "tests/runtime/lito.toml"_str, R"graph([package]
name = "fixture-conventional-overlap-test"
version.workspace = true

[[test]]
link-stdlib = false
name = "fixture-conventional-overlap-test"
sources = ["main.cpp"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_convention_test_invalid_profile_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-profile-root"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-conventional-invalid-profile-root"
sources = ["main.cpp"]
)graph"_str },
        { "tests/lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-profile-test"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-conventional-invalid-profile-test"
sources = ["main.cpp"]

[profile]
exceptions = false
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_convention_test_invalid_version_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-version-root"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-conventional-invalid-version-root"
sources = ["main.cpp"]
)graph"_str },
        { "tests/lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-version-test"
version.workspace = true

[[test]]
link-stdlib = false
name = "fixture-conventional-invalid-version-test"
sources = ["main.cpp"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_convention_test_invalid_workspace_kind_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-workspace-root"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-conventional-invalid-workspace-root"
sources = ["main.cpp"]
)graph"_str },
        { "tests/library/lito.toml"_str, R"graph([package]
name = "fixture-conventional-invalid-test-library"
version.workspace = true

[lib]
name = "fixture-conventional-invalid-test-library"
module = "fixture.conventional.invalid_test_library"
archive = "fixture-conventional-invalid-test-library"
sources = ["lib.cppm"]
)graph"_str },
        { "tests/lito.toml"_str, R"graph([workspace]
name = "fixture-conventional-invalid-test-workspace"
members = ["library"]

[workspace.package]
version = "0.1.0"
)graph"_str },
    };
    return source_tree(files);
}

auto package_resolver_cycle_a_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "a/lito.toml"_str, R"graph([package]
name = "fixture-cycle-a"
version = "0.1.0"

[lib]
name = "fixture-cycle-a"
module = "fixture.cycle.a"
archive = "fixture.cycle.a"
sources = ["source.cppm"]

[dependencies.fixture-cycle-b]
path = "../b"
visibility = "private"
)graph"_str },
        { "b/lito.toml"_str, R"graph([package]
name = "fixture-cycle-b"
version = "0.1.0"

[lib]
name = "fixture-cycle-b"
module = "fixture.cycle.b"
archive = "fixture.cycle.b"
sources = ["source.cppm"]

[dependencies.fixture-cycle-a]
path = "../a"
visibility = "private"
)graph"_str },
    };
    return source_tree(files);
}

auto package_resolver_missing_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-resolver-missing"
version = "0.1.0"

[lib]
name = "fixture-resolver-missing"
module = "fixture.resolver.missing"
archive = "fixture.resolver.missing"
sources = ["source.cppm"]

[dependencies.missing]
path = "../does-not-exist"
visibility = "private"
)graph"_str },
    };
    return source_tree(files);
}

auto package_resolver_name_mismatch_root_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "actual/lito.toml"_str, R"graph([package]
name = "fixture-actual"
version = "0.1.0"

[lib]
name = "fixture-actual"
module = "fixture.actual"
archive = "fixture.actual"
sources = ["source.cppm"]
)graph"_str },
        { "root/lito.toml"_str, R"graph([package]
name = "fixture-name-mismatch-root"
version = "0.1.0"

[lib]
name = "fixture-name-mismatch-root"
module = "fixture.name.mismatch.root"
archive = "fixture.name.mismatch.root"
sources = ["source.cppm"]

[dependencies.fixture-expected]
path = "../actual"
visibility = "private"
)graph"_str },
    };
    return source_tree(files);
}

auto package_resolver_runtime_cycle_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "a/lito.toml"_str, R"graph([package]
name = "runtime-cycle-a"
version.workspace = true

[[bin]]
link-stdlib = false
name = "runtime-cycle-a"
sources = ["main.cpp"]

[runtime-dependencies.runtime-cycle-b]
workspace = true
)graph"_str },
        { "a/main.cpp"_str, R"graph(int main() { return 0; }
)graph"_str },
        { "b/lito.toml"_str, R"graph([package]
name = "runtime-cycle-b"
version.workspace = true

[[bin]]
link-stdlib = false
name = "runtime-cycle-b"
sources = ["main.cpp"]

[runtime-dependencies.runtime-cycle-a]
workspace = true
)graph"_str },
        { "b/main.cpp"_str, R"graph(int main() { return 0; }
)graph"_str },
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-runtime-cycle"
members = ["a", "b"]

[workspace.package]
version = "1.0.0"

[workspace.dependencies.runtime-cycle-a]
path = "a"

[workspace.dependencies.runtime-cycle-b]
path = "b"
)graph"_str },
    };
    return source_tree(files);
}

auto package_resolver_same_name_root_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "one/lito.toml"_str, R"graph([package]
name = "fixture-same-dependency"
version = "0.1.0"

[lib]
name = "fixture-same-dependency"
module = "fixture.same.dependency"
archive = "fixture.same.one"
sources = ["source.cppm"]
)graph"_str },
        { "root/lito.toml"_str, R"graph([package]
name = "fixture-same-root"
version = "0.1.0"

[lib]
name = "fixture-same-root"
module = "fixture.same.root"
archive = "fixture.same.root"
sources = ["source.cppm"]

[dependencies.fixture-same-wrapper-one]
path = "../wrapper-one"
visibility = "private"

[dependencies.fixture-same-wrapper-two]
path = "../wrapper-two"
visibility = "private"
)graph"_str },
        { "two/lito.toml"_str, R"graph([package]
name = "fixture-same-dependency"
version = "0.2.0"

[lib]
name = "fixture-same-dependency"
module = "fixture.same.dependency"
archive = "fixture.same.two"
sources = ["source.cppm"]
)graph"_str },
        { "wrapper-one/lito.toml"_str, R"graph([package]
name = "fixture-same-wrapper-one"
version = "0.1.0"

[lib]
name = "fixture-same-wrapper-one"
module = "fixture.same.wrapper.one"
archive = "fixture.same.wrapper.one"
sources = ["source.cppm"]

[dependencies.fixture-same-dependency]
path = "../one"
visibility = "private"
)graph"_str },
        { "wrapper-two/lito.toml"_str, R"graph([package]
name = "fixture-same-wrapper-two"
version = "0.1.0"

[lib]
name = "fixture-same-wrapper-two"
module = "fixture.same.wrapper.two"
archive = "fixture.same.wrapper.two"
sources = ["source.cppm"]

[dependencies.fixture-same-dependency]
path = "../two"
visibility = "private"
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_default_not_member_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-default-not-member"
members = ["member"]
default-members = ["other"]
)graph"_str },
        { "member/lito.toml"_str, R"graph([package]
name = "fixture-workspace-member"
version = "0.1.0"

[lib]
name = "fixture-workspace-member"
module = "fixture.workspace.member"
archive = "fixture.workspace.member"
sources = ["source.cppm"]
)graph"_str },
        { "other/lito.toml"_str, R"graph([package]
name = "fixture-workspace-other"
version = "0.1.0"

[lib]
name = "fixture-workspace-other"
module = "fixture.workspace.other"
archive = "fixture.workspace.other"
sources = ["source.cppm"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_duplicate_name_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-duplicate-name"
members = ["one", "two"]

[workspace.package]
version = "0.1.0"
)graph"_str },
        { "one/lito.toml"_str, R"graph([package]
name = "fixture-workspace-duplicate-name"
version.workspace = true

[lib]
name = "fixture-workspace-duplicate-name"
module = "fixture.workspace.duplicate_name.one"
archive = "fixture.workspace.duplicate_name.one"
sources = ["source.cppm"]
)graph"_str },
        { "two/lito.toml"_str, R"graph([package]
name = "fixture-workspace-duplicate-name"
version.workspace = true

[lib]
name = "fixture-workspace-duplicate-name"
module = "fixture.workspace.duplicate_name.two"
archive = "fixture.workspace.duplicate_name.two"
sources = ["source.cppm"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_duplicate_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-duplicate-member"
members = ["member", "./member"]
)graph"_str },
        { "member/lito.toml"_str, R"graph([package]
name = "fixture-workspace-duplicate"
version = "0.1.0"

[lib]
name = "fixture-workspace-duplicate"
module = "fixture.workspace.duplicate"
archive = "fixture.workspace.duplicate"
sources = ["source.cppm"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_inherited_dependency_missing_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "app/lito.toml"_str, R"graph([package]
name = "fixture-workspace-inherited-missing-app"
version.workspace = true

[[bin]]
link-stdlib = false
name = "fixture-workspace-inherited-missing-app"
sources = ["main.cpp"]

[dependencies.missing]
workspace = true
visibility = "private"
)graph"_str },
        { "app/main.cpp"_str, R"graph(int main() { return 0; }
)graph"_str },
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-workspace-inherited-missing"
members = ["app"]

[workspace.package]
version = "0.1.0"
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_inherited_dependency_outside_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([package]
name = "fixture-workspace-inherited-outside"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-workspace-inherited-outside"
sources = ["main.cpp"]

[dependencies.missing]
workspace = true
visibility = "private"
)graph"_str },
        { "main.cpp"_str, R"graph(int main() { return 0; }
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_inherited_runtime_dependency_missing_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "app/install.lua"_str, R"graph(lito.install({
    files = {
        { source = "resource.txt", destination = "share/runtime-missing/resource.txt" },
    },
})
)graph"_str },
        { "app/lito.toml"_str, R"graph([package]
name = "fixture-inherited-runtime-dependency-missing-app"
version.workspace = true

[runtime-dependencies.missing]
workspace = true
)graph"_str },
        { "app/resource.txt"_str, R"graph(fixture
)graph"_str },
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-inherited-runtime-dependency-missing"
members = ["app"]

[workspace.package]
version = "0.1.0"
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_inherited_version_missing_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-inherited-version-missing"
members = ["member"]
)graph"_str },
        { "member/lito.toml"_str, R"graph([package]
name = "fixture-workspace-inherited_version_missing"
version.workspace = true

[lib]
name = "fixture-workspace-inherited_version_missing"
module = "fixture.workspace.inherited_version_missing"
archive = "fixture.workspace.inherited_version_missing"
sources = ["source.cppm"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_member_profile_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "app/lito.toml"_str, R"graph([package]
name = "fixture-workspace-member-profile-app"
version = { workspace = true }

[[bin]]
link-stdlib = false
name = "workspace-member-profile-app"
sources = ["main.cpp"]

[profile]
exceptions = false
)graph"_str },
        { "app/main.cpp"_str, R"graph(int main() {
    return 0;
}
)graph"_str },
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-workspace-member-profile"
members = ["app"]

[workspace.package]
version = "0.1.0"
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_missing_member_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-missing-member"
members = ["missing"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_nested_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-nested-workspace"
members = ["member"]
)graph"_str },
        { "member/lito.toml"_str, R"graph([workspace]
name = "fixture-nested-member-workspace"
members = ["child"]
)graph"_str },
    };
    return source_tree(files);
}

auto workspace_outside_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"graph([workspace]
name = "fixture-outside-workspace"
members = [".."]
)graph"_str },
    };
    return source_tree(files);
}

struct InvalidGraphCase {
    ref<str>     name;
    ref<str>     request;
    GraphFactory make;
};

constexpr InvalidGraphCase invalid_graph_cases[] = {
    { "workspace_convention_test_invalid_kind"_str,
      ""_str,
      &workspace_convention_test_invalid_kind_tree },
    { "workspace_convention_test_invalid_name"_str,
      ""_str,
      &workspace_convention_test_invalid_name_tree },
    { "workspace_convention_test_invalid_overlap"_str,
      ""_str,
      &workspace_convention_test_invalid_overlap_tree },
    { "workspace_convention_test_invalid_profile"_str,
      ""_str,
      &workspace_convention_test_invalid_profile_tree },
    { "workspace_convention_test_invalid_version"_str,
      ""_str,
      &workspace_convention_test_invalid_version_tree },
    { "workspace_convention_test_invalid_workspace_kind"_str,
      ""_str,
      &workspace_convention_test_invalid_workspace_kind_tree },
    { "package_resolver_cycle_a"_str, "a"_str, &package_resolver_cycle_a_tree },
    { "package_resolver_missing"_str, ""_str, &package_resolver_missing_tree },
    { "package_resolver_name_mismatch_root"_str,
      "root"_str,
      &package_resolver_name_mismatch_root_tree },
    { "package_resolver_runtime_cycle"_str, ""_str, &package_resolver_runtime_cycle_tree },
    { "package_resolver_same_name_root"_str, "root"_str, &package_resolver_same_name_root_tree },
    { "workspace_default_not_member"_str, ""_str, &workspace_default_not_member_tree },
    { "workspace_duplicate_name"_str, ""_str, &workspace_duplicate_name_tree },
    { "workspace_duplicate"_str, ""_str, &workspace_duplicate_tree },
    { "workspace_inherited_dependency_missing"_str,
      ""_str,
      &workspace_inherited_dependency_missing_tree },
    { "workspace_inherited_dependency_outside"_str,
      ""_str,
      &workspace_inherited_dependency_outside_tree },
    { "workspace_inherited_runtime_dependency_missing"_str,
      ""_str,
      &workspace_inherited_runtime_dependency_missing_tree },
    { "workspace_inherited_version_missing"_str,
      ""_str,
      &workspace_inherited_version_missing_tree },
    { "workspace_member_profile"_str, ""_str, &workspace_member_profile_tree },
    { "workspace_missing_member"_str, ""_str, &workspace_missing_member_tree },
    { "workspace_nested"_str, ""_str, &workspace_nested_tree },
    { "workspace_outside"_str, ""_str, &workspace_outside_tree },
};

TEST_F(PackageResolver, InvalidDependencyGraphsAreRejectedByResolverOwner) {
    for (const auto& item : invalid_graph_cases) {
        SCOPED_TRACE(item.name);
        auto tree = item.make();
        ASSERT_TRUE(tree.is_ok());
        auto project = materialize(item.name, *tree);
        ASSERT_TRUE(project.is_ok());
        auto requested = project->root.clone();
        if (! item.request.is_empty()) {
            requested = requested.join(PathBuf::from(item.request).as_path());
        }
        auto resolved = lito::package::resolve_package_graph(requested.as_path());
        EXPECT_TRUE(resolved.is_err());
    }
}

TEST_F(PackageResolver, LocalDependencyMustMatchItsRegistryVersionRequirement) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "fixture-local-registry-version"
members = ["library", "app"]
default-members = ["app"]

[workspace.dependencies.fixture-local-registry-version-library]
path = "library"
version = "=0.1.0"
)toml"_str },
        { "library/lito.toml"_str, R"toml([package]
name = "fixture-local-registry-version-library"
version = "0.2.0"

[lib]
name = "fixture-local-registry-version-library"
module = "fixture.local_registry_version.library"
archive = "fixture-local-registry-version-library"
)toml"_str },
        { "app/lito.toml"_str, R"toml([package]
name = "fixture-local-registry-version-app"
version = "0.1.0"

[[bin]]
name = "fixture-local-registry-version-app"
link-stdlib = false

[dependencies.fixture-local-registry-version-library]
workspace = true
visibility = "private"
)toml"_str },
    };
    auto project = materialize("local-registry-version-mismatch"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto resolved = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(resolved.is_err());
    auto message = rstd::format("{}", rstd::move(resolved).unwrap_err());
    EXPECT_TRUE(message.as_str().contains("requires Registry version '=0.1.0'"_str));
    EXPECT_TRUE(message.as_str().contains("has version '0.2.0'"_str));
}

TEST_F(PackageResolver, GitPatchKeepsTheDeclaredRegistryVersionRequirement) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-patched-registry-version-app"
version = "0.1.0"

[[bin]]
name = "fixture-patched-registry-version-app"
link-stdlib = false

[dependencies.fixture-patched-registry-version-library]
git = "https://example.invalid/patched-registry-version.git"
version = "0.1.0"
visibility = "private"
)toml"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-patched-registry-version-library"
version = "0.1.0"

[lib]
name = "fixture-patched-registry-version-library"
module = "fixture.patched_registry_version.library"
archive = "fixture-patched-registry-version-library"
)toml"_str },
    };
    auto project = materialize("patched-registry-version"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto options            = lito::source::SourceResolutionOptions {};
    options.sources.network = lito::source::NetworkPolicy::Offline;
    options.sources.patches.push(lito::source::GitSourcePatch {
        .git  = String::make("https://example.invalid/patched-registry-version.git"_str),
        .path = project->root.join(PathBuf::from("provider"_str).as_path()),
    });
    auto resolved =
        lito::package::resolve_package_graph(project->root.as_path(), rstd::move(options));
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->packages.len(), usize(2));
}

TEST_F(PackageResolver, SameNameConflictReportsBothSources) {
    auto tree = package_resolver_same_name_root_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("package-resolver-same-name-sources"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto requested = project->root.join(PathBuf::from("root"_str).as_path());
    auto resolved  = lito::package::resolve_package_graph(requested.as_path());
    ASSERT_TRUE(resolved.is_err());
    auto message = rstd::format("{}", rstd::move(resolved).unwrap_err());
    EXPECT_TRUE(message.as_str().contains("package conflict for 'fixture-same-dependency'"_str));
    EXPECT_TRUE(message.as_str().contains("path+../one"_str));
    EXPECT_TRUE(message.as_str().contains("path+../two"_str));
}

TEST_F(PackageResolver, ProjectNameComesFromRootManifest) {
    const ProjectFile workspace_files[] = {
        {
            "lito.toml"_str,
            R"toml([workspace]
name = "demo-workspace"
members = ["app-one"]

[workspace.package]
version = "0.1.0"
)toml"_str,
        },
        {
            "app-one/lito.toml"_str,
            R"toml([package]
name = "demo-workspace-app-one"
version.workspace = true

[[bin]]
link-stdlib = false
name = "demo-app"
sources = ["main.cpp"]
)toml"_str,
        },
        { "app-one/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto workspace_project = materialize("project-name-workspace"_str, workspace_files);
    ASSERT_TRUE(workspace_project.is_ok());
    auto workspace = lito::package::resolve_package_graph(workspace_project->root.as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->root_is_workspace);
    EXPECT_EQ(workspace->name.as_str(), "demo-workspace"_str);

    auto member           = workspace_project->root.join(PathBuf::from("app-one"_str).as_path());
    auto workspace_member = lito::package::resolve_package_graph(member.as_path());
    ASSERT_TRUE(workspace_member.is_ok());
    EXPECT_TRUE(workspace_member->root_is_workspace);
    EXPECT_EQ(workspace_member->name.as_str(), "demo-workspace"_str);

    const ProjectFile package_files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "demo-app"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "demo-app"
sources = ["main.cpp"]
)toml"_str,
        },
        { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto package_project = materialize("project-name-package"_str, package_files);
    ASSERT_TRUE(package_project.is_ok());
    auto package = lito::package::resolve_package_graph(package_project->root.as_path());
    ASSERT_TRUE(package.is_ok());
    EXPECT_FALSE(package->root_is_workspace);
    EXPECT_EQ(package->name.as_str(), "demo-app"_str);
}

TEST_F(PackageResolver, ResolvesBuiltinScriptPackagesThroughRequiredDependencies) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-script-consumer"
version = "0.1.0"

[lib]
name = "fixture-script-consumer"
module = "fixture.script.consumer"
archive = "fixture-script-consumer"
sources = ["lib.cppm"]

[dependencies.lito-qt]
builtin = "qt"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.script.consumer;\n"_str },
    };
    auto project = materialize("builtin-script-dependency"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(2));

    const lito::package::ResolvedPackage* consumer = nullptr;
    const lito::package::ResolvedPackage* provider = nullptr;
    for (const auto& package : graph->packages) {
        if (package.manifest.name == "fixture-script-consumer"_str) consumer = &package;
        if (package.manifest.name == "lito-qt"_str) provider = &package;
    }
    ASSERT_NE(consumer, nullptr);
    ASSERT_NE(provider, nullptr);
    ASSERT_EQ(consumer->dependencies.len(), usize(1));
    ASSERT_TRUE(consumer->dependencies[usize {}].is_Script());
    const auto& script = consumer->dependencies[usize {}].as_Script().value;
    EXPECT_EQ(script.name.as_str(), "lito-qt"_str);
    EXPECT_EQ(script.require_name.as_str(), "@lito.qt"_str);
    ASSERT_EQ(script.supports.len(), usize(1));
    EXPECT_EQ(script.supports[usize {}], lito::manifest::ScriptHostKind::Build);
    EXPECT_EQ(provider->source.kind, lito::source::PackageSourceKind::Builtin);
    EXPECT_EQ(provider->source.builtin.as_str(), "qt"_str);
    EXPECT_TRUE(provider->embedded_source.is_some());
}

TEST_F(PackageResolver, RejectsCppFieldsOnScriptDependencyContracts) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-script-contract-mismatch"
version = "0.1.0"

[lib]
name = "fixture-script-contract-mismatch"
module = "fixture.script.contract_mismatch"
archive = "fixture-script-contract-mismatch"
sources = ["lib.cppm"]

[dependencies.lito-qt]
builtin = "qt"
visibility = "private"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.script.contract_mismatch;\n"_str },
    };
    auto project = materialize("script-contract-mismatch"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_err());
    EXPECT_TRUE(rstd::format("{}", graph.unwrap_err()).as_str().contains("C++ fields"_str));
}

TEST_F(PackageResolver, ScriptCatalogRejectsImporterRelativeModuleNames) {
    const ProjectFile files[] = {
        { "build.lua"_str, "require(\"./value\")\n"_str },
        { "value.lua"_str, "return 42\n"_str },
    };
    auto project = materialize("script-relative-require"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto dependencies = Vec<String>::make();
    auto packages     = Vec<lito::package::ResolvedScriptPackageView>::make();
    auto catalog = lito::package::ScriptModuleCatalog::make(project->root.as_path(),
                                                            "fixture:script-relative"_str,
                                                            dependencies.as_slice(),
                                                            packages.as_slice(),
                                                            lito::manifest::ScriptHostKind::Build);
    ASSERT_TRUE(catalog.is_ok());
    auto modules = rstd::move(catalog).unwrap();
    auto state   = luato::State::create(luato::StateOptions::build_script());
    ASSERT_TRUE(state.is_ok());
    auto lua = rstd::move(state).unwrap();
    ASSERT_TRUE(
        lua
            .set_module_resolver(luato::ModuleResolverSpec::make(
                [&modules](luato::ModuleRequest request) -> luato::Result<luato::LuaModuleSource> {
                    return modules.resolve(rstd::move(request));
                }))
            .is_ok());
    auto script = project->root.join(PathBuf::from("build.lua"_str).as_path());
    auto entry  = modules.entry(script.as_path(), "fixture-relative-entry"_str);
    ASSERT_TRUE(entry.is_ok());
    auto executed = lua.execute_entry(rstd::move(entry).unwrap());
    ASSERT_TRUE(executed.is_err());
    EXPECT_TRUE(executed.unwrap_err().message.as_str().contains("exact safe source-root"_str));
}

TEST_F(PackageResolver, EffectiveTargetsIncludeDependencyLibraries) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-effective-root"
version = "0.1.0"

[[bin]]
name = "fixture-effective-root"
sources = ["main.cpp"]
link-stdlib = false

[dependencies.fixture-effective-provider]
path = "provider"
visibility = "private"
)toml"_str },
        { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-effective-provider"
version = "0.1.0"

[lib]
name = "fixture-effective-provider"
module = "fixture.effective.provider"
archive = "fixture-effective-provider"
sources = ["lib.cppm"]
)toml"_str },
        { "provider/lib.cppm"_str, "export module fixture.effective.provider;\n"_str },
    };
    auto project = materialize("effective-targets"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_targets.len(), usize(1));
    EXPECT_EQ(selected->selected_targets[usize {}].package.as_str(), "fixture-effective-root"_str);
    ASSERT_EQ(selected->effective_targets.len(), usize(2));
    auto provider_library = false;
    for (const auto& target : selected->effective_targets) {
        if (target.package.as_str() == "fixture-effective-provider"_str &&
            target.kind == lito::package::PackageTargetKind::Library) {
            provider_library = true;
        }
    }
    EXPECT_TRUE(provider_library);
}

TEST_F(PackageResolver, PmacroDependenciesUseTheHostContract) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-pmacro-consumer"
version = "0.1.0"

[[bin]]
name = "fixture-pmacro-consumer"
sources = ["main.cpp"]
link-stdlib = false

[dependencies.fixture-pmacro-provider]
path = "provider"
features = ["diagnostics"]
default-features = false
)toml"_str },
        { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-pmacro-provider"
version = "0.1.0"

[pmacro]
module = "fixture.pmacro.provider"
sources = ["lib.cppm"]

[features.diagnostics]

[dependencies.fixture-pmacro-host-lib]
path = "../host-lib"
visibility = "private"
)toml"_str },
        { "provider/lib.cppm"_str, "export module fixture.pmacro.provider;\n"_str },
        { "host-lib/lito.toml"_str, R"toml([package]
name = "fixture-pmacro-host-lib"
version = "0.1.0"

[lib]
name = "fixture-pmacro-host-lib"
module = "fixture.pmacro.host_lib"
archive = "fixture-pmacro-host-lib"
sources = ["lib.cppm"]
)toml"_str },
        { "host-lib/lib.cppm"_str, "export module fixture.pmacro.host_lib;\n"_str },
        { "support/lito.toml"_str, R"toml([package]
name = "pmacro"
version = "0.3.0"

[plugin]
module = "pmacro"
sources = ["lib.cppm"]
)toml"_str },
        { "support/lib.cppm"_str, "export module pmacro;\n"_str },
    };
    auto project = materialize("pmacro-host-contract"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto options =
        pmacro_source_options(project->root.join(PathBuf::from("support"_str).as_path()).as_path());
    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production,
        rstd::move(options));
    if (selected.is_err()) {
        auto message = error_chain_text(rstd::move(selected).unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }

    const lito::package::ResolvedPackage* consumer = nullptr;
    const lito::package::ResolvedPackage* provider = nullptr;
    for (const auto& package : selected->graph.packages) {
        if (package.manifest.name == "fixture-pmacro-consumer"_str) consumer = &package;
        if (package.manifest.name == "fixture-pmacro-provider"_str) provider = &package;
    }
    ASSERT_NE(consumer, nullptr);
    ASSERT_NE(provider, nullptr);
    ASSERT_EQ(consumer->dependencies.len(), usize(1));
    ASSERT_TRUE(consumer->dependencies[usize {}].is_Pmacro());
    const auto& dependency = consumer->dependencies[usize {}].as_Pmacro().value;
    EXPECT_EQ(dependency.name.as_str(), "fixture-pmacro-provider"_str);
    ASSERT_EQ(dependency.features.len(), usize(1));
    EXPECT_EQ(dependency.features[usize {}].as_str(), "diagnostics"_str);
    EXPECT_FALSE(dependency.default_features);
    ASSERT_EQ(provider->dependencies.len(), usize(2));
    auto has_cpp    = false;
    auto has_plugin = false;
    for (const auto& provider_dependency : provider->dependencies) {
        has_cpp |= provider_dependency.is_Cpp();
        has_plugin |= provider_dependency.is_Plugin();
    }
    EXPECT_TRUE(has_cpp);
    EXPECT_TRUE(has_plugin);

    ASSERT_EQ(selected->selected_package_names.len(), usize(1));
    EXPECT_EQ(selected->selected_package_names[usize {}].as_str(), "fixture-pmacro-consumer"_str);
    ASSERT_EQ(selected->proc_macro_provider_names.len(), usize(1));
    EXPECT_EQ(selected->proc_macro_provider_names[usize {}].as_str(),
              "fixture-pmacro-provider"_str);
    ASSERT_EQ(selected->plugin_package_names.len(), usize(1));
    EXPECT_EQ(selected->plugin_package_names[usize {}].as_str(), "pmacro"_str);
    EXPECT_EQ(selected->host_package_names.len(), usize(3));
    auto diagnostics = false;
    for (const auto& feature : provider->features) {
        if (feature.name == "diagnostics"_str) diagnostics = feature.enabled;
    }
    EXPECT_TRUE(diagnostics);
}

TEST_F(PackageResolver, PluginDependenciesUseTheHostContract) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-plugin-consumer"
version = "0.1.0"

[lib]
name = "fixture-plugin-consumer"
module = "fixture.plugin_consumer"
archive = "fixture-plugin-consumer"
sources = ["lib.cppm"]

[dependencies.fixture-compiler-plugin]
path = "plugin"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.plugin_consumer;\n"_str },
        { "plugin/lito.toml"_str, R"toml([package]
name = "fixture-compiler-plugin"
version = "0.1.0"

[plugin]
module = "fixture.compiler_plugin"
sources = ["lib.cppm"]
)toml"_str },
        { "plugin/lib.cppm"_str, "export module fixture.compiler_plugin;\n"_str },
    };
    auto project = materialize("plugin-host-contract"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production);
    if (selected.is_err()) {
        auto message = error_chain_text(rstd::move(selected).unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    const lito::package::ResolvedPackage* consumer = nullptr;
    for (const auto& package : selected->graph.packages) {
        if (package.manifest.name == "fixture-plugin-consumer"_str) consumer = &package;
    }
    ASSERT_NE(consumer, nullptr);
    ASSERT_EQ(consumer->dependencies.len(), usize(1));
    EXPECT_TRUE(consumer->dependencies[usize {}].is_Plugin());
    ASSERT_EQ(selected->plugin_package_names.len(), usize(1));
    EXPECT_EQ(selected->plugin_package_names[usize {}].as_str(), "fixture-compiler-plugin"_str);
    ASSERT_EQ(selected->host_package_names.len(), usize(1));
    EXPECT_EQ(selected->host_package_names[usize {}].as_str(), "fixture-compiler-plugin"_str);
}

TEST_F(PackageResolver, PmacroBuiltinOverrideRequiresAPluginTarget) {
    constexpr ref<str> support_manifests[] = {
        R"toml([package]
name = "pmacro"
version = "0.3.0"

[lib]
name = "pmacro"
module = "pmacro"
archive = "pmacro"
sources = ["lib.cppm"]
)toml"_str,
    };
    auto index = usize {};
    for (auto support_manifest : support_manifests) {
        const ProjectFile files[] = {
            { "lito.toml"_str, R"toml([package]
name = "fixture-pmacro-override-consumer"
version = "0.1.0"

[[bin]]
name = "fixture-pmacro-override-consumer"
sources = ["main.cpp"]
link-stdlib = false

[dependencies.fixture-pmacro-override-provider]
path = "provider"
)toml"_str },
            { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
            { "provider/lito.toml"_str, R"toml([package]
name = "fixture-pmacro-override-provider"
version = "0.1.0"

[pmacro]
module = "fixture.pmacro.override_provider"
sources = ["lib.cppm"]
)toml"_str },
            { "provider/lib.cppm"_str, "export module fixture.pmacro.override_provider;\n"_str },
            { "support/lito.toml"_str, support_manifest },
            { "support/lib.cppm"_str, "export module pmacro;\n"_str },
        };
        auto project =
            materialize(rstd::format("pmacro-invalid-override-{}", index).as_str(), files);
        ASSERT_TRUE(project.is_ok());
        auto options = pmacro_source_options(
            project->root.join(PathBuf::from("support"_str).as_path()).as_path());
        auto selected = lito::package::resolve_package_selection(
            lito::package::PackageSelection { .root = project->root.clone() },
            lito::package::PackageSelectionPurpose::Production,
            rstd::move(options));
        ASSERT_TRUE(selected.is_err());
        EXPECT_TRUE(error_chain_text(rstd::move(selected).unwrap_err())
                        .as_str()
                        .contains("must provide [plugin]"_str));
        ++index;
    }
}

TEST_F(PackageResolver, PluginPackagesCannotDependOnPlugins) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-plugin-root"
version = "0.1.0"

[lib]
name = "fixture-plugin-root"
module = "fixture.plugin_root"
archive = "fixture-plugin-root"
sources = ["lib.cppm"]

[dependencies.fixture-plugin-a]
path = "plugin-a"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.plugin_root;\n"_str },
        { "plugin-a/lito.toml"_str, R"toml([package]
name = "fixture-plugin-a"
version = "0.1.0"

[plugin]
module = "fixture.plugin_a"
sources = ["lib.cppm"]

[dependencies.fixture-plugin-b]
path = "../plugin-b"
)toml"_str },
        { "plugin-a/lib.cppm"_str, "export module fixture.plugin_a;\n"_str },
        { "plugin-b/lito.toml"_str, R"toml([package]
name = "fixture-plugin-b"
version = "0.1.0"

[plugin]
module = "fixture.plugin_b"
sources = ["lib.cppm"]
)toml"_str },
        { "plugin-b/lib.cppm"_str, "export module fixture.plugin_b;\n"_str },
    };
    auto project = materialize("nested-plugin-dependency"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(selected.is_err());
    EXPECT_TRUE(error_chain_text(rstd::move(selected).unwrap_err())
                    .as_str()
                    .contains("cannot depend on plugin package"_str));
}

TEST_F(PackageResolver, PmacroDependencyRejectsVisibility) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-pmacro-visibility"
version = "0.1.0"

[lib]
name = "fixture-pmacro-visibility"
module = "fixture.pmacro.visibility"
archive = "fixture-pmacro-visibility"
sources = ["lib.cppm"]

[dependencies.fixture-pmacro-visibility-provider]
path = "provider"
visibility = "private"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.pmacro.visibility;\n"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-pmacro-visibility-provider"
version = "0.1.0"

[pmacro]
module = "fixture.pmacro.visibility_provider"
sources = ["lib.cppm"]
)toml"_str },
        { "provider/lib.cppm"_str, "export module fixture.pmacro.visibility_provider;\n"_str },
        { "support/lito.toml"_str, R"toml([package]
name = "pmacro"
version = "0.3.0"

[plugin]
module = "pmacro"
sources = ["lib.cppm"]
)toml"_str },
        { "support/lib.cppm"_str, "export module pmacro;\n"_str },
    };
    auto project = materialize("pmacro-visibility"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto options =
        pmacro_source_options(project->root.join(PathBuf::from("support"_str).as_path()).as_path());
    auto graph = lito::package::resolve_package_graph(project->root.as_path(), rstd::move(options));
    ASSERT_TRUE(graph.is_err());
    auto message = error_chain_text(graph.unwrap_err());
    if (! message.as_str().contains("visibility"_str)) {
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
}

TEST_F(PackageResolver, DevelopmentPmacroDependenciesOnlySelectForDevelopmentTargets) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-dev-pmacro-consumer"
version = "0.1.0"

[[bin]]
name = "fixture-dev-pmacro-consumer"
sources = ["main.cpp"]
link-stdlib = false

[[test]]
name = "fixture-dev-pmacro-test"
sources = ["test.cpp"]
link-stdlib = false

[dev-dependencies.fixture-dev-pmacro-provider]
path = "provider"
)toml"_str },
        { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "test.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-dev-pmacro-provider"
version = "0.1.0"

[pmacro]
module = "fixture.dev_pmacro.provider"
sources = ["lib.cppm"]
)toml"_str },
        { "provider/lib.cppm"_str, "export module fixture.dev_pmacro.provider;\n"_str },
        { "support/lito.toml"_str, R"toml([package]
name = "pmacro"
version = "0.3.0"

[plugin]
module = "pmacro"
sources = ["lib.cppm"]
)toml"_str },
        { "support/lib.cppm"_str, "export module pmacro;\n"_str },
    };
    auto project = materialize("development-pmacro"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto support    = project->root.join(PathBuf::from("support"_str).as_path());
    auto production = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production,
        pmacro_source_options(support.as_path()));
    if (production.is_err()) {
        auto message = error_chain_text(rstd::move(production).unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_TRUE(production->host_package_names.is_empty());
    EXPECT_TRUE(production->proc_macro_provider_names.is_empty());

    auto development = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::All,
        pmacro_source_options(support.as_path()));
    if (development.is_err()) {
        auto message = error_chain_text(rstd::move(development).unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    ASSERT_EQ(development->proc_macro_provider_names.len(), usize(1));
    EXPECT_EQ(development->proc_macro_provider_names[usize {}].as_str(),
              "fixture-dev-pmacro-provider"_str);
    EXPECT_EQ(development->host_package_names.len(), usize(2));
}

TEST_F(PackageResolver, PmacroProvidersCannotDependOnPmacroProviders) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-recursive-pmacro-consumer"
version = "0.1.0"

[lib]
name = "fixture-recursive-pmacro-consumer"
module = "fixture.recursive_pmacro.consumer"
archive = "fixture-recursive-pmacro-consumer"
sources = ["lib.cppm"]

[dependencies.fixture-pmacro-provider-a]
path = "provider-a"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.recursive_pmacro.consumer;\n"_str },
        { "provider-a/lito.toml"_str, R"toml([package]
name = "fixture-pmacro-provider-a"
version = "0.1.0"

[pmacro]
module = "fixture.pmacro.provider_a"
sources = ["lib.cppm"]

[dependencies.fixture-pmacro-provider-b]
path = "../provider-b"
)toml"_str },
        { "provider-a/lib.cppm"_str, "export module fixture.pmacro.provider_a;\n"_str },
        { "provider-b/lito.toml"_str, R"toml([package]
name = "fixture-pmacro-provider-b"
version = "0.1.0"

[pmacro]
module = "fixture.pmacro.provider_b"
sources = ["lib.cppm"]
)toml"_str },
        { "provider-b/lib.cppm"_str, "export module fixture.pmacro.provider_b;\n"_str },
        { "support/lito.toml"_str, R"toml([package]
name = "pmacro"
version = "0.3.0"

[plugin]
module = "pmacro"
sources = ["lib.cppm"]
)toml"_str },
        { "support/lib.cppm"_str, "export module pmacro;\n"_str },
    };
    auto project = materialize("recursive-pmacro"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto options =
        pmacro_source_options(project->root.join(PathBuf::from("support"_str).as_path()).as_path());
    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production,
        rstd::move(options));
    ASSERT_TRUE(selected.is_err());
    auto message = error_chain_text(selected.unwrap_err());
    if (! message.as_str().contains("cannot depend"_str)) {
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
}

TEST_F(PackageResolver, WorkspaceDefaultsSelectOnlyDeclaredMembers) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "fixture-default-selection"
members = ["app", "tool"]
default-members = ["app"]
)toml"_str },
        { "app/lito.toml"_str, R"toml([package]
name = "fixture-default-app"
version = "0.1.0"

[[bin]]
name = "fixture-default-app"
sources = ["main.cpp"]
link-stdlib = false
)toml"_str },
        { "app/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "tool/lito.toml"_str, R"toml([package]
name = "fixture-default-tool"
version = "0.1.0"

[[bin]]
name = "fixture-default-tool"
sources = ["main.cpp"]
link-stdlib = false
)toml"_str },
        { "tool/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto project = materialize("workspace-default-selection"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_root_names.len(), usize(1));
    EXPECT_EQ(selected->selected_root_names[usize {}].as_str(), "fixture-default-app"_str);
    ASSERT_EQ(selected->selected_package_names.len(), usize(1));
    EXPECT_EQ(selected->selected_package_names[usize {}].as_str(), "fixture-default-app"_str);

    auto all = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = project->root.clone() },
        lito::package::PackageSelectionPurpose::All);
    ASSERT_TRUE(all.is_ok());
    ASSERT_EQ(all->selected_root_names.len(), usize(2));
    EXPECT_EQ(all->selected_root_names[usize {}].as_str(), "fixture-default-app"_str);
    EXPECT_EQ(all->selected_root_names[usize(1)].as_str(), "fixture-default-tool"_str);
    ASSERT_EQ(all->selected_package_names.len(), usize(2));
    EXPECT_EQ(all->selected_package_names[usize {}].as_str(), "fixture-default-app"_str);
    EXPECT_EQ(all->selected_package_names[usize(1)].as_str(), "fixture-default-tool"_str);
}
