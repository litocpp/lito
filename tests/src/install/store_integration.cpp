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

class InstallStore : public ProjectFixture {};

TEST_F(InstallStore, InstallStoreTracksOwnershipAndProtectsConflicts) {
    auto root_directory   = install_root("install-store"_str);
    auto source_directory = source_root("install-store-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto first_source = source_directory.join(PathBuf::from("tool"_str).as_path());
    auto old_source   = source_directory.join(PathBuf::from("old"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(first_source.as_path(), "first"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(old_source.as_path(), "old"_str.as_bytes()).is_ok());

    const auto make_binary = [&](ref<rstd::path::Path> source, ref<str> name) {
        return lito::InstallBinary {
            .target =
                lito::package::PackageTargetId {
                    .package = String::make("fixture-tool"_str),
                    .kind    = lito::package::PackageTargetKind::Binary,
                    .name    = String::make(name),
                },
            .source = PathBuf::from(source),
        };
    };
    const auto install_package =
        [&](ref<rstd::path::Path> source, ref<str> version, bool include_old, bool force = false) {
            auto binaries = Vec<lito::InstallBinary>::make();
            binaries.push(make_binary(source, "tool"_str));
            if (include_old) binaries.push(make_binary(old_source.as_path(), "old"_str));
            auto packages = Vec<lito::InstallPackageRecord>::make();
            packages.push(lito::InstallPackageRecord {
                .name       = String::make("fixture-tool"_str),
                .version    = String::make(version),
                .profile    = String::make("release"_str),
                .target     = String::make("x86_64-test"_str),
                .binaries   = rstd::move(binaries),
                .provenance = local_provenance(source_directory.as_path()),
            });
            return lito::install_artifacts(lito::InstallStoreRequest {
                .destination = managed_destination(root_directory.as_path()),
                .packages    = rstd::move(packages),
                .force       = force,
            });
        };

    auto first = install_package(first_source.as_path(), "1.0.0"_str, true);
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->binaries.len(), usize(2));
    ASSERT_TRUE(first->managed_layout.is_some());
    const auto& layout = *first->managed_layout;
    EXPECT_EQ(first->binaries[usize {}].action, lito::InstallAction::Created);
    auto installed = layout.bin_directory.join(PathBuf::from("tool"_str).as_path());
    auto contents  = rstd::fs::read_to_string(installed.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_EQ(contents->as_str(), "first"_str);
    auto old_installed = layout.bin_directory.join(PathBuf::from("old"_str).as_path());
    ASSERT_TRUE(rstd::fs::exists(old_installed.as_path()).unwrap());

    ASSERT_TRUE(rstd::fs::write(first_source.as_path(), "next"_str.as_bytes()).is_ok());
    auto reinstalled = install_package(first_source.as_path(), "1.1.0"_str, false);
    ASSERT_TRUE(reinstalled.is_ok());
    EXPECT_EQ(reinstalled->binaries[usize {}].action, lito::InstallAction::Replaced);
    contents = rstd::fs::read_to_string(installed.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_EQ(contents->as_str(), "next"_str);
    EXPECT_FALSE(rstd::fs::exists(old_installed.as_path()).unwrap());

    auto unchanged = install_package(first_source.as_path(), "1.1.0"_str, false);
    ASSERT_TRUE(unchanged.is_ok());
    ASSERT_EQ(unchanged->entries.len(), usize(1));
    EXPECT_EQ(unchanged->entries[usize {}].action, lito::InstallAction::Unchanged);
    EXPECT_EQ(unchanged->binaries[usize {}].action, lito::InstallAction::Unchanged);

    auto external = source_directory.join(PathBuf::from("unmanaged"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(external.as_path(), "external"_str.as_bytes()).is_ok());
    auto unmanaged = root_directory.join(PathBuf::from("bin/unmanaged"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(unmanaged.as_path(), "owned elsewhere"_str.as_bytes()).is_ok());
    auto binaries = Vec<lito::InstallBinary>::make();
    binaries.push(lito::InstallBinary {
        .target =
            lito::package::PackageTargetId {
                .package = String::make("fixture-other"_str),
                .kind    = lito::package::PackageTargetKind::Binary,
                .name    = String::make("unmanaged"_str),
            },
        .source = rstd::move(external),
    });
    auto conflict_packages = Vec<lito::InstallPackageRecord>::make();
    conflict_packages.push(lito::InstallPackageRecord {
        .name       = String::make("fixture-other"_str),
        .version    = String::make("1.0.0"_str),
        .profile    = String::make("release"_str),
        .target     = String::make("x86_64-test"_str),
        .binaries   = rstd::move(binaries),
        .provenance = local_provenance(source_directory.as_path()),
    });
    auto conflict = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(conflict_packages),
    });
    ASSERT_TRUE(conflict.is_err());
    auto conflict_error = rstd::move(conflict).unwrap_err();
    ASSERT_TRUE(conflict_error.is_Cause());
    ASSERT_TRUE(conflict_error.as_Cause().source.is_Message());
    EXPECT_TRUE(
        conflict_error.as_Cause().source.as_Message().message.as_str().contains("not managed"_str));

    auto forced_binaries = Vec<lito::InstallBinary>::make();
    auto forced_source   = source_directory.join(PathBuf::from("unmanaged"_str).as_path());
    forced_binaries.push(lito::InstallBinary {
        .target =
            lito::package::PackageTargetId {
                .package = String::make("fixture-other"_str),
                .kind    = lito::package::PackageTargetKind::Binary,
                .name    = String::make("unmanaged"_str),
            },
        .source = rstd::move(forced_source),
    });
    auto forced_packages = Vec<lito::InstallPackageRecord>::make();
    forced_packages.push(lito::InstallPackageRecord {
        .name       = String::make("fixture-other"_str),
        .version    = String::make("1.0.0"_str),
        .profile    = String::make("release"_str),
        .target     = String::make("x86_64-test"_str),
        .binaries   = rstd::move(forced_binaries),
        .provenance = local_provenance(source_directory.as_path()),
    });
    auto forced = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(forced_packages),
        .force       = true,
    });
    ASSERT_TRUE(forced.is_ok());
    auto forced_contents = rstd::fs::read_to_string(unmanaged.as_path());
    ASSERT_TRUE(forced_contents.is_ok());
    EXPECT_EQ(forced_contents->as_str(), "external"_str);
}

TEST_F(InstallStore, InstallStoreCommitsMultiplePackagesTogether) {
    auto root_directory   = install_root("install-store-batch"_str);
    auto source_directory = source_root("install-store-batch-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());

    const auto package = [&](ref<str> name, ref<str> binary_name, ref<str> contents) {
        auto source = source_directory.join(PathBuf::from(binary_name).as_path());
        EXPECT_TRUE(rstd::fs::write(source.as_path(), contents.as_bytes()).is_ok());
        auto binaries = Vec<lito::InstallBinary>::make();
        binaries.push(lito::InstallBinary {
            .target =
                lito::package::PackageTargetId {
                    .package = String::make(name),
                    .kind    = lito::package::PackageTargetKind::Binary,
                    .name    = String::make(binary_name),
                },
            .source = rstd::move(source),
        });
        return lito::InstallPackageRecord {
            .name       = String::make(name),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .binaries   = rstd::move(binaries),
            .provenance = local_provenance(source_directory.as_path()),
        };
    };

    auto packages = Vec<lito::InstallPackageRecord>::make();
    packages.push(package("fixture-one"_str, "one"_str, "first"_str));
    packages.push(package("fixture-two"_str, "two"_str, "second"_str));
    auto installed = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(packages),
    });
    ASSERT_TRUE(installed.is_ok());
    ASSERT_TRUE(installed->managed_layout.is_some());
    const auto& layout = *installed->managed_layout;
    EXPECT_EQ(installed->packages.len(), usize(2));
    EXPECT_EQ(installed->binaries.len(), usize(2));

    auto one = rstd::fs::read_to_string(
        layout.bin_directory.join(PathBuf::from("one"_str).as_path()).as_path());
    auto two = rstd::fs::read_to_string(
        layout.bin_directory.join(PathBuf::from("two"_str).as_path()).as_path());
    ASSERT_TRUE(one.is_ok());
    ASSERT_TRUE(two.is_ok());
    EXPECT_EQ(one->as_str(), "first"_str);
    EXPECT_EQ(two->as_str(), "second"_str);

    auto catalog_before = lito::load_managed_install_catalog(layout);
    ASSERT_TRUE(catalog_before.is_ok());
    ASSERT_EQ(catalog_before->packages.len(), usize(2));
    auto left_directory  = source_directory.join(PathBuf::from("left"_str).as_path());
    auto right_directory = source_directory.join(PathBuf::from("right"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(left_directory.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(right_directory.as_path()).is_ok());
    auto left_source  = left_directory.join(PathBuf::from("collision"_str).as_path());
    auto right_source = right_directory.join(PathBuf::from("collision"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(left_source.as_path(), "left"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(right_source.as_path(), "right"_str.as_bytes()).is_ok());
    const auto collision_package = [&](ref<str> name, PathBuf source) {
        auto binaries = Vec<lito::InstallBinary>::make();
        binaries.push(lito::InstallBinary {
            .target =
                lito::package::PackageTargetId {
                    .package = String::make(name),
                    .kind    = lito::package::PackageTargetKind::Binary,
                    .name    = String::make("collision"_str),
                },
            .source = rstd::move(source),
        });
        return lito::InstallPackageRecord {
            .name       = String::make(name),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .binaries   = rstd::move(binaries),
            .provenance = local_provenance(source_directory.as_path()),
        };
    };
    auto collisions = Vec<lito::InstallPackageRecord>::make();
    collisions.push(collision_package("fixture-left"_str, rstd::move(left_source)));
    collisions.push(collision_package("fixture-right"_str, rstd::move(right_source)));
    auto conflict = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(collisions),
        .force       = true,
    });
    ASSERT_TRUE(conflict.is_err());
    auto conflict_error = rstd::move(conflict).unwrap_err();
    ASSERT_TRUE(conflict_error.is_Cause());
    ASSERT_TRUE(conflict_error.as_Cause().source.is_Message());
    EXPECT_TRUE(conflict_error.as_Cause().source.as_Message().message.as_str().contains(
        "more than one entry"_str));
    auto catalog_after = lito::load_managed_install_catalog(layout);
    ASSERT_TRUE(catalog_after.is_ok());
    EXPECT_EQ(catalog_after->packages.len(), catalog_before->packages.len());
    EXPECT_EQ(rstd::fs::read_to_string(
                  layout.bin_directory.join(PathBuf::from("one"_str).as_path()).as_path())
                  .unwrap()
                  .as_str(),
              "first"_str);
    EXPECT_EQ(rstd::fs::read_to_string(
                  layout.bin_directory.join(PathBuf::from("two"_str).as_path()).as_path())
                  .unwrap()
                  .as_str(),
              "second"_str);
    EXPECT_FALSE(rstd::fs::exists(
                     layout.bin_directory.join(PathBuf::from("collision"_str).as_path()).as_path())
                     .unwrap());
}

TEST_F(InstallStore, InstallStoreRejectsForceThatWouldBreakRuntimeDependencies) {
    auto root_directory   = install_root("install-store-runtime-rollback"_str);
    auto source_directory = source_root("install-store-runtime-rollback-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());

    const auto package = [&](ref<str> name, ref<str> destination, ref<str> contents) {
        auto source = source_directory.join(PathBuf::from(name).as_path());
        EXPECT_TRUE(rstd::fs::write(source.as_path(), contents.as_bytes()).is_ok());
        auto entries = Vec<lito::InstallEntry>::make();
        entries.push(lito::InstallEntry {
            .origin =
                lito::InstallEntryOrigin::PackageFile(String::make(name), PathBuf::from(name)),
            .payload              = lito::InstallEntryPayload::CopyFile(rstd::move(source)),
            .relative_destination = PathBuf::from(destination),
        });
        return lito::InstallPackageRecord {
            .name       = String::make(name),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .entries    = rstd::move(entries),
            .provenance = local_provenance(source_directory.as_path()),
        };
    };

    auto runtime = package("fixture-runtime"_str, "bin/shared"_str, "runtime"_str);
    auto app     = package("fixture-app"_str, "bin/app"_str, "app"_str);
    app.runtime_dependencies.push(lito::InstallRuntimeDependency {
        .name            = String::make("fixture-runtime"_str),
        .source_identity = lito::source::path_source_identity(source_directory.as_path()),
    });
    auto initial = Vec<lito::InstallPackageRecord>::make();
    initial.push(rstd::move(runtime));
    initial.push(rstd::move(app));
    auto installed = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(initial),
    });
    ASSERT_TRUE(installed.is_ok());
    ASSERT_TRUE(installed->managed_layout.is_some());
    const auto& layout         = *installed->managed_layout;
    auto        catalog_before = lito::load_managed_install_catalog(layout);
    ASSERT_TRUE(catalog_before.is_ok());
    ASSERT_EQ(catalog_before->packages.len(), usize(2));

    auto replacement = package("fixture-replacement"_str, "bin/shared"_str, "replacement"_str);
    auto replacing   = Vec<lito::InstallPackageRecord>::make();
    replacing.push(rstd::move(replacement));
    auto rejected = lito::install_artifacts(lito::InstallStoreRequest {
        .destination = managed_destination(root_directory.as_path()),
        .packages    = rstd::move(replacing),
        .force       = true,
    });
    ASSERT_TRUE(rejected.is_err());
    ASSERT_TRUE(rejected.unwrap_err().is_Cause());
    auto catalog_after = lito::load_managed_install_catalog(layout);
    ASSERT_TRUE(catalog_after.is_ok());
    EXPECT_EQ(catalog_after->packages.len(), catalog_before->packages.len());
    EXPECT_EQ(rstd::fs::read_to_string(
                  layout.bin_directory.join(PathBuf::from("shared"_str).as_path()).as_path())
                  .unwrap()
                  .as_str(),
              "runtime"_str);
}

TEST_F(InstallStore, InstallStorePublishesNestedGenericEntries) {
    auto root_directory   = install_root("install-store-generic"_str);
    auto source_directory = source_root("install-store-generic-source"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto source = source_directory.join(PathBuf::from("runtime.so"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "runtime"_str.as_bytes()).is_ok());
    auto install = [&]() {
        auto entries = Vec<lito::InstallEntry>::make();
        entries.push(lito::InstallEntry {
            .origin =
                lito::InstallEntryOrigin::ExternalAsset(String::make("runtime"_str),
                                                        String::make("files"_str),
                                                        PathBuf::from("nested/runtime.so"_str)),
            .payload              = lito::InstallEntryPayload::CopyFile(source.clone()),
            .relative_destination = PathBuf::from("lib/runtime/nested/runtime.so"_str),
        });
        entries.push(lito::InstallEntry {
            .origin  = lito::InstallEntryOrigin::Inventory(),
            .payload = lito::InstallEntryPayload::Bytes(Vec<u8>::from("manifest\n"_str.as_bytes()),
                                                        u32(0644)),
            .relative_destination = PathBuf::from("share/fixture/files.txt"_str),
        });
        auto packages = Vec<lito::InstallPackageRecord>::make();
        packages.push(lito::InstallPackageRecord {
            .name       = String::make("fixture-generic"_str),
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
    auto first = install();
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(first->managed_layout.is_some());
    ASSERT_EQ(first->entries.len(), usize(2));
    EXPECT_EQ(first->entries[usize {}].action, lito::InstallAction::Created);
    EXPECT_EQ(first->entries[usize(1)].action, lito::InstallAction::Created);
    auto package_id = lito::install_package_id(
        "fixture-generic"_str,
        lito::source::path_source_identity(source_directory.as_path()).as_str());
    ASSERT_TRUE(package_id.is_ok());
    auto private_root = first->managed_layout->packages_directory.join(
        PathBuf::from(package_id->as_str()).as_path());
    EXPECT_EQ(rstd::fs::read_to_string(
                  private_root.join(PathBuf::from("lib/runtime/nested/runtime.so"_str).as_path())
                      .as_path())
                  .unwrap()
                  .as_str(),
              "runtime"_str);
    EXPECT_EQ(
        rstd::fs::read_to_string(
            private_root.join(PathBuf::from("share/fixture/files.txt"_str).as_path()).as_path())
            .unwrap()
            .as_str(),
        "manifest\n"_str);
    auto second = install();
    ASSERT_TRUE(second.is_ok());
    for (const auto& entry : second->entries) {
        EXPECT_EQ(entry.action, lito::InstallAction::Unchanged);
    }
    auto catalog = lito::load_managed_install_catalog(*second->managed_layout);
    ASSERT_TRUE(catalog.is_ok());
    ASSERT_EQ(catalog->packages.len(), usize(1));
    EXPECT_EQ(catalog->packages[usize {}].layout,
              lito::InstallManagedPackageLayout::IsolatedPrefix);
    EXPECT_EQ(catalog->packages[usize {}].entries.len(), usize(2));
}
