#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class InstallSelection : public ProjectFixture {};

TEST_F(InstallSelection, InstallPurposeSelectsWorkspaceBinaries) {
    auto selection_tree = install_selection_project_tree();
    ASSERT_TRUE(selection_tree.is_ok());
    auto selection_project = materialize("install-selection"_str, *selection_tree);
    ASSERT_TRUE(selection_project.is_ok());
    auto directory = selection_project->root.clone();
    auto package   = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root = directory.join(PathBuf::from("multi-target"_str).as_path()),
        },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(package.is_ok());
    ASSERT_EQ(package->selected_root_names.len(), usize(3));
    EXPECT_TRUE(contains_name(package->selected_root_names, "fixture-multi-consumer"_str));
    EXPECT_TRUE(contains_name(package->selected_root_names, "fixture-multi-target"_str));
    EXPECT_TRUE(contains_name(package->selected_root_names, "fixture-test-app"_str));

    constexpr ProjectFile profile_files[] = {
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
    auto profile_project = materialize("workspace-profile"_str, profile_files);
    ASSERT_TRUE(profile_project.is_ok());
    auto workspace = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = profile_project->root.clone() },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(workspace.is_ok());
    ASSERT_EQ(workspace->selected_root_names.len(), usize(1));
    EXPECT_EQ(workspace->selected_root_names[usize {}].as_str(),
              "fixture-workspace-profile-app"_str);

    constexpr ProjectFile workspace_files[] = {
        { "lito.toml"_str, R"([workspace]
name = "fixture-configure-workspace"
members = ["app", "other"]
default-members = ["app", "other"]
[workspace.package]
version = "0.1.0"
)"_str },
        { "app/lito.toml"_str, R"([package]
name = "fixture-configure-workspace-app"
version = { workspace = true }
[[bin]]
link-stdlib = false
name = "configure-workspace"
sources = ["src/main.cpp"]
)"_str },
        { "app/src/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "other/lito.toml"_str, R"([package]
name = "fixture-configure-workspace-other"
version = { workspace = true }
[[bin]]
link-stdlib = false
name = "configure-other"
sources = ["src/main.cpp"]
)"_str },
        { "other/src/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto configure_project = materialize("configure-workspace"_str, workspace_files);
    ASSERT_TRUE(configure_project.is_ok());
    auto member_root = configure_project->root.join(PathBuf::from("app"_str).as_path());
    auto member      = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = member_root.clone() },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(member.is_ok());
    ASSERT_EQ(member->selected_root_names.len(), usize(2));
    EXPECT_TRUE(contains_name(member->selected_root_names, "fixture-configure-workspace-app"_str));
    EXPECT_TRUE(
        contains_name(member->selected_root_names, "fixture-configure-workspace-other"_str));

    auto selected_member = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = member_root.clone(),
            .packages = strings("fixture-configure-workspace-app"_str),
        },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(selected_member.is_ok());
    ASSERT_EQ(selected_member->selected_root_names.len(), usize(1));
    EXPECT_EQ(selected_member->selected_root_names[usize {}].as_str(),
              "fixture-configure-workspace-app"_str);

    auto selected = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-multi-target"_str),
        },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_root_names.len(), usize(1));
    ASSERT_EQ(selected->selected_targets.len(), usize(2));
    for (const auto& target : selected->selected_targets) {
        EXPECT_EQ(target.package.as_str(), "fixture-multi-target"_str);
        EXPECT_EQ(target.kind, lito::package::PackageTargetKind::Binary);
    }

    auto consumer = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-multi-consumer"_str),
        },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(consumer.is_ok());
    ASSERT_EQ(consumer->selected_targets.len(), usize(1));
    EXPECT_EQ(consumer->selected_targets[usize {}].package.as_str(), "fixture-multi-consumer"_str);
    EXPECT_EQ(consumer->selected_targets[usize {}].kind, lito::package::PackageTargetKind::Binary);

    auto library = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-test-lib"_str),
        },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(library.is_err());
    auto library_error = rstd::move(library).unwrap_err();
    ASSERT_TRUE(library_error.is_Message());
    EXPECT_TRUE(library_error.as_Message().message.as_str().contains("no install target"_str));

    auto all = lito::package::resolve_package_selection(
        lito::package::PackageSelection { .root = directory.clone() },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(all.is_ok());
    ASSERT_EQ(all->selected_root_names.len(), usize(3));
}

TEST_F(InstallSelection, RuntimeDependenciesAreInstallOnlyAndDependencyFirst) {
    constexpr ProjectFile explicit_files[] = {
        { "explicit/lito.toml"_str, R"([package]
name = "fixture-runtime-explicit"
version = "0.1.0"
[runtime-dependencies.git-helper]
git = "https://example.invalid/runtime-helper.git"
commit = "0123456789abcdef0123456789abcdef01234567"
[runtime-dependencies.path-helper]
path = "../install-only"
)"_str },
        { "explicit/install.lua"_str, "lito.install({})\n"_str },
        { "explicit/resource.txt"_str, "fixture\n"_str },
        { "install-only/lito.toml"_str,
          "[package]\nname = \"fixture-install-only\"\nversion = \"1.2.3\"\n"_str },
        { "install-only/install.lua"_str, "lito.install({})\n"_str },
        { "install-only/resource.txt"_str, "fixture\n"_str },
    };
    auto explicit_project = materialize("runtime-explicit-projects"_str, explicit_files);
    ASSERT_TRUE(explicit_project.is_ok());
    auto explicit_directory = explicit_project->root.join(PathBuf::from("explicit"_str).as_path());
    auto explicit_manifest  = lito::manifest::load_package_manifest(explicit_directory.as_path());
    ASSERT_TRUE(explicit_manifest.is_ok());
    ASSERT_EQ(explicit_manifest->runtime_dependencies.len(), usize(2));
    EXPECT_EQ(explicit_manifest->runtime_dependencies[usize {}].name.as_str(), "git-helper"_str);
    ASSERT_TRUE(explicit_manifest->runtime_dependencies[usize {}].source.is_Git());
    EXPECT_EQ(explicit_manifest->runtime_dependencies[usize {}].source.as_Git().reference.kind,
              lito::source::GitReferenceKind::Commit);
    EXPECT_EQ(explicit_manifest->runtime_dependencies[usize(1)].name.as_str(), "path-helper"_str);
    EXPECT_TRUE(explicit_manifest->runtime_dependencies[usize(1)].source.is_Path());

    constexpr ProjectFile runtime_files[] = {
        { "lito.toml"_str, R"([workspace]
name = "fixture-runtime-dependency"
members = ["app", "helper", "leaf"]
[workspace.package]
version = "1.0.0"
[workspace.dependencies.helper]
path = "helper"
[workspace.dependencies.leaf]
path = "leaf"
)"_str },
        { "app/lito.toml"_str, R"([package]
name = "fixture-runtime-app"
version.workspace = true
[[bin]]
link-stdlib = false
name = "runtime-app"
sources = ["main.cpp"]
[runtime-dependencies.helper]
workspace = true
[runtime-dependencies.leaf]
workspace = true
)"_str },
        { "app/main.cpp"_str, "int main() { return 0; }\n"_str },
        { "helper/lito.toml"_str, R"([package]
name = "helper"
version.workspace = true
[[bin]]
link-stdlib = false
name = "runtime-helper"
sources = ["main.cpp"]
[runtime-dependencies.leaf]
workspace = true
)"_str },
        { "helper/main.cpp"_str, "int main() { return 0; }\n"_str },
        { "leaf/lito.toml"_str, R"([package]
name = "leaf"
version.workspace = true
[[bin]]
link-stdlib = false
name = "runtime-leaf"
sources = ["main.cpp"]
)"_str },
        { "leaf/main.cpp"_str, "int main() { return 0; }\n"_str },
    };
    auto runtime_project = materialize("runtime-dependencies"_str, runtime_files);
    ASSERT_TRUE(runtime_project.is_ok());
    auto directory = runtime_project->root.clone();
    auto build     = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-runtime-app"_str),
        },
        lito::package::PackageSelectionPurpose::Production);
    ASSERT_TRUE(build.is_ok());
    ASSERT_EQ(build->graph.packages.len(), usize(3));
    ASSERT_EQ(build->selected_package_names.len(), usize(1));
    EXPECT_EQ(build->selected_package_names[usize {}].as_str(), "fixture-runtime-app"_str);

    auto install = lito::package::resolve_package_selection(
        lito::package::PackageSelection {
            .root     = rstd::move(directory),
            .packages = strings("fixture-runtime-app"_str),
        },
        lito::package::PackageSelectionPurpose::Install);
    ASSERT_TRUE(install.is_ok());
    ASSERT_EQ(install->install_package_names.len(), usize(3));
    EXPECT_EQ(install->install_package_names[usize {}].as_str(), "leaf"_str);
    EXPECT_EQ(install->install_package_names[usize(1)].as_str(), "helper"_str);
    EXPECT_EQ(install->install_package_names[usize(2)].as_str(), "fixture-runtime-app"_str);
    ASSERT_EQ(install->selected_targets.len(), usize(3));

    auto packages = lito::resolve_install_packages(*install, pkg_config_target());
    ASSERT_TRUE(packages.is_ok());
    ASSERT_EQ(packages->len(), usize(3));
    EXPECT_FALSE((*packages)[usize {}].direct);
    EXPECT_FALSE((*packages)[usize(1)].direct);
    EXPECT_TRUE((*packages)[usize(2)].direct);
    ASSERT_EQ((*packages)[usize(2)].runtime_dependencies.len(), usize(2));
}
