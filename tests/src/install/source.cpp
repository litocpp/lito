#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.driver;
import lito.system;
import lito.toolchain.cmake;
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
        lito::InstallConfig { .root = Some(PathBuf::from("/tmp/lito-configured-install"_str)) });
    ASSERT_TRUE(configured.is_ok());
    EXPECT_EQ(configured->path.as_path(),
              PathBuf::from("/tmp/lito-configured-install"_str).as_path());

    auto command = lito::resolve_install_root(
        directory.as_path(),
        Some(PathBuf::from("local-prefix"_str)),
        lito::InstallConfig { .root = Some(PathBuf::from("/tmp/lito-configured-install"_str)) });
    ASSERT_TRUE(command.is_ok());
    EXPECT_EQ(command->path.as_path(),
              directory.join(PathBuf::from("local-prefix"_str).as_path()).as_path());

    auto empty = lito::resolve_install_root(
        directory.as_path(), Some(PathBuf::make()), lito::InstallConfig {});
    ASSERT_TRUE(empty.is_err());
    auto empty_error = rstd::move(empty).unwrap_err();
    ASSERT_TRUE(empty_error.is_Message());
    EXPECT_TRUE(empty_error.as_Message().message.as_str().contains("must not be empty"_str));

    auto prefix = lito::resolve_install_destination(
        directory.as_path(),
        lito::InstallDestinationRequirement::Prefix(PathBuf::from("staging"_str)),
        lito::InstallConfig {
            .root = Some(PathBuf::from("/tmp/ignored-managed-root"_str)),
        });
    ASSERT_TRUE(prefix.is_ok());
    ASSERT_TRUE(prefix->is_Prefix());
    EXPECT_EQ(prefix->path(), directory.join(PathBuf::from("staging"_str).as_path()).as_path());

    auto managed = lito::resolve_install_destination(
        directory.as_path(),
        lito::InstallDestinationRequirement::Managed(Some(PathBuf::from("managed"_str))),
        lito::InstallConfig {});
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
              lito::path_source_identity(workspace.as_path()).as_str());
}
