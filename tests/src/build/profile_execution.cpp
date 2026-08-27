#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.tools.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class BuildProfileExecution : public ProjectFixture {};

TEST_F(BuildProfileExecution, BuildProfileOwnsOptimizationAndDebugDefinitions) {
    auto tree = build_profile_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("profile"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto output    = build_root("profile"_str);

    auto debug =
        lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(debug.is_ok());
    auto debug_executable = executable(*debug);
    ASSERT_TRUE(debug_executable.is_some());
    auto debug_status = rstd::process::Command::make((*debug_executable).as_os_str())
                            .current_dir(directory.as_path())
                            .status();
    ASSERT_TRUE(debug_status.is_ok());
    ASSERT_TRUE(debug_status->code().is_some());
    EXPECT_EQ(*debug_status->code(), i32(1));

    auto release = lito::build(build_request(
        directory.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str)));
    ASSERT_TRUE(release.is_ok());
    auto release_executable = executable(*release);
    ASSERT_TRUE(release_executable.is_some());
    auto release_status = rstd::process::Command::make((*release_executable).as_os_str())
                              .current_dir(directory.as_path())
                              .status();
    ASSERT_TRUE(release_status.is_ok());
    EXPECT_TRUE(release_status->success());

    auto plain_request = build_request(
        directory.as_path(), output.as_path(), Vec<String>::make(), build_profile("plain"_str));
    plain_request.configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-O2"_str, "-g"_str, "-fno-exceptions"_str, "-fno-rtti"_str),
        .source    = String::make("CXXFLAGS"_str),
    });
    auto plain_report            = CompileProgressCapture {};
    plain_request.setup_reporter = Some(lito::BuildSetupReportSink {
        .context = rstd::addressof(plain_report),
        .notify  = capture_build_setup,
    });
    auto plain                   = lito::build(plain_request);
    ASSERT_TRUE(plain.is_ok());
    EXPECT_EQ(plain_report.profile.as_str(), "plain"_str);
    auto reported_optimization = false;
    auto reported_exceptions   = false;
    auto reported_rtti         = false;
    for (const auto& value : plain_report.profile_values) {
        if (value.domain == lito::BuildOptionReportDomain::Cpp &&
            value.field.as_str() == "exceptions"_str) {
            reported_exceptions = true;
            EXPECT_EQ(value.value.as_str(), "disabled"_str);
            EXPECT_EQ(value.source.as_str(), "CXXFLAGS"_str);
        }
        if (value.domain == lito::BuildOptionReportDomain::Cpp &&
            value.field.as_str() == "RTTI"_str) {
            reported_rtti = true;
            EXPECT_EQ(value.value.as_str(), "disabled"_str);
            EXPECT_EQ(value.source.as_str(), "CXXFLAGS"_str);
        }
        if (value.domain != lito::BuildOptionReportDomain::Cpp ||
            value.field.as_str() != "optimization"_str) {
            continue;
        }
        reported_optimization = true;
        EXPECT_EQ(value.value.as_str(), "-O2"_str);
        EXPECT_EQ(value.source.as_str(), "CXXFLAGS"_str);
    }
    EXPECT_TRUE(reported_optimization);
    EXPECT_TRUE(reported_exceptions);
    EXPECT_TRUE(reported_rtti);
    auto plain_executable = executable(*plain);
    ASSERT_TRUE(plain_executable.is_some());
    auto plain_status = rstd::process::Command::make((*plain_executable).as_os_str())
                            .current_dir(directory.as_path())
                            .status();
    ASSERT_TRUE(plain_status.is_ok());
    ASSERT_TRUE(plain_status->code().is_some());
    EXPECT_EQ(*plain_status->code(), i32(1));
}

TEST_F(BuildProfileExecution, COnlyBuildReportsOnlyApplicableProfileSettings) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\n"
          "name = \"fixture-c-only-profile-report\"\n"
          "version = \"0.1.0\"\n"
          "standard = \"c17\"\n"
          "[lib]\n"
          "name = \"fixture-c-only-profile-report\"\n"
          "archive = \"fixture_c_only_profile_report\"\n"
          "sources = [\"value.c\"]\n"_str },
        { "value.c"_str, "int fixture_c_only_profile_report(void) { return 0; }\n"_str },
    };
    auto project = materialize("c-only-profile-report"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request           = build_request(project->root.as_path(),
                                           build_root("c-only-profile-report"_str).as_path(),
                                           Vec<String>::make());
    auto report            = CompileProgressCapture {};
    request.setup_reporter = Some(lito::BuildSetupReportSink {
        .context = rstd::addressof(report),
        .notify  = capture_build_setup,
    });
    auto built             = lito::build(request);
    ASSERT_TRUE(built.is_ok());

    auto reported_c_optimization = false;
    for (const auto& value : report.profile_values) {
        EXPECT_NE(value.domain, lito::BuildOptionReportDomain::Cpp);
        if (value.domain == lito::BuildOptionReportDomain::C &&
            value.field.as_str() == "optimization"_str) {
            reported_c_optimization = true;
        }
        if (value.domain == lito::BuildOptionReportDomain::Link) {
            EXPECT_NE(value.field.as_str(), "standard library"_str);
            EXPECT_NE(value.field.as_str(), "standard library runtime"_str);
        }
    }
    EXPECT_TRUE(reported_c_optimization);
}

TEST_F(BuildProfileExecution, ConfiguredGnuLdIsRejectedBeforeCompilation) {
#if RSTD_OS_UNIX
    if (! rstd::fs::exists(PathBuf::from("/usr/bin/ld"_str).as_path()).unwrap()) return;
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\n"
          "name = \"fixture-gnu-ld-lto\"\n"
          "standard = \"c17\"\n"
          "[[bin]]\n"
          "name = \"fixture-gnu-ld-lto\"\n"
          "link-stdlib = false\n"
          "sources = [\"main.c\"]\n"_str },
        { "main.c"_str, "int main(void) { return 0; }\n"_str },
    };
    auto project = materialize("gnu-ld-lto"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = build_request(
        project->root.as_path(), build_root("gnu-ld-lto"_str).as_path(), Vec<String>::make());
    request.configuration.toolchain.ld = PathBuf::from("/usr/bin/ld"_str);
    request.configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("-flto=thin"_str),
        .source    = String::make("internal SDK override"_str),
    });
    auto built = lito::build(request);
    ASSERT_TRUE(built.is_err());
    auto error = error_chain_text(built.unwrap_err());
    EXPECT_TRUE(error.as_str().contains("expected LLD"_str));
#endif
}
