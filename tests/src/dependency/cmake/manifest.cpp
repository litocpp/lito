#include <rstd/test/gtest.hpp>

import rstd;
import lito.crypto;
import lito.tools;
import rstd.test;
import lito.core;
import lito.system;
import lito.tools.cmake;
import lito.driver;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class CMakeManifest : public ProjectFixture {
protected:
    auto manifest_project(ref<str> name, ref<str> contents)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        const ProjectFile files[] = {
            { .path = "lito.toml"_str, .contents = contents },
        };
        return materialize(name, files);
    }
};

TEST_F(CMakeManifest, CMakeManifestIsTypedAndSourceIsResolvedByExternalOwner) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "cmake-valid"
version = "0.1.0"
[features.qt]
default = false
[lib]
name = "cmake-valid"
module = "cmake_valid"
archive = "cmake_valid"
[external-dependencies.cmake.vulkan]
package = "Vulkan"
components = ["Headers"]
condition = "!feature.qt"
targets = [{ name = "Vulkan::Vulkan", visibility = "public" }]
[external-sources.fixture]
path = "package"
[external-dependencies.cmake.fixture]
package = "LitoFixture"
source = "fixture"
config-directory = "lib/cmake/LitoFixture"
cache = { LITO_FIXTURE_OPTION = true }
host-tools = [{ name = "generator", target = "LitoFixture::generator" }]
targets = [
  { name = "LitoFixture::fixture", visibility = "private" },
  { name = "LitoFixture::headers", visibility = "public" },
  { name = "LitoFixture::order", visibility = "link" },
]
)"_str },
        { "package/CMakeLists.txt"_str, "cmake_minimum_required(VERSION 3.28)\n"_str },
    };
    auto project = materialize("valid"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->dependencies.is_empty());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(2));

    const auto installed_index =
        loaded->cmake_external_dependencies[usize {}].alias.as_str() == "vulkan"_str ? usize {}
                                                                                     : usize(1);
    const auto  source_index          = installed_index == usize {} ? usize(1) : usize {};
    const auto& installed_requirement = loaded->cmake_external_dependencies[installed_index];
    EXPECT_EQ(installed_requirement.package.as_str(), "Vulkan"_str);
    ASSERT_EQ(installed_requirement.components.len(), usize(1));
    EXPECT_EQ(installed_requirement.components[usize {}].as_str(), "Headers"_str);
    ASSERT_TRUE(installed_requirement.condition.is_some());
    EXPECT_EQ(installed_requirement.condition->source.as_str(), "!feature.qt"_str);
    ASSERT_EQ(installed_requirement.targets.len(), usize(1));
    EXPECT_EQ(installed_requirement.targets[usize {}].name.as_str(), "Vulkan::Vulkan"_str);
    EXPECT_TRUE(installed_requirement.source.is_none());

    const auto& source_requirement = loaded->cmake_external_dependencies[source_index];
    EXPECT_EQ(source_requirement.package.as_str(), "LitoFixture"_str);
    ASSERT_EQ(source_requirement.targets.len(), usize(3));
    EXPECT_EQ(source_requirement.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);
    EXPECT_EQ(source_requirement.targets[usize(1)].name.as_str(), "LitoFixture::headers"_str);
    EXPECT_EQ(source_requirement.targets[usize(2)].name.as_str(), "LitoFixture::order"_str);
    ASSERT_TRUE(source_requirement.source.is_some());
    EXPECT_EQ(source_requirement.source->as_str(), "fixture"_str);
    ASSERT_EQ(source_requirement.cache.len(), usize(1));
    EXPECT_EQ(source_requirement.cache[usize {}].name.as_str(), "LITO_FIXTURE_OPTION"_str);
    EXPECT_EQ(source_requirement.cache[usize {}].value.as_str(), "ON"_str);
    ASSERT_TRUE(source_requirement.config_directory.is_some());
    EXPECT_EQ(source_requirement.config_directory->as_path().to_str().unwrap(),
              "lib/cmake/LitoFixture"_str);
    ASSERT_EQ(source_requirement.host_tools.len(), usize(1));
    EXPECT_EQ(source_requirement.host_tools[usize {}].name.as_str(), "generator"_str);
    EXPECT_EQ(source_requirement.host_tools[usize {}].target.as_str(),
              "LitoFixture::generator"_str);
    ASSERT_EQ(loaded->external_sources.len(), usize(1));
    EXPECT_EQ(loaded->external_sources[usize {}].name.as_str(), "fixture"_str);
    EXPECT_TRUE(loaded->external_sources[usize {}].source.is_Path());

    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    auto external = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(external.is_ok());
    ASSERT_EQ(external->dependencies.len(), usize(2));
    const auto resolved_index =
        external->dependencies[usize {}].requirement.alias.as_str() == "fixture"_str ? usize {}
                                                                                     : usize(1);
    const auto& resolved = external->dependencies[resolved_index].requirement;
    ASSERT_TRUE(resolved.source.is_Directory());
    EXPECT_FALSE(resolved.source.as_Directory().identity.is_empty());
    EXPECT_FALSE(resolved.source.as_Directory().root.as_path().to_str().unwrap().is_empty());
}

TEST_F(CMakeManifest, CMakeSourceAdapterIsTypedAndResolvedByPackageOwner) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "cmake-source-adapter"
version = "0.1.0"
[lib]
name = "cmake-source-adapter"
module = "cmake_source_adapter"
archive = "cmake_source_adapter"
[external-sources.fixture]
path = "project"
[external-dependencies.cmake.fixture]
package = "LitoSourceAdapter"
source = "fixture"
adapter = "adapter.cmake"
targets = [{ name = "LitoSourceAdapter::fixture", visibility = "private" }]
)"_str },
        { "adapter.cmake"_str, "set(LitoSourceAdapter_VERSION \"4.5.6\")\n"_str },
        { "project/CMakeLists.txt"_str, "cmake_minimum_required(VERSION 3.28)\n"_str },
    };
    auto project = materialize("source-adapter-manifest"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto loaded    = lito::manifest::load_package_manifest(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& declared = loaded->cmake_external_dependencies[usize {}];
    ASSERT_TRUE(declared.source.is_some());
    EXPECT_EQ(declared.source->as_str(), "fixture"_str);
    ASSERT_EQ(loaded->external_sources.len(), usize(1));
    ASSERT_TRUE(loaded->external_sources[usize {}].source.is_Path());
    ASSERT_TRUE(declared.adapter.is_some());
    EXPECT_EQ(declared.adapter->as_path().to_str().unwrap(), "adapter.cmake"_str);

    auto graph = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    auto external = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(external.is_ok());
    ASSERT_EQ(external->dependencies.len(), usize(1));
    const auto& resolved = external->dependencies[usize {}].requirement;
    EXPECT_TRUE(resolved.source.is_Directory());
    ASSERT_TRUE(resolved.adapter.is_some());
    EXPECT_TRUE(resolved.adapter->as_path().starts_with(directory.as_path()));
}

TEST_F(CMakeManifest, PackageOwnedPathUsesOneResolvedRelationForLockAndPreparation) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "cmake-package-owned"
version = "0.1.0"
[lib]
name = "cmake-package-owned"
module = "cmake.package_owned"
archive = "cmake.package_owned"
[external-sources.shaders]
path = "shaders"
[external-dependencies.cmake.shader]
package = "FixtureShader"
source = "shaders"
targets = [{ name = "FixtureShader::shader", visibility = "private" }]
)"_str },
        { "shaders/CMakeLists.txt"_str,
          "cmake_minimum_required(VERSION 3.28)\nproject(FixtureShader)\n"_str },
    };
    auto project = materialize("package-owned"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto graph     = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    auto source_count = graph->sources.len();

    auto prepared_sources = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(prepared_sources.is_ok());
    ASSERT_EQ(graph->packages[usize {}].externals.len(), usize(1));
    const auto& external = graph->packages[usize {}].externals[usize {}];
    ASSERT_TRUE(external.source.is_Package());
    EXPECT_EQ(external.source.as_Package().path.as_path().to_str().unwrap(), "shaders"_str);

    ASSERT_EQ(prepared_sources->dependencies.len(), usize(1));
    const auto& prepared = prepared_sources->dependencies[usize {}].requirement;
    ASSERT_TRUE(prepared.source.is_Directory());
    EXPECT_TRUE(prepared.source.as_Directory().identity.as_str().starts_with(
        "lito-package-external-v1\n"_str));
    auto expected =
        rstd::fs::canonicalize(directory.join(PathBuf::from("shaders"_str).as_path()).as_path());
    ASSERT_TRUE(expected.is_ok());
    EXPECT_TRUE(prepared.source.as_Directory().root.as_path().starts_with(expected->as_path()));
    EXPECT_TRUE(expected->as_path().starts_with(prepared.source.as_Directory().root.as_path()));
    EXPECT_EQ(graph->sources.len(), source_count);
}

TEST_F(CMakeManifest, InstalledOverridePreservesLockedGitProvenanceWithoutFetching) {
    auto reference = lito::source::GitReference {
        .kind  = lito::source::GitReferenceKind::Branch,
        .value = String::make("main"_str),
    };
    auto graph   = external_git_graph("https://example.invalid/fixture.git"_str,
                                      source_root("cmake-override-git"_str).as_path(),
                                      reference.clone());
    auto options = lito::source::SourceResolutionOptions {
        .locked = true,
    };
    options.git_sources.push(lito::source::GitSourcePin {
        .git       = String::make("https://example.invalid/fixture.git"_str),
        .reference = rstd::move(reference),
        .commit    = String::make("0123456789abcdef0123456789abcdef01234567"_str),
    });
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto declared = lito::resolve_external_dependency_sources(
        graph, rstd::move(options), resolver, *environment);
    ASSERT_TRUE(declared.is_ok());
    ASSERT_EQ(graph.packages[usize {}].externals.len(), usize(1));
    ASSERT_TRUE(graph.packages[usize {}].externals[usize {}].source.is_Git());
    EXPECT_EQ(graph.packages[usize {}].externals[usize {}].source.as_Git().commit.as_str(),
              "0123456789abcdef0123456789abcdef01234567"_str);

    auto selected  = strings("fixture-root"_str);
    auto overrides = lito::dependency::CMakeBuildOverrideSet {};
    overrides.entries.push(lito::dependency::CMakeBuildOverride {
        .package = String::make("Fixture"_str),
    });
    auto prepared = lito::prepare_external_dependency_sources(
        graph, selected, rstd::move(declared).unwrap(), overrides, resolver, *environment);
    ASSERT_TRUE(prepared.is_ok());
    ASSERT_EQ(prepared->dependencies.len(), usize(1));
    EXPECT_TRUE(prepared->dependencies[usize {}].requirement.source.is_Find());
    EXPECT_TRUE(prepared->dependencies[usize {}].requirement.source_name.is_none());
}

TEST_F(CMakeManifest, InstalledOverrideRejectsMissesAndRetainsAdapter) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "cmake-override-source-adapter"
version = "0.1.0"
[lib]
name = "cmake-override-source-adapter"
module = "cmake.override_source_adapter"
archive = "cmake.override_source_adapter"
[external-sources.fixture]
path = "project"
[external-dependencies.cmake.fixture]
package = "LitoSourceAdapter"
source = "fixture"
adapter = "adapter.cmake"
targets = [{ name = "LitoSourceAdapter::fixture", visibility = "private" }]
)"_str },
        { "adapter.cmake"_str, "set(LitoSourceAdapter_VERSION \"1.0.0\")\n"_str },
        { "project/CMakeLists.txt"_str, "cmake_minimum_required(VERSION 3.28)\n"_str },
    };
    auto project = materialize("cmake-override-source-adapter"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver  = lito::tools::ToolResolver(*environment);
    auto overrides = lito::dependency::CMakeBuildOverrideSet {};
    overrides.entries.push(lito::dependency::CMakeBuildOverride {
        .package = String::make("LitoSourceAdapter"_str),
    });
    auto unselected_declared =
        lito::resolve_external_dependency_sources(*graph, {}, resolver, *environment);
    ASSERT_TRUE(unselected_declared.is_ok());
    auto unselected = lito::prepare_external_dependency_sources(
        *graph, {}, rstd::move(unselected_declared).unwrap(), overrides, resolver, *environment);
    ASSERT_TRUE(unselected.is_ok());
    EXPECT_TRUE(unselected->dependencies.is_empty());

    auto declared = lito::resolve_external_dependency_sources(*graph, {}, resolver, *environment);
    ASSERT_TRUE(declared.is_ok());

    auto selected = strings("cmake-override-source-adapter"_str);
    auto missing  = lito::dependency::CMakeBuildOverrideSet {};
    missing.entries.push(lito::dependency::CMakeBuildOverride {
        .package = String::make("Missing"_str),
    });
    auto missing_result = lito::prepare_external_dependency_sources(
        *graph, selected, rstd::move(declared).unwrap(), missing, resolver, *environment);
    ASSERT_TRUE(missing_result.is_err());
    EXPECT_TRUE(error_chain_text(missing_result.unwrap_err())
                    .as_str()
                    .contains("does not match any CMake package"_str));

    auto declared_again =
        lito::resolve_external_dependency_sources(*graph, {}, resolver, *environment);
    ASSERT_TRUE(declared_again.is_ok());
    auto override_result = lito::prepare_external_dependency_sources(
        *graph, selected, rstd::move(declared_again).unwrap(), overrides, resolver, *environment);
    ASSERT_TRUE(override_result.is_ok());
    ASSERT_EQ(override_result->dependencies.len(), usize(1));
    const auto& overridden = override_result->dependencies[usize {}];
    EXPECT_TRUE(overridden.installed_override);
    EXPECT_TRUE(overridden.requirement.source.is_Find());
    EXPECT_TRUE(overridden.requirement.source_name.is_none());
    ASSERT_TRUE(overridden.requirement.adapter.is_some());
    EXPECT_TRUE(overridden.requirement.adapter->as_path().starts_with(project->root.as_path()));
}

TEST_F(CMakeManifest, CMakeInvalidManifestDocumentsAreRejectedByManifestOwner) {
    struct InvalidManifest {
        ref<str> name;
        ref<str> dependency;
    };
    constexpr InvalidManifest manifests[] = {
        { "adapter-config-directory"_str, R"([external-dependencies.cmake.fixture]
package = "Fixture"
path = "package"
adapter = "adapter.cmake"
config-directory = "lib/cmake/Fixture"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "legacy-find-package"_str, R"([external-dependencies.cmake.fixture]
find-package = "Fixture"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "add-subdirectory-install"_str, R"([external-dependencies.cmake.fixture]
package = "Fixture"
path = "package"
add-subdirectory = false
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "add-subdirectory-type"_str, R"([external-dependencies.cmake.fixture]
package = "Fixture"
path = "package"
integration = "build-tree"
add-subdirectory = "false"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "add-subdirectory-without-adapter"_str, R"([external-dependencies.cmake.fixture]
package = "Fixture"
path = "package"
integration = "build-tree"
add-subdirectory = false
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "archive-missing-sha"_str, R"([external-dependencies.cmake.fixture]
package = "Fixture"
archive = "https://example.com/fixture.tar.gz"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "build-tree-installed"_str, R"([external-dependencies.cmake.fixture]
package = "Fixture"
integration = "build-tree"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "config-directory-parent"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
path = "package"
config-directory = "../LitoFixture"
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)"_str },
        { "duplicate-target"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
targets = [
  { name = "LitoFixture::fixture", visibility = "private" },
  { name = "LitoFixture::fixture", visibility = "public" },
]
)"_str },
        { "duplicate-component"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
components = ["Core", "Core"]
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)"_str },
        { "empty-components"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
components = []
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)"_str },
        { "invalid-condition"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
condition = "feature.qt &&"
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)"_str },
        { "unsafe-component"_str, R"toml([external-dependencies.cmake.fixture]
package = "LitoFixture"
components = ["Core;include(evil)"]
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)toml"_str },
        { "empty-targets"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
targets = []
)"_str },
        { "installed-cache"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
cache = { LITO_FIXTURE_OPTION = true }
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)"_str },
        { "installed-config-directory"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
config-directory = "lib/cmake/LitoFixture"
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)"_str },
        { "legacy-dependency"_str, R"([dependencies.fixture]
cmake = "LitoFixture"
target = "LitoFixture::fixture"
)"_str },
        { "missing-target"_str, R"([external-dependencies.cmake.fixture]
package = "LitoFixture"
)"_str },
        { "provider-mix"_str, R"([external-dependencies.fixture]
module = "lito-fixture"
)"_str },
        { "unsafe-target"_str, R"toml([external-dependencies.cmake.fixture]
package = "LitoFixture"
targets = [{ name = "LitoFixture::fixture)", visibility = "private" }]
)toml"_str },
    };
    for (const auto& manifest : manifests) {
        auto contents = rstd::format("[package]\nname = \"cmake-{}\"\nversion = \"0.1.0\"\n{}",
                                     manifest.name,
                                     manifest.dependency);
        auto project  = manifest_project(manifest.name, contents.as_str());
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST_F(CMakeManifest, CMakeArchitectureArchivesManifestIsTyped) {
    auto project = manifest_project("architecture-archives"_str, R"([package]
name = "cmake-architecture-archives"
version = "0.1.0"
[lib]
name = "cmake-architecture-archives"
module = "cmake_architecture_archives"
archive = "cmake_architecture_archives"
[external-sources.fixture]
[external-sources.fixture.archives.x86_64]
archive = "https://example.com/fixture-linux64.tar.gz"
sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[external-sources.fixture.archives.aarch64]
archive = "https://example.com/fixture-linuxarm64.tar.gz"
sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
[external-dependencies.cmake.fixture]
package = "Fixture"
source = "fixture"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    ASSERT_EQ(loaded->external_sources.len(), usize(1));
    const auto& source = loaded->external_sources[usize {}].source;
    ASSERT_TRUE(source.is_ArchitectureArchives());
    const auto& variants = source.as_ArchitectureArchives().variants;
    ASSERT_EQ(variants.len(), usize(2));
    EXPECT_EQ(variants[usize {}].architecture.as_str(), "aarch64"_str);
    EXPECT_EQ(variants[usize {}].url.as_str(), "https://example.com/fixture-linuxarm64.tar.gz"_str);
    EXPECT_EQ(variants[usize(1)].architecture.as_str(), "x86_64"_str);
    EXPECT_EQ(variants[usize(1)].sha256.to_hex().as_str(),
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str);
}

TEST_F(CMakeManifest, CMakeArchitectureArchivesInvalidManifestsAreRejected) {
    struct InvalidArchiveManifest {
        ref<str> name;
        ref<str> source;
    };
    constexpr InvalidArchiveManifest manifests[] = {
        { "archives-empty"_str, "archives = {}"_str },
        { "archives-git-mix"_str, R"(git = "https://example.com/fixture.git"
archives = { x86_64 = { archive = "https://example.com/fixture.tar.gz", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
        { "archives-legacy-integration"_str, R"(integration = "install"
archives = { x86_64 = { archive = "https://example.com/fixture.tar.gz", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
        { "archives-invalid-sha"_str,
          R"(archives = { x86_64 = { archive = "https://example.com/fixture.tar.gz", sha256 = "invalid" } })"_str },
        { "archives-invalid-url"_str,
          R"(archives = { x86_64 = { archive = "-unsafe", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
        { "archives-missing-archive"_str,
          R"(archives = { x86_64 = { sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
        { "archives-missing-sha"_str,
          R"(archives = { x86_64 = { archive = "https://example.com/fixture.tar.gz" } })"_str },
        { "archives-noncanonical-architecture"_str,
          R"(archives = { arm64 = { archive = "https://example.com/fixture.tar.gz", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
        { "archives-path-mix"_str, R"(path = "package"
archives = { x86_64 = { archive = "https://example.com/fixture.tar.gz", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
        { "archives-source-mix"_str, R"(archive = "https://example.com/fixture.tar.gz"
sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
archives = { x86_64 = { archive = "https://example.com/fixture-linux64.tar.gz", sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" } })"_str },
        { "archives-unknown-field"_str,
          R"(archives = { x86_64 = { archive = "https://example.com/fixture.tar.gz", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", platform = "linux" } })"_str },
        { "archives-unsafe-architecture"_str,
          R"(archives = { "x86/64" = { archive = "https://example.com/fixture.tar.gz", sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } })"_str },
    };
    for (const auto& manifest : manifests) {
        auto contents =
            rstd::format("[package]\nname = \"cmake-{}\"\nversion = \"0.1.0\"\n"
                         "[external-sources.fixture]\n{}\n"
                         "[external-dependencies.cmake.fixture]\npackage = \"Fixture\"\n"
                         "source = \"fixture\"\n"
                         "targets = [{{ name = \"Fixture::fixture\", "
                         "visibility = \"private\" }}]\n",
                         manifest.name,
                         manifest.source);
        auto project = manifest_project(manifest.name, contents.as_str());
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST_F(CMakeManifest, CMakeArchitectureArchivesAreSelectedForEffectiveTarget) {
    auto variants = Vec<lito::dependency::ExternalArchiveVariant>::make();
    variants.push(lito::dependency::ExternalArchiveVariant {
        .architecture = lito::system::Architecture { .name = String::make("aarch64"_str) },
        .url    = lito::parse::FetchUrl::parse("https://example.com/arm64.tar.gz"_str).unwrap(),
        .sha256 = lito::crypto::Sha256Digest::parse_hex(
                      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"_str)
                      .unwrap(),
    });
    variants.push(lito::dependency::ExternalArchiveVariant {
        .architecture = lito::system::Architecture { .name = String::make("x86_64"_str) },
        .url          = lito::parse::FetchUrl::parse("https://example.com/x64.tar.gz"_str).unwrap(),
        .sha256       = lito::crypto::Sha256Digest::parse_hex(
                            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str)
                            .unwrap(),
    });
    auto requirement = lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::PreparedCMakeDependencySource::ArchitectureArchives(rstd::move(variants)),
    };

    auto native = lito::resolve_cmake_requirement_for_platform(requirement, native_platform());
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->source.is_Archive());
    EXPECT_EQ(native->source.as_Archive().url.as_str(), "https://example.com/x64.tar.gz"_str);

    auto arm = lito::resolve_cmake_requirement_for_platform(
        requirement, explicit_platform("aarch64-unknown-linux-gnu"_str));
    ASSERT_TRUE(arm.is_ok());
    ASSERT_TRUE(arm->source.is_Archive());
    EXPECT_EQ(arm->source.as_Archive().url.as_str(), "https://example.com/arm64.tar.gz"_str);
    EXPECT_EQ(arm->source.as_Archive().sha256.to_hex().as_str(),
              "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"_str);

    auto missing = lito::resolve_cmake_requirement_for_platform(
        requirement, explicit_platform("riscv64-unknown-linux-gnu"_str));
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    ASSERT_TRUE(missing_error.is_Message());
    const auto& message = missing_error.as_Message().message;
    EXPECT_TRUE(message.as_str().contains("fixture"_str));
    EXPECT_TRUE(message.as_str().contains("riscv64-unknown-linux-gnu"_str));
    EXPECT_TRUE(message.as_str().contains("architecture 'riscv64'"_str));
    EXPECT_TRUE(message.as_str().contains("aarch64, x86_64"_str));

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto cross_platform = explicit_platform("aarch64-unknown-linux-gnu"_str);
    arm->source         = lito::SelectedCMakeDependencySource::Directory(
        build_root("cross-cmake-source"_str), String::make("cross-cmake-source-v1"_str), false);
    auto cross_requirement = lito::materialize_cmake_requirement(*arm);
    ASSERT_TRUE(cross_requirement.is_ok());
    auto cross_cmake = lito::plan_cmake_package(*cross_requirement,
                                                fixture_cmake(),
                                                configuration(),
                                                default_profile(*parser),
                                                linker_identity(),
                                                cross_platform.compiler_default,
                                                cross_platform.effective_target.triple.as_str(),
                                                build_root("cross-cmake"_str).as_path());
    ASSERT_TRUE(cross_cmake.is_err());
    auto cross_error = rstd::move(cross_cmake).unwrap_err();
    EXPECT_TRUE(error_chain_text(cross_error)
                    .as_str()
                    .contains("without an explicit CMake toolchain file"_str));
}

TEST_F(CMakeManifest, CMakeArchitectureArchivesSurviveWorkspaceInheritanceAndPreparation) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([workspace]
name = "fixture-workspace-architecture-archives"
members = ["app"]
default-members = ["app"]
[workspace.package]
version = "0.1.0"
[workspace.external-sources.fixture]
[workspace.external-sources.fixture.archives.x86_64]
archive = "https://example.com/fixture-linux64.tar.gz"
sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[workspace.external-sources.fixture.archives.aarch64]
archive = "https://example.com/fixture-linuxarm64.tar.gz"
sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
[workspace.external-dependencies.cmake.fixture]
package = "Fixture"
components = ["Core", "Network"]
source = "fixture"
)"_str },
        { "app/lito.toml"_str, R"([package]
name = "fixture-workspace-architecture-archives-app"
version.workspace = true
[[bin]]
link-stdlib = false
name = "fixture-workspace-architecture-archives-app"
sources = ["main.cpp"]
[external-dependencies.cmake.fixture]
workspace = true
condition = "true"
targets = [{ name = "Fixture::fixture", visibility = "private" }]
)"_str },
        { "app/main.cpp"_str, "int main() { return 0; }\n"_str },
    };
    auto project = materialize("architecture-workspace"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto document  = lito::manifest::load_manifest_document(directory.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    ASSERT_EQ(document->workspace->external_sources.len(), usize(1));
    const auto& declared = document->workspace->external_sources[usize {}];
    ASSERT_TRUE(declared.source.is_ArchitectureArchives());
    ASSERT_EQ(declared.source.as_ArchitectureArchives().variants.len(), usize(2));

    auto graph = lito::package::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    ASSERT_EQ(graph->packages[usize {}].manifest.cmake_external_dependencies.len(), usize(1));
    const auto& inherited =
        graph->packages[usize {}].manifest.cmake_external_dependencies[usize {}];
    ASSERT_TRUE(inherited.source.is_some());
    EXPECT_EQ(inherited.source->as_str(), "fixture"_str);
    ASSERT_EQ(inherited.components.len(), usize(2));
    EXPECT_EQ(inherited.components[usize {}].as_str(), "Core"_str);
    EXPECT_EQ(inherited.components[usize(1)].as_str(), "Network"_str);
    ASSERT_TRUE(inherited.condition.is_some());
    EXPECT_EQ(inherited.condition->source.as_str(), "true"_str);
    ASSERT_EQ(inherited.targets.len(), usize(1));
    EXPECT_EQ(inherited.targets[usize {}].name.as_str(), "Fixture::fixture"_str);
    ASSERT_EQ(graph->packages[usize {}].manifest.external_sources.len(), usize(1));
    const auto& inherited_source = graph->packages[usize {}].manifest.external_sources[usize {}];
    ASSERT_TRUE(inherited_source.source.is_ArchitectureArchives());
    ASSERT_EQ(inherited_source.source.as_ArchitectureArchives().variants.len(), usize(2));

    const auto source_count     = graph->sources.len();
    auto       prepared_sources = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(prepared_sources.is_ok());
    ASSERT_EQ(prepared_sources->dependencies.len(), usize(1));
    const auto& prepared = prepared_sources->dependencies[usize {}].requirement;
    ASSERT_TRUE(prepared.source.is_ArchitectureArchives());
    EXPECT_EQ(prepared.source.as_ArchitectureArchives().variants.len(), usize(2));
    EXPECT_EQ(graph->sources.len(), source_count);
}

TEST_F(CMakeManifest, BuildPlatformMakesNativeAndExplicitTargetIntentObservable) {
    auto compiler_default = pkg_config_target();
    auto host             = lito::system::HostInfo {
        .architecture = compiler_default.architecture.clone(),
        .os           = compiler_default.os.clone(),
    };
    auto native = lito::system::resolve_build_platform(host, compiler_default, None());
    ASSERT_TRUE(native.is_ok());
    EXPECT_EQ(native->intent, lito::system::BuildTargetIntent::Native);
    EXPECT_FALSE(native->cross);
    EXPECT_EQ(native->effective_target.architecture.as_str(), "x86_64"_str);

    auto arm_default = lito::system::parse_target_info("aarch64-unknown-linux-gnu"_str);
    ASSERT_TRUE(arm_default.is_ok());
    auto unintentional_cross = lito::system::resolve_build_platform(host, *arm_default, None());
    ASSERT_TRUE(unintentional_cross.is_err());
    auto cross_error = rstd::move(unintentional_cross).unwrap_err();
    EXPECT_TRUE(rstd::format("{}", cross_error)
                    .as_str()
                    .contains("declare an explicit target/toolchain configuration"_str));

    auto explicit_cross = lito::system::resolve_build_platform(
        host, compiler_default, Some("aarch64-unknown-linux-gnu"_str));
    ASSERT_TRUE(explicit_cross.is_ok());
    EXPECT_EQ(explicit_cross->intent, lito::system::BuildTargetIntent::ExplicitTarget);
    EXPECT_TRUE(explicit_cross->cross);
    EXPECT_EQ(explicit_cross->effective_target.architecture.as_str(), "aarch64"_str);
}

TEST_F(CMakeManifest, CMakeManifestAcceptsUnnamespacedTargets) {
    auto project = manifest_project("unnamespaced-target"_str, R"([package]
name = "cmake-unnamespaced-target"
version = "0.1.0"
[lib]
name = "cmake-unnamespaced-target"
module = "cmake_unnamespaced_target"
archive = "cmake_unnamespaced_target"
[external-dependencies.cmake.quickjs]
package = "qjs"
targets = [{ name = "qjs", visibility = "private" }]
)"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& requirement = loaded->cmake_external_dependencies[usize {}];
    EXPECT_EQ(requirement.package.as_str(), "qjs"_str);
    ASSERT_EQ(requirement.targets.len(), usize(1));
    EXPECT_EQ(requirement.targets[usize {}].name.as_str(), "qjs"_str);
}
