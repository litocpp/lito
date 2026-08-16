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

class InstallCommand : public ProjectFixture {};

TEST_F(InstallCommand, InstallBuildConsumesTheResolvedProject) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "fixture-configure-file"
version = "0.1.0"
[[bin]]
link-stdlib = false
name = "configure-file"
sources = ["src/main.cpp"]
[usage]
private-include-directories = [{ path = "include", root = "generated" }]
)"_str },
        { "build.lua"_str, R"(lito.configure_file({
    package = "fixture-configure-file",
    input = "config/build_config.hpp.in",
    output = "include/fixture/build_config.hpp",
    values = { PROFILE = lito.profile, ENABLE_TRACE = false, ABI_REVISION = 3,
               LITERAL = "@NOT_RECURSIVE@" },
})
)"_str },
        { "config/build_config.hpp.in"_str, R"(#pragma once
#define FIXTURE_PROFILE "@PROFILE@"
#define FIXTURE_ENABLE_TRACE @ENABLE_TRACE@
#define FIXTURE_ABI_REVISION @ABI_REVISION@
#define FIXTURE_LITERAL "@LITERAL@"
)"_str },
        { "src/main.cpp"_str, R"(#include <fixture/build_config.hpp>
static_assert(FIXTURE_ABI_REVISION == 3);
static_assert(! FIXTURE_ENABLE_TRACE);
int main() { return FIXTURE_PROFILE[0] == 'r' ? 0 : 1; }
)"_str },
    };
    auto project = materialize("install-resolved-project"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto fixture = project->root.clone();
    auto output  = build_root("install-resolved-project"_str);

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
    if (built.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(built.unwrap_err()));
        FAIL();
        return;
    }
    ASSERT_EQ(built->selected_targets.len(), usize(1));
    EXPECT_EQ(built->selected_targets[usize {}].package.as_str(), "fixture-configure-file"_str);
    EXPECT_EQ(built->selected_targets[usize {}].kind, lito::PackageTargetKind::Binary);
}

TEST_F(InstallCommand, InstallOnlyRecipeDoesNotRequireBuildArtifacts) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = \"fixture-install-only\"\nversion = \"1.2.3\"\n"_str },
        { "install.lua"_str, R"(lito.install({
    files = {
        { source = "resource.txt", destination = "share/fixture/resource.txt" },
    },
})
)"_str },
        { "resource.txt"_str, "fixture\n"_str },
    };
    auto project = materialize("install-only"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto fixture = project->root.clone();
    auto output  = build_root("install-only"_str);
    auto install = install_root("install-only"_str);

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
}

TEST_F(InstallCommand, ConcurrentInstallStoreUpdatesPreserveBothPackages) {
    auto root_directory   = install_root("install-store-concurrent"_str);
    auto source_directory = source_root("install-store-concurrent-source"_str);
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
}

TEST_F(InstallCommand, InstallRequiresEveryExplicitPackageToMatchTheBinaryFilter) {
    auto tree = install_selection_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("install-selection"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root    = project->root.clone();
    auto output  = build_root("install-package-filter-build"_str);
    auto install = install_root("install-package-filter-root"_str);
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
}
