#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
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

auto associated_workspace_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([workspace]
name = "fixture-associated-workspace"
members = ["library"]
default-members = ["library"]
[workspace.package]
version = "0.1.0"
license = "MIT OR Apache-2.0"
[workspace.dependencies.fixture-associated-workspace-library]
path = "library"
[workspace.dependencies.fixture-associated-workspace-bench-helper]
path = "bench-helper"
)"_str },
        { "library/lito.toml"_str, R"([package]
name = "fixture-associated-workspace-library"
version.workspace = true
[lib]
name = "fixture-associated-workspace-library"
module = "fixture.associated.workspace"
archive = "fixture.associated.workspace"
sources = ["lib.cppm"]
[dev-dependencies.fixture-associated-workspace-bench-helper]
workspace = true
)"_str },
        { "library/lib.cppm"_str, "export module fixture.associated.workspace;\n"_str },
        { "library/benches/speed.cpp"_str,
          "import fixture.associated.workspace;\nimport fixture.associated.workspace.bench_helper;\n"_str },
        { "bench-helper/lito.toml"_str, R"([package]
name = "fixture-associated-workspace-bench-helper"
version = "0.1.0"
[lib]
name = "fixture-associated-workspace-bench-helper"
module = "fixture.associated.workspace.bench_helper"
archive = "fixture.associated.workspace.bench-helper"
sources = ["lib.cppm"]
)"_str },
        { "bench-helper/lib.cppm"_str,
          "export module fixture.associated.workspace.bench_helper;\n"_str },
        { "tests/lito.toml"_str, R"([package]
name = "fixture-associated-workspace-test"
version.workspace = true
license.workspace = true
[[test]]
link-stdlib = false
name = "fixture-associated-workspace-test"
sources = ["main.cpp"]
[dependencies.fixture-associated-workspace-library]
workspace = true
visibility = "private"
)"_str },
        { "tests/main.cpp"_str, "import fixture.associated.workspace;\n"_str },
        { "benches/lito.toml"_str, R"([package]
name = "fixture-associated-workspace-benchmark"
version = "0.1.0"
[[bench]]
link-stdlib = false
name = "fixture-associated-workspace-benchmark"
sources = ["main.cpp"]
[dependencies.fixture-associated-workspace-library]
path = "../library"
visibility = "private"
)"_str },
        { "benches/main.cpp"_str, "import fixture.associated.workspace;\n"_str },
    };
    return source_tree(files);
}

auto benchmark_only_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = \"fixture-conventional-benchmark-only\"\nversion = \"0.1.0\"\n"_str },
        { "benches/only.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    return source_tree(files);
}

auto associated_package_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "fixture-conventional-library"
version = "0.1.0"
[lib]
name = "fixture-conventional-library"
module = "fixture.conventional"
archive = "fixture-conventional"
sources = ["src/lib.cppm"]
[dev-dependencies.fixture-conventional-bench-helper]
path = "bench-helper"
)"_str },
        { "src/lib.cppm"_str, "export module fixture.conventional;\n"_str },
        { "bench-helper/lito.toml"_str, R"([package]
name = "fixture-conventional-bench-helper"
version = "0.1.0"
[lib]
name = "fixture-conventional-bench-helper"
module = "fixture.conventional.bench_helper"
archive = "fixture.conventional.bench-helper"
sources = ["lib.cppm"]
)"_str },
        { "bench-helper/lib.cppm"_str, "export module fixture.conventional.bench_helper;\n"_str },
        { "benches/speed.cpp"_str,
          "import fixture.conventional;\nimport fixture.conventional.bench_helper;\n"_str },
        { "benches/multi/main.cpp"_str, "import fixture.conventional.multi;\n"_str },
        { "benches/multi/support.cppm"_str, "export module fixture.conventional.multi;\n"_str },
        { "tests/lito.toml"_str, R"([package]
name = "fixture-conventional-test"
[[test]]
link-stdlib = false
name = "fixture-conventional-test"
sources = ["main.cpp"]
[dependencies.fixture-conventional-library]
path = ".."
visibility = "private"
)"_str },
        { "tests/main.cpp"_str, "import fixture.conventional;\n"_str },
    };
    return source_tree(files);
}

auto associated_test_workspace_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([workspace]
name = "fixture-conventional-workspace"
members = ["library"]
[workspace.package]
version = "0.1.0"
)"_str },
        { "library/lito.toml"_str, R"([package]
name = "fixture-conventional-workspace-library"
version.workspace = true
[lib]
name = "fixture-conventional-workspace-library"
module = "fixture.conventional.workspace"
archive = "fixture-conventional-workspace"
sources = ["src/lib.cppm"]
)"_str },
        { "library/src/lib.cppm"_str, "export module fixture.conventional.workspace;\n"_str },
        { "tests/lito.toml"_str, R"([workspace]
name = "fixture-conventional-test-workspace"
members = ["runtime", "compile"]
)"_str },
        { "tests/runtime/lito.toml"_str, R"([package]
name = "fixture-conventional-workspace-test"
[[test]]
link-stdlib = false
name = "fixture-conventional-workspace-test"
sources = ["main.cpp"]
[dependencies.fixture-conventional-workspace-library]
path = "../../library"
visibility = "private"
)"_str },
        { "tests/runtime/main.cpp"_str, "import fixture.conventional.workspace;\n"_str },
        { "tests/compile/lito.toml"_str, R"([package]
name = "fixture-conventional-workspace-compile-test"
[compile-test]
[[compile-test.cases]]
name = "AssociatedWorkspace.Success"
source = "pass.cpp"
outcome = "success"
[dependencies.fixture-conventional-workspace-library]
path = "../../library"
visibility = "private"
)"_str },
        { "tests/compile/pass.cpp"_str, "import fixture.conventional.workspace;\n"_str },
    };
    return source_tree(files);
}

auto dependency_boundary_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "root/lito.toml"_str, R"([package]
name = "fixture-conventional-boundary-root"
version = "0.1.0"
[[bin]]
link-stdlib = false
name = "fixture-conventional-boundary-root"
sources = ["main.cpp"]
[dependencies.fixture-conventional-boundary-dependency]
path = "../dependency"
visibility = "private"
)"_str },
        { "dependency/lito.toml"_str, R"([package]
name = "fixture-conventional-boundary-dependency"
version = "0.1.0"
[lib]
name = "fixture-conventional-boundary-dependency"
module = "fixture.conventional.boundary"
archive = "fixture-conventional-boundary"
sources = ["lib.cppm"]
)"_str },
        { "dependency/tests/lito.toml"_str, R"([package]
name = "fixture-conventional-boundary-dependency-test"
version = "0.1.0"
[[test]]
link-stdlib = false
name = "fixture-conventional-boundary-dependency-test"
sources = ["main.cpp"]
)"_str },
    };
    return source_tree(files);
}

class WorkspaceConvention : public ProjectFixture {};

TEST_F(WorkspaceConvention, WorkspaceDiscoversAssociatedTestPackageWithInheritance) {
    auto tree = associated_workspace_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("associated-workspace"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto associated = false;
    for (const auto& project_root : graph->roots) {
        if (project_root.name.as_str() != "fixture-associated-workspace-test"_str) continue;
        associated = true;
        EXPECT_EQ(project_root.role, lito::package::ProjectRootRole::AssociatedTest);
        EXPECT_EQ(project_root.source_identity.as_str(), "path+tests"_str);
    }
    EXPECT_TRUE(associated);

    for (const auto& package : graph->packages) {
        if (package.manifest.name.as_str() != "fixture-associated-workspace-test"_str) continue;
        EXPECT_TRUE(package.manifest.workspace_dependencies.is_empty());
        EXPECT_EQ(package.manifest.version.source, lito::manifest::PackageVersionSource::Workspace);
        ASSERT_TRUE(package.manifest.version.value.is_some());
        EXPECT_EQ(package.manifest.version.value->as_str(), "0.1.0"_str);
        EXPECT_EQ(package.manifest.license.source, lito::manifest::PackageLicenseSource::Workspace);
        ASSERT_TRUE(package.manifest.license.value.is_some());
        EXPECT_EQ(package.manifest.license.value->as_str(), "MIT OR Apache-2.0"_str);
        EXPECT_EQ(package.dependencies.len(), usize(1));
    }
}

TEST_F(WorkspaceConvention, WorkspaceBenchmarkTargetsUseDevelopmentDependencies) {
    auto tree = associated_workspace_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("associated-workspace"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto graph     = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto library_found = false;
    for (const auto& project_root : graph->roots) {
        EXPECT_NE(project_root.name.as_str(), "fixture-associated-workspace-benchmark"_str);
    }
    for (const auto& package : graph->packages) {
        EXPECT_NE(package.manifest.name.as_str(), "fixture-associated-workspace-benchmark"_str);
        if (package.manifest.name.as_str() == "fixture-associated-workspace-library"_str) {
            library_found = true;
            EXPECT_TRUE(package.manifest.workspace_dev_dependencies.is_empty());
            ASSERT_EQ(package.manifest.dev_dependencies.len(), usize(1));
            ASSERT_EQ(package.dev_dependencies.len(), usize(1));
            EXPECT_EQ(package.dev_dependencies[usize {}].name.as_str(),
                      "fixture-associated-workspace-bench-helper"_str);
            auto benchmark_found = false;
            for (const auto& target : package.manifest.targets) {
                if (target.is_Benchmark() && target.as_Benchmark().name.as_str() == "speed"_str) {
                    benchmark_found = true;
                    ASSERT_EQ(target.as_Benchmark().source.declared_sources.len(), usize(1));
                }
            }
            EXPECT_TRUE(benchmark_found);
        }
    }
    EXPECT_TRUE(library_found);

    auto production = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    EXPECT_TRUE(
        contains_name(production->selected_root_names, "fixture-associated-workspace-library"_str));
    EXPECT_FALSE(contains_name(production->selected_package_names,
                               "fixture-associated-workspace-bench-helper"_str));

    auto tests = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    EXPECT_TRUE(contains_name(tests->selected_root_names, "fixture-associated-workspace-test"_str));
    EXPECT_FALSE(contains_name(tests->selected_package_names,
                               "fixture-associated-workspace-bench-helper"_str));

    auto benchmark = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmark.is_ok());
    EXPECT_TRUE(
        contains_name(benchmark->selected_root_names, "fixture-associated-workspace-library"_str));
    EXPECT_TRUE(contains_name(benchmark->selected_package_names,
                              "fixture-associated-workspace-bench-helper"_str));

    auto standalone = lito::package::resolve_package_graph(
        directory.join(PathBuf::from("benches"_str).as_path()).as_path());
    ASSERT_TRUE(standalone.is_ok());
    ASSERT_EQ(standalone->roots.len(), usize(1));
    EXPECT_EQ(standalone->roots[usize {}].name.as_str(),
              "fixture-associated-workspace-benchmark"_str);
    EXPECT_EQ(standalone->roots[usize {}].role, lito::package::ProjectRootRole::PrimaryPackage);
}

TEST_F(WorkspaceConvention, PackageOnlyManifestDiscoversConventionalBenchmark) {
    auto tree = benchmark_only_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("benchmark-only"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto package   = lito::manifest::load_package_manifest(directory.as_path());
    ASSERT_TRUE(package.is_ok());
    ASSERT_EQ(package->targets.len(), usize(1));
    ASSERT_TRUE(package->targets[usize {}].is_Benchmark());
    EXPECT_EQ(package->targets[usize {}].as_Benchmark().name.as_str(), "only"_str);

    auto benchmark = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmark.is_ok());
    ASSERT_EQ(benchmark->selected_targets.len(), usize(1));
    EXPECT_EQ(benchmark->selected_targets[usize {}].name.as_str(), "only"_str);

    auto production = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Production);
    EXPECT_TRUE(production.is_err());
}

TEST_F(WorkspaceConvention, SinglePackageDiscoversAssociatedTestPackage) {
    auto tree = associated_package_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("associated-package"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto graph     = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(2));
    auto primary = project_root_role(*graph, "fixture-conventional-library"_str);
    auto test    = project_root_role(*graph, "fixture-conventional-test"_str);
    ASSERT_TRUE(primary.is_some());
    ASSERT_TRUE(test.is_some());
    EXPECT_EQ(*primary, lito::package::ProjectRootRole::PrimaryPackage);
    EXPECT_EQ(*test, lito::package::ProjectRootRole::AssociatedTest);
    for (const auto& package : graph->packages) {
        if (package.manifest.name.as_str() != "fixture-conventional-test"_str) continue;
        EXPECT_EQ(package.manifest.version.source,
                  lito::manifest::PackageVersionSource::Unspecified);
        EXPECT_TRUE(package.manifest.version.value.is_none());
    }

    auto production = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    ASSERT_EQ(production->selected_root_names.len(), usize(1));
    EXPECT_EQ(production->selected_root_names[usize {}].as_str(),
              "fixture-conventional-library"_str);
    EXPECT_FALSE(
        contains_name(production->selected_package_names, "fixture-conventional-bench-helper"_str));

    auto benchmarks = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmarks.is_ok());
    EXPECT_TRUE(contains_name(benchmarks->selected_root_names, "fixture-conventional-library"_str));
    EXPECT_TRUE(
        contains_name(benchmarks->selected_package_names, "fixture-conventional-bench-helper"_str));
    ASSERT_EQ(benchmarks->selected_targets.len(), usize(2));
    auto multi_source_count = usize {};
    for (const auto& package : benchmarks->graph.packages) {
        if (package.manifest.name.as_str() != "fixture-conventional-library"_str) continue;
        for (const auto& target : package.manifest.targets) {
            if (target.is_Benchmark() && target.as_Benchmark().name.as_str() == "multi"_str) {
                multi_source_count = target.as_Benchmark().source.declared_sources.len();
            }
        }
    }
    EXPECT_EQ(multi_source_count, usize(2));

    auto tests = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    ASSERT_EQ(tests->selected_root_names.len(), usize(1));
    EXPECT_EQ(tests->selected_root_names[usize {}].as_str(), "fixture-conventional-test"_str);
    EXPECT_TRUE(contains_name(tests->selected_package_names, "fixture-conventional-library"_str));

    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-test"_str),
        },
        lito::package::PackageSelectionPurpose::Test);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_root_names.len(), usize(1));

    auto selected_primary = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-library"_str),
        },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(selected_primary.is_ok());
    ASSERT_EQ(selected_primary->selected_root_names.len(), usize(1));

    auto test_as_production = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-test"_str),
        },
        lito::package::PackageSelectionPurpose::Production);
    EXPECT_TRUE(test_as_production.is_err());
}

TEST_F(WorkspaceConvention, WorkspaceDiscoversAssociatedTestWorkspace) {
    auto tree = associated_test_workspace_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("associated-test-workspace"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto graph     = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(3));
    auto library = project_root_role(*graph, "fixture-conventional-workspace-library"_str);
    auto runtime = project_root_role(*graph, "fixture-conventional-workspace-test"_str);
    auto compile = project_root_role(*graph, "fixture-conventional-workspace-compile-test"_str);
    ASSERT_TRUE(library.is_some());
    ASSERT_TRUE(runtime.is_some());
    ASSERT_TRUE(compile.is_some());
    EXPECT_EQ(*library, lito::package::ProjectRootRole::WorkspaceMember);
    EXPECT_EQ(*runtime, lito::package::ProjectRootRole::AssociatedTest);
    EXPECT_EQ(*compile, lito::package::ProjectRootRole::AssociatedTest);

    auto production = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    ASSERT_EQ(production->selected_root_names.len(), usize(1));
    EXPECT_EQ(production->selected_root_names[usize {}].as_str(),
              "fixture-conventional-workspace-library"_str);

    auto tests = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    EXPECT_EQ(tests->selected_root_names.len(), usize(2));
    EXPECT_TRUE(
        contains_name(tests->selected_root_names, "fixture-conventional-workspace-test"_str));
    EXPECT_TRUE(contains_name(tests->selected_root_names,
                              "fixture-conventional-workspace-compile-test"_str));
    EXPECT_TRUE(
        contains_name(tests->selected_package_names, "fixture-conventional-workspace-library"_str));
}

TEST_F(WorkspaceConvention, DependencyTestsAreNotAssociatedWithTheRootProject) {
    auto tree = dependency_boundary_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("dependency-boundary"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root  = project->root.join(PathBuf::from("root"_str).as_path());
    auto graph = lito::package::resolve_package_graph(root.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(1));
    EXPECT_EQ(graph->roots[usize {}].name.as_str(), "fixture-conventional-boundary-root"_str);
    EXPECT_EQ(graph->packages.len(), usize(2));
    EXPECT_TRUE(
        project_root_role(*graph, "fixture-conventional-boundary-dependency-test"_str).is_none());
}
