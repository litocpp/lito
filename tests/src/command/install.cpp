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

TEST(InstallCommand, InstallBuildConsumesTheResolvedProject) {
    auto base    = output_root("install-resolved-project"_str);
    auto fixture = base.join(PathBuf::from("project"_str).as_path());
    auto output  = base.join(PathBuf::from("output"_str).as_path());
    ASSERT_TRUE(clear_output(base.as_path()));
    ASSERT_TRUE(copy_directory(fixture_path("build/script/configure-file"_str).as_path(),
                               fixture.as_path()));

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

TEST(InstallCommand, InstallOnlyRecipeDoesNotRequireBuildArtifacts) {
    auto base    = output_root("install-only"_str);
    auto fixture = base.join(PathBuf::from("project"_str).as_path());
    auto output  = base.join(PathBuf::from("output"_str).as_path());
    auto install = base.join(PathBuf::from("install"_str).as_path());
    ASSERT_TRUE(clear_output(base.as_path()));
    ASSERT_TRUE(copy_directory(fixture_path("install/manifest/install-only"_str).as_path(),
                               fixture.as_path()));

    auto source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(fixture.clone()));
    ASSERT_TRUE(source.is_ok());
    auto request = lito::InstallRequest {
        .source      = rstd::move(source).unwrap(),
        .build       = build_request(fixture.as_path(),
                                     output.as_path(),
                                     strings("fixture-install-only"_str),
                                     build_profile("release"_str)),
        .destination = managed_destination(install.as_path()),
    };
    request.build.locked = false;
    auto result          = lito::install(rstd::move(request));
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        rstd::io::eprintln("{}", error_chain_text(error));
        FAIL();
        return;
    }
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->build.artifacts.is_empty());
    EXPECT_EQ(result->packages.len(), usize(1));
    auto layout = lito::create_install_layout(lito::InstallRoot { .path = install.clone() });
    ASSERT_TRUE(layout.is_ok());
    auto catalog = lito::load_managed_install_catalog(*layout);
    ASSERT_TRUE(catalog.is_ok());
    ASSERT_EQ(catalog->packages.len(), usize(1));
    auto resource =
        layout->packages_directory
            .join(PathBuf::from(catalog->packages[usize {}].identity.id.as_str()).as_path())
            .join(PathBuf::from("share/fixture/resource.txt"_str).as_path());
    EXPECT_EQ(rstd::fs::read_to_string(resource.as_path()).unwrap().as_str(), "fixture\n"_str);

    EXPECT_TRUE(clear_output(base.as_path()));
}

TEST(InstallCommand, ConcurrentInstallStoreUpdatesPreserveBothPackages) {
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
            .name       = String::make(package_name),
            .version    = String::make("1.0.0"_str),
            .profile    = String::make("release"_str),
            .target     = String::make("x86_64-test"_str),
            .binaries   = rstd::move(binaries),
            .provenance = local_provenance(source_directory.as_path()),
        });
        return lito::InstallStoreRequest {
            .destination = managed_destination(root_directory.as_path()),
            .packages    = rstd::move(packages),
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

    auto layout = lito::create_install_layout(lito::InstallRoot { .path = root_directory.clone() });
    ASSERT_TRUE(layout.is_ok());
    auto catalog = lito::load_managed_install_catalog(*layout);
    ASSERT_TRUE(catalog.is_ok());
    ASSERT_EQ(catalog->packages.len(), usize(2));
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

TEST(InstallCommand, InstallRequiresEveryExplicitPackageToMatchTheBinaryFilter) {
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
        .destination = managed_destination(install.as_path()),
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
