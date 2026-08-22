#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

class BuildCommand : public ProjectFixture {
protected:
    auto verify_standard_library_module(lito::config::StandardLibrary family,
                                        ref<str>                      name,
                                        ref<str> logical_name = "std"_str) -> void;
};

struct CMakeOverrideEvents {
    usize fetch {};
    usize source_operations {};
    usize query_operations {};
};

void capture_cmake_override_events(void* context, const lito::BuildEvent& event) noexcept {
    auto& events = *static_cast<CMakeOverrideEvents*>(context);
    if (event.kind == lito::BuildEventKind::Fetch) ++events.fetch;
    if (event.kind == lito::BuildEventKind::CMakeConfigure ||
        event.kind == lito::BuildEventKind::CMakeBuild ||
        event.kind == lito::BuildEventKind::CMakeInstall) {
        ++events.source_operations;
    }
    if (event.kind == lito::BuildEventKind::CMakeQuery ||
        event.kind == lito::BuildEventKind::CMakeQueryBuild ||
        event.kind == lito::BuildEventKind::CMakeSnapshot) {
        ++events.query_operations;
    }
}

auto build_command_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"build([workspace]
name = "build-command"
members = ["test-lib", "test-app"]

[workspace.package]
version = "0.1.0"

[profile]
exceptions = false
rtti = false
)build"_str },
        { "test-lib/lito.toml"_str, R"build([package]
name = "fixture-test-lib"
version = { workspace = true }

[lib]
name = "fixture-test-lib"
module = "fixture.test.lib"
archive = "fixture_test_lib"
sources = ["src/lib.cppm"]
)build"_str },
        { "test-lib/src/lib.cppm"_str, R"build(export module fixture.test.lib;

export namespace fixture::test
{

constexpr auto answer() noexcept -> int {
    return 42;
}

} // namespace fixture::test
)build"_str },
        { "test-app/lito.toml"_str, R"build([package]
name = "fixture-test-app"
version = { workspace = true }

[[bin]]
link-stdlib = false
name = "fixture-test-app"
sources = ["src/main.cpp"]
)build"_str },
        { "test-app/src/main.cpp"_str, R"build(int main() {
    return 0;
}
)build"_str },
    };
    return source_tree(files);
}

auto BuildCommand::verify_standard_library_module(lito::config::StandardLibrary family,
                                                  ref<str>                      name,
                                                  ref<str> logical_name) -> void {
    auto              source  = rstd::format(R"cpp(import {};

int main() {{
    std::vector<int> values {{ 1, 2, 3 }};
    return values.size() == 3 ? 0 : 1;
}}
)cpp",
                                             logical_name);
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-standard-library-module"
version = "0.1.0"
standard = "c++23"

[[bin]]
name = "fixture-standard-library-module"
sources = ["src/main.cpp"]
)toml"_str },
        { "src/main.cpp"_str, source.as_str() },
    };
    auto project = materialize(name, files);
    ASSERT_TRUE(project.is_ok());
    auto request = project_build_request(
        name, project->root.as_path(), strings("fixture-standard-library-module"_str));
    request.configuration.standard_library = lito::config::standard_library_selection(family);
    auto result                            = lito::build(request);
    if (result.is_err()) {
        auto message = error_chain_text(result.unwrap_err());
        if (message.as_str().contains("has no module manifest"_str)) {
            GTEST_SKIP() << message.as_str();
        }
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    ASSERT_EQ(result->product.artifacts.len(), usize(1));
    auto status =
        rstd::process::Command::make(result->product.artifacts[usize {}].path.as_path().as_os_str())
            .status();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status->success());
    auto repeated = lito::build(request);
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_EQ(repeated->compiled, usize {});
    EXPECT_TRUE(repeated->reused >= usize(2));
}

TEST_F(BuildCommand, BuildsImportStdWithLibcxx) {
    verify_standard_library_module(lito::config::StandardLibrary::Libcxx,
                                   "standard-library-module-libcxx"_str);
}

TEST_F(BuildCommand, BuildsImportStdWithLibstdcxx) {
    verify_standard_library_module(lito::config::StandardLibrary::Libstdcxx,
                                   "standard-library-module-libstdcxx"_str);
}

TEST_F(BuildCommand, BuildsImportStdCompatClosureWithLibstdcxx) {
    verify_standard_library_module(lito::config::StandardLibrary::Libstdcxx,
                                   "standard-library-module-compat-libstdcxx"_str,
                                   "std.compat"_str);
}

TEST_F(BuildCommand, RejectsImportStdBeforeCxx23) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-standard-library-module-cxx20"
version = "0.1.0"

[[bin]]
name = "fixture-standard-library-module-cxx20"
sources = ["src/main.cpp"]
)toml"_str },
        { "src/main.cpp"_str, "import std;\nint main() { return 0; }\n"_str },
    };
    auto project = materialize("standard-library-module-cxx20"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = project_build_request("standard-library-module-cxx20"_str,
                                         project->root.as_path(),
                                         strings("fixture-standard-library-module-cxx20"_str));
    auto result  = lito::build(request);
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_StandardLibrary());
    auto message = error_chain_text(error);
    EXPECT_TRUE(message.as_str().contains("imports standard library module 'std'"_str));
    EXPECT_TRUE(message.as_str().contains("C++23 or later"_str));
}

TEST_F(BuildCommand, BuildSelectsProductionArtifacts) {
    auto tree = build_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("build"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root    = project->root.clone();
    auto output  = build_root("build"_str);
    auto request = build_request(
        root.as_path(), output.as_path(), strings("fixture-test-lib"_str, "fixture-test-app"_str));
    auto summary = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::TestExecutable), usize {});
    EXPECT_FALSE(summary->documentation_units.is_empty());
    for (const auto& unit : summary->documentation_units) {
        EXPECT_FALSE(unit.invocation.arguments.is_empty());
        EXPECT_FALSE(unit.invocation.identity.is_empty());
        auto selected = false;
        for (const auto& target : summary->product.selected_targets) {
            if (target == unit.target) selected = true;
        }
        EXPECT_TRUE(selected);
    }
}

TEST_F(BuildCommand, InstallLinkVariantReusesObjectsAndHasAnIndependentReceipt) {
#if RSTD_OS_WINDOWS
    GTEST_SKIP() << "ELF runpath link variants are not available for PE/COFF targets";
#endif
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-install-link"
version = "1.0.0"

[[bin]]
name = "fixture-install-link"
link-stdlib = false
sources = ["main.cpp"]

[usage]
linker-options = ["-Wl,-rpath,/tmp/lito-build-only"]
)toml"_str },
        { "main.cpp"_str, "int main() { return 0; }\n"_str },
    };
    auto project = materialize("install-link-variant"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output = build_root("install-link-variant"_str);
    auto target = lito::package::PackageTargetId {
        .package = String::make("fixture-install-link"_str),
        .kind    = lito::package::PackageTargetKind::Binary,
        .name    = String::make("fixture-install-link"_str),
    };

    auto normal_request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-install-link"_str));
    normal_request.exact_targets.push(target.clone());
    auto normal = lito::build(rstd::move(normal_request));
    ASSERT_TRUE(normal.is_ok());
    ASSERT_EQ(normal->product.artifacts.len(), usize(1));
    ASSERT_EQ(normal->product.selected_targets.len(), usize(1));
    EXPECT_TRUE(normal->product.artifacts[usize {}].install_link.is_none());
    auto selected_target = normal->product.selected_targets[usize {}].clone();

    auto origin = lito::artifact::make_origin_relative_runtime_path(PathBuf::from("."_str));
    ASSERT_TRUE(origin.is_ok());
    auto paths = Vec<lito::artifact::OriginRelativeRuntimePath>::make();
    paths.push(rstd::move(origin).unwrap());
    auto runpath = lito::artifact::make_elf_runpath(rstd::move(paths));
    ASSERT_TRUE(runpath.is_ok());
    auto install_request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-install-link"_str));
    install_request.exact_targets.push(selected_target.clone());
    install_request.artifact_link_variants.push(lito::RequestedArtifactLinkVariant {
        .target = rstd::move(selected_target),
        .policy =
            lito::InstallArtifactLinkPolicy {
                .runtime_search = rstd::move(runpath).unwrap(),
                .identity       = String::make("fixture-install-link-v1"_str),
            },
    });
    auto installed = lito::build(rstd::move(install_request));
    ASSERT_TRUE(installed.is_ok());
    ASSERT_EQ(installed->product.artifacts.len(), usize(1));
    ASSERT_TRUE(installed->product.artifacts[usize {}].install_link.is_some());
    EXPECT_EQ(installed->product.artifacts[usize {}].install_link->identity.as_str(),
              "fixture-install-link-v1"_str);
    EXPECT_NE(installed->product.artifacts[usize {}].path.as_path(),
              normal->product.artifacts[usize {}].path.as_path());
    EXPECT_EQ(installed->compiled, usize {});
    EXPECT_TRUE(installed->reused > usize {});
}

TEST_F(BuildCommand, CArchiveFeedsCppExecutableWithoutEnteringModuleResolution) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "c-cpp-build"
members = ["c-lib", "app"]

[workspace.package]
version = "0.1.0"
)toml"_str },
        { "c-lib/lito.toml"_str, R"toml([package]
name = "fixture-c-lib"
version.workspace = true
standard = "c17"

[lib]
name = "fixture-c-lib"
archive = "fixture_c_lib"
sources = ["src/value.c"]

[usage]
options = ["-fno-builtin"]
)toml"_str },
        { "c-lib/src/value.c"_str, R"c(#ifndef LITO_C_GLOBAL
#error LITO_C_GLOBAL must be provided by the C option domain
#endif
#ifdef LITO_CPP_GLOBAL
#error C++ options must not enter C compilation
#endif

int fixture_c_value(void) { return 42; }
)c"_str },
        { "app/lito.toml"_str, R"toml([package]
name = "fixture-cpp-consumer"
version.workspace = true

[[bin]]
name = "fixture-cpp-consumer"
link-stdlib = false
sources = ["src/main.cpp"]

[dependencies.fixture-c-lib]
path = "../c-lib"
visibility = "private"
)toml"_str },
        { "app/src/main.cpp"_str, R"cpp(#ifndef LITO_CPP_GLOBAL
#error LITO_CPP_GLOBAL must be provided by the C++ option domain
#endif
#ifdef LITO_C_GLOBAL
#error C options must not enter C++ compilation
#endif

extern "C" int fixture_c_value(void);

int main() {
    return fixture_c_value() == 42 ? 0 : 1;
}
)cpp"_str },
    };
    auto project = materialize("c-cpp-build"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = project_build_request(
        "c-cpp-build"_str, project->root.as_path(), strings("fixture-cpp-consumer"_str));
    request.configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-DLITO_CPP_GLOBAL=1"_str),
        .source    = String::make("CXXFLAGS"_str),
    });
    request.configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("-DLITO_C_GLOBAL=1"_str),
        .source    = String::make("CFLAGS"_str),
    });
    request.configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-Wl,--as-needed"_str),
        .source    = String::make("LDFLAGS"_str),
    });
    auto result = lito::build(request);
    if (result.is_err()) {
        auto message = error_chain_text(result.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::Executable), usize(1));

    auto conflicting = project_build_request(
        "c-cpp-target-conflict"_str, project->root.as_path(), strings("fixture-cpp-consumer"_str));
    conflicting.configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("--target=wasm32-unknown-unknown"_str),
        .source    = String::make("CFLAGS"_str),
    });
    auto rejected = lito::build(conflicting);
    ASSERT_TRUE(rejected.is_err());
    auto message = error_chain_text(rejected.unwrap_err());
    EXPECT_TRUE(message.as_str().contains("CFLAGS"_str));
    EXPECT_TRUE(message.as_str().contains("C++-owned effective target"_str));
}

TEST_F(BuildCommand, DependencyPackageBuildScriptGeneratesSourceBeforeDiscovery) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "generated-source-workspace"
members = ["app"]

[workspace.package]
version = "0.1.0"

[workspace.dependencies.fixture-generated-lib]
path = "generated"
)toml"_str },
        { "generated/lito.toml"_str, R"toml([package]
name = "fixture-generated-lib"
version = "0.1.0"

[lib]
name = "fixture-generated-lib"
module = "fixture.generated"
archive = "fixture_generated"
sources = ["src/lib.cppm"]
source-groups = ["generated"]

[source-groups.generated]
root = "generated"
sources = ["src/generated.cpp"]
)toml"_str },
        { "generated/build.lua"_str, R"lua(lito.configure_file({
  input = "src/generated.cpp.in",
  output = "src/generated.cpp",
  values = {}
})
)lua"_str },
        { "generated/src/lib.cppm"_str, R"cpp(export module fixture.generated;

export auto generated_answer() noexcept -> int;
)cpp"_str },
        { "generated/src/generated.cpp.in"_str, R"cpp(module fixture.generated;

auto generated_answer() noexcept -> int {
    return 42;
}
)cpp"_str },
        { "app/lito.toml"_str, R"toml([package]
name = "fixture-generated-app"
version = { workspace = true }

[[bin]]
link-stdlib = false
name = "fixture-generated-app"
sources = ["src/main.cpp"]

[dependencies.fixture-generated-lib]
workspace = true
visibility = "private"
)toml"_str },
        { "app/src/main.cpp"_str, R"cpp(import fixture.generated;

auto main() -> int {
    return generated_answer() == 42 ? 0 : 1;
}
)cpp"_str },
    };
    auto project = materialize("package-build-script-generated-source"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("package-build-script-generated-source"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-generated-app"_str));
    auto summary = lito::build(request);
    if (summary.is_err()) {
        auto message = error_chain_text(summary.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_TRUE(summary->script.executed);
    ASSERT_EQ(summary->script.executions.len(), usize(1));
    EXPECT_EQ(summary->script.executions[usize {}].owner.as_str(),
              "package-fixture-generated-lib"_str);
    auto generated = output.join(
        PathBuf::from("generated/fixture-generated-lib/src/generated.cpp"_str).as_path());
    EXPECT_TRUE(rstd::fs::exists(generated.as_path()).unwrap_or(false));
    EXPECT_EQ(summary->compiled, usize(3));
}

TEST_F(BuildCommand, BuildScriptGeneratedActionsPublishDependencyOrderedSources) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-action-lib"
version = "0.1.0"

[lib]
name = "fixture-action-lib"
module = "fixture.action"
archive = "fixture_action"
sources = ["src/lib.cppm"]
)toml"_str },
        { "build.lua"_str, R"lua(local target = lito.target({
  kind = "lib",
  name = "fixture-action-lib",
})
local written = lito.write({
  output = "intermediate/generated.cpp",
  content = [[module fixture.action;

auto generated_answer() noexcept -> int {
  return 42;
}
]],
})
local copied = lito.copy({
  input = written.output,
  output = "src/generated.cpp",
})
lito.target_add_generated_source(target, copied.output)
)lua"_str },
        { "src/lib.cppm"_str, R"cpp(export module fixture.action;

export auto generated_answer() noexcept -> int;
)cpp"_str },
    };
    auto project = materialize("build-script-action-dag"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output = build_root("build-script-action-dag"_str);
    auto request =
        build_request(project->root.as_path(), output.as_path(), strings("fixture-action-lib"_str));
    auto first = lito::build(request);
    if (first.is_err()) {
        auto message = error_chain_text(first.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(first->compiled, usize(2));
    auto generated =
        output.join(PathBuf::from("generated/fixture-action-lib/src/generated.cpp"_str).as_path());
    EXPECT_TRUE(rstd::fs::exists(generated.as_path()).unwrap_or(false));

    auto second = lito::build(request);
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(second->compiled, usize {});
    EXPECT_EQ(second->frontend.persistent_scan_hits, usize(2));
}

TEST_F(BuildCommand, BuildScriptLoadsRequiredScriptPackageAndSourceRootFiles) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-script-host"
version = "0.1.0"

[lib]
name = "fixture-script-host"
module = "fixture.script.host"
archive = "fixture-script-host"
sources = ["lib.cppm"]

[dependencies.fixture-script-helper]
path = "tools/helper"
)toml"_str },
        { "lib.cppm"_str, "export module fixture.script.host;\n"_str },
        { "build.lua"_str, R"lua(local host = require("@lito")
assert(host == lito)
local helper = require("@fixture.script.helper")
local own = require("scripts/value.lua")
assert(helper.answer == 42)
assert(own == "consumer")
)lua"_str },
        { "scripts/value.lua"_str, "return \"consumer\"\n"_str },
        { "tools/helper/lito.toml"_str, R"toml([package]
name = "fixture-script-helper"
version = "0.1.0"

[script]
supports = ["build"]
)toml"_str },
        { "tools/helper/lib.lua"_str, R"lua(local value = require("internal/value.lua")
return { answer = value }
)lua"_str },
        { "tools/helper/internal/value.lua"_str, "return 42\n"_str },
    };
    auto project = materialize("required-script-package"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("required-script-package"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-script-host"_str));
    auto summary = lito::build(request);
    if (summary.is_err()) {
        auto message = error_chain_text(summary.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_TRUE(summary->script.executed);
    EXPECT_EQ(summary->compiled, usize(1));
}

TEST_F(BuildCommand, CMakeInstalledOverrideBuildsFromSearchPathAndPreservesLockSource) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-cmake-installed-override"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-cmake-installed-override"
sources = ["main.cpp"]

[external-sources.fixture]
git = "https://example.invalid/lito-override-fixture.git"
commit = "0123456789abcdef0123456789abcdef01234567"

[external-dependencies.cmake.fixture]
package = "LitoOverrideFixture"
source = "fixture"
targets = [{ name = "LitoOverrideFixture::fixture", visibility = "private" }]
)toml"_str },
        { "main.cpp"_str, R"cpp(#ifndef LITO_SYSTEM_OVERRIDE_FIXTURE
#error expected system CMake package usage
#endif

auto main() -> int {
    return 0;
}
)cpp"_str },
        { "system/lib/cmake/LitoOverrideFixture/LitoOverrideFixtureConfig.cmake"_str,
          R"cmake(set(LitoOverrideFixture_VERSION "1.0.0")
add_library(LitoOverrideFixture::fixture INTERFACE IMPORTED)
set_property(TARGET LitoOverrideFixture::fixture PROPERTY
  INTERFACE_COMPILE_DEFINITIONS LITO_SYSTEM_OVERRIDE_FIXTURE=1)
)cmake"_str },
    };
    auto project = materialize("cmake-installed-override-build"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("cmake-installed-override-build"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-cmake-installed-override"_str));
    request.cmake = fixture_cmake();
    request.cmake.search_paths.push(project->root.join(PathBuf::from("system"_str).as_path()));
    request.cmake_build_overrides.entries.push(lito::dependency::CMakeBuildOverride {
        .package = String::make("LitoOverrideFixture"_str),
    });
    auto events      = CMakeOverrideEvents {};
    request.observer = Some(lito::BuildEventSink {
        .context = rstd::addressof(events),
        .notify  = capture_cmake_override_events,
    });

    auto summary = lito::build(request);
    if (summary.is_err()) {
        auto message = error_chain_text(summary.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(events.fetch, usize {});
    EXPECT_EQ(events.source_operations, usize {});
    EXPECT_TRUE(events.query_operations > usize {});
    EXPECT_EQ(summary->compiled, usize(1));

    auto lock = rstd::fs::read_to_string(
        project->root.join(PathBuf::from("lito.lock"_str).as_path()).as_path());
    ASSERT_TRUE(lock.is_ok());
    EXPECT_TRUE(lock->as_str().contains("https://example.invalid/lito-override-fixture.git"_str));
    EXPECT_TRUE(lock->as_str().contains("0123456789abcdef0123456789abcdef01234567"_str));
    EXPECT_FALSE(lock->as_str().contains("kind = \"installed\""_str));

    auto locked_contents    = lock->clone();
    request.locked          = true;
    request.sources.network = lito::source::NetworkPolicy::Offline;
    events                  = CMakeOverrideEvents {};
    auto locked             = lito::build(request);
    ASSERT_TRUE(locked.is_ok());
    EXPECT_EQ(events.fetch, usize {});
    EXPECT_EQ(events.source_operations, usize {});
    auto unchanged = rstd::fs::read_to_string(
        project->root.join(PathBuf::from("lito.lock"_str).as_path()).as_path());
    ASSERT_TRUE(unchanged.is_ok());
    EXPECT_EQ(unchanged->as_str(), locked_contents.as_str());

    request.build_directory = build_root("cmake-installed-override-missing"_str);
    request.cmake.search_paths.clear();
    events       = CMakeOverrideEvents {};
    auto missing = lito::build(request);
    ASSERT_TRUE(missing.is_err());
    auto error = error_chain_text(missing.unwrap_err());
    EXPECT_TRUE(error.as_str().contains(
        "tools.cmake.overrides.LitoOverrideFixture.source = 'installed'"_str));
    EXPECT_EQ(events.fetch, usize {});
    EXPECT_EQ(events.source_operations, usize {});
}

TEST_F(BuildCommand, DocumentationSelectsOnlyLibraryArtifacts) {
    auto tree = build_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("build-doc"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root    = project->root.clone();
    auto output  = build_root("build-doc"_str);
    auto request = build_request(root.as_path(), output.as_path(), strings("fixture-test-lib"_str));
    request.purpose = lito::package::PackageSelectionPurpose::Documentation;
    auto summary    = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::cpp::ArtifactKind::Executable), usize {});
    ASSERT_FALSE(summary->documentation_units.is_empty());
    for (const auto& unit : summary->documentation_units) {
        EXPECT_EQ(unit.target.kind, lito::package::PackageTargetKind::Library);
        ASSERT_TRUE(unit.root_module.is_some());
        EXPECT_EQ(unit.root_module->as_str(), "fixture.test.lib"_str);
    }
}

TEST_F(BuildCommand, FeatureChangesInvalidateDiscoveryAndCompileCaches) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-feature-build"
version = "0.1.0"

[lib]
name = "fixture-feature-build"
module = "fixture.feature"
archive = "fixture_feature"

[features.optional]
default = false

[[when]]
condition = "feature.optional"

[when.usage]
private-definitions = ["FIXTURE_FEATURE_CONDITION=1"]
)toml"_str },
        { "src/lib.cppm"_str, R"cpp(export module fixture.feature;

#if LITO_FEAT_OPTIONAL
export import :optional;
#endif
)cpp"_str },
        { "src/optional.cppm"_str, R"cpp(module;

#if LITO_FEAT_OPTIONAL
#ifndef FIXTURE_FEATURE_CONDITION
#error feature condition did not contribute to the scan context
#endif

export module fixture.feature:optional;
#endif
)cpp"_str },
    };
    auto project = materialize("feature-build"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("feature-build"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-feature-build"_str));

    auto disabled = lito::build(request);
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_EQ(disabled->compiled, usize(1));

    request.selection.features.enabled.push(String::make("optional"_str));
    auto enabled = lito::build(request);
    if (enabled.is_err()) {
        auto message = error_chain_text(enabled.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(enabled->compiled, usize(2));
    EXPECT_EQ(enabled->scanned, usize(2));

    request.selection.features.enabled.clear();
    auto disabled_again = lito::build(request);
    if (disabled_again.is_err()) {
        auto message = error_chain_text(disabled_again.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(disabled_again->compiled, usize(1));
}

TEST_F(BuildCommand, ConditionalConflictsReportBothSources) {
    const ProjectFile definition_files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-condition-definition-conflict"
version = "0.1.0"

[lib]
name = "fixture-condition-definition-conflict"
module = "fixture.condition.definition_conflict"
archive = "fixture_condition_definition_conflict"

[[when]]
condition = "true"

[when.usage]
private-definitions = ["FIXTURE_CONFLICT=1"]

[[when]]
condition = "target.os == host.os"

[when.usage]
private-definitions = ["FIXTURE_CONFLICT=2"]
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.condition.definition_conflict;\n"_str },
    };
    auto definition_project = materialize("condition-definition-conflict"_str, definition_files);
    ASSERT_TRUE(definition_project.is_ok());
    auto definition_request =
        project_build_request("condition-definition-conflict"_str,
                              definition_project->root.as_path(),
                              strings("fixture-condition-definition-conflict"_str));
    auto definition_result = lito::build(definition_request);
    ASSERT_TRUE(definition_result.is_err());
    auto definition_error = error_chain_text(definition_result.unwrap_err());
    EXPECT_TRUE(definition_error.as_str().contains("condition 'true'"_str));
    EXPECT_TRUE(definition_error.as_str().contains("condition 'target.os == host.os'"_str));

    const ProjectFile scalar_files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-condition-scalar-conflict"
version = "0.1.0"

[lib]
name = "fixture-condition-scalar-conflict"
module = "fixture.condition.scalar_conflict"
archive = "fixture_condition_scalar_conflict"

[[when]]
condition = "true"

[when.usage]
threads = true

[[when]]
condition = "target.os == host.os"

[when.usage]
threads = false
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.condition.scalar_conflict;\n"_str },
    };
    auto scalar_project = materialize("condition-scalar-conflict"_str, scalar_files);
    ASSERT_TRUE(scalar_project.is_ok());
    auto scalar_request = project_build_request("condition-scalar-conflict"_str,
                                                scalar_project->root.as_path(),
                                                strings("fixture-condition-scalar-conflict"_str));
    auto scalar_result  = lito::build(scalar_request);
    ASSERT_TRUE(scalar_result.is_err());
    auto scalar_error = error_chain_text(scalar_result.unwrap_err());
    EXPECT_TRUE(scalar_error.as_str().contains("condition 'true'"_str));
    EXPECT_TRUE(scalar_error.as_str().contains("condition 'target.os == host.os'"_str));
}

TEST_F(BuildCommand, PackageFeatureMacrosRemainOwnedByTheirPackage) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "fixture-dependency-features"
members = ["provider", "consumer"]

[workspace.package]
version = "0.1.0"

)toml"_str },
        { "provider/lito.toml"_str, R"toml([package]
name = "fixture-feature-provider"
version.workspace = true

[lib]
name = "fixture-feature-provider"
module = "fixture.feature.provider"
archive = "fixture_feature_provider"

[features.api]
default = false
)toml"_str },
        { "provider/src/lib.cppm"_str, "export module fixture.feature.provider;\n"_str },
        { "provider/src/api.cppm"_str, R"cpp(export module fixture.feature.provider.api;

export constexpr auto fixture_feature_value() -> int {
#ifdef LITO_FEAT_API
    return LITO_FEAT_API;
#else
    return 0;
#endif
}
)cpp"_str },
        { "consumer/lito.toml"_str, R"toml([package]
name = "fixture-feature-consumer"
version.workspace = true

[[bin]]
name = "fixture-feature-consumer"
link-stdlib = false
sources = ["src/main.cpp"]

[features.api]
default = false

[dependencies.fixture-feature-provider]
path = "../provider"
visibility = "private"
features = ["api"]
default-features = false
)toml"_str },
        { "consumer/src/main.cpp"_str, R"cpp(import fixture.feature.provider.api;

#ifdef LITO_FEAT_API
#error dependency feature macro escaped its package
#endif

auto main() -> int {
    return fixture_feature_value() == 1 ? 0 : 1;
}
)cpp"_str },
    };
    auto project = materialize("dependency-features"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = project_build_request("dependency-features"_str,
                                         project->root.as_path(),
                                         strings("fixture-feature-consumer"_str));
    auto result  = lito::build(request);
    if (result.is_err()) {
        auto message = error_chain_text(result.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::Executable), usize(1));
}

TEST_F(BuildCommand, VisibilityRemainsTargetLocalAcrossModulesAndStaticLinks) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "fixture-visibility"
members = ["hidden-lib", "default-app", "default-lib", "hidden-app"]

[workspace.package]
version = "0.1.0"

[profile.visibility-lto]
inherits = "release"
lto = "thin"
)toml"_str },
        { "hidden-lib/lito.toml"_str, R"toml([package]
name = "fixture-hidden-lib"
version.workspace = true

[lib]
name = "fixture-hidden-lib"
module = "fixture.visibility.hidden"
archive = "fixture_hidden_lib"

[usage]
options = ["-fvisibility=hidden"]
)toml"_str },
        { "hidden-lib/src/lib.cppm"_str, R"cpp(export module fixture.visibility.hidden;

export extern "C" auto fixture_hidden_symbol() -> int {
    return 21;
}

export extern "C" __attribute__((visibility("default")))
auto fixture_explicit_public_symbol() -> int {
    return 21;
}
)cpp"_str },
        { "default-app/lito.toml"_str, R"toml([package]
name = "fixture-default-app"
version.workspace = true

[[bin]]
name = "fixture-default-app"
link-stdlib = false
sources = ["src/main.cpp"]

[usage]
options = ["-fvisibility=default"]

[dependencies.fixture-hidden-lib]
path = "../hidden-lib"
visibility = "private"
)toml"_str },
        { "default-app/src/main.cpp"_str, R"cpp(import fixture.visibility.hidden;

auto main() -> int {
    return fixture_hidden_symbol() + fixture_explicit_public_symbol() == 42 ? 0 : 1;
}
)cpp"_str },
        { "default-lib/lito.toml"_str, R"toml([package]
name = "fixture-default-lib"
version.workspace = true

[lib]
name = "fixture-default-lib"
module = "fixture.visibility.public_"
archive = "fixture_default_lib"

[usage]
options = ["-fvisibility=default"]
)toml"_str },
        { "default-lib/src/lib.cppm"_str, R"cpp(export module fixture.visibility.public_;

export extern "C" auto fixture_default_symbol() -> int {
    return 42;
}
)cpp"_str },
        { "hidden-app/lito.toml"_str, R"toml([package]
name = "fixture-hidden-app"
version.workspace = true

[[bin]]
name = "fixture-hidden-app"
link-stdlib = false
sources = ["src/main.cpp"]

[dependencies.fixture-default-lib]
path = "../default-lib"
visibility = "private"
)toml"_str },
        { "hidden-app/src/main.cpp"_str, R"cpp(import fixture.visibility.public_;

auto main() -> int {
    return fixture_default_symbol() == 42 ? 0 : 1;
}
)cpp"_str },
    };
    auto project = materialize("visibility"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request =
        project_build_request("visibility"_str,
                              project->root.as_path(),
                              strings("fixture-default-app"_str, "fixture-hidden-app"_str));
    auto result = lito::build(request);
    if (result.is_err()) {
        auto message = error_chain_text(result.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::StaticLibrary), usize(2));
    EXPECT_EQ(artifact_count(*result, lito::cpp::ArtifactKind::Executable), usize(2));

#if RSTD_OS_LINUX
    for (const auto& artifact : result->product.artifacts) {
        if (artifact.kind != lito::cpp::ArtifactKind::StaticLibrary) continue;
        auto command = rstd::process::Command::make("/nix/opt/llvm/22/bin/llvm-readelf"_str);
        command.arg("--symbols"_str).arg(artifact.path.as_path().as_os_str());
        auto output = command.output();
        ASSERT_TRUE(output.is_ok());
        ASSERT_TRUE(output->status.success());
        auto text = String::from_utf8(rstd::move(output->stdout_buf));
        ASSERT_TRUE(text.is_ok());
        if (artifact.target.package.as_str() == "fixture-hidden-lib"_str) {
            EXPECT_TRUE(text->as_str().contains("HIDDEN"_str));
            EXPECT_TRUE(text->as_str().contains("fixture_hidden_symbol"_str));
            EXPECT_TRUE(text->as_str().contains("DEFAULT"_str));
            EXPECT_TRUE(text->as_str().contains("fixture_explicit_public_symbol"_str));
        } else if (artifact.target.package.as_str() == "fixture-default-lib"_str) {
            EXPECT_TRUE(text->as_str().contains("DEFAULT"_str));
            EXPECT_TRUE(text->as_str().contains("fixture_default_symbol"_str));
        }
    }
#endif

    auto hidden_manifest = project->root.join(PathBuf::from("hidden-lib/lito.toml"_str).as_path());
    auto changed         = rstd::fs::write(hidden_manifest.as_path(),
                                           R"toml([package]
name = "fixture-hidden-lib"
version.workspace = true

[lib]
name = "fixture-hidden-lib"
module = "fixture.visibility.hidden"
archive = "fixture_hidden_lib"

[usage]
options = ["-fvisibility=default"]
)toml"_str.as_bytes());
    ASSERT_TRUE(changed.is_ok());
    auto rebuilt = lito::build(request);
    ASSERT_TRUE(rebuilt.is_ok());
    EXPECT_EQ(rebuilt->frontend.persistent_scan_hits, usize(4));
    EXPECT_EQ(rebuilt->frontend.persistent_scan_misses, usize {});
    EXPECT_EQ(rebuilt->frontend.analyze_builds, usize {});
    EXPECT_TRUE(rebuilt->compiled > usize {});

    auto lto_request =
        project_build_request("visibility-lto"_str,
                              project->root.as_path(),
                              strings("fixture-default-app"_str, "fixture-hidden-app"_str),
                              build_profile("visibility-lto"_str));
    auto lto = lito::build(lto_request);
    ASSERT_TRUE(lto.is_ok());
    EXPECT_EQ(artifact_count(*lto, lito::cpp::ArtifactKind::StaticLibrary), usize(2));
    EXPECT_EQ(artifact_count(*lto, lito::cpp::ArtifactKind::Executable), usize(2));
}
