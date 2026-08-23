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
    request.purpose = lito::package::PackageSelectionPurpose::Install;
    request.targets = strings("bin:configure-file"_str);
    auto built = lito::build_resolved_project(rstd::move(request), rstd::move(resolved.project));
    if (built.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(built.unwrap_err()));
        FAIL();
        return;
    }
    ASSERT_EQ(built->selected_targets.len(), usize(1));
    EXPECT_EQ(built->selected_targets[usize {}].package.as_str(), "fixture-configure-file"_str);
    EXPECT_EQ(built->selected_targets[usize {}].kind, lito::package::PackageTargetKind::Binary);
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
    ASSERT_TRUE(result->build.is_Built());
    EXPECT_TRUE(result->build.as_Built().summary.product.artifacts.is_empty());
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

TEST_F(InstallCommand, CargoRuntimeDependencyPublishesSelectableBinaryAssets) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-cargo-runtime-install"
version = "1.0.0"

[external-sources.rust]
path = "rust"

[external-dependencies.cargo.runtime]
source = "rust"
package = "fixture-cargo-runtime-package"
usage = "runtime"
)toml"_str },
        { "install.lua"_str, R"lua(lito.install({
  external_assets = {
    {
      dependency = "runtime",
      set = "fixture-daemon",
      destination = "bin",
    },
  },
})
)lua"_str },
        { "rust/Cargo.toml"_str, R"toml([package]
name = "fixture-cargo-runtime-package"
version = "1.0.0"
edition = "2024"

[[bin]]
name = "fixture-daemon"
path = "src/daemon.rs"

[[bin]]
name = "fixture-helper"
path = "src/helper.rs"
)toml"_str },
        { "rust/Cargo.lock"_str, R"lock(# This file is automatically @generated by Cargo.
# It is not intended for manual editing.
version = 4

[[package]]
name = "fixture-cargo-runtime-package"
version = "1.0.0"
)lock"_str },
        { "rust/src/daemon.rs"_str, "fn main() { println!(\"daemon\"); }\n"_str },
        { "rust/src/helper.rs"_str, "fn main() { println!(\"helper\"); }\n"_str },
    };
    auto project = materialize("cargo-runtime-install"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto fixture = project->root.clone();
    auto output  = build_root("cargo-runtime-install"_str);
    auto prefix  = install_root("cargo-runtime-install"_str);
    auto source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(fixture.clone()));
    ASSERT_TRUE(source.is_ok());
    auto request = lito::InstallRequest {
        .source = rstd::move(source).unwrap(),
        .build  = build_request(fixture.as_path(),
                                output.as_path(),
                                strings("fixture-cargo-runtime-install"_str),
                                build_profile("release"_str)),
        .destination =
            lito::InstallDestination::Prefix(lito::InstallPrefix { .path = prefix.clone() }),
    };
    request.build.locked = false;
    auto installed       = lito::install(rstd::move(request));
    if (installed.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(installed.unwrap_err()));
        FAIL();
        return;
    }
    ASSERT_TRUE(installed->build.is_Built());
    ASSERT_EQ(installed->build.as_Built().summary.product.external_assets.sets.len(), usize(2));
#if RSTD_OS_WINDOWS
    constexpr auto daemon = "bin/fixture-daemon.exe"_str;
    constexpr auto helper = "bin/fixture-helper.exe"_str;
#else
    constexpr auto daemon = "bin/fixture-daemon"_str;
    constexpr auto helper = "bin/fixture-helper"_str;
#endif
    EXPECT_TRUE(rstd::fs::exists(prefix.join(PathBuf::from(daemon).as_path()).as_path()).unwrap());
    EXPECT_FALSE(rstd::fs::exists(prefix.join(PathBuf::from(helper).as_path()).as_path()).unwrap());
}

TEST_F(InstallCommand, InstallRecipeCanSelectPreparedLibraryArtifact) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-install-library"
version = "1.0.0"
standard = "c99"

[lib]
name = "fixture-install-library"
kind = "shared"
artifact = "fixture-install-library"
sources = ["library.c"]
)toml"_str },
        { "install.lua"_str, R"lua(lito.install({
    artifacts = {
        {
            target = { kind = "lib", name = "fixture-install-library" },
            destination = "lib/libfixture-install-library.so",
        },
    },
    files = {{
        source = "include/fixture/library.h",
        destination = "include/fixture/library.h",
    }},
    pkg_config = {{
        target = { kind = "lib", name = "fixture-install-library" },
        description = "Fixture install library",
        include_directory = "include",
    }},
})
)lua"_str },
        { "include/fixture/library.h"_str, "int fixture_install_library(void);\n"_str },
        { "library.c"_str, "int fixture_install_library(void) { return 42; }\n"_str },
    };
    auto project = materialize("install-library-artifact"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto fixture = project->root.clone();
    auto output  = build_root("install-library-artifact"_str);
    auto prefix  = install_root("install-library-artifact"_str);
    auto source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(fixture.clone()));
    ASSERT_TRUE(source.is_ok());
    auto request = lito::InstallRequest {
        .source = rstd::move(source).unwrap(),
        .build  = build_request(fixture.as_path(),
                                output.as_path(),
                                strings("fixture-install-library"_str),
                                build_profile("release"_str)),
        .destination =
            lito::InstallDestination::Prefix(lito::InstallPrefix { .path = prefix.clone() }),
    };
    request.build.locked = false;
    auto result          = lito::install(rstd::move(request));
    if (result.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(result.unwrap_err()));
        FAIL();
        return;
    }
    EXPECT_TRUE(
        rstd::fs::exists(
            prefix.join(PathBuf::from("lib/libfixture-install-library.so"_str).as_path()).as_path())
            .unwrap());
    EXPECT_TRUE(rstd::fs::exists(
                    prefix.join(PathBuf::from("include/fixture/library.h"_str).as_path()).as_path())
                    .unwrap());
    auto pkg_config = rstd::fs::read_to_string(
        prefix.join(PathBuf::from("lib/pkgconfig/fixture-install-library.pc"_str).as_path())
            .as_path());
    ASSERT_TRUE(pkg_config.is_ok());
    EXPECT_EQ(pkg_config->as_str(), R"pc(prefix=${pcfiledir}/../..
libdir=${prefix}/lib
includedir=${prefix}/include

Name: fixture-install-library
Description: Fixture install library
Version: 1.0.0
Libs: -L${libdir} -lfixture-install-library
Cflags: -I${includedir}
)pc"_str);
}

TEST_F(InstallCommand, NoBuildInstallsPublishedProductWithoutCompilerTools) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"([package]
name = "fixture-no-build"
version = "0.1.0"
[[bin]]
link-stdlib = false
name = "fixture-no-build"
sources = ["src/main.cpp"]
[[bin]]
link-stdlib = false
name = "fixture-no-build-extra"
sources = ["src/extra.cpp"]
)"_str },
        { "src/main.cpp"_str, "int main() { return 0; }\n"_str },
        { "src/extra.cpp"_str, "int main() { return 0; }\n"_str },
    };
    auto project = materialize("install-no-build"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto fixture = project->root.clone();
    auto output  = build_root("install-no-build"_str);
    auto prefix  = install_root("install-no-build"_str);

    auto build = build_request(fixture.as_path(),
                               output.as_path(),
                               strings("fixture-no-build"_str),
                               build_profile("release"_str));
    auto built = lito::build(build);
    if (built.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(built.unwrap_err()));
        FAIL();
        return;
    }

    auto source =
        lito::resolve_install_source(lito::InstallSourceRequirement::LocalProject(fixture.clone()));
    ASSERT_TRUE(source.is_ok());
    auto request = lito::InstallRequest {
        .source = rstd::move(source).unwrap(),
        .build  = build_request(fixture.as_path(),
                                output.as_path(),
                                strings("fixture-no-build"_str),
                                build_profile("release"_str)),
        .destination =
            lito::InstallDestination::Prefix(lito::InstallPrefix { .path = prefix.clone() }),
        .build_mode = lito::InstallBuildMode::ReuseCompleted,
    };
    auto missing = fixture.join(PathBuf::from("missing-tool"_str).as_path());
    request.build.configuration.toolchain.cc  = missing.clone();
    request.build.configuration.toolchain.cxx = missing.clone();
    request.build.configuration.toolchain.ld  = missing.clone();
    request.build.configuration.toolchain.ar  = rstd::move(missing);
    request.binaries.push(String::make("fixture-no-build"_str));
    auto installed = lito::install(rstd::move(request));
    if (installed.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(installed.unwrap_err()));
        FAIL();
        return;
    }
    ASSERT_TRUE(installed->build.is_Reused());
    EXPECT_EQ(installed->build.profile(), "release"_str);
    auto executable = prefix.join(PathBuf::from("bin/fixture-no-build"_str).as_path());
    EXPECT_TRUE(rstd::fs::exists(executable.as_path()).unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(prefix.join(PathBuf::from(".lito"_str).as_path()).as_path()).unwrap());

    auto product_state = output.join(PathBuf::from(".lito/build-product.json"_str).as_path());
    auto product_json  = rstd::fs::read_to_string(product_state.as_path());
    ASSERT_TRUE(product_json.is_ok());
    EXPECT_TRUE(product_json->as_str().contains("\"schema\": 2"_str));
    EXPECT_TRUE(product_json->as_str().contains("\"install-files\""_str));
    EXPECT_TRUE(product_json->as_str().contains("\"modified-seconds\""_str));
    EXPECT_FALSE(product_json->as_str().contains("\"sha256\""_str));
    EXPECT_FALSE(product_json->as_str().contains("\"selected-packages\""_str));
    EXPECT_FALSE(product_json->as_str().contains("\"requested-packages\""_str));

    ASSERT_TRUE(
        rstd::fs::write(
            product_state.as_path(),
            R"({"schema":1,"state":"complete","generation":"old","product":{}})"_str.as_bytes())
            .is_ok());
    auto unsupported =
        lito::load_completed_build_product(fixture.as_path(), output.as_path(), "release"_str);
    ASSERT_TRUE(unsupported.is_err());
    EXPECT_TRUE(
        error_chain_text(unsupported.unwrap_err()).as_str().contains("unsupported schema"_str));
    ASSERT_TRUE(
        rstd::fs::write(product_state.as_path(), product_json->as_str().as_bytes()).is_ok());

    ASSERT_TRUE(
        rstd::fs::write(
            product_state.as_path(),
            R"({"schema":2,"schema":2,"state":"building","generation":"duplicate"})"_str.as_bytes())
            .is_ok());
    auto duplicate =
        lito::load_completed_build_product(fixture.as_path(), output.as_path(), "release"_str);
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_TRUE(duplicate.unwrap_err().is_Json());
    ASSERT_TRUE(
        rstd::fs::write(product_state.as_path(), product_json->as_str().as_bytes()).is_ok());

    ASSERT_FALSE(built->product.artifacts.is_empty());
    const lito::BuiltArtifact* selected_artifact   = nullptr;
    const lito::BuiltArtifact* unselected_artifact = nullptr;
    for (const auto& candidate : built->product.artifacts) {
        if (candidate.target.name == "fixture-no-build"_str) {
            selected_artifact = rstd::addressof(candidate);
        } else if (candidate.target.name == "fixture-no-build-extra"_str) {
            unselected_artifact = rstd::addressof(candidate);
        }
    }
    ASSERT_NE(selected_artifact, nullptr);
    ASSERT_NE(unselected_artifact, nullptr);
    auto artifact          = selected_artifact->path.clone();
    auto original_contents = rstd::fs::read(artifact.as_path());
    ASSERT_TRUE(original_contents.is_ok());
    auto original_metadata = rstd::fs::metadata(artifact.as_path());
    ASSERT_TRUE(original_metadata.is_ok());
    auto original_modified = original_metadata->modified();
    ASSERT_TRUE(original_modified.is_ok());

    const auto reuse_request = [&](ref<rstd::path::Path> destination) {
        auto resolved = lito::resolve_install_source(
            lito::InstallSourceRequirement::LocalProject(fixture.clone()));
        auto reused = lito::InstallRequest {
            .source      = rstd::move(resolved).unwrap(),
            .build       = build_request(fixture.as_path(),
                                         output.as_path(),
                                         strings("fixture-no-build"_str),
                                         build_profile("release"_str)),
            .destination = lito::InstallDestination::Prefix(
                lito::InstallPrefix { .path = PathBuf::from(destination) }),
            .build_mode = lito::InstallBuildMode::ReuseCompleted,
        };
        auto unavailable = fixture.join(PathBuf::from("missing-tool"_str).as_path());
        reused.build.configuration.toolchain.cc  = unavailable.clone();
        reused.build.configuration.toolchain.cxx = unavailable.clone();
        reused.build.configuration.toolchain.ld  = unavailable.clone();
        reused.build.configuration.toolchain.ar  = rstd::move(unavailable);
        reused.binaries.push(String::make("fixture-no-build"_str));
        return reused;
    };

    ASSERT_TRUE(rstd::fs::write(artifact.as_path(), "tampered"_str.as_bytes()).is_ok());
    auto loaded =
        lito::load_completed_build_product(fixture.as_path(), output.as_path(), "release"_str);
    ASSERT_TRUE(loaded.is_ok());
    auto size_changed =
        lito::install(reuse_request(install_root("install-no-build-size"_str).as_path()));
    ASSERT_TRUE(size_changed.is_err());
    EXPECT_TRUE(error_chain_text(size_changed.unwrap_err()).as_str().contains("size changed"_str));

    ASSERT_TRUE(rstd::fs::write(artifact.as_path(), original_contents->as_slice()).is_ok());
    auto artifact_file =
        rstd::fs::OpenOptions::make().read(true).write(true).open(artifact.as_path());
    ASSERT_TRUE(artifact_file.is_ok());
    ASSERT_TRUE(
        artifact_file->set_modified(*original_modified + rstd::time::Duration::from_secs(u64(1)))
            .is_ok());
    auto time_changed =
        lito::install(reuse_request(install_root("install-no-build-time"_str).as_path()));
    ASSERT_TRUE(time_changed.is_err());
    EXPECT_TRUE(
        error_chain_text(time_changed.unwrap_err()).as_str().contains("modified time changed"_str));

    auto replacement = original_contents->clone();
    ASSERT_FALSE(replacement.is_empty());
    auto first_byte       = replacement.as_slice()[usize {}];
    replacement[usize {}] = u8(first_byte.to_primitive() ^ 1U);
    ASSERT_TRUE(rstd::fs::write(artifact.as_path(), replacement.as_slice()).is_ok());
    ASSERT_TRUE(artifact_file->set_modified(*original_modified).is_ok());
    ASSERT_TRUE(rstd::fs::write(fixture.join(PathBuf::from("src/main.cpp"_str).as_path()).as_path(),
                                "int main() { return 0; } // package-only change\n"_str.as_bytes())
                    .is_ok());
    auto manifest          = fixture.join(PathBuf::from("lito.toml"_str).as_path());
    auto manifest_contents = rstd::fs::read_to_string(manifest.as_path());
    ASSERT_TRUE(manifest_contents.is_ok());
    manifest_contents->push_str("\n# package-only change\n"_str);
    ASSERT_TRUE(
        rstd::fs::write(manifest.as_path(), manifest_contents->as_str().as_bytes()).is_ok());
    auto lock = fixture.join(PathBuf::from("lito.lock"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(lock.as_path(),
                                R"toml(version = 1
[[packages]]
name = "fixture-no-build"
version = "0.1.0"
# package-only change
)toml"_str.as_bytes())
                    .is_ok());
    auto preserved =
        lito::install(reuse_request(install_root("install-no-build-preserved"_str).as_path()));
    if (preserved.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(preserved.unwrap_err()));
        FAIL();
        return;
    }
    ASSERT_TRUE(
        rstd::fs::write(unselected_artifact->path.as_path(), "unselected changed"_str.as_bytes())
            .is_ok());
    auto unselected =
        lito::install(reuse_request(install_root("install-no-build-unselected"_str).as_path()));
    if (unselected.is_err()) {
        rstd::io::eprintln("{}", error_chain_text(unselected.unwrap_err()));
        FAIL();
        return;
    }

    auto first =
        lito::begin_build_product_publication(fixture.as_path(), output.as_path(), "release"_str);
    ASSERT_TRUE(first.is_ok());
    auto second =
        lito::begin_build_product_publication(fixture.as_path(), output.as_path(), "release"_str);
    ASSERT_TRUE(second.is_ok());
    auto stale = lito::complete_build_product_publication(*first, built->product);
    ASSERT_TRUE(stale.is_err());
    auto incomplete =
        lito::load_completed_build_product(fixture.as_path(), output.as_path(), "release"_str);
    ASSERT_TRUE(incomplete.is_err());
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
                lito::package::PackageTargetId {
                    .package = String::make(package_name),
                    .kind    = lito::package::PackageTargetKind::Binary,
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
