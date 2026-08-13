#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.manifest;
import lito.source;
import lito.test.support;
import lito.workspace.contract;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

namespace
{

auto regular_file_count(ref<rstd::path::Path> directory) -> Option<usize> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) return None();
    auto count   = usize {};
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) return None();
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) return None();
        if (type->is_file()) {
            ++count;
        } else if (type->is_dir()) {
            auto nested = regular_file_count(entry.path().as_path());
            if (nested.is_none()) return None();
            count += *nested;
        }
    }
    return Some(count);
}

struct CompileProgressCapture {
    Vec<lito::BuildProgress> values;
    bool                     missing {};
};

void capture_compile_progress(void* raw_context, const lito::BuildEvent& event) noexcept {
    if (event.kind != lito::BuildEventKind::Compile) return;
    auto& capture = *static_cast<CompileProgressCapture*>(raw_context);
    if (event.progress.is_none()) {
        capture.missing = true;
        return;
    }
    capture.values.push(lito::BuildProgress {
        .current = event.progress->current,
        .total   = event.progress->total,
    });
}

} // namespace

TEST(Integration, ScanUsesNativePreprocessorAndDefinitions) {
    auto root   = project_root();
    auto native = lito::scan(lito::ScanRequest {
        .selection =
            lito::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-preprocessor-native"_str),
            },
        .source        = PathBuf::from("preprocessor-native/src/lib.cppm"_str),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->result.provided.is_some());
    EXPECT_EQ(native->result.provided->logical_name.as_str(), "fixture.preprocessor.native"_str);
    EXPECT_TRUE(has_import(*native, "fixture.preprocessor.native:dependency"_str));
    ASSERT_EQ(native->result.imports.len(), usize(1));
    EXPECT_TRUE(native->result.imports[usize {}].exported);
    EXPECT_FALSE(has_import(*native, "fixture.preprocessor.native:native_builtin_failure"_str));

    auto native_json = lito::scan_report_json(*native);
    ASSERT_TRUE(native_json.is_ok());
    EXPECT_TRUE(native_json->as_str().contains("\"format\": \"lito-scan\""_str));
    EXPECT_TRUE(native_json->as_str().contains("\"version\": 2"_str));
    EXPECT_TRUE(native_json->as_str().contains("\"exported\": true"_str));

    auto p1689_json = lito::scan_report_json(*native, lito::ScanOutputFormat::P1689);
    ASSERT_TRUE(p1689_json.is_ok());
    EXPECT_TRUE(p1689_json->as_str().contains("\"version\": 1"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"revision\": 0"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"primary-output\":"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"provides\":"_str));
    EXPECT_TRUE(
        p1689_json->as_str().contains("\"logical-name\": \"fixture.preprocessor.native\""_str));
    EXPECT_TRUE(p1689_json->as_str().contains(
        "\"logical-name\": \"fixture.preprocessor.native:dependency\""_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"exported\":"_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"format\":"_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"headers\":"_str));

    auto definitions = lito::scan(lito::ScanRequest {
        .selection =
            lito::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-scan-definitions"_str),
            },
        .source        = PathBuf::from("scan-definitions/src/lib.cppm"_str),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(definitions.is_ok());
    EXPECT_TRUE(has_import(*definitions, "fixture.scan.definitions:defined"_str));
    EXPECT_FALSE(has_import(*definitions, "fixture.scan.definitions:missing"_str));
    EXPECT_FALSE(
        has_import(*definitions, "fixture.scan.definitions:command_line_undef_failure"_str));
}

TEST(Integration, BuildSelectsProductionArtifacts) {
    auto root   = project_root();
    auto output = output_root("build"_str);
    clear_output(output.as_path());
    auto request = build_request(
        root.as_path(), output.as_path(), strings("fixture-test-lib"_str, "fixture-test-app"_str));
    auto summary = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::TestExecutable), usize {});
    clear_output(output.as_path());
}

TEST(Integration, InstallStoreTracksOwnershipAndProtectsConflicts) {
    auto root_directory   = output_root("install-store"_str);
    auto source_directory = output_root("install-store-source"_str);
    ASSERT_TRUE(clear_output(root_directory.as_path()));
    ASSERT_TRUE(clear_output(source_directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto first_source = source_directory.join(PathBuf::from("tool"_str).as_path());
    auto old_source   = source_directory.join(PathBuf::from("old"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(first_source.as_path(), "first"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(old_source.as_path(), "old"_str.as_bytes()).is_ok());

    const auto make_binary = [&](ref<rstd::path::Path> source, ref<str> name) {
        return lito::InstallBinary {
            .target =
                lito::PackageTargetId {
                    .package = String::make("fixture-tool"_str),
                    .kind    = lito::PackageTargetKind::Binary,
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
                .name     = String::make("fixture-tool"_str),
                .version  = String::make(version),
                .profile  = String::make("release"_str),
                .target   = String::make("x86_64-test"_str),
                .binaries = rstd::move(binaries),
            });
            return lito::install_artifacts(lito::InstallStoreRequest {
                .root       = lito::InstallRoot { .path = root_directory.clone() },
                .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
                .packages   = rstd::move(packages),
                .force      = force,
            });
        };

    auto first = install_package(first_source.as_path(), "1.0.0"_str, true);
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->binaries.len(), usize(2));
    EXPECT_EQ(first->binaries[usize {}].action, lito::InstallAction::Created);
    auto installed = first->layout.bin_directory.join(PathBuf::from("tool"_str).as_path());
    auto contents  = rstd::fs::read_to_string(installed.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_EQ(contents->as_str(), "first"_str);
    auto old_installed = first->layout.bin_directory.join(PathBuf::from("old"_str).as_path());
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
            lito::PackageTargetId {
                .package = String::make("fixture-other"_str),
                .kind    = lito::PackageTargetKind::Binary,
                .name    = String::make("unmanaged"_str),
            },
        .source = rstd::move(external),
    });
    auto conflict_packages = Vec<lito::InstallPackageRecord>::make();
    conflict_packages.push(lito::InstallPackageRecord {
        .name     = String::make("fixture-other"_str),
        .version  = String::make("1.0.0"_str),
        .profile  = String::make("release"_str),
        .target   = String::make("x86_64-test"_str),
        .binaries = rstd::move(binaries),
    });
    auto conflict = lito::install_artifacts(lito::InstallStoreRequest {
        .root       = lito::InstallRoot { .path = root_directory.clone() },
        .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
        .packages   = rstd::move(conflict_packages),
    });
    ASSERT_TRUE(conflict.is_err());
    auto conflict_error = rstd::move(conflict).unwrap_err();
    ASSERT_TRUE(conflict_error.is_Cause());
    ASSERT_TRUE(conflict_error.as_Cause().source.is_Message());
    EXPECT_TRUE(conflict_error.as_Cause().source.as_Message().message.as_str().contains(
        "not managed"_str));

    auto forced_binaries = Vec<lito::InstallBinary>::make();
    auto forced_source   = source_directory.join(PathBuf::from("unmanaged"_str).as_path());
    forced_binaries.push(lito::InstallBinary {
        .target =
            lito::PackageTargetId {
                .package = String::make("fixture-other"_str),
                .kind    = lito::PackageTargetKind::Binary,
                .name    = String::make("unmanaged"_str),
            },
        .source = rstd::move(forced_source),
    });
    auto forced_packages = Vec<lito::InstallPackageRecord>::make();
    forced_packages.push(lito::InstallPackageRecord {
        .name     = String::make("fixture-other"_str),
        .version  = String::make("1.0.0"_str),
        .profile  = String::make("release"_str),
        .target   = String::make("x86_64-test"_str),
        .binaries = rstd::move(forced_binaries),
    });
    auto forced = lito::install_artifacts(lito::InstallStoreRequest {
        .root       = lito::InstallRoot { .path = root_directory.clone() },
        .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
        .packages   = rstd::move(forced_packages),
        .force      = true,
    });
    ASSERT_TRUE(forced.is_ok());
    auto forced_contents = rstd::fs::read_to_string(unmanaged.as_path());
    ASSERT_TRUE(forced_contents.is_ok());
    EXPECT_EQ(forced_contents->as_str(), "external"_str);

    EXPECT_TRUE(clear_output(root_directory.as_path()));
    EXPECT_TRUE(clear_output(source_directory.as_path()));
}

TEST(Integration, InstallStoreCommitsMultiplePackagesTogether) {
    auto root_directory   = output_root("install-store-batch"_str);
    auto source_directory = output_root("install-store-batch-source"_str);
    ASSERT_TRUE(clear_output(root_directory.as_path()));
    ASSERT_TRUE(clear_output(source_directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());

    const auto package = [&](ref<str> name, ref<str> binary_name, ref<str> contents) {
        auto source = source_directory.join(PathBuf::from(binary_name).as_path());
        EXPECT_TRUE(rstd::fs::write(source.as_path(), contents.as_bytes()).is_ok());
        auto binaries = Vec<lito::InstallBinary>::make();
        binaries.push(lito::InstallBinary {
            .target =
                lito::PackageTargetId {
                    .package = String::make(name),
                    .kind    = lito::PackageTargetKind::Binary,
                    .name    = String::make(binary_name),
                },
            .source = rstd::move(source),
        });
        return lito::InstallPackageRecord {
            .name     = String::make(name),
            .version  = String::make("1.0.0"_str),
            .profile  = String::make("release"_str),
            .target   = String::make("x86_64-test"_str),
            .binaries = rstd::move(binaries),
        };
    };

    auto packages = Vec<lito::InstallPackageRecord>::make();
    packages.push(package("fixture-one"_str, "one"_str, "first"_str));
    packages.push(package("fixture-two"_str, "two"_str, "second"_str));
    auto installed = lito::install_artifacts(lito::InstallStoreRequest {
        .root       = lito::InstallRoot { .path = root_directory.clone() },
        .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
        .packages   = rstd::move(packages),
    });
    ASSERT_TRUE(installed.is_ok());
    EXPECT_EQ(installed->packages.len(), usize(2));
    EXPECT_EQ(installed->binaries.len(), usize(2));

    auto one = rstd::fs::read_to_string(
        installed->layout.bin_directory.join(PathBuf::from("one"_str).as_path()).as_path());
    auto two = rstd::fs::read_to_string(
        installed->layout.bin_directory.join(PathBuf::from("two"_str).as_path()).as_path());
    ASSERT_TRUE(one.is_ok());
    ASSERT_TRUE(two.is_ok());
    EXPECT_EQ(one->as_str(), "first"_str);
    EXPECT_EQ(two->as_str(), "second"_str);

    auto metadata_before = rstd::fs::read_to_string(installed->layout.metadata.as_path());
    ASSERT_TRUE(metadata_before.is_ok());
    auto left_directory  = source_directory.join(PathBuf::from("left"_str).as_path());
    auto right_directory = source_directory.join(PathBuf::from("right"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(left_directory.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(right_directory.as_path()).is_ok());
    auto left_source  = left_directory.join(PathBuf::from("collision"_str).as_path());
    auto right_source = right_directory.join(PathBuf::from("collision"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(left_source.as_path(), "left"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(right_source.as_path(), "right"_str.as_bytes()).is_ok());
    const auto collision_package = [](ref<str> name, PathBuf source) {
        auto binaries = Vec<lito::InstallBinary>::make();
        binaries.push(lito::InstallBinary {
            .target =
                lito::PackageTargetId {
                    .package = String::make(name),
                    .kind    = lito::PackageTargetKind::Binary,
                    .name    = String::make("collision"_str),
                },
            .source = rstd::move(source),
        });
        return lito::InstallPackageRecord {
            .name     = String::make(name),
            .version  = String::make("1.0.0"_str),
            .profile  = String::make("release"_str),
            .target   = String::make("x86_64-test"_str),
            .binaries = rstd::move(binaries),
        };
    };
    auto collisions = Vec<lito::InstallPackageRecord>::make();
    collisions.push(collision_package("fixture-left"_str, rstd::move(left_source)));
    collisions.push(collision_package("fixture-right"_str, rstd::move(right_source)));
    auto conflict = lito::install_artifacts(lito::InstallStoreRequest {
        .root       = lito::InstallRoot { .path = root_directory.clone() },
        .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
        .packages   = rstd::move(collisions),
        .force      = true,
    });
    ASSERT_TRUE(conflict.is_err());
    auto conflict_error = rstd::move(conflict).unwrap_err();
    ASSERT_TRUE(conflict_error.is_Cause());
    ASSERT_TRUE(conflict_error.as_Cause().source.is_Message());
    EXPECT_TRUE(conflict_error.as_Cause().source.as_Message().message.as_str().contains(
        "more than one entry"_str));
    auto metadata_after = rstd::fs::read_to_string(installed->layout.metadata.as_path());
    ASSERT_TRUE(metadata_after.is_ok());
    EXPECT_EQ(metadata_after->as_str(), metadata_before->as_str());
    EXPECT_EQ(
        rstd::fs::read_to_string(
            installed->layout.bin_directory.join(PathBuf::from("one"_str).as_path()).as_path())
            .unwrap()
            .as_str(),
        "first"_str);
    EXPECT_EQ(
        rstd::fs::read_to_string(
            installed->layout.bin_directory.join(PathBuf::from("two"_str).as_path()).as_path())
            .unwrap()
            .as_str(),
        "second"_str);
    EXPECT_FALSE(rstd::fs::exists(
                     installed->layout.bin_directory.join(PathBuf::from("collision"_str).as_path())
                         .as_path())
                     .unwrap());

    EXPECT_TRUE(clear_output(root_directory.as_path()));
    EXPECT_TRUE(clear_output(source_directory.as_path()));
}

TEST(Integration, InstallStorePublishesNestedGenericEntries) {
    auto root_directory   = output_root("install-store-generic"_str);
    auto source_directory = output_root("install-store-generic-source"_str);
    ASSERT_TRUE(clear_output(root_directory.as_path()));
    ASSERT_TRUE(clear_output(source_directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());
    auto source = source_directory.join(PathBuf::from("runtime.so"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "runtime"_str.as_bytes()).is_ok());
    auto install = [&]() {
        auto entries = Vec<lito::InstallEntry>::make();
        entries.push(lito::InstallEntry {
            .origin = lito::InstallEntryOrigin::ExternalAsset(
                String::make("runtime"_str),
                String::make("files"_str),
                PathBuf::from("nested/runtime.so"_str)),
            .payload = lito::InstallEntryPayload::CopyFile(source.clone()),
            .relative_destination = PathBuf::from("lib/runtime/nested/runtime.so"_str),
        });
        entries.push(lito::InstallEntry {
            .origin = lito::InstallEntryOrigin::Inventory(),
            .payload = lito::InstallEntryPayload::Bytes(
                Vec<u8>::from("manifest\n"_str.as_bytes()), u32(0644)),
            .relative_destination = PathBuf::from("share/fixture/files.txt"_str),
        });
        auto packages = Vec<lito::InstallPackageRecord>::make();
        packages.push(lito::InstallPackageRecord {
            .name     = String::make("fixture-generic"_str),
            .version  = String::make("1.0.0"_str),
            .profile  = String::make("release"_str),
            .target   = String::make("x86_64-test"_str),
            .entries  = rstd::move(entries),
        });
        return lito::install_artifacts(lito::InstallStoreRequest {
            .root       = lito::InstallRoot { .path = root_directory.clone() },
            .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
            .packages   = rstd::move(packages),
        });
    };
    auto first = install();
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->entries.len(), usize(2));
    EXPECT_EQ(first->entries[usize {}].action, lito::InstallAction::Created);
    EXPECT_EQ(first->entries[usize(1)].action, lito::InstallAction::Created);
    EXPECT_EQ(rstd::fs::read_to_string(
                  root_directory.join(PathBuf::from("lib/runtime/nested/runtime.so"_str).as_path())
                      .as_path())
                  .unwrap()
                  .as_str(),
              "runtime"_str);
    EXPECT_EQ(rstd::fs::read_to_string(
                  root_directory.join(PathBuf::from("share/fixture/files.txt"_str).as_path())
                      .as_path())
                  .unwrap()
                  .as_str(),
              "manifest\n"_str);
    auto second = install();
    ASSERT_TRUE(second.is_ok());
    for (const auto& entry : second->entries) {
        EXPECT_EQ(entry.action, lito::InstallAction::Unchanged);
    }
    auto metadata = rstd::fs::read_to_string(second->layout.metadata.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->as_str().contains("\"version\": 2"_str));
    EXPECT_TRUE(metadata->as_str().contains("lib/runtime/nested/runtime.so"_str));
    EXPECT_TRUE(clear_output(root_directory.as_path()));
    EXPECT_TRUE(clear_output(source_directory.as_path()));
}

TEST(Integration, InstallBuildConsumesTheResolvedProject) {
    auto base    = output_root("install-resolved-project"_str);
    auto fixture = base.join(PathBuf::from("project"_str).as_path());
    auto output  = base.join(PathBuf::from("output"_str).as_path());
    ASSERT_TRUE(clear_output(base.as_path()));
    ASSERT_TRUE(
        copy_directory(root("build-script/configure-file"_str).as_path(), fixture.as_path()));

    auto source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(fixture.clone()));
    ASSERT_TRUE(source.is_ok());
    auto resolved = rstd::move(source).unwrap();
    auto manifest = fixture.join(PathBuf::from("lito.toml"_str).as_path());
    auto hidden   = fixture.join(PathBuf::from("lito.toml.hidden"_str).as_path());
    ASSERT_TRUE(rstd::fs::rename(manifest.as_path(), hidden.as_path()).is_ok());

    auto request    = build_request(resolved.project.root.as_path(),
                                    output.as_path(),
                                    strings("fixture-configure-file"_str),
                                    build_profile("release"_str));
    request.purpose = lito::PackageSelectionPurpose::Install;
    request.targets = strings("bin:configure-file"_str);
    auto built = lito::build_resolved_project(rstd::move(request), rstd::move(resolved.project));
    ASSERT_TRUE(built.is_ok());
    ASSERT_EQ(built->selected_targets.len(), usize(1));
    EXPECT_EQ(built->selected_targets[usize {}].package.as_str(), "fixture-configure-file"_str);
    EXPECT_EQ(built->selected_targets[usize {}].kind, lito::PackageTargetKind::Binary);

    EXPECT_TRUE(clear_output(base.as_path()));
}

TEST(Integration, InstallOnlyRecipeDoesNotRequireBuildArtifacts) {
    auto base    = output_root("install-only"_str);
    auto fixture = base.join(PathBuf::from("project"_str).as_path());
    auto output  = base.join(PathBuf::from("output"_str).as_path());
    auto install = base.join(PathBuf::from("install"_str).as_path());
    ASSERT_TRUE(clear_output(base.as_path()));
    ASSERT_TRUE(copy_directory(root("manifest/install-only"_str).as_path(), fixture.as_path()));

    auto source = lito::resolve_install_source(
        lito::InstallSourceRequirement::LocalProject(fixture.clone()));
    ASSERT_TRUE(source.is_ok());
    auto request = lito::InstallRequest {
        .source = rstd::move(source).unwrap(),
        .build  = build_request(fixture.as_path(),
                                output.as_path(),
                                strings("fixture-install-only"_str),
                                build_profile("release"_str)),
        .root   = lito::InstallRoot { .path = install.clone() },
    };
    request.build.locked = false;
    auto result = lito::install(rstd::move(request));
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        rstd::io::eprintln("{}", error_chain_text(error));
        FAIL();
        return;
    }
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->build.artifacts.is_empty());
    EXPECT_EQ(result->packages.len(), usize(1));
    EXPECT_EQ(rstd::fs::read_to_string(
                  install.join(PathBuf::from("share/fixture/resource.txt"_str).as_path()).as_path())
                  .unwrap()
                  .as_str(),
              "fixture\n"_str);

    EXPECT_TRUE(clear_output(base.as_path()));
}

TEST(Integration, ConcurrentInstallStoreUpdatesPreserveBothPackages) {
    auto root_directory   = output_root("install-store-concurrent"_str);
    auto source_directory = output_root("install-store-concurrent-source"_str);
    ASSERT_TRUE(clear_output(root_directory.as_path()));
    ASSERT_TRUE(clear_output(source_directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(source_directory.as_path()).is_ok());

    const auto request = [&](ref<str> package_name, ref<str> binary_name) {
        auto source = source_directory.join(PathBuf::from(binary_name).as_path());
        EXPECT_TRUE(rstd::fs::write(source.as_path(), binary_name.as_bytes()).is_ok());
        auto binaries = Vec<lito::InstallBinary>::make();
        binaries.push(lito::InstallBinary {
            .target =
                lito::PackageTargetId {
                    .package = String::make(package_name),
                    .kind    = lito::PackageTargetKind::Binary,
                    .name    = String::make(binary_name),
                },
            .source = rstd::move(source),
        });
        auto packages = Vec<lito::InstallPackageRecord>::make();
        packages.push(lito::InstallPackageRecord {
            .name     = String::make(package_name),
            .version  = String::make("1.0.0"_str),
            .profile  = String::make("release"_str),
            .target   = String::make("x86_64-test"_str),
            .binaries = rstd::move(binaries),
        });
        return lito::InstallStoreRequest {
            .root       = lito::InstallRoot { .path = root_directory.clone() },
            .provenance = lito::InstallSourceProvenance::Local(source_directory.clone()),
            .packages   = rstd::move(packages),
        };
    };

    auto created =
        rstd::thread::BlockingTaskGroup<lito::InstallStoreResult<lito::InstallStoreSummary>>::make(
            usize(2), usize(2));
    ASSERT_TRUE(created.is_ok());
    auto group = rstd::move(created).unwrap();
    ASSERT_TRUE(group
                    .submit([value = request("fixture-alpha"_str, "alpha"_str)]() mutable {
                        return lito::install_artifacts(rstd::move(value));
                    })
                    .is_ok());
    ASSERT_TRUE(group
                    .submit([value = request("fixture-beta"_str, "beta"_str)]() mutable {
                        return lito::install_artifacts(rstd::move(value));
                    })
                    .is_ok());
    auto outcomes = rstd::move(group).join();
    ASSERT_EQ(outcomes.len(), usize(2));
    for (auto& outcome : outcomes) {
        ASSERT_TRUE(outcome.is_completed());
        auto result = rstd::move(outcome).into_value().unwrap();
        EXPECT_TRUE(result.is_ok());
    }

    auto metadata = rstd::fs::read_to_string(
        root_directory.join(PathBuf::from(".lito/installed.json"_str).as_path()).as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->as_str().contains("fixture-alpha"_str));
    EXPECT_TRUE(metadata->as_str().contains("fixture-beta"_str));
    EXPECT_EQ(rstd::fs::read_to_string(
                  root_directory.join(PathBuf::from("bin/alpha"_str).as_path()).as_path())
                  .unwrap()
                  .as_str(),
              "alpha"_str);
    EXPECT_EQ(rstd::fs::read_to_string(
                  root_directory.join(PathBuf::from("bin/beta"_str).as_path()).as_path())
                  .unwrap()
                  .as_str(),
              "beta"_str);

    EXPECT_TRUE(clear_output(root_directory.as_path()));
    EXPECT_TRUE(clear_output(source_directory.as_path()));
}

TEST(Integration, InstallRequiresEveryExplicitPackageToMatchTheBinaryFilter) {
    auto root    = project_root();
    auto output  = output_root("install-package-filter-build"_str);
    auto install = output_root("install-package-filter-root"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    ASSERT_TRUE(clear_output(install.as_path()));
    auto source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(root.clone()));
    ASSERT_TRUE(source.is_ok());
    auto request = lito::InstallRequest {
        .source = rstd::move(source).unwrap(),
        .build  = build_request(root.as_path(),
                                output.as_path(),
                                strings("fixture-multi-target"_str, "fixture-multi-consumer"_str),
                                build_profile("release"_str)),
        .root   = lito::InstallRoot { .path = install.clone() },
    };
    request.binaries.push(String::make("tool"_str));
    auto result = lito::install(rstd::move(request));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_Message());
    EXPECT_TRUE(error.as_Message().message.as_str().contains(
        "package 'fixture-multi-consumer' has no selected installable binaries"_str));
    EXPECT_FALSE(rstd::fs::exists(install.as_path()).unwrap());

    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Integration, PackageTargetsSelectTypedArtifactsAndRunBenchmarks) {
    auto root   = project_root();
    auto output = output_root("multi-target"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::Executable), usize(2));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::TestExecutable), usize {});
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::BenchmarkExecutable), usize {});

    auto consumed = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-consumer"_str)));
    ASSERT_TRUE(consumed.is_ok());
    EXPECT_EQ(artifact_count(*consumed, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*consumed, lito::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*consumed, lito::ArtifactKind::BenchmarkExecutable), usize {});

    auto ambiguous_request =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    ambiguous_request.targets = strings("shared"_str);
    auto ambiguous            = lito::build(rstd::move(ambiguous_request));
    ASSERT_TRUE(ambiguous.is_err());
    auto ambiguous_error = rstd::move(ambiguous).unwrap_err();
    ASSERT_TRUE(ambiguous_error.is_Package());
    ASSERT_TRUE(ambiguous_error.as_Package().source.is_Message());
    EXPECT_TRUE(ambiguous_error.as_Package().source.as_Message().message.as_str().contains(
        "ambiguous"_str));

    auto typed_request =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    typed_request.targets = strings("bin:shared"_str);
    auto typed            = lito::build(rstd::move(typed_request));
    ASSERT_TRUE(typed.is_ok());
    EXPECT_EQ(artifact_count(*typed, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*typed, lito::ArtifactKind::Executable), usize(1));

    auto test_request = lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str)),
        .no_run = true,
    };
    auto tested = lito::test(rstd::move(test_request));
    ASSERT_TRUE(tested.is_ok());
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::TestExecutable), usize(2));
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::BenchmarkExecutable), usize {});

    auto bench_build =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    bench_build.profile = None();
    auto benchmarked    = lito::bench(lito::BenchRequest {
        .build     = rstd::move(bench_build),
        .arguments = strings("expected-benchmark"_str),
    });
    ASSERT_TRUE(benchmarked.is_ok());
    EXPECT_TRUE(benchmarked->success());
    EXPECT_EQ(benchmarked->build.profile.as_str(), "release"_str);
    EXPECT_EQ(artifact_count(benchmarked->build, lito::ArtifactKind::BenchmarkExecutable),
              usize(2));
    ASSERT_EQ(benchmarked->executions.len(), usize(2));
    EXPECT_EQ(benchmarked->executions[usize {}].target.name.as_str(), "shared"_str);
    EXPECT_EQ(benchmarked->executions[usize(1)].target.name.as_str(), "speed"_str);
    auto package_root = root.join(PathBuf::from("multi-target"_str).as_path());
    for (const auto& execution : benchmarked->executions) {
        EXPECT_EQ(execution.working_directory.as_path(), package_root.as_path());
    }

    auto debug_build =
        build_request(root.as_path(), output.as_path(), strings("fixture-multi-target"_str));
    auto debug_bench = lito::bench(lito::BenchRequest {
        .build  = rstd::move(debug_build),
        .no_run = true,
    });
    ASSERT_TRUE(debug_bench.is_ok());
    EXPECT_EQ(debug_bench->build.profile.as_str(), "debug"_str);
    EXPECT_TRUE(debug_bench->executions.is_empty());

    ASSERT_TRUE(clear_output(output.as_path()));
}

TEST(Integration, TestAttachmentKeepsProductionArtifactsIsolated) {
    auto root   = project_root();
    auto output = output_root("test-attachment"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-test-attach-lib"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::TestAttachmentArchive), usize {});
    auto attachment_directory =
        output.join(PathBuf::from("test-attachments/fixture-test-attach/fixture-test-attach/"
                                  "fixture-test-attach-lib/fixture-test-attach-lib"_str)
                        .as_path());
    EXPECT_FALSE(rstd::fs::exists(attachment_directory.as_path()).unwrap());

    auto tested = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-test-attach"_str)),
    });
    ASSERT_TRUE(tested.is_ok());
    EXPECT_TRUE(tested->success());
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::TestAttachmentArchive), usize(1));
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::TestExecutable), usize(1));
    EXPECT_TRUE(
        rstd::fs::exists(
            attachment_directory.join(PathBuf::from("libfixture_test_attach.test.a"_str).as_path())
                .as_path())
            .unwrap());
    ASSERT_TRUE(clear_output(output.as_path()));
}

TEST(Integration, TestRunsPassFailureSignalAndNoRun) {
    auto root   = project_root();
    auto output = output_root("test-command"_str);
    clear_output(output.as_path());

    auto pass_request = lito::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-pass"_str)),
        .arguments = strings("expected-argument"_str),
    };
    auto passed = lito::test(rstd::move(pass_request));
    ASSERT_TRUE(passed.is_ok());
    EXPECT_TRUE(passed->success());
    ASSERT_EQ(passed->executions.len(), usize(1));
    EXPECT_TRUE(passed->executions[usize {}].success());

    auto no_run_request = lito::TestRequest {
        .build = build_request(
            root.as_path(),
            output.as_path(),
            strings("fixture-test-pass"_str, "fixture-test-fail"_str, "fixture-test-signal"_str),
            build_profile("release"_str)),
        .no_run = true,
    };
    auto no_run = lito::test(rstd::move(no_run_request));
    ASSERT_TRUE(no_run.is_ok());
    EXPECT_EQ(no_run->build.profile.as_str(), "release"_str);
    EXPECT_TRUE(no_run->executions.is_empty());
    EXPECT_EQ(artifact_count(no_run->build, lito::ArtifactKind::TestExecutable), usize(3));

    auto failure = lito::test(lito::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-fail"_str)),
    });
    ASSERT_TRUE(failure.is_ok());
    EXPECT_FALSE(failure->success());

    auto signal = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-test-signal"_str)),
    });
    ASSERT_TRUE(signal.is_ok());
    EXPECT_FALSE(signal->success());

    auto production = lito::test(lito::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-app"_str)),
    });
    EXPECT_TRUE(production.is_err());
    clear_output(output.as_path());
}

TEST(Integration, CompileTestsReportOutcomesAndReuse) {
    auto root   = project_root();
    auto output = output_root("compile-test"_str);
    clear_output(output.as_path());

    auto passed = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-compile-pass"_str)),
    });
    ASSERT_TRUE(passed.is_ok());
    EXPECT_TRUE(passed->success());
    ASSERT_EQ(passed->build.compile_tests.len(), usize(3));
    for (const auto& execution : passed->build.compile_tests) {
        EXPECT_TRUE(execution.success());
    }

    auto reused = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-compile-pass"_str)),
    });
    ASSERT_TRUE(reused.is_ok());
    EXPECT_TRUE(reused->success());
    EXPECT_GE(reused->build.reused, usize(4));

    auto mismatch = lito::test(lito::TestRequest {
        .build = build_request(
            root.as_path(), output.as_path(), strings("fixture-compile-mismatch"_str)),
    });
    ASSERT_TRUE(mismatch.is_ok());
    EXPECT_FALSE(mismatch->success());
    ASSERT_EQ(mismatch->build.compile_tests.len(), usize(1));
    EXPECT_TRUE(mismatch->build.compile_tests[usize {}].mismatch.is_some());

    auto unsupported = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-windows-only"_str)));
    EXPECT_TRUE(unsupported.is_err());
    clear_output(output.as_path());
}

TEST(Integration, EnvironmentIsSharedWithinBuild) {
    auto root   = project_root();
    auto output = output_root("environment"_str);
    clear_output(output.as_path());
    auto request =
        build_request(root.as_path(), output.as_path(), strings("fixture-environment-cache"_str));
    request.execution.scan.jobs    = Some(usize(2));
    request.execution.compile.jobs = Some(usize(2));
    auto progress                   = CompileProgressCapture {};
    request.observer                = Some(lito::BuildObserver {
        .context = rstd::addressof(progress),
        .notify  = capture_compile_progress,
    });
    auto summary                   = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_FALSE(progress.missing);
    ASSERT_EQ(progress.values.len(), usize(2));
    EXPECT_EQ(progress.values[usize {}].current, usize(1));
    EXPECT_EQ(progress.values[usize(1)].current, usize(2));
    EXPECT_EQ(progress.values[usize {}].total, usize(2));
    EXPECT_EQ(progress.values[usize(1)].total, usize(2));
    EXPECT_EQ(summary->toolchain.preprocessor_environment_entries, usize(1));
    EXPECT_EQ(summary->toolchain.preprocessor_environment_queries, usize(1));
    EXPECT_GE(summary->toolchain.preprocessor_environment_hits, usize(1));
    EXPECT_EQ(summary->scan_profile.execution().jobs, usize(2));
    EXPECT_EQ(summary->scan_profile.execution().tasks, usize(2));
    EXPECT_EQ(summary->scan_profile.execution().max_active, usize(2));
    EXPECT_FALSE(summary->scan_profile.execution().task_work.is_zero());
    EXPECT_FALSE(summary->scan_profile.execution().completion_wait.is_zero());
    EXPECT_EQ(summary->compile_execution.jobs, usize(2));
    EXPECT_EQ(summary->compile_execution.tasks, usize(2));
    EXPECT_EQ(summary->compile_execution.max_active, usize(2));
    EXPECT_FALSE(summary->compile_execution.task_work.is_zero());
    EXPECT_FALSE(summary->compile_execution.wall.is_zero());
    EXPECT_EQ(summary->frontend.source_requests, usize(4));
    EXPECT_EQ(summary->frontend.source_reads, usize(3));
    EXPECT_EQ(summary->frontend.lex_builds, usize(3));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_requests, usize(4));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_hits, usize(1));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_builds, usize(3));
    auto report  = output.join(PathBuf::from("timing.txt"_str).as_path());
    auto emitted = lito::timing_output::emit(*summary,
                                             lito::timing_output::OutputOptions {
                                                 .file = Some(report.clone()),
                                             });
    ASSERT_TRUE(emitted.is_ok());
    auto contents = rstd::fs::read_to_string(report.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_TRUE(contents->as_str().contains("frontend"_str));
    EXPECT_TRUE(contents->as_str().contains("compile execution"_str));
    EXPECT_TRUE(contents->as_str().contains("build.compile"_str));
    EXPECT_TRUE(contents->as_str().contains("aggregate timing"_str));
    clear_output(output.as_path());
}

TEST(Integration, ScanCacheReusesAndInvalidatesOwnedInputs) {
    auto base = output_root("scan-cache"_str);
    clear_output(base.as_path());
    auto fixture = base.join(PathBuf::from("fixture"_str).as_path());
    auto output  = base.join(PathBuf::from("output"_str).as_path());
    auto source  = root("cache/scan"_str);
    ASSERT_TRUE(copy_directory(source.as_path(), fixture.as_path()));

    auto cold =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(cold.is_ok());
    EXPECT_EQ(cold->frontend.persistent_scan_misses, usize(1));
    EXPECT_EQ(cold->frontend.persistent_scan_refresh, usize(1));
    EXPECT_EQ(cold->frontend.analyze_builds, usize(1));
    auto bmi_directory = output.join(PathBuf::from("bmi"_str).as_path());
    auto cold_bmis     = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(cold_bmis.is_some());
    EXPECT_EQ(*cold_bmis, usize(1));

    auto warm =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(1));
    EXPECT_EQ(warm->frontend.analyze_builds, usize {});
    EXPECT_EQ(warm->compiled, usize {});

    auto staged_optional = fixture.join(PathBuf::from("staged/optional.hpp"_str).as_path());
    auto high_optional   = fixture.join(PathBuf::from("high/optional.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_optional.as_path(), high_optional.as_path()).is_ok());
    auto optional =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(optional.is_ok());
    EXPECT_EQ(optional->frontend.persistent_scan_include_lookup, usize(1));
    EXPECT_EQ(optional->frontend.analyze_builds, usize(1));

    auto staged_priority = fixture.join(PathBuf::from("staged/choice.hpp"_str).as_path());
    auto high_priority   = fixture.join(PathBuf::from("high/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_priority.as_path(), high_priority.as_path()).is_ok());
    auto priority =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(priority.is_ok());
    EXPECT_EQ(priority->frontend.persistent_scan_include_lookup, usize(1));

    auto staged_header = fixture.join(PathBuf::from("staged/choice-low.hpp"_str).as_path());
    auto low_header    = fixture.join(PathBuf::from("low/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_header.as_path(), low_header.as_path()).is_ok());
    auto header =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(header.is_ok());
    EXPECT_EQ(header->frontend.persistent_scan_file_dependency, usize(1));

    auto staged_source  = fixture.join(PathBuf::from("staged/lib.cppm"_str).as_path());
    auto primary_source = fixture.join(PathBuf::from("src/lib.cppm"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_source.as_path(), primary_source.as_path()).is_ok());
    auto changed_source =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(changed_source.is_ok());
    EXPECT_EQ(changed_source->frontend.persistent_scan_source, usize(1));
    auto changed_bmis = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(changed_bmis.is_some());
    EXPECT_EQ(*changed_bmis, usize(1));

    clear_output(base.as_path());

    auto dynamic_base = output_root("scan-cache-dynamic"_str);
    clear_output(dynamic_base.as_path());
    auto dynamic_fixture = dynamic_base.join(PathBuf::from("fixture"_str).as_path());
    auto dynamic_output  = dynamic_base.join(PathBuf::from("output"_str).as_path());
    auto dynamic_source  = root("cache/dynamic"_str);
    ASSERT_TRUE(copy_directory(dynamic_source.as_path(), dynamic_fixture.as_path()));
    auto dynamic_cold = lito::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_cold.is_ok());
    auto dynamic_warm = lito::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_warm.is_ok());
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_hits, usize {});
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_uncacheable, usize(1));
    EXPECT_EQ(dynamic_warm->frontend.analyze_builds, usize(1));
    clear_output(dynamic_base.as_path());
}

TEST(Integration, InvalidArtifactsAndDependenciesAreRejected) {
    auto multiple =
        lito::load_manifest_document(root("test-command/invalid-artifact"_str).as_path());
    EXPECT_TRUE(multiple.is_err());

    auto invalid = root("test-command/invalid-dependency"_str);
    auto output  = output_root("invalid-dependency"_str);
    clear_output(output.as_path());
    auto request    = build_request(invalid.as_path(), output.as_path(), Vec<String>::make());
    request.locked  = false;
    auto dependency = lito::build(request);
    EXPECT_TRUE(dependency.is_err());
    clear_output(output.as_path());
}

TEST(Integration, FormatCheckReportsWithoutChangingSources) {
    auto base    = output_root("format-check"_str);
    auto fixture = base.join(PathBuf::from("fixture"_str).as_path());
    ASSERT_TRUE(clear_output(base.as_path()));
    ASSERT_TRUE(copy_directory(root("environment/append-path"_str).as_path(), fixture.as_path()));

    auto           source      = fixture.join(PathBuf::from("src/main.cpp"_str).as_path());
    constexpr auto unformatted = "auto main()->int{return 0;}\n"_str;
    ASSERT_TRUE(rstd::fs::write(source.as_path(), unformatted.as_bytes()).is_ok());

    auto checked_project = lito::load_project_config(fixture.as_path());
    ASSERT_TRUE(checked_project.is_ok());
    auto checked = lito::format(lito::FormatRequest {
        .selection   = lito::PackageSelection { .root = fixture.clone() },
        .environment = rstd::move(checked_project->environment),
        .toolchain   = rstd::move(checked_project->toolchain),
        .sources     = rstd::move(checked_project->sources),
        .mode        = lito::FormatMode::Check,
    });
    ASSERT_TRUE(checked.is_ok());
    EXPECT_EQ(checked->packages, usize(1));
    EXPECT_EQ(checked->files, usize(1));
    ASSERT_EQ(checked->unformatted_files.len(), usize(1));
    EXPECT_EQ(checked->unformatted_files[usize {}].as_path(), source.as_path());
    EXPECT_FALSE(checked->success());
    auto unchanged = rstd::fs::read_to_string(source.as_path());
    ASSERT_TRUE(unchanged.is_ok());
    EXPECT_EQ(unchanged->as_str(), unformatted);

    auto format_project = lito::load_project_config(fixture.as_path());
    ASSERT_TRUE(format_project.is_ok());
    auto formatted = lito::format(lito::FormatRequest {
        .selection   = lito::PackageSelection { .root = fixture.clone() },
        .environment = rstd::move(format_project->environment),
        .toolchain   = rstd::move(format_project->toolchain),
        .sources     = rstd::move(format_project->sources),
    });
    ASSERT_TRUE(formatted.is_ok());
    EXPECT_TRUE(formatted->success());

    auto clean_project = lito::load_project_config(fixture.as_path());
    ASSERT_TRUE(clean_project.is_ok());
    auto clean = lito::format(lito::FormatRequest {
        .selection   = lito::PackageSelection { .root = fixture.clone() },
        .environment = rstd::move(clean_project->environment),
        .toolchain   = rstd::move(clean_project->toolchain),
        .sources     = rstd::move(clean_project->sources),
        .mode        = lito::FormatMode::Check,
    });
    ASSERT_TRUE(clean.is_ok());
    EXPECT_TRUE(clean->success());
    EXPECT_TRUE(clear_output(base.as_path()));
}
