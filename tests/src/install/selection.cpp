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

TEST(Install, InstallPurposeSelectsWorkspaceBinaries) {
    auto directory = fixture_path("project"_str);
    auto package   = lito::resolve_package_selection(
        lito::PackageSelection { .root = fixture_path("project/multi-target"_str) },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(package.is_ok());
    ASSERT_EQ(package->selected_root_names.len(), usize(3));
    EXPECT_TRUE(contains_name(package->selected_root_names, "fixture-multi-consumer"_str));
    EXPECT_TRUE(contains_name(package->selected_root_names, "fixture-multi-target"_str));
    EXPECT_TRUE(contains_name(package->selected_root_names, "fixture-test-app"_str));

    auto workspace = lito::resolve_package_selection(
        lito::PackageSelection { .root = fixture_path("workspace/profile"_str) },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(workspace.is_ok());
    ASSERT_EQ(workspace->selected_root_names.len(), usize(1));
    EXPECT_EQ(workspace->selected_root_names[usize {}].as_str(),
              "fixture-workspace-profile-app"_str);

    auto member = lito::resolve_package_selection(
        lito::PackageSelection { .root = fixture_path("build/script/workspace/app"_str) },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(member.is_ok());
    ASSERT_EQ(member->selected_root_names.len(), usize(2));
    EXPECT_TRUE(contains_name(member->selected_root_names, "fixture-configure-workspace-app"_str));
    EXPECT_TRUE(
        contains_name(member->selected_root_names, "fixture-configure-workspace-other"_str));

    auto selected_member = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = fixture_path("build/script/workspace/app"_str),
            .packages = strings("fixture-configure-workspace-app"_str),
        },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(selected_member.is_ok());
    ASSERT_EQ(selected_member->selected_root_names.len(), usize(1));
    EXPECT_EQ(selected_member->selected_root_names[usize {}].as_str(),
              "fixture-configure-workspace-app"_str);

    auto selected = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-multi-target"_str),
        },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_root_names.len(), usize(1));
    ASSERT_EQ(selected->selected_targets.len(), usize(2));
    for (const auto& target : selected->selected_targets) {
        EXPECT_EQ(target.package.as_str(), "fixture-multi-target"_str);
        EXPECT_EQ(target.kind, lito::PackageTargetKind::Binary);
    }

    auto consumer = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-multi-consumer"_str),
        },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(consumer.is_ok());
    ASSERT_EQ(consumer->selected_targets.len(), usize(1));
    EXPECT_EQ(consumer->selected_targets[usize {}].package.as_str(), "fixture-multi-consumer"_str);
    EXPECT_EQ(consumer->selected_targets[usize {}].kind, lito::PackageTargetKind::Binary);

    auto library = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-test-lib"_str),
        },
        lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(library.is_err());
    auto library_error = rstd::move(library).unwrap_err();
    ASSERT_TRUE(library_error.is_Message());
    EXPECT_TRUE(library_error.as_Message().message.as_str().contains("no install target"_str));

    auto all = lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                               lito::PackageSelectionPurpose::Install);
    ASSERT_TRUE(all.is_ok());
    ASSERT_EQ(all->selected_root_names.len(), usize(3));
}

TEST(Install, RuntimeDependenciesAreInstallOnlyAndDependencyFirst) {
    auto explicit_manifest = lito::load_package_manifest(
        fixture_path("install/manifest/runtime/explicit"_str).as_path());
    ASSERT_TRUE(explicit_manifest.is_ok());
    ASSERT_EQ(explicit_manifest->runtime_dependencies.len(), usize(2));
    EXPECT_EQ(explicit_manifest->runtime_dependencies[usize {}].name.as_str(), "git-helper"_str);
    ASSERT_TRUE(explicit_manifest->runtime_dependencies[usize {}].source.is_Git());
    EXPECT_EQ(explicit_manifest->runtime_dependencies[usize {}].source.as_Git().reference.kind,
              lito::GitReferenceKind::Commit);
    EXPECT_EQ(explicit_manifest->runtime_dependencies[usize(1)].name.as_str(), "path-helper"_str);
    EXPECT_TRUE(explicit_manifest->runtime_dependencies[usize(1)].source.is_Path());

    auto directory = fixture_path("install/runtime"_str);
    auto build     = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-runtime-app"_str),
        },
        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(build.is_ok());
    ASSERT_EQ(build->graph.packages.len(), usize(3));
    ASSERT_EQ(build->selected_package_names.len(), usize(1));
    EXPECT_EQ(build->selected_package_names[usize {}].as_str(), "fixture-runtime-app"_str);

    auto install = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = rstd::move(directory),
            .packages = strings("fixture-runtime-app"_str),
        },
        lito::PackageSelectionPurpose::Install);
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
