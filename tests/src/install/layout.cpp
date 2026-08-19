#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

class InstallLayout : public ProjectFixture {};

TEST_F(InstallLayout, RuntimeSearchRelationSurvivesPackagePublicationLayouts) {
    auto direct =
        lito::install_runtime_search_path(PathBuf::from("bin/weweb/renderer"_str).as_path(),
                                          PathBuf::from("bin/weweb"_str).as_path());
    ASSERT_TRUE(direct.is_ok());
    EXPECT_EQ(direct->path.as_path(), PathBuf::from("."_str).as_path());

    auto nested = lito::install_runtime_search_path(
        PathBuf::from("bin/tools/renderer"_str).as_path(), PathBuf::from("lib/cef"_str).as_path());
    ASSERT_TRUE(nested.is_ok());
    EXPECT_EQ(nested->path.as_path(), PathBuf::from("../../lib/cef"_str).as_path());

    auto isolated = lito::install_runtime_search_path(
        PathBuf::from("packages/fixture/bin/tools/renderer"_str).as_path(),
        PathBuf::from("packages/fixture/lib/cef"_str).as_path());
    ASSERT_TRUE(isolated.is_ok());
    EXPECT_EQ(isolated->path.as_path(), nested->path.as_path());
}

TEST_F(InstallLayout, ManagedInstallMigratesBetweenDirectAndIsolatedLayouts) {
    auto root_directory   = install_root("install-layout-migration"_str);
    auto source_directory = source_root("install-layout-migration-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto tool     = source_directory.join(PathBuf::from("tool"_str).as_path());
    auto resource = source_directory.join(PathBuf::from("resource.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(tool.as_path(), "tool"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(resource.as_path(), "resource"_str.as_bytes()).is_ok());

    const auto install = [&](bool isolated) {
        auto entries = Vec<lito::InstallEntry>::make();
        entries.push(lito::InstallEntry {
            .origin  = lito::InstallEntryOrigin::BuildArtifact(lito::package::PackageTargetId {
                .package = String::make("fixture-layout"_str),
                .kind    = lito::package::PackageTargetKind::Binary,
                .name    = String::make("tool"_str),
            }),
            .payload = lito::InstallEntryPayload::CopyFile(tool.clone()),
            .relative_destination = PathBuf::from("bin/nested/tool"_str),
        });
        if (isolated) {
            entries.push(lito::InstallEntry {
                .origin  = lito::InstallEntryOrigin::PackageFile(String::make("fixture-layout"_str),
                                                                 PathBuf::from("resource.txt"_str)),
                .payload = lito::InstallEntryPayload::CopyFile(resource.clone()),
                .relative_destination = PathBuf::from("share/fixture/resource.txt"_str),
            });
        }
        auto packages = Vec<lito::InstallPackageRecord>::make();
        packages.push(lito::InstallPackageRecord {
            .name       = String::make("fixture-layout"_str),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .entries    = rstd::move(entries),
            .provenance = local_provenance(source_directory.as_path()),
        });
        return lito::install_artifacts(lito::InstallStoreRequest {
            .destination = managed_destination(root_directory.as_path()),
            .packages    = rstd::move(packages),
        });
    };

    auto direct = install(false);
    ASSERT_TRUE(direct.is_ok());
    ASSERT_TRUE(direct->managed_layout.is_some());
    auto public_tool = root_directory.join(PathBuf::from("bin/nested/tool"_str).as_path());
    auto metadata    = rstd::fs::symlink_metadata(public_tool.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->is_file());
    EXPECT_FALSE(metadata->is_symlink());

    auto package_id = lito::install_package_id(
        "fixture-layout"_str,
        lito::source::path_source_identity(source_directory.as_path()).as_str());
    ASSERT_TRUE(package_id.is_ok());
    auto private_root = direct->managed_layout->packages_directory.join(
        PathBuf::from(package_id->as_str()).as_path());
    EXPECT_FALSE(rstd::fs::exists(private_root.as_path()).unwrap());

    auto isolated = install(true);
    ASSERT_TRUE(isolated.is_ok());
    ASSERT_EQ(isolated->links.len(), usize(1));
    metadata = rstd::fs::symlink_metadata(public_tool.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->is_symlink());
#if defined(_WIN32)
    auto target          = rstd::fs::canonicalize(public_tool.as_path());
    auto expected_target = rstd::fs::canonicalize(
        private_root.join(PathBuf::from("bin/nested/tool"_str).as_path()).as_path());
    ASSERT_TRUE(target.is_ok());
    ASSERT_TRUE(expected_target.is_ok());
    EXPECT_EQ(target->as_path(), expected_target->as_path());
#else
    auto target = rstd::fs::read_link(public_tool.as_path());
    ASSERT_TRUE(target.is_ok());
    EXPECT_EQ(target->as_path(),
              PathBuf::from("../../packages"_str)
                  .join(PathBuf::from(package_id->as_str()).as_path())
                  .join(PathBuf::from("bin/nested/tool"_str).as_path())
                  .as_path());
#endif
    EXPECT_EQ(
        rstd::fs::read_to_string(
            private_root.join(PathBuf::from("share/fixture/resource.txt"_str).as_path()).as_path())
            .unwrap()
            .as_str(),
        "resource"_str);
    auto catalog = lito::load_managed_install_catalog(*isolated->managed_layout);
    ASSERT_TRUE(catalog.is_ok());
    ASSERT_EQ(catalog->packages.len(), usize(1));
    EXPECT_EQ(catalog->packages[usize {}].layout,
              lito::InstallManagedPackageLayout::IsolatedPrefix);
    EXPECT_EQ(catalog->packages[usize {}].entries.len(), usize(3));

    auto direct_again = install(false);
    ASSERT_TRUE(direct_again.is_ok());
    metadata = rstd::fs::symlink_metadata(public_tool.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->is_file());
    EXPECT_FALSE(metadata->is_symlink());
    EXPECT_FALSE(rstd::fs::exists(private_root.as_path()).unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(root_directory.join(PathBuf::from(".lito"_str).as_path()).as_path())
            .unwrap());
    EXPECT_FALSE(rstd::fs::exists(direct_again->managed_layout->transactions.as_path()).unwrap());
}

TEST_F(InstallLayout, PrefixInstallPublishesAnUntrackedLogicalTree) {
    auto prefix_directory = install_root("install-prefix"_str);
    auto source_directory = source_root("install-prefix-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto tool     = source_directory.join(PathBuf::from("tool"_str).as_path());
    auto resource = source_directory.join(PathBuf::from("resource.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(tool.as_path(), "first"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(resource.as_path(), "resource"_str.as_bytes()).is_ok());

    const auto install = [&]() {
        auto entries = Vec<lito::InstallEntry>::make();
        entries.push(lito::InstallEntry {
            .origin  = lito::InstallEntryOrigin::BuildArtifact(lito::package::PackageTargetId {
                .package = String::make("fixture-prefix"_str),
                .kind    = lito::package::PackageTargetKind::Binary,
                .name    = String::make("tool"_str),
            }),
            .payload = lito::InstallEntryPayload::CopyFile(tool.clone()),
            .relative_destination = PathBuf::from("bin/tool"_str),
        });
        entries.push(lito::InstallEntry {
            .origin  = lito::InstallEntryOrigin::PackageFile(String::make("fixture-prefix"_str),
                                                             PathBuf::from("resource.txt"_str)),
            .payload = lito::InstallEntryPayload::CopyFile(resource.clone()),
            .relative_destination = PathBuf::from("share/fixture/resource.txt"_str),
        });
        auto packages = Vec<lito::InstallPackageRecord>::make();
        auto package  = lito::InstallPackageRecord {
            .name       = String::make("fixture-prefix"_str),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .entries    = rstd::move(entries),
            .provenance = local_provenance(source_directory.as_path()),
        };
        package.runtime_dependencies.push(lito::InstallRuntimeDependency {
            .name            = String::make("not-installed"_str),
            .source_identity = String::make("path+/not-installed"_str),
        });
        packages.push(rstd::move(package));
        return lito::install_artifacts(lito::InstallStoreRequest {
            .destination = lito::InstallDestination::Prefix(
                lito::InstallPrefix { .path = prefix_directory.clone() }),
            .packages = rstd::move(packages),
        });
    };

    auto first = install();
    ASSERT_TRUE(first.is_ok());
    EXPECT_TRUE(first->managed_layout.is_none());
    EXPECT_TRUE(first->links.is_empty());
    ASSERT_EQ(first->entries.len(), usize(2));
    EXPECT_EQ(first->entries[usize {}].action, lito::InstallAction::Created);
    EXPECT_EQ(first->entries[usize(1)].action, lito::InstallAction::Created);
    auto installed_tool = prefix_directory.join(PathBuf::from("bin/tool"_str).as_path());
    auto metadata       = rstd::fs::symlink_metadata(installed_tool.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->is_file());
    EXPECT_FALSE(metadata->is_symlink());
    EXPECT_EQ(rstd::fs::read_to_string(installed_tool.as_path()).unwrap().as_str(), "first"_str);
    EXPECT_EQ(rstd::fs::read_to_string(
                  prefix_directory.join(PathBuf::from("share/fixture/resource.txt"_str).as_path())
                      .as_path())
                  .unwrap()
                  .as_str(),
              "resource"_str);
    EXPECT_FALSE(
        rstd::fs::exists(prefix_directory.join(PathBuf::from("packages"_str).as_path()).as_path())
            .unwrap());
    EXPECT_FALSE(rstd::fs::exists(
                     prefix_directory.join(PathBuf::from(".lito-install"_str).as_path()).as_path())
                     .unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(prefix_directory.join(PathBuf::from(".lito"_str).as_path()).as_path())
            .unwrap());

    auto unchanged = install();
    ASSERT_TRUE(unchanged.is_ok());
    for (const auto& entry : unchanged->entries) {
        EXPECT_EQ(entry.action, lito::InstallAction::Unchanged);
    }
    ASSERT_TRUE(rstd::fs::write(tool.as_path(), "second"_str.as_bytes()).is_ok());
    auto replaced = install();
    ASSERT_TRUE(replaced.is_ok());
    ASSERT_EQ(replaced->entries.len(), usize(2));
    EXPECT_EQ(replaced->entries[usize {}].action, lito::InstallAction::Replaced);
    EXPECT_EQ(replaced->entries[usize(1)].action, lito::InstallAction::Unchanged);
    EXPECT_EQ(rstd::fs::read_to_string(installed_tool.as_path()).unwrap().as_str(), "second"_str);
}

TEST_F(InstallLayout, ManagedInstallRecoversPreparedTransactionsBeforeCatalogLoad) {
    auto root_directory   = install_root("install-transaction-recovery"_str);
    auto source_directory = source_root("install-transaction-recovery-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto source = source_directory.join(PathBuf::from("tool"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "stable"_str.as_bytes()).is_ok());

    const auto install = [&]() {
        auto binaries = Vec<lito::InstallBinary>::make();
        binaries.push(lito::InstallBinary {
            .target =
                lito::package::PackageTargetId {
                    .package = String::make("fixture-recovery"_str),
                    .kind    = lito::package::PackageTargetKind::Binary,
                    .name    = String::make("tool"_str),
                },
            .source = source.clone(),
        });
        auto packages = Vec<lito::InstallPackageRecord>::make();
        packages.push(lito::InstallPackageRecord {
            .name       = String::make("fixture-recovery"_str),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .binaries   = rstd::move(binaries),
            .provenance = local_provenance(source_directory.as_path()),
        });
        return lito::install_artifacts(lito::InstallStoreRequest {
            .destination = managed_destination(root_directory.as_path()),
            .packages    = rstd::move(packages),
        });
    };

    auto first = install();
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(first->managed_layout.is_some());
    const auto& layout      = *first->managed_layout;
    auto        transaction = layout.transactions.join(PathBuf::from("interrupted"_str).as_path());
    auto        backup      = transaction.join(PathBuf::from("backup/bin"_str).as_path());
    auto        staged      = transaction.join(PathBuf::from("new/bin"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(backup.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(staged.as_path()).is_ok());
    auto installed = layout.bin_directory.join(PathBuf::from("tool"_str).as_path());
    ASSERT_TRUE(rstd::fs::rename(installed.as_path(),
                                 backup.join(PathBuf::from("tool"_str).as_path()).as_path())
                    .is_ok());
    ASSERT_TRUE(rstd::fs::write(staged.join(PathBuf::from("tool"_str).as_path()).as_path(),
                                "interrupted"_str.as_bytes())
                    .is_ok());
    auto journal = transaction.join(PathBuf::from("journal.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(journal.as_path(),
                                "{\"schema\":1,\"items\":[{\"path\":\"bin/tool\","
                                "\"had-existing\":true,\"publish\":true}]}"_str.as_bytes())
                    .is_ok());

    auto recovered = install();
    ASSERT_TRUE(recovered.is_ok());
    EXPECT_EQ(rstd::fs::read_to_string(installed.as_path()).unwrap().as_str(), "stable"_str);
    EXPECT_FALSE(rstd::fs::exists(layout.transactions.as_path()).unwrap());
}

TEST_F(InstallLayout, ManagedCatalogRejectsInvalidPackageInfo) {
    auto root_directory   = install_root("install-invalid-info"_str);
    auto source_directory = source_root("install-invalid-info-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto source = source_directory.join(PathBuf::from("tool"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "tool"_str.as_bytes()).is_ok());
    auto binaries = Vec<lito::InstallBinary>::make();
    binaries.push(lito::InstallBinary {
        .target =
            lito::package::PackageTargetId {
                .package = String::make("fixture-invalid-info"_str),
                .kind    = lito::package::PackageTargetKind::Binary,
                .name    = String::make("tool"_str),
            },
        .source = source.clone(),
    });
    auto packages = Vec<lito::InstallPackageRecord>::make();
    packages.push(lito::InstallPackageRecord {
        .name       = String::make("fixture-invalid-info"_str),
        .version    = String::make("1.0.0"_str),
        .profile    = String::make("release"_str),
        .target     = String::make("x86_64-test"_str),
        .binaries   = rstd::move(binaries),
        .provenance = local_provenance(source_directory.as_path()),
    });
    auto installed = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(packages),
    });
    ASSERT_TRUE(installed.is_ok());
    ASSERT_TRUE(installed->managed_layout.is_some());
    auto package_id = lito::install_package_id(
        "fixture-invalid-info"_str,
        lito::source::path_source_identity(source_directory.as_path()).as_str());
    ASSERT_TRUE(package_id.is_ok());
    auto info = installed->managed_layout->packages_directory.join(
        PathBuf::from(rstd::format("{}.info", package_id->as_str())).as_path());
    auto mismatched = installed->managed_layout->packages_directory.join(
        PathBuf::from("mismatched.info"_str).as_path());
    ASSERT_TRUE(rstd::fs::rename(info.as_path(), mismatched.as_path()).is_ok());
    auto catalog = lito::load_managed_install_catalog(*installed->managed_layout);
    ASSERT_TRUE(catalog.is_err());
    ASSERT_TRUE(catalog.unwrap_err().is_Cause());
    ASSERT_TRUE(rstd::fs::rename(mismatched.as_path(), info.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(info.as_path(), "{\"schema\":99}"_str.as_bytes()).is_ok());
    catalog = lito::load_managed_install_catalog(*installed->managed_layout);
    ASSERT_TRUE(catalog.is_err());
    ASSERT_TRUE(catalog.unwrap_err().is_Cause());
}
