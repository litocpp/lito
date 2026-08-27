#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.driver;
import lito.system;
import lito.tools.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class InstallSource : public ProjectFixture {};

TEST_F(InstallSource, InstallSourceAndConfiguredRootAreOwnedByInstallDomain) {
    auto tree = install_selection_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("install-source-workspace"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto workspace = project->root.clone();
    auto directory = workspace.join(PathBuf::from("multi-target"_str).as_path());
    auto source    = lito::resolve_install_source(
        lito::InstallSourceRequirement::LocalProject(directory.clone()));
    ASSERT_TRUE(source.is_ok());
    EXPECT_EQ(source->project.root.as_path(), workspace.as_path());
    EXPECT_TRUE(source->provenance.is_Local());
    EXPECT_EQ(source->identity.as_str(), rstd::format("path+{}", workspace.as_path()).as_str());

    auto configured = lito::resolve_install_root(
        directory.as_path(),
        None(),
        lito::config::InstallConfig {
            .root = Some(PathBuf::from("/tmp/lito-configured-install"_str)) });
    ASSERT_TRUE(configured.is_ok());
    EXPECT_EQ(configured->path.as_path(),
              PathBuf::from("/tmp/lito-configured-install"_str).as_path());

    auto command = lito::resolve_install_root(
        directory.as_path(),
        Some(PathBuf::from("local-prefix"_str)),
        lito::config::InstallConfig {
            .root = Some(PathBuf::from("/tmp/lito-configured-install"_str)) });
    ASSERT_TRUE(command.is_ok());
    EXPECT_EQ(command->path.as_path(),
              directory.join(PathBuf::from("local-prefix"_str).as_path()).as_path());

    auto empty = lito::resolve_install_root(
        directory.as_path(), Some(PathBuf::make()), lito::config::InstallConfig {});
    ASSERT_TRUE(empty.is_err());
    auto empty_error = rstd::move(empty).unwrap_err();
    ASSERT_TRUE(empty_error.is_Message());
    EXPECT_TRUE(empty_error.as_Message().message.as_str().contains("must not be empty"_str));

    auto prefix = lito::resolve_install_destination(
        directory.as_path(),
        lito::InstallDestinationRequirement::Prefix(PathBuf::from("staging"_str)),
        lito::config::InstallConfig {
            .root = Some(PathBuf::from("/tmp/ignored-managed-root"_str)),
        });
    ASSERT_TRUE(prefix.is_ok());
    ASSERT_TRUE(prefix->is_Prefix());
    EXPECT_EQ(prefix->path(), directory.join(PathBuf::from("staging"_str).as_path()).as_path());

    auto managed = lito::resolve_install_destination(
        directory.as_path(),
        lito::InstallDestinationRequirement::Managed(Some(PathBuf::from("managed"_str))),
        lito::config::InstallConfig {});
    ASSERT_TRUE(managed.is_ok());
    ASSERT_TRUE(managed->is_Managed());
    EXPECT_EQ(managed->path(), directory.join(PathBuf::from("managed"_str).as_path()).as_path());
}

TEST_F(InstallSource, InstallPackageIdentityUsesNameAndExactSourceIdentity) {
    auto first        = lito::install_package_id("fixture-tool"_str, "path+/workspace/tool"_str);
    auto repeated     = lito::install_package_id("fixture-tool"_str, "path+/workspace/tool"_str);
    auto other_name   = lito::install_package_id("fixture-other"_str, "path+/workspace/tool"_str);
    auto other_source = lito::install_package_id("fixture-tool"_str, "path+/workspace/other"_str);
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(repeated.is_ok());
    ASSERT_TRUE(other_name.is_ok());
    ASSERT_TRUE(other_source.is_ok());
    EXPECT_EQ(first->as_str(), repeated->as_str());
    EXPECT_TRUE(first->as_str().starts_with("fixture-tool-"_str));
    EXPECT_NE(first->as_str(), other_name->as_str());
    EXPECT_NE(first->as_str(), other_source->as_str());
}

TEST_F(InstallSource, RegistryProvenanceRoundTripsExactContentIdentity) {
    auto package = lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .name     = lito::registry::RegistryPackageName::parse("fixture-tool"_str).unwrap(),
    };
    auto version  = lito::registry::SemanticVersion::parse("1.2.3"_str).unwrap();
    auto checksum = lito::registry::PackageChecksum::parse(
                        "1111111111111111111111111111111111111111111111111111111111111111"_str)
                        .unwrap();
    auto source   = lito::source::ResolvedPackageSource {
        .identity         = lito::source::registry_source_identity(package, version),
        .kind             = lito::source::PackageSourceKind::Registry,
        .registry_package = Some(package.clone()),
        .registry_version = Some(version.clone()),
        .package_checksum = Some(checksum.clone()),
    };
    auto provenance = lito::install_source_provenance(source);
    ASSERT_TRUE(provenance.is_ok());
    ASSERT_TRUE(provenance->is_Registry());
    auto serialized = lito::serialize_install_source_provenance(*provenance);
    ASSERT_TRUE(serialized.is_ok());
    auto parsed = lito::parse_install_source_provenance(*serialized);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(parsed->is_Registry());
    EXPECT_EQ(parsed->as_Registry().package, package);
    EXPECT_EQ(parsed->as_Registry().version, version);
    EXPECT_EQ(parsed->as_Registry().checksum.text(), provenance->as_Registry().checksum.text());
    EXPECT_EQ(lito::install_source_identity(*parsed).unwrap().as_str(), source.identity.as_str());
}

TEST_F(InstallSource, RegistryRootKeepsExactSourceIdentityThroughPackageResolution) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-registry-tool"
version = "1.2.3"

[[bin]]
link-stdlib = false
name = "fixture-registry-tool"
sources = ["main.cpp"]
)toml"_str },
        { "main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto materialized = materialize("registry-root-identity"_str, files);
    ASSERT_TRUE(materialized.is_ok());
    auto document = lito::manifest::load_manifest_document(materialized->root.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->package.is_some());
    auto catalog =
        lito::workspace::WorkspaceCatalog::single(rstd::move(document).unwrap().package.unwrap());
    ASSERT_TRUE(catalog.is_ok());

    auto package = lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .name = lito::registry::RegistryPackageName::parse("fixture-registry-tool"_str).unwrap(),
    };
    auto version       = lito::registry::SemanticVersion::parse("1.2.3"_str).unwrap();
    auto checksum      = lito::registry::PackageChecksum::parse(
                             "4444444444444444444444444444444444444444444444444444444444444444"_str)
                             .unwrap();
    auto identity      = lito::source::registry_source_identity(package, version);
    auto graph_sources = Vec<lito::registry::ResolvedRegistryGraphSource>::make();
    graph_sources.push(lito::registry::ResolvedRegistryGraphSource {
        .package = package.clone(),
        .version = version.clone(),
        .source =
            lito::source::ResolvedPackageSource {
                .identity         = identity.clone(),
                .kind             = lito::source::PackageSourceKind::Registry,
                .root_directory   = materialized->root.clone(),
                .registry_package = Some(package.clone()),
                .registry_version = Some(version.clone()),
                .package_checksum = Some(checksum.clone()),
            },
        .catalog = rstd::move(catalog).unwrap(),
    });
    auto resolved =
        lito::resolve_registry_install_source(package.name,
                                              rstd::move(graph_sources),
                                              cache_root("registry-root-identity"_str).as_path());
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_TRUE(resolved->registry_graph.is_some());
    auto provider = resolved->registry_graph->provider();
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto tools = lito::tools::ToolResolver(*environment);
    auto graph = lito::package::resolve_package_graph_with_environment(
        resolved->project.root.as_path(),
        lito::source::SourceResolutionOptions {},
        tools,
        *environment,
        usize(1),
        {},
        Some(rstd::move(resolved->project.catalog)),
        provider);
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    EXPECT_EQ(graph->packages[usize {}].source.kind, lito::source::PackageSourceKind::Registry);
    EXPECT_EQ(graph->packages[usize {}].source_identity.as_str(), identity.as_str());
    EXPECT_EQ(graph->roots[usize {}].source_identity.as_str(), identity.as_str());
}

TEST_F(InstallSource, WorkspaceAndMemberInstallUseTheSameSource) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([workspace]
name = "fixture-workspace-profile"
members = ["app"]
[workspace.package]
version = "0.1.0"
)"_str },
        { "app/lito.toml"_str, R"([package]
name = "fixture-workspace-profile-app"
version = { workspace = true }
[[bin]]
link-stdlib = false
name = "workspace-profile-app"
sources = ["main.cpp"]
)"_str },
        { "app/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto project = materialize("workspace-profile"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto workspace        = project->root.clone();
    auto member           = workspace.join(PathBuf::from("app"_str).as_path());
    auto workspace_source = lito::resolve_install_source(
        lito::InstallSourceRequirement::LocalProject(workspace.clone()));
    auto member_source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(member.clone()));
    ASSERT_TRUE(workspace_source.is_ok());
    ASSERT_TRUE(member_source.is_ok());
    EXPECT_EQ(workspace_source->project.root.as_path(), workspace.as_path());
    EXPECT_EQ(member_source->project.root.as_path(), workspace.as_path());
    EXPECT_EQ(workspace_source->identity.as_str(), member_source->identity.as_str());
    EXPECT_EQ(workspace_source->identity.as_str(),
              lito::source::path_source_identity(workspace.as_path()).as_str());
}
