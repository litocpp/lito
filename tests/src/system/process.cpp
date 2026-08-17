#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class SystemProcess : public ProjectFixture {};

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

    auto inherited_tool  = inherited.join(rstd::path::PathBuf::from("lito-tool"_str).as_path());
    auto appended_tool   = first.join(rstd::path::PathBuf::from("lito-tool"_str).as_path());
    auto first_fallback  = first.join(rstd::path::PathBuf::from("lito-fallback"_str).as_path());
    auto second_fallback = second.join(rstd::path::PathBuf::from("lito-fallback"_str).as_path());
    auto absolute_tool   = second.join(rstd::path::PathBuf::from("lito-absolute"_str).as_path());
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

    auto resolver = lito::system::ToolResolver(*environment);
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
    ASSERT_TRUE(missing_error.is_Environment());
    EXPECT_TRUE(missing_error.as_Environment().message.as_str().contains("lito-missing"_str));
    EXPECT_TRUE(missing_error.as_Environment().message.as_str().contains(
        first.as_path().to_str().unwrap()));

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
    auto project = materialized->root.join(PathBuf::from("append-path"_str).as_path());
    auto config  = lito::config::load_project_config(project.as_path());
    ASSERT_TRUE(config.is_ok());
    auto output  = source_root("environment-append-path"_str);
    auto request = build_request(
        project.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str));
    request.environment             = rstd::move(config->environment);
    request.configuration.toolchain = rstd::move(config->toolchain);
    auto built                      = lito::build(request);
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(artifact_count(*built, lito::cpp::ArtifactKind::Executable), usize(1));
}

TEST_F(SystemProcess, TestArtifactReceivesConfiguredEffectivePath) {
    auto tree = environment_tool_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto materialized = materialize("environment-test"_str, *tree);
    ASSERT_TRUE(materialized.is_ok());
    auto project = materialized->root.join(PathBuf::from("test-path"_str).as_path());
    auto config  = lito::config::load_project_config(project.as_path());
    ASSERT_TRUE(config.is_ok());
    auto output  = source_root("environment-test-path"_str);
    auto request = build_request(
        project.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str));
    request.environment             = rstd::move(config->environment);
    request.configuration.toolchain = rstd::move(config->toolchain);
    auto tested                     = lito::test(lito::TestRequest {
        .build = rstd::move(request),
    });
    ASSERT_TRUE(tested.is_ok());
    EXPECT_TRUE(tested->success());
    ASSERT_EQ(tested->executions.len(), usize(1));
    EXPECT_TRUE(tested->executions[usize {}].success());
}
