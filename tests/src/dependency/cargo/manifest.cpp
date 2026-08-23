#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

class CargoManifest : public ProjectFixture {};

TEST_F(CargoManifest, PackageCargoDependencyUsesTypedRecipeAndConsumption) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "cargo-manifest"
version = "0.1.0"

[features.rust]
default = true

[lib]
name = "cargo-manifest"
module = "cargo.manifest"
archive = "cargo.manifest"
sources = ["lib.cppm"]

[external-sources.rust]
path = "rust"

[external-dependencies.cargo.ffi]
source = "rust"
package = "fixture-ffi"
manifest-path = "crates/ffi/Cargo.toml"
features = ["zeta", "alpha"]
default-features = false
profile = "ffi-release"
usage = "link"
visibility = "link"
condition = "feature.rust"
)toml"_str },
        { "lib.cppm"_str, "export module cargo.manifest;\n"_str },
        { "rust/crates/ffi/Cargo.toml"_str, "[package]\nname = \"fixture-ffi\"\n"_str },
    };
    auto project = materialize("cargo-manifest"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto manifest = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(manifest.is_ok());
    ASSERT_EQ(manifest->cargo_external_dependencies.len(), usize(1));
    const auto& cargo = manifest->cargo_external_dependencies[usize {}];
    EXPECT_EQ(cargo.alias.as_str(), "ffi"_str);
    EXPECT_EQ(cargo.recipe.source.as_str(), "rust"_str);
    EXPECT_EQ(cargo.recipe.package.as_str(), "fixture-ffi"_str);
    EXPECT_EQ(cargo.recipe.manifest_path.as_path().to_str().unwrap(), "crates/ffi/Cargo.toml"_str);
    ASSERT_EQ(cargo.consumption.features.len(), usize(2));
    EXPECT_EQ(cargo.consumption.features[usize {}].as_str(), "alpha"_str);
    EXPECT_EQ(cargo.consumption.features[usize(1)].as_str(), "zeta"_str);
    EXPECT_FALSE(cargo.consumption.default_features);
    ASSERT_TRUE(cargo.consumption.profile.is_some());
    EXPECT_EQ(cargo.consumption.profile->as_str(), "ffi-release"_str);
    EXPECT_EQ(cargo.consumption.usage, lito::dependency::CargoDependencyUsage::Link);
    ASSERT_TRUE(cargo.consumption.visibility.is_some());
    EXPECT_EQ(*cargo.consumption.visibility, lito::dependency::DependencyVisibility::LinkOnly);
    ASSERT_TRUE(cargo.consumption.condition.is_some());
    EXPECT_EQ(cargo.consumption.condition->source.as_str(), "feature.rust"_str);
    ASSERT_TRUE(cargo.declaration_root.is_some());
    EXPECT_EQ(cargo.declaration_root->as_path(), project->root.as_path());

    auto standalone = lito::manifest::serialize_standalone_package_manifest(
        *manifest,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry =
                lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        });
    ASSERT_TRUE(standalone.is_ok());
    EXPECT_TRUE(standalone->as_str().contains("[external-dependencies.cargo.ffi]"_str));
    EXPECT_TRUE(standalone->as_str().contains("manifest-path = \"crates/ffi/Cargo.toml\""_str));
    EXPECT_TRUE(standalone->as_str().contains("usage = \"link\""_str));
    EXPECT_FALSE(standalone->as_str().contains("crate-type"_str));
    EXPECT_TRUE(standalone->as_str().contains("features = [\"alpha\", \"zeta\"]"_str));
    EXPECT_TRUE(standalone->as_str().contains("default-features = false"_str));
}

TEST_F(CargoManifest, CargoSchemaRejectsInvalidProviderContracts) {
    struct InvalidCase {
        ref<str> name;
        ref<str> declaration;
    };
    constexpr InvalidCase cases[] = {
        { "removed-crate-type"_str, R"toml(source = "rust"
package = "fixture-ffi"
crate-type = "staticlib"
visibility = "private")toml"_str },
        { "invalid-usage"_str, R"toml(source = "rust"
package = "fixture-ffi"
usage = "build"
visibility = "private")toml"_str },
        { "manifest-escape"_str, R"toml(source = "rust"
package = "fixture-ffi"
manifest-path = "../Cargo.toml"
visibility = "private")toml"_str },
        { "duplicate-feature"_str, R"toml(source = "rust"
package = "fixture-ffi"
features = ["same", "same"]
visibility = "private")toml"_str },
        { "missing-visibility"_str, R"toml(source = "rust"
package = "fixture-ffi")toml"_str },
        { "runtime-visibility"_str, R"toml(source = "rust"
package = "fixture-ffi"
usage = "runtime"
visibility = "private")toml"_str },
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto              text    = rstd::format(R"toml([package]
name = "cargo-invalid"
version = "0.1.0"
[lib]
name = "cargo-invalid"
module = "cargo.invalid"
archive = "cargo.invalid"
[external-sources.rust]
path = "rust"
[external-dependencies.cargo.ffi]
{}
)toml",
                                                 item.declaration);
        const ProjectFile files[] = {
            { "lito.toml"_str, text.as_str() },
            { "rust/Cargo.toml"_str, "[package]\nname = \"fixture-ffi\"\n"_str },
        };
        auto project = materialize(item.name, files);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::manifest::load_package_manifest(project->root.as_path()).is_err());
    }
}

TEST_F(CargoManifest, WorkspaceOwnsCargoRecipeAndMemberOwnsConsumption) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "cargo-workspace"
members = ["app"]

[workspace.package]
version = "0.1.0"

[workspace.external-sources.rust]
path = "rust"

[workspace.external-dependencies.cargo.ffi]
source = "rust"
package = "fixture-ffi"
)toml"_str },
        { "app/lito.toml"_str, R"toml([package]
name = "cargo-workspace-app"
version = { workspace = true }

[lib]
name = "cargo-workspace-app"
module = "cargo.workspace.app"
archive = "cargo.workspace.app"

[external-dependencies.cargo.ffi]
workspace = true
features = ["member"]
default-features = false
profile = "release"
usage = "runtime"
condition = "true"
)toml"_str },
        { "rust/Cargo.toml"_str, "[package]\nname = \"fixture-ffi\"\n"_str },
    };
    auto project = materialize("cargo-workspace"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    const auto& manifest = graph->packages[usize {}].manifest;
    EXPECT_TRUE(manifest.workspace_cargo_external_dependencies.is_empty());
    ASSERT_EQ(manifest.cargo_external_dependencies.len(), usize(1));
    const auto& cargo = manifest.cargo_external_dependencies[usize {}];
    EXPECT_EQ(cargo.recipe.source.as_str(), "rust"_str);
    EXPECT_EQ(cargo.recipe.package.as_str(), "fixture-ffi"_str);
    EXPECT_EQ(cargo.consumption.usage, lito::dependency::CargoDependencyUsage::Runtime);
    EXPECT_TRUE(cargo.consumption.visibility.is_none());
    ASSERT_EQ(cargo.consumption.features.len(), usize(1));
    EXPECT_EQ(cargo.consumption.features[usize {}].as_str(), "member"_str);
    ASSERT_TRUE(cargo.declaration_root.is_some());
    EXPECT_EQ(cargo.declaration_root->as_path(), project->root.as_path());
    ASSERT_EQ(manifest.external_sources.len(), usize(1));
    ASSERT_TRUE(manifest.external_sources[usize {}].declaration_root.is_some());
    EXPECT_EQ(manifest.external_sources[usize {}].declaration_root->as_path(),
              project->root.as_path());
}
