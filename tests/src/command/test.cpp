#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.tools;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

class TestCommand : public ProjectFixture {};

auto test_command_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"test([workspace]
name = "test-command"
members = ["test-lib", "test-attach-lib", "test-attach", "test-pass", "test-fail", "test-signal", "test-app", "compile-lib", "compile-pass", "compile-mismatch", "windows-only"]

[workspace.package]
version = "0.1.0"

[profile]
exceptions = false
rtti = false
)test"_str },
        { "test-attach-lib/lito.toml"_str, R"test([package]
name = "fixture-test-attach-lib"
version = { workspace = true }

[lib]
name = "fixture-test-attach-lib"
module = "fixture.test.attach"
archive = "fixture_test_attach"
sources = ["src/lib.cppm", "src/internal.cppm"]

[usage]
private-definitions = ["FIXTURE_ATTACH_PRIVATE=17"]
)test"_str },
        { "test-attach-lib/src/internal.cppm"_str,
          R"test(export module fixture.test.attach:internal;

namespace fixture::attach::internal
{

inline auto registrations = 0;

inline auto register_test() noexcept -> void {
    ++registrations;
}

} // namespace fixture::attach::internal
)test"_str },
        { "test-attach-lib/src/lib.cppm"_str, R"test(export module fixture.test.attach;

import :internal;

export namespace fixture::attach
{

auto registrations() noexcept -> int {
    return internal::registrations;
}

} // namespace fixture::attach
)test"_str },
        { "test-attach/lito.toml"_str, R"test([package]
name = "fixture-test-attach"
version = { workspace = true }

[[test]]
link-stdlib = false
name = "fixture-test-attach"
sources = ["src/main.cpp"]

[[test.attach]]
package = "fixture-test-attach-lib"
sources = ["src/registration.cppm"]

[dependencies.fixture-test-attach-lib]
path = "../test-attach-lib"
visibility = "private"
)test"_str },
        { "test-attach/src/main.cpp"_str, R"test(import fixture.test.attach;

auto main() -> int {
    return fixture::attach::registrations() == 1 ? 0 : 1;
}
)test"_str },
        { "test-attach/src/registration.cppm"_str,
          R"test(export module fixture.test.attach:registration;

import :internal;

static_assert(FIXTURE_ATTACH_PRIVATE == 17);

namespace
{

struct Registrar {
    Registrar() noexcept { fixture::attach::internal::register_test(); }
};

Registrar registrar;

} // namespace
)test"_str },
        { "test-pass/lito.toml"_str, R"test([package]
name = "fixture-test-pass"
version = { workspace = true }

[[test]]
link-stdlib = false
name = "fixture-test-pass"
sources = ["src/main.cpp"]

[dependencies.fixture-test-lib]
path = "../test-lib"
visibility = "private"
)test"_str },
        { "test-pass/marker.txt"_str, R"test(package working directory marker
)test"_str },
        { "test-pass/src/main.cpp"_str, R"test(#include <cstdio>
#include <cstring>

import fixture.test.lib;

int main(int argc, char** argv) {
    std::puts("fixture pass executed");
    if (fixture::test::answer() != 42) return 1;
    if (argc != 2 || std::strcmp(argv[1], "expected-argument") != 0) return 2;
    auto* marker = std::fopen("marker.txt", "r");
    if (marker == nullptr) return 3;
    std::fclose(marker);
    return 0;
}
)test"_str },
        { "test-fail/lito.toml"_str, R"test([package]
name = "fixture-test-fail"
version = { workspace = true }

[[test]]
link-stdlib = false
name = "fixture-test-fail"
sources = ["src/main.cpp"]

[dependencies.fixture-test-lib]
path = "../test-lib"
visibility = "private"
)test"_str },
        { "test-fail/src/main.cpp"_str, R"test(#include <cstdio>

import fixture.test.lib;

int main() {
    std::puts("fixture fail executed");
    return fixture::test::answer() == 42 ? 7 : 0;
}
)test"_str },
        { "test-signal/lito.toml"_str, R"test([package]
name = "fixture-test-signal"
version = { workspace = true }

[[test]]
link-stdlib = false
name = "fixture-test-signal"
sources = ["src/main.cpp"]
)test"_str },
        { "test-signal/src/main.cpp"_str, R"test(#include <csignal>

int main() {
    std::raise(SIGTERM);
    return 0;
}
)test"_str },
        { "test-app/lito.toml"_str, R"test([package]
name = "fixture-test-app"
version = { workspace = true }

[[bin]]
link-stdlib = false
name = "fixture-test-app"
sources = ["src/main.cpp"]
)test"_str },
        { "test-app/src/main.cpp"_str, R"test(int main() {
    return 0;
}
)test"_str },
        { "compile-pass/lito.toml"_str, R"test([package]
name = "fixture-compile-pass"
version = { workspace = true }

[compile-test]

[[compile-test.cases]]
name = "Compile.Success"
source = "src/success.cpp"
outcome = "success"

[[compile-test.cases]]
name = "Compile.ExpectedFailure"
source = "src/failure.cpp"
outcome = "failure"
diagnostic-contains = ["fixture expected compile failure"]
diagnostic-contains-any = ["static assertion failed", "static_assert"]

[[compile-test.cases]]
name = "Compile.CaseOption"
source = "src/option.cpp"
outcome = "success"
options = ["-DFIXTURE_COMPILE_OPTION=1"]

[dependencies.fixture-compile-lib]
path = "../compile-lib"
visibility = "private"
)test"_str },
        { "compile-pass/src/failure.cpp"_str, R"test(import fixture.compile.lib;

static_assert(false, "fixture expected compile failure");
)test"_str },
        { "compile-pass/src/option.cpp"_str, R"test(#if FIXTURE_COMPILE_OPTION != 1
#error compile-test case option was not applied
#endif
)test"_str },
        { "compile-pass/src/success.cpp"_str, R"test(import fixture.compile.lib;

static_assert(fixture_compile_value() == 42);
)test"_str },
        { "compile-mismatch/lito.toml"_str, R"test([package]
name = "fixture-compile-mismatch"
version = { workspace = true }

[compile-test]

[[compile-test.cases]]
name = "Compile.DiagnosticMismatch"
source = "src/failure.cpp"
outcome = "failure"
diagnostic-contains = ["diagnostic that is intentionally absent"]
)test"_str },
        { "compile-mismatch/src/failure.cpp"_str,
          R"test(static_assert(false, "actual fixture diagnostic");
)test"_str },
        { "windows-only/lito.toml"_str, R"test([package]
name = "fixture-windows-only"
version = { workspace = true }
target = { family = "windows" }

[lib]
name = "fixture-windows-only"
module = "fixture.windows.only"
archive = "fixture_windows_only"
sources = ["src/lib.cppm"]
)test"_str },
        { "windows-only/src/lib.cppm"_str, R"test(export module fixture.windows.only;
)test"_str },
        { "test-lib/lito.toml"_str, R"test([package]
name = "fixture-test-lib"
version = { workspace = true }

[lib]
name = "fixture-test-lib"
module = "fixture.test.lib"
archive = "fixture_test_lib"
sources = ["src/lib.cppm"]
)test"_str },
        { "test-lib/src/lib.cppm"_str, R"test(export module fixture.test.lib;

export namespace fixture::test
{

constexpr auto answer() noexcept -> int {
    return 42;
}

} // namespace fixture::test
)test"_str },
        { "compile-lib/lito.toml"_str, R"test([package]
name = "fixture-compile-lib"
version = { workspace = true }

[lib]
name = "fixture-compile-lib"
module = "fixture.compile.lib"
archive = "fixture_compile_lib"
sources = ["src/lib.cppm", "src/unix.cpp", "src/windows.cpp"]
)test"_str },
        { "compile-lib/src/lib.cppm"_str, R"test(export module fixture.compile.lib;

export constexpr auto fixture_compile_value() -> int { return 42; }
)test"_str },
        { "compile-lib/src/unix.cpp"_str, R"test(module fixture.compile.lib;

static_assert(fixture_compile_value() == 42);
)test"_str },
        { "compile-lib/src/windows.cpp"_str, "module fixture.compile.lib;\n"_str },
    };
    return source_tree(files);
}

TEST_F(TestCommand, TestAttachmentKeepsProductionArtifactsIsolated) {
    auto tree = test_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("test-attachment"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root   = project->root.clone();
    auto output = build_root("test-attachment"_str);

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-test-attach-lib"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::TestAttachmentArchive),
              usize {});
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
    EXPECT_EQ(artifact_count(tested->build, lito::cpp::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(tested->build, lito::cpp::ArtifactKind::TestAttachmentArchive),
              usize(1));
    EXPECT_EQ(artifact_count(tested->build, lito::cpp::ArtifactKind::TestExecutable), usize(1));
    EXPECT_TRUE(
        rstd::fs::exists(
            attachment_directory.join(PathBuf::from("libfixture_test_attach.test.a"_str).as_path())
                .as_path())
            .unwrap());
}

TEST_F(TestCommand, TestRunsPassFailureSignalAndNoRun) {
    auto tree = test_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("test-command"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root   = project->root.clone();
    auto output = build_root("test-command"_str);

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
    EXPECT_EQ(no_run->build.product.profile.as_str(), "release"_str);
    EXPECT_TRUE(no_run->executions.is_empty());
    EXPECT_EQ(artifact_count(no_run->build, lito::cpp::ArtifactKind::TestExecutable), usize(3));

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
}

TEST_F(TestCommand, CargoStaticlibProvidesACAbiToCppTestsAndReusesFreshOutput) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-cargo-ffi"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-cargo-ffi"
sources = ["test/main.cpp"]

[external-sources.rust]
path = "rust"

[external-dependencies.cargo.ffi]
source = "rust"
package = "lito-cargo-ffi-fixture"
features = ["ffi"]
default-features = false
visibility = "private"

[usage]
private-include-directories = [{ path = "include", external-source = "rust" }]
)toml"_str },
        { "test/main.cpp"_str, R"cpp(#include <lito_cargo_ffi.h>

auto main() -> int {
    if (lito_cargo_add(20, 22) != 42) return 1;
    if (lito_cargo_checked_divide(84, 2) != 42) return 2;
    if (lito_cargo_checked_divide(1, 0) != -1) return 3;
    return 0;
}
)cpp"_str },
        { "rust/Cargo.toml"_str, R"toml([package]
name = "lito-cargo-ffi-fixture"
version = "0.1.0"
edition = "2024"

[lib]
name = "lito_cargo_ffi_fixture"
crate-type = ["staticlib"]

[features]
default = ["unused-default"]
unused-default = []
ffi = []
)toml"_str },
        { "rust/Cargo.lock"_str, R"lock(# This file is automatically @generated by Cargo.
# It is not intended for manual editing.
version = 4

[[package]]
name = "lito-cargo-ffi-fixture"
version = "0.1.0"
)lock"_str },
        { "rust/include/lito_cargo_ffi.h"_str, R"h(#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t lito_cargo_add(int32_t left, int32_t right);
int32_t lito_cargo_checked_divide(int32_t value, int32_t divisor);

#ifdef __cplusplus
}
#endif
)h"_str },
        { "rust/src/lib.rs"_str, R"rust(#[unsafe(no_mangle)]
pub extern "C" fn lito_cargo_add(left: i32, right: i32) -> i32 {
    left + right
}

#[unsafe(no_mangle)]
pub extern "C" fn lito_cargo_checked_divide(value: i32, divisor: i32) -> i32 {
    if divisor == 0 { -1 } else { value / divisor }
}
)rust"_str },
    };
    auto project = materialize("cargo-ffi"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto       output       = build_root("cargo-ffi"_str);
    const auto make_request = [&] {
        return lito::TestRequest {
            .build = build_request(project->root.as_path(),
                                   output.as_path(),
                                   strings("fixture-cargo-ffi"_str),
                                   build_profile("release"_str)),
        };
    };
    auto tested = lito::test(make_request());
    if (tested.is_err()) {
        auto message = error_chain_text(tested.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_TRUE(tested->success());
    ASSERT_EQ(tested->executions.len(), usize(1));

    auto repeated = lito::test(make_request());
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_TRUE(repeated->success());
    EXPECT_EQ(repeated->build.compiled, usize {});
    EXPECT_TRUE(repeated->build.reused >= usize(1));
}

TEST_F(TestCommand, InactiveCargoDependencyDoesNotResolveToolOrSource) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-cargo-inactive"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-cargo-inactive"
sources = ["test/main.cpp"]

[external-sources.rust]
path = "missing-rust-source"

[external-dependencies.cargo.ffi]
source = "rust"
package = "missing-cargo-package"
visibility = "private"
condition = "false"
)toml"_str },
        { "test/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "missing-rust-source/marker.txt"_str, "inactive external source\n"_str },
    };
    auto project = materialize("cargo-inactive"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request        = build_request(project->root.as_path(),
                                        build_root("cargo-inactive"_str).as_path(),
                                        strings("fixture-cargo-inactive"_str));
    request.tools.cargo = PathBuf::from("missing-lito-cargo-provider"_str);
    request.tools.mark_configured(lito::tools::Tool::Cargo);
    auto tested = lito::test(lito::TestRequest { .build = rstd::move(request) });
    if (tested.is_err()) {
        auto message = error_chain_text(tested.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_TRUE(tested->success());
}

TEST_F(TestCommand, CompileTestsReportOutcomesAndReuse) {
    auto tree = test_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("compile-test"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root   = project->root.clone();
    auto output = build_root("compile-test"_str);

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
#if defined(_WIN32)
    EXPECT_TRUE(unsupported.is_ok());
#else
    EXPECT_TRUE(unsupported.is_err());
#endif
}

TEST_F(TestCommand, InvalidArtifactsAndDependenciesAreRejected) {
    const ProjectFile artifact_files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-invalid-test-artifact"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-invalid-test-artifact"
sources = ["main.cpp"]

[[bin]]
link-stdlib = false
name = "fixture-invalid-test-artifact"
sources = ["main.cpp"]
)toml"_str,
        },
    };
    auto artifact = materialize("invalid-artifact"_str, artifact_files);
    ASSERT_TRUE(artifact.is_ok());
    auto multiple = lito::manifest::load_manifest_document(artifact->root.as_path());
    EXPECT_TRUE(multiple.is_err());

    const ProjectFile dependency_files[] = {
        {
            "lito.toml"_str,
            R"toml([workspace]
name = "fixture-invalid-dependency"
members = ["app", "test-dependency"]

[workspace.package]
version = "0.1.0"
)toml"_str,
        },
        {
            "app/lito.toml"_str,
            R"toml([package]
name = "fixture-invalid-test-app"
version.workspace = true

[[bin]]
link-stdlib = false
name = "fixture-invalid-test-app"
sources = ["main.cpp"]

[dependencies.fixture-test-dependency]
path = "../test-dependency"
visibility = "private"
)toml"_str,
        },
        { "app/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        {
            "test-dependency/lito.toml"_str,
            R"toml([package]
name = "fixture-test-dependency"
version.workspace = true

[[test]]
link-stdlib = false
name = "fixture-test-dependency"
sources = ["main.cpp"]
)toml"_str,
        },
        { "test-dependency/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
    };
    auto invalid_project = materialize("invalid-dependency"_str, dependency_files);
    ASSERT_TRUE(invalid_project.is_ok());
    auto output = build_root("invalid-dependency"_str);
    auto request =
        build_request(invalid_project->root.as_path(), output.as_path(), Vec<String>::make());
    request.locked  = false;
    auto dependency = lito::build(request);
    EXPECT_TRUE(dependency.is_err());
}

TEST(TestCommandRunner, AndroidArtifactsRequireAnExplicitRunner) {
    auto platform = lito::system::BuildPlatform {};
    platform.effective_target =
        lito::system::parse_target_info("aarch64-linux-android21"_str).unwrap();
    platform.android_abi = Some(String::make("arm64-v8a"_str));

    auto rejected = lito::ensure_artifact_runner(platform, "test"_str);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_TRUE(rstd::format("{}", rejected.unwrap_err())
                    .as_str()
                    .contains("without a configured target runner"_str));

    platform.android_abi = None();
    EXPECT_TRUE(lito::ensure_artifact_runner(platform, "test"_str).is_ok());
}
