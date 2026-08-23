#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class Workspace : public ProjectFixture {
protected:
    auto manifest(ref<str> name, ref<str> contents)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        const ProjectFile files[] = {
            { "lito.toml"_str, contents },
        };
        return materialize(name, files);
    }
};

TEST_F(Workspace, WorkspaceNameIsRequiredAndValidatedByManifestOwner) {
    auto missing_project = manifest("name-missing"_str, "[workspace]\nmembers = []\n"_str);
    ASSERT_TRUE(missing_project.is_ok());
    auto missing = lito::manifest::load_manifest_document(missing_project->root.as_path());
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    EXPECT_TRUE(error_chain_text(missing_error).as_str().contains("missing 'name'"_str));

    auto invalid_project =
        manifest("name-invalid"_str, "[workspace]\nname = \"fixture.invalid\"\nmembers = []\n"_str);
    ASSERT_TRUE(invalid_project.is_ok());
    auto invalid = lito::manifest::load_manifest_document(invalid_project->root.as_path());
    ASSERT_TRUE(invalid.is_err());
    auto invalid_error = rstd::move(invalid).unwrap_err();
    EXPECT_TRUE(error_chain_text(invalid_error).as_str().contains("workspace.name"_str));

    const ProjectFile valid_files[] = {
        { "lito.toml"_str, "[workspace]\nname = \"demo-workspace\"\nmembers = [\"app\"]\n"_str },
        {
            "app/lito.toml"_str,
            R"toml([package]
name = "demo-app"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "demo-app"
sources = ["main.cpp"]
)toml"_str,
        },
    };
    auto valid_project = materialize("name-valid"_str, valid_files);
    ASSERT_TRUE(valid_project.is_ok());
    auto valid = lito::manifest::load_manifest_document(valid_project->root.as_path());
    ASSERT_TRUE(valid.is_ok());
    ASSERT_TRUE(valid->workspace.is_some());
    EXPECT_EQ(valid->workspace->name.as_str(), "demo-workspace"_str);
}

TEST_F(Workspace, DevelopmentDependenciesHavePrivateDistinctScope) {
    auto visibility_project = manifest("dev-visibility"_str, R"toml([package]
name = "fixture-dev-dependency-visibility"
version = "0.1.0"

[lib]
name = "fixture-dev-dependency-visibility"
module = "fixture.dev_dependency_visibility"
archive = "fixture-dev-dependency-visibility"
sources = ["lib.cppm"]

[dev-dependencies.helper]
path = "helper"
visibility = "private"
)toml"_str);
    ASSERT_TRUE(visibility_project.is_ok());
    auto visibility = lito::manifest::load_package_manifest(visibility_project->root.as_path());
    ASSERT_TRUE(visibility.is_err());
    auto visibility_error = rstd::move(visibility).unwrap_err();
    EXPECT_TRUE(error_chain_text(visibility_error).as_str().contains("visibility"_str));

    auto duplicate_project = manifest("dev-duplicate"_str, R"toml([package]
name = "fixture-dev-dependency-duplicate"
version = "0.1.0"

[lib]
name = "fixture-dev-dependency-duplicate"
module = "fixture.dev_dependency_duplicate"
archive = "fixture-dev-dependency-duplicate"
sources = ["lib.cppm"]

[dependencies.helper]
path = "helper"
visibility = "private"

[dev-dependencies.helper]
path = "helper"
)toml"_str);
    ASSERT_TRUE(duplicate_project.is_ok());
    auto duplicate = lito::manifest::load_package_manifest(duplicate_project->root.as_path());
    ASSERT_TRUE(duplicate.is_err());
    auto duplicate_error = rstd::move(duplicate).unwrap_err();
    EXPECT_TRUE(error_chain_text(duplicate_error)
                    .as_str()
                    .contains("both dependencies and dev-dependencies"_str));
}

TEST_F(Workspace, WorkspaceDependenciesAreDeclaredOnceAndMaterializedForMembers) {
    const ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([workspace]
name = "fixture-workspace-inherited"
members = ["library", "app"]
default-members = ["app"]

[workspace.package]
version = "0.1.0"

[workspace.dependencies.fixture-workspace-inherited-library]
path = "library"

[workspace.external-dependencies.pkg-config.curl]
module = "libcurl"
version = ">= 7.86.0"
static = true

[workspace.external-sources.fixture]
path = "cmake-package"

[workspace.external-dependencies.cmake.fixture]
package = "LitoFixture"
components = ["Core"]
source = "fixture"
adapter = "fixture-adapter.cmake"
)toml"_str,
        },
        {
            "library/lito.toml"_str,
            R"toml([package]
name = "fixture-workspace-inherited-library"
version.workspace = true

[lib]
name = "fixture-workspace-inherited-library"
module = "fixture.workspace_inherited"
archive = "fixture-workspace-inherited-library"
sources = ["library.cppm"]
)toml"_str,
        },
        { "library/library.cppm"_str, "export module fixture.workspace_inherited;\n"_str },
        {
            "app/lito.toml"_str,
            R"toml([package]
name = "fixture-workspace-inherited-app"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-workspace-inherited-app"
sources = ["main.cpp"]

[dependencies.fixture-workspace-inherited-library]
workspace = true
visibility = "private"

[external-dependencies.pkg-config.curl]
workspace = true
visibility = "public"
condition = "true"

[external-dependencies.cmake.fixture]
workspace = true
condition = "true"
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)toml"_str,
        },
        { "app/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "cmake-package/CMakeLists.txt"_str,
          "cmake_minimum_required(VERSION 3.28)\nproject(LitoFixture)\n"_str },
        {
            "fixture-adapter.cmake"_str,
            R"cmake(if(NOT TARGET LitoFixture::fixture)
  add_library(LitoFixture::fixture ALIAS lito_fixture)
endif()

set(LitoFixture_VERSION "1.2.3")
)cmake"_str,
        },
    };
    auto project = materialize("inherited-dependencies"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto app_path  = directory.join(PathBuf::from("app"_str).as_path());
    auto member    = lito::manifest::load_package_manifest(app_path.as_path());
    ASSERT_TRUE(member.is_ok());
    EXPECT_TRUE(member->dependencies.is_empty());
    EXPECT_TRUE(member->pkg_config_external_dependencies.is_empty());
    EXPECT_TRUE(member->cmake_external_dependencies.is_empty());
    ASSERT_EQ(member->workspace_dependencies.len(), usize(1));
    ASSERT_EQ(member->workspace_pkg_config_external_dependencies.len(), usize(1));
    ASSERT_EQ(member->workspace_cmake_external_dependencies.len(), usize(1));
    EXPECT_TRUE(member->workspace_external_sources.is_empty());

    auto document = lito::manifest::load_manifest_document(directory.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    ASSERT_EQ(document->workspace->dependencies.len(), usize(1));
    ASSERT_EQ(document->workspace->pkg_config_external_dependencies.len(), usize(1));
    ASSERT_EQ(document->workspace->cmake_external_dependencies.len(), usize(1));
    EXPECT_TRUE(document->workspace->dependencies[usize {}].source.is_Path());
    ASSERT_EQ(document->workspace->external_sources.len(), usize(1));
    EXPECT_TRUE(document->workspace->external_sources[usize {}].source.is_Path());
    ASSERT_TRUE(document->workspace->cmake_external_dependencies[usize {}].source.is_some());
    EXPECT_EQ(document->workspace->cmake_external_dependencies[usize {}].source->as_str(),
              "fixture"_str);

    auto graph = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(2));
    const auto& app = graph->packages[usize {}];
    EXPECT_EQ(app.manifest.name.as_str(), "fixture-workspace-inherited-app"_str);
    EXPECT_TRUE(app.manifest.workspace_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_pkg_config_external_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_cmake_external_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_external_sources.is_empty());
    ASSERT_EQ(app.dependencies.len(), usize(1));
    EXPECT_EQ(lito::package::resolved_dependency_name(app.dependencies[usize {}]),
              "fixture-workspace-inherited-library"_str);
    ASSERT_EQ(app.manifest.dependencies.len(), usize(1));
    ASSERT_TRUE(app.manifest.dependencies[usize {}].declaration_root.is_some());
    EXPECT_EQ(app.manifest.dependencies[usize {}].declaration_root->as_path(), directory.as_path());

    ASSERT_EQ(app.manifest.pkg_config_external_dependencies.len(), usize(1));
    const auto& curl = app.manifest.pkg_config_external_dependencies[usize {}];
    EXPECT_EQ(curl.alias.as_str(), "curl"_str);
    EXPECT_EQ(curl.requirement.module.as_str(), "libcurl"_str);
    EXPECT_EQ(curl.requirement.mode, lito::dependency::PkgConfigQueryMode::Static);
    EXPECT_EQ(curl.visibility, lito::dependency::DependencyVisibility::Public);
    ASSERT_TRUE(curl.condition.is_some());
    EXPECT_EQ(curl.condition->source.as_str(), "true"_str);

    ASSERT_EQ(app.manifest.cmake_external_dependencies.len(), usize(1));
    const auto& cmake = app.manifest.cmake_external_dependencies[usize {}];
    EXPECT_EQ(cmake.alias.as_str(), "fixture"_str);
    ASSERT_EQ(cmake.components.len(), usize(1));
    EXPECT_EQ(cmake.components[usize {}].as_str(), "Core"_str);
    ASSERT_TRUE(cmake.condition.is_some());
    EXPECT_EQ(cmake.condition->source.as_str(), "true"_str);
    ASSERT_EQ(cmake.targets.len(), usize(1));
    EXPECT_EQ(cmake.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);
    ASSERT_TRUE(cmake.adapter.is_some());
    EXPECT_EQ(cmake.adapter->as_path().to_str().unwrap(), "fixture-adapter.cmake"_str);
    ASSERT_TRUE(cmake.declaration_root.is_some());
    EXPECT_EQ(cmake.declaration_root->as_path(), directory.as_path());
    ASSERT_TRUE(cmake.adapter_root.is_some());
    EXPECT_EQ(cmake.adapter_root->as_path(), directory.as_path());
    ASSERT_TRUE(cmake.source.is_some());
    EXPECT_EQ(cmake.source->as_str(), "fixture"_str);
    ASSERT_EQ(app.manifest.external_sources.len(), usize(1));
    EXPECT_TRUE(app.manifest.external_sources[usize {}].source.is_Path());

    auto prepared_sources = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(prepared_sources.is_ok());
    ASSERT_EQ(prepared_sources->cmake_dependencies.len(), usize(1));
    const auto& resolved = prepared_sources->cmake_dependencies[usize {}].requirement;
    ASSERT_TRUE(resolved.source.is_Directory());
    EXPECT_EQ(resolved.source.as_Directory().root.as_path(),
              directory.join(PathBuf::from("cmake-package"_str).as_path()).as_path());
    ASSERT_TRUE(resolved.adapter.is_some());
    EXPECT_EQ(resolved.adapter->as_path(),
              directory.join(PathBuf::from("fixture-adapter.cmake"_str).as_path()).as_path());

    auto member_graph = lito::package::resolve_package_graph(app_path.as_path());
    ASSERT_TRUE(member_graph.is_ok());
    EXPECT_TRUE(member_graph->root_is_workspace);
    ASSERT_EQ(member_graph->packages.len(), usize(2));
    EXPECT_EQ(member_graph->packages[usize {}].dependencies.len(), usize(1));
}
