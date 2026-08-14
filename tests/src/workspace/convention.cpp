#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
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
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(WorkspaceConvention, WorkspaceDiscoversAssociatedTestPackageWithInheritance) {
    auto graph = lito::resolve_package_graph(
        fixture_path("workspace/convention/test/workspace-package"_str).as_path());
    ASSERT_TRUE(graph.is_ok());

    auto associated = false;
    for (const auto& project_root : graph->roots) {
        if (project_root.name.as_str() != "fixture-associated-workspace-test"_str) continue;
        associated = true;
        EXPECT_EQ(project_root.role, lito::ProjectRootRole::AssociatedTest);
        EXPECT_EQ(project_root.source_identity.as_str(), "path+tests"_str);
    }
    EXPECT_TRUE(associated);

    for (const auto& package : graph->packages) {
        if (package.manifest.name.as_str() != "fixture-associated-workspace-test"_str) continue;
        EXPECT_TRUE(package.manifest.workspace_dependencies.is_empty());
        EXPECT_EQ(package.manifest.version.source, lito::PackageVersionSource::Workspace);
        ASSERT_TRUE(package.manifest.version.value.is_some());
        EXPECT_EQ(package.manifest.version.value->as_str(), "0.1.0"_str);
        EXPECT_EQ(package.dependencies.len(), usize(1));
    }
}

TEST(WorkspaceConvention, WorkspaceBenchmarkTargetsUseDevelopmentDependencies) {
    auto directory = fixture_path("workspace/convention/test/workspace-package"_str);
    auto graph     = lito::resolve_package_graph(directory.as_path());
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

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    EXPECT_TRUE(
        contains_name(production->selected_root_names, "fixture-associated-workspace-library"_str));
    EXPECT_FALSE(contains_name(production->selected_package_names,
                               "fixture-associated-workspace-bench-helper"_str));

    auto tests = lito::resolve_package_selection(
        lito::PackageSelection { .root = directory.clone() }, lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    EXPECT_TRUE(contains_name(tests->selected_root_names, "fixture-associated-workspace-test"_str));
    EXPECT_FALSE(contains_name(tests->selected_package_names,
                               "fixture-associated-workspace-bench-helper"_str));

    auto benchmark =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmark.is_ok());
    EXPECT_TRUE(
        contains_name(benchmark->selected_root_names, "fixture-associated-workspace-library"_str));
    EXPECT_TRUE(contains_name(benchmark->selected_package_names,
                              "fixture-associated-workspace-bench-helper"_str));

    auto standalone = lito::resolve_package_graph(
        fixture_path("workspace/convention/test/workspace-package/benches"_str).as_path());
    ASSERT_TRUE(standalone.is_ok());
    ASSERT_EQ(standalone->roots.len(), usize(1));
    EXPECT_EQ(standalone->roots[usize {}].name.as_str(),
              "fixture-associated-workspace-benchmark"_str);
    EXPECT_EQ(standalone->roots[usize {}].role, lito::ProjectRootRole::PrimaryPackage);
}

TEST(WorkspaceConvention, PackageOnlyManifestDiscoversConventionalBenchmark) {
    auto directory = fixture_path("workspace/convention/benchmark/package-only"_str);
    auto package   = lito::load_package_manifest(directory.as_path());
    ASSERT_TRUE(package.is_ok());
    ASSERT_EQ(package->targets.len(), usize(1));
    ASSERT_TRUE(package->targets[usize {}].is_Benchmark());
    EXPECT_EQ(package->targets[usize {}].as_Benchmark().name.as_str(), "only"_str);

    auto benchmark =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmark.is_ok());
    ASSERT_EQ(benchmark->selected_targets.len(), usize(1));
    EXPECT_EQ(benchmark->selected_targets[usize {}].name.as_str(), "only"_str);

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    EXPECT_TRUE(production.is_err());
}

TEST(WorkspaceConvention, SinglePackageDiscoversAssociatedTestPackage) {
    auto directory = fixture_path("workspace/convention/test/package"_str);
    auto graph     = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(2));
    auto primary = project_root_role(*graph, "fixture-conventional-library"_str);
    auto test    = project_root_role(*graph, "fixture-conventional-test"_str);
    ASSERT_TRUE(primary.is_some());
    ASSERT_TRUE(test.is_some());
    EXPECT_EQ(*primary, lito::ProjectRootRole::PrimaryPackage);
    EXPECT_EQ(*test, lito::ProjectRootRole::AssociatedTest);
    for (const auto& package : graph->packages) {
        if (package.manifest.name.as_str() != "fixture-conventional-test"_str) continue;
        EXPECT_EQ(package.manifest.version.source, lito::PackageVersionSource::Unspecified);
        EXPECT_TRUE(package.manifest.version.value.is_none());
    }

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    ASSERT_EQ(production->selected_root_names.len(), usize(1));
    EXPECT_EQ(production->selected_root_names[usize {}].as_str(),
              "fixture-conventional-library"_str);
    EXPECT_FALSE(
        contains_name(production->selected_package_names, "fixture-conventional-bench-helper"_str));

    auto benchmarks =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Benchmark);
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

    auto tests = lito::resolve_package_selection(
        lito::PackageSelection { .root = directory.clone() }, lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    ASSERT_EQ(tests->selected_root_names.len(), usize(1));
    EXPECT_EQ(tests->selected_root_names[usize {}].as_str(), "fixture-conventional-test"_str);
    EXPECT_TRUE(contains_name(tests->selected_package_names, "fixture-conventional-library"_str));

    auto selected = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-test"_str),
        },
        lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_root_names.len(), usize(1));

    auto selected_primary = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-library"_str),
        },
        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(selected_primary.is_ok());
    ASSERT_EQ(selected_primary->selected_root_names.len(), usize(1));

    auto test_as_production = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-test"_str),
        },
        lito::PackageSelectionPurpose::Production);
    EXPECT_TRUE(test_as_production.is_err());
}

TEST(WorkspaceConvention, WorkspaceDiscoversAssociatedTestWorkspace) {
    auto directory = fixture_path("workspace/convention/test/workspace"_str);
    auto graph     = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(3));
    auto library = project_root_role(*graph, "fixture-conventional-workspace-library"_str);
    auto runtime = project_root_role(*graph, "fixture-conventional-workspace-test"_str);
    auto compile = project_root_role(*graph, "fixture-conventional-workspace-compile-test"_str);
    ASSERT_TRUE(library.is_some());
    ASSERT_TRUE(runtime.is_some());
    ASSERT_TRUE(compile.is_some());
    EXPECT_EQ(*library, lito::ProjectRootRole::WorkspaceMember);
    EXPECT_EQ(*runtime, lito::ProjectRootRole::AssociatedTest);
    EXPECT_EQ(*compile, lito::ProjectRootRole::AssociatedTest);

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    ASSERT_EQ(production->selected_root_names.len(), usize(1));
    EXPECT_EQ(production->selected_root_names[usize {}].as_str(),
              "fixture-conventional-workspace-library"_str);

    auto tests = lito::resolve_package_selection(
        lito::PackageSelection { .root = directory.clone() }, lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    EXPECT_EQ(tests->selected_root_names.len(), usize(2));
    EXPECT_TRUE(
        contains_name(tests->selected_root_names, "fixture-conventional-workspace-test"_str));
    EXPECT_TRUE(contains_name(tests->selected_root_names,
                              "fixture-conventional-workspace-compile-test"_str));
    EXPECT_TRUE(
        contains_name(tests->selected_package_names, "fixture-conventional-workspace-library"_str));
}

TEST(WorkspaceConvention, DependencyTestsAreNotAssociatedWithTheRootProject) {
    auto graph = lito::resolve_package_graph(
        fixture_path("workspace/convention/test/dependency-boundary/root"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(1));
    EXPECT_EQ(graph->roots[usize {}].name.as_str(), "fixture-conventional-boundary-root"_str);
    EXPECT_EQ(graph->packages.len(), usize(2));
    EXPECT_TRUE(
        project_root_role(*graph, "fixture-conventional-boundary-dependency-test"_str).is_none());
}
