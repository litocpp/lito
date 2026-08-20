#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;
import lito.tools;
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

class SystemProcess : public ProjectFixture {};

struct HostToolCapture {
    Vec<lito::tools::HostToolCapability> capabilities;
    Vec<String>                          providers;
};

TEST_F(SystemProcess, ResolvedEnvironmentCanRemoveInheritedVariables) {
    EnvironmentVariableGuard inherited("LITO_TEST_REMOVED_VARIABLE"_str, "present"_str);
    auto                     environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto env_tool = resolver.resolve(PathBuf::from("env"_str).as_path(), "env"_str);
    ASSERT_TRUE(env_tool.is_ok());
    auto scrubbed  = environment->without_variable("LITO_TEST_REMOVED_VARIABLE"_str);
    auto arguments = strings(env_tool->executable.as_path().to_str().unwrap());
    auto output    = lito::system::run_command(arguments, scrubbed);
    ASSERT_TRUE(output.is_ok());
    EXPECT_FALSE(output->standard_output.as_str().contains("LITO_TEST_REMOVED_VARIABLE="_str));
}

void capture_host_tool_resolution(void*                                  raw_context,
                                  const lito::tools::HostToolResolution& resolution) noexcept {
    if (resolution.kind != lito::tools::HostToolResolution::Kind::Selected) return;
    auto& capture = *static_cast<HostToolCapture*>(raw_context);
    capture.capabilities.push(lito::tools::HostToolCapability(resolution.requirement.capability));
    capture.providers.push(resolution.provider.clone());
}

TEST_F(SystemProcess, WindowsCommandFragmentsPreservePathSeparators) {
    auto parsed = tokenize_windows_command_fragments(
        "fixture-source\\lito_source_adapter.lib \"directory with spaces\\fixture.lib\""_str,
        "test Windows command fragment"_str);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(parsed->len(), usize(2));
    EXPECT_EQ((*parsed)[usize {}].as_str(), "fixture-source\\lito_source_adapter.lib"_str);
    EXPECT_EQ((*parsed)[usize(1)].as_str(), "directory with spaces\\fixture.lib"_str);
}

TEST_F(SystemProcess, ToolResolverUsesOneOrderedEffectivePathSnapshot) {
    auto directory = source_root("tool-resolver"_str);
    auto inherited = directory.join(rstd::path::PathBuf::from("inherited"_str).as_path());
    auto first     = directory.join(rstd::path::PathBuf::from("first"_str).as_path());
    auto second    = directory.join(rstd::path::PathBuf::from("second"_str).as_path());
    auto relative  = directory.join(rstd::path::PathBuf::from("relative"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(inherited.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(first.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(second.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(relative.as_path()).is_ok());

#if RSTD_OS_WINDOWS
    constexpr auto tool_name     = "lito-tool.EXE"_str;
    constexpr auto fallback_name = "lito-fallback.EXE"_str;
    constexpr auto absolute_name = "lito-absolute.EXE"_str;
#else
    constexpr auto tool_name     = "lito-tool"_str;
    constexpr auto fallback_name = "lito-fallback"_str;
    constexpr auto absolute_name = "lito-absolute"_str;
#endif
    auto inherited_tool  = inherited.join(rstd::path::PathBuf::from(tool_name).as_path());
    auto appended_tool   = first.join(rstd::path::PathBuf::from(tool_name).as_path());
    auto first_fallback  = first.join(rstd::path::PathBuf::from(fallback_name).as_path());
    auto second_fallback = second.join(rstd::path::PathBuf::from(fallback_name).as_path());
    auto absolute_tool   = second.join(rstd::path::PathBuf::from(absolute_name).as_path());
    auto non_executable =
        first.join(rstd::path::PathBuf::from("lito-non-executable"_str).as_path());
    ASSERT_TRUE(write_executable(inherited_tool.as_path()));
    ASSERT_TRUE(write_executable(appended_tool.as_path()));
    ASSERT_TRUE(write_executable(first_fallback.as_path()));
    ASSERT_TRUE(write_executable(second_fallback.as_path()));
    ASSERT_TRUE(write_executable(absolute_tool.as_path()));
#if RSTD_OS_WINDOWS
    auto extension_tool =
        second.join(rstd::path::PathBuf::from("lito-extension.EXE"_str).as_path());
    ASSERT_TRUE(write_executable(extension_tool.as_path()));
#endif
    ASSERT_TRUE(rstd::fs::write(non_executable.as_path(), "fixture\n"_str.as_bytes()).is_ok());
#if RSTD_OS_UNIX
    ASSERT_TRUE(rstd::fs::set_permissions(non_executable.as_path(),
                                          rstd::fs::Permissions::from_mode(u32(0644)))
                    .is_ok());
#endif

    auto inherited_entries = Vec<rstd::path::PathBuf>::make();
    inherited_entries.push(inherited.clone());
    inherited_entries.push(rstd::path::PathBuf::make());
    inherited_entries.push(rstd::path::PathBuf::from("relative"_str));
    auto inherited_path = rstd::env::join_paths(inherited_entries.as_slice());
    ASSERT_TRUE(inherited_path.is_ok());
    auto append = Vec<rstd::path::PathBuf>::make();
    append.push(first.clone());
    append.push(second.clone());
    auto environment = lito::system::ResolvedProcessEnvironment::resolve(
        lito::system::ProcessEnvironmentSpec { .append_path = rstd::move(append) },
        Some(inherited_path->as_os_str()),
        directory.as_path());
    ASSERT_TRUE(environment.is_ok());
    ASSERT_EQ(environment->search_directories().len(), usize(5));
    EXPECT_EQ(environment->search_directories()[usize {}].as_path(), inherited.as_path());
    EXPECT_EQ(environment->search_directories()[usize(1)].as_path(), directory.as_path());
    EXPECT_EQ(environment->search_directories()[usize(2)].as_path(), relative.as_path());
    EXPECT_EQ(environment->search_directories()[usize(3)].as_path(), first.as_path());
    EXPECT_EQ(environment->search_directories()[usize(4)].as_path(), second.as_path());

    auto child_entries =
        rstd::env::split_paths(environment->child_path()).collect<Vec<rstd::path::PathBuf>>();
    ASSERT_EQ(child_entries.len(), environment->search_directories().len());
    for (usize index {}; index < child_entries.len(); ++index) {
        EXPECT_EQ(child_entries[index].as_path(),
                  environment->search_directories()[index].as_path());
    }

    auto resolver = lito::tools::ToolResolver(*environment);
    auto selected = resolver.resolve(rstd::path::PathBuf::from("lito-tool"_str).as_path(),
                                     "test executable"_str);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ(selected->executable.as_path(), inherited_tool.as_path());

    auto fallback = resolver.resolve(rstd::path::PathBuf::from("lito-fallback"_str).as_path(),
                                     "test executable"_str);
    ASSERT_TRUE(fallback.is_ok());
    EXPECT_EQ(fallback->executable.as_path(), first_fallback.as_path());

    auto absolute = resolver.resolve(absolute_tool.as_path(), "test executable"_str);
    ASSERT_TRUE(absolute.is_ok());
    EXPECT_EQ(absolute->executable.as_path(), absolute_tool.as_path());
    EXPECT_TRUE(absolute->executable.as_path().is_absolute());
#if RSTD_OS_WINDOWS
    auto extension = resolver.resolve(rstd::path::PathBuf::from("lito-extension"_str).as_path(),
                                      "test executable"_str);
    ASSERT_TRUE(extension.is_ok());
    EXPECT_EQ(extension->executable.as_path(), extension_tool.as_path());
#endif

    EXPECT_TRUE(
        resolver
            .resolve(rstd::path::PathBuf::from("nested/tool"_str).as_path(), "test executable"_str)
            .is_err());
    EXPECT_TRUE(resolver
                    .resolve(rstd::path::PathBuf::from("lito-non-executable"_str).as_path(),
                             "test executable"_str)
                    .is_err());
    auto missing = resolver.resolve(rstd::path::PathBuf::from("lito-missing"_str).as_path(),
                                    "test executable"_str);
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    ASSERT_TRUE(missing_error.is_Message());
    EXPECT_TRUE(missing_error.as_Message().message.as_str().contains("lito-missing"_str));
    EXPECT_TRUE(
        missing_error.as_Message().message.as_str().contains(first.as_path().to_str().unwrap()));

#if RSTD_OS_WINDOWS
    auto late_tool = first.join(rstd::path::PathBuf::from("lito-missing.EXE"_str).as_path());
#else
    auto late_tool = first.join(rstd::path::PathBuf::from("lito-missing"_str).as_path());
#endif
    ASSERT_TRUE(write_executable(late_tool.as_path()));
    EXPECT_TRUE(
        resolver
            .probe(rstd::path::PathBuf::from("lito-missing"_str).as_path(), "test executable"_str)
            .unwrap()
            .is_none());

    auto capture            = HostToolCapture {};
    auto tool_spec          = lito::tools::ToolSpec {};
    tool_spec.clang_format  = rstd::path::PathBuf::from("lito-fallback"_str);
    auto reporting_resolver = lito::tools::ToolResolver(*environment,
                                                        rstd::move(tool_spec),
                                                        Some(lito::tools::HostToolResolutionSink {
                                                            .context = rstd::addressof(capture),
                                                            .notify  = capture_host_tool_resolution,
                                                        }));
    auto requirement        = lito::tools::command_tool_requirement(
        lito::tools::HostToolCapability::SourceFormatting, "lito format"_str);
    EXPECT_TRUE(reporting_resolver.require(lito::tools::Tool::ClangFormat, requirement).is_ok());
    EXPECT_TRUE(reporting_resolver.require(lito::tools::Tool::ClangFormat, requirement).is_ok());
    ASSERT_EQ(capture.capabilities.len(), usize(1));
    EXPECT_EQ(capture.capabilities[usize {}], lito::tools::HostToolCapability::SourceFormatting);
    EXPECT_EQ(capture.providers[usize {}].as_str(), "clang-format"_str);

    ASSERT_TRUE(rstd::fs::remove_file(inherited_tool.as_path()).is_ok());
    auto cached = resolver.resolve(rstd::path::PathBuf::from("lito-tool"_str).as_path(),
                                   "test executable"_str);
    ASSERT_TRUE(cached.is_ok());
    EXPECT_EQ(cached->executable.as_path(), inherited_tool.as_path());
}

TEST_F(SystemProcess, ProcessExecutionRejectsUnresolvedToolNames) {
    auto environment = lito::system::ResolvedProcessEnvironment::resolve(
        lito::system::ProcessEnvironmentSpec {}, None(), temp_root());
    ASSERT_TRUE(environment.is_ok());
    auto arguments = strings("lito-unresolved-tool"_str);
    EXPECT_TRUE(lito::system::run_command(arguments, *environment).is_err());
    EXPECT_TRUE(lito::system::run_command_with_input(arguments, ""_str, *environment).is_err());
}

TEST_F(SystemProcess, BuildUsesConfiguredAppendedToolPath) {
    auto tree = environment_tool_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto materialized = materialize("environment-build"_str, *tree);
    ASSERT_TRUE(materialized.is_ok());
    ASSERT_TRUE(prepare_environment_tool_project(materialized->root.as_path()));
    auto project = materialized->root.join(PathBuf::from("append-path"_str).as_path());
    auto config  = lito::config::load_project_config(project.as_path());
    ASSERT_TRUE(config.is_ok());
    auto output  = source_root("environment-append-path"_str);
    auto request = build_request(
        project.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str));
    request.environment             = rstd::move(config->environment);
    request.tools                   = rstd::move(config->tools);
    request.configuration.toolchain = rstd::move(config->toolchain);
    auto built                      = lito::build(request);
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(artifact_count(*built, lito::cpp::ArtifactKind::Executable), usize(1));
}

TEST_F(SystemProcess, BuildProducesPrimarySharedLibraryArtifact) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-primary-shared"
version = "0.1.0"

[lib]
name = "fixture-primary-shared"
kind = "shared"
module = "fixture.primary.shared"
artifact = "fixture_primary_shared"
sources = ["src/library.cppm"]
)toml"_str },
        { "src/library.cppm"_str,
          "export module fixture.primary.shared;\n"
          "export auto fixture_primary_shared() -> int { return 42; }\n"_str },
    };
    auto project = materialize("primary-shared"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = build_request(project->root.as_path(),
                                 build_root("primary-shared"_str).as_path(),
                                 Vec<String>::make(),
                                 build_profile("release"_str));
    auto built   = lito::build(request);
    if (built.is_err()) {
        auto message = error_chain_text(built.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(artifact_count(*built, lito::cpp::ArtifactKind::SharedLibrary), usize(1));
    for (const auto& artifact : built->artifacts) {
        if (artifact.kind != lito::cpp::ArtifactKind::SharedLibrary) continue;
        EXPECT_EQ(artifact.path.as_path().file_name().unwrap().to_str().unwrap(),
                  "libfixture_primary_shared.so"_str);
        EXPECT_TRUE(rstd::fs::exists(artifact.path.as_path()).unwrap());
    }
}

TEST_F(SystemProcess, TestArtifactReceivesConfiguredEffectivePath) {
    auto tree = environment_tool_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto materialized = materialize("environment-test"_str, *tree);
    ASSERT_TRUE(materialized.is_ok());
    ASSERT_TRUE(prepare_environment_tool_project(materialized->root.as_path()));
    auto project = materialized->root.join(PathBuf::from("test-path"_str).as_path());
    auto config  = lito::config::load_project_config(project.as_path());
    ASSERT_TRUE(config.is_ok());
    auto output  = source_root("environment-test-path"_str);
    auto request = build_request(
        project.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str));
    request.environment             = rstd::move(config->environment);
    request.tools                   = rstd::move(config->tools);
    request.configuration.toolchain = rstd::move(config->toolchain);
    auto tested                     = lito::test(lito::TestRequest {
        .build = rstd::move(request),
    });
    ASSERT_TRUE(tested.is_ok());
    EXPECT_TRUE(tested->success());
    ASSERT_EQ(tested->executions.len(), usize(1));
    EXPECT_TRUE(tested->executions[usize {}].success());
}
