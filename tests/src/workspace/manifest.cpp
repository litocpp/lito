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

TEST(Workspace, WorkspaceNameIsRequiredAndValidatedByManifestOwner) {
    auto missing =
        lito::load_manifest_document(fixture_path("workspace/name-missing"_str).as_path());
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    EXPECT_TRUE(error_chain_text(missing_error).as_str().contains("missing 'name'"_str));

    auto invalid =
        lito::load_manifest_document(fixture_path("workspace/name-invalid"_str).as_path());
    ASSERT_TRUE(invalid.is_err());
    auto invalid_error = rstd::move(invalid).unwrap_err();
    EXPECT_TRUE(error_chain_text(invalid_error).as_str().contains("workspace.name"_str));

    auto valid = lito::load_manifest_document(repository_path("demo/workspace"_str).as_path());
    ASSERT_TRUE(valid.is_ok());
    ASSERT_TRUE(valid->workspace.is_some());
    EXPECT_EQ(valid->workspace->name.as_str(), "demo-workspace"_str);
}

TEST(Workspace, DevelopmentDependenciesHavePrivateDistinctScope) {
    auto visibility = lito::load_package_manifest(
        fixture_path("workspace/dev-dependency-visibility"_str).as_path());
    ASSERT_TRUE(visibility.is_err());
    auto visibility_error = rstd::move(visibility).unwrap_err();
    EXPECT_TRUE(error_chain_text(visibility_error).as_str().contains("visibility"_str));

    auto duplicate = lito::load_package_manifest(
        fixture_path("workspace/dev-dependency-duplicate"_str).as_path());
    ASSERT_TRUE(duplicate.is_err());
    auto duplicate_error = rstd::move(duplicate).unwrap_err();
    EXPECT_TRUE(error_chain_text(duplicate_error)
                    .as_str()
                    .contains("both dependencies and dev-dependencies"_str));
}

TEST(Workspace, WorkspaceDependenciesAreDeclaredOnceAndMaterializedForMembers) {
    auto directory = fixture_path("workspace/inherited-dependencies"_str);
    auto member    = lito::load_package_manifest(
        fixture_path("workspace/inherited-dependencies/app"_str).as_path());
    ASSERT_TRUE(member.is_ok());
    EXPECT_TRUE(member->dependencies.is_empty());
    EXPECT_TRUE(member->pkg_config_external_dependencies.is_empty());
    EXPECT_TRUE(member->cmake_external_dependencies.is_empty());
    ASSERT_EQ(member->workspace_dependencies.len(), usize(1));
    ASSERT_EQ(member->workspace_pkg_config_external_dependencies.len(), usize(1));
    ASSERT_EQ(member->workspace_cmake_external_dependencies.len(), usize(1));

    auto document = lito::load_manifest_document(directory.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    ASSERT_EQ(document->workspace->dependencies.len(), usize(1));
    ASSERT_EQ(document->workspace->pkg_config_external_dependencies.len(), usize(1));
    ASSERT_EQ(document->workspace->cmake_external_dependencies.len(), usize(1));
    EXPECT_TRUE(document->workspace->dependencies[usize {}].source.is_Path());
    EXPECT_TRUE(document->workspace->cmake_external_dependencies[usize {}].source.is_Path());

    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(2));
    const auto& app = graph->packages[usize {}];
    EXPECT_EQ(app.manifest.name.as_str(), "fixture-workspace-inherited-app"_str);
    EXPECT_TRUE(app.manifest.workspace_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_pkg_config_external_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_cmake_external_dependencies.is_empty());
    ASSERT_EQ(app.dependencies.len(), usize(1));
    EXPECT_EQ(app.dependencies[usize {}].name.as_str(), "fixture-workspace-inherited-library"_str);
    ASSERT_EQ(app.manifest.dependencies.len(), usize(1));
    ASSERT_TRUE(app.manifest.dependencies[usize {}].declaration_root.is_some());
    EXPECT_EQ(app.manifest.dependencies[usize {}].declaration_root->as_path(), directory.as_path());

    ASSERT_EQ(app.manifest.pkg_config_external_dependencies.len(), usize(1));
    const auto& curl = app.manifest.pkg_config_external_dependencies[usize {}];
    EXPECT_EQ(curl.alias.as_str(), "curl"_str);
    EXPECT_EQ(curl.requirement.module.as_str(), "libcurl"_str);
    EXPECT_EQ(curl.requirement.mode, lito::PkgConfigQueryMode::Static);
    EXPECT_EQ(curl.visibility, lito::DependencyVisibility::Public);

    ASSERT_EQ(app.manifest.cmake_external_dependencies.len(), usize(1));
    const auto& cmake = app.manifest.cmake_external_dependencies[usize {}];
    EXPECT_EQ(cmake.alias.as_str(), "fixture"_str);
    ASSERT_EQ(cmake.targets.len(), usize(1));
    EXPECT_EQ(cmake.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);
    EXPECT_EQ(cmake.integration, lito::CMakeIntegration::BuildTree);
    ASSERT_TRUE(cmake.adapter.is_some());
    EXPECT_EQ(cmake.adapter->as_path().to_str().unwrap(), "fixture-adapter.cmake"_str);
    ASSERT_TRUE(cmake.declaration_root.is_some());
    EXPECT_EQ(cmake.declaration_root->as_path(), directory.as_path());
    ASSERT_TRUE(cmake.adapter_root.is_some());
    EXPECT_EQ(cmake.adapter_root->as_path(), directory.as_path());

    ASSERT_TRUE(lito::prepare_external_dependency_sources(*graph, {}).is_ok());
    ASSERT_EQ(graph->packages[usize {}].cmake_external_dependencies.len(), usize(1));
    const auto& resolved = graph->packages[usize {}].cmake_external_dependencies[usize {}];
    ASSERT_TRUE(resolved.source.is_Directory());
    EXPECT_EQ(resolved.source.as_Directory().root.as_path(),
              fixture_path("dependency/cmake/project/package"_str).as_path());
    ASSERT_TRUE(resolved.adapter.is_some());
    EXPECT_EQ(resolved.adapter->as_path(),
              fixture_path("workspace/inherited-dependencies/fixture-adapter.cmake"_str).as_path());

    auto member_graph = lito::resolve_package_graph(
        fixture_path("workspace/inherited-dependencies/app"_str).as_path());
    ASSERT_TRUE(member_graph.is_ok());
    EXPECT_TRUE(member_graph->root_is_workspace);
    ASSERT_EQ(member_graph->packages.len(), usize(2));
    EXPECT_EQ(member_graph->packages[usize {}].dependencies.len(), usize(1));
}
