#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
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

class PackageResolver : public ProjectFixture {};

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
