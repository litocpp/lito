#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class BuildProfile : public ProjectFixture {
protected:
    auto profile_project(ref<str> name) -> lito::SourceTreeResult<lito::SourceMaterialization> {
        auto tree = build_profile_project_tree();
        if (tree.is_err()) return Err(rstd::move(tree).unwrap_err());
        return materialize(name, *tree);
    }

    auto manifest_project(ref<str> name, ref<str> contents)
        -> lito::SourceTreeResult<lito::SourceMaterialization> {
        const ProjectFile files[] = {
            { "lito.toml"_str, contents },
        };
        return materialize(name, files);
    }
};

TEST_F(BuildProfile, ProjectProfileDefaultsAndRootOwnershipAreTyped) {
    auto default_project = profile_project("defaults"_str);
    ASSERT_TRUE(default_project.is_ok());
    auto defaults = lito::resolve_package_graph(default_project->root.as_path());
    ASSERT_TRUE(defaults.is_ok());
    EXPECT_TRUE(defaults->profile.exceptions);
    EXPECT_TRUE(defaults->profile.rtti);

    auto disabled_project = manifest_project("disabled"_str, R"toml([package]
name = "fixture-profile-disabled"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-disabled"
sources = ["main.cpp"]

[profile]
exceptions = false
rtti = false
)toml"_str);
    ASSERT_TRUE(disabled_project.is_ok());
    auto disabled = lito::resolve_package_graph(disabled_project->root.as_path());
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_FALSE(disabled->profile.exceptions);
    EXPECT_FALSE(disabled->profile.rtti);

    const ProjectFile workspace_files[] = {
        {
            "lito.toml"_str,
            R"toml([workspace]
name = "fixture-workspace-profile"
members = ["app"]

[workspace.package]
version = "0.1.0"

[profile]
rtti = false
)toml"_str,
        },
        {
            "app/lito.toml"_str,
            R"toml([package]
name = "fixture-workspace-profile-app"
version.workspace = true

[[bin]]
link-stdlib = false
name = "workspace-profile-app"
sources = ["main.cpp"]
)toml"_str,
        },
    };
    auto workspace_project = materialize("workspace"_str, workspace_files);
    ASSERT_TRUE(workspace_project.is_ok());
    auto workspace = lito::resolve_package_graph(workspace_project->root.as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->profile.exceptions);
    EXPECT_FALSE(workspace->profile.rtti);

    const ProjectFile dependency_files[] = {
        {
            "root/lito.toml"_str,
            R"toml([package]
name = "fixture-profile-dependency-root"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-dependency-root"
sources = ["main.cpp"]

[dependencies.fixture-profile-dependency]
path = "../dependency"
visibility = "private"
)toml"_str,
        },
        {
            "dependency/lito.toml"_str,
            R"toml([package]
name = "fixture-profile-dependency"
version = "0.1.0"

[lib]
name = "fixture-profile-dependency"
module = "fixture.profile_dependency"
archive = "fixture-profile-dependency"
sources = ["library.cppm"]

[profile]
exceptions = false
rtti = false
)toml"_str,
        },
    };
    auto dependency_project = materialize("dependency"_str, dependency_files);
    ASSERT_TRUE(dependency_project.is_ok());
    auto dependency_root = dependency_project->root.join(PathBuf::from("root"_str).as_path());
    auto dependency      = lito::resolve_package_graph(dependency_root.as_path());
    ASSERT_TRUE(dependency.is_ok());
    EXPECT_TRUE(dependency->profile.exceptions);
    EXPECT_TRUE(dependency->profile.rtti);
}

TEST_F(BuildProfile, ProjectProfileMaterializesIdenticalLanguageSemanticsAcrossBuildModes) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto project = lito::ProjectProfile {
        .exceptions = false,
        .rtti       = false,
    };
    auto debug =
        lito::cpp::make_profile_spec(configuration(), project, build_profile("debug"_str), *parser);
    auto release = lito::cpp::make_profile_spec(
        configuration(), project, build_profile("release"_str), *parser);
    ASSERT_TRUE(debug.is_ok());
    ASSERT_TRUE(release.is_ok());
    EXPECT_FALSE(debug->cpp.language.exceptions);
    EXPECT_FALSE(debug->cpp.language.rtti);
    EXPECT_FALSE(release->cpp.language.exceptions);
    EXPECT_FALSE(release->cpp.language.rtti);
}

TEST_F(BuildProfile, PthreadBuildOptionOwnsCompileAndLinkRequirements) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.options.push(String::make("-pthread"_str));
    auto profile = lito::cpp::make_profile_spec(
        build_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(profile.is_ok());
    EXPECT_TRUE(profile->cpp.threading.posix);
    EXPECT_TRUE(profile->link_requirements.posix_threads);
    ASSERT_EQ(profile->link_requirements.thread_sources.len(), usize(1));
    EXPECT_EQ(profile->link_requirements.thread_sources[usize {}].as_str(), "build.options"_str);
}

TEST_F(BuildProfile, BuildProfilesResolveCargoStyleValuesAndInheritance) {
    auto project = profile_project("profile-values"_str);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto perf = lito::resolve_build_profile(graph->profile, build_profile("perf"_str));
    ASSERT_TRUE(perf.is_ok());
    EXPECT_EQ(perf->family, lito::BuildProfileFamily::Release);
    EXPECT_EQ(perf->optimization, lito::CppOptimization::Level2);
    EXPECT_EQ(perf->debug_info, lito::CppDebugInfo::LineTablesOnly);
    EXPECT_EQ(perf->strip, lito::StripMode::None);
    EXPECT_EQ(perf->lto, lito::CppLto::Off);
    EXPECT_TRUE(perf->ndebug);

    auto aliases = lito::resolve_build_profile(graph->profile, build_profile("aliases"_str));
    ASSERT_TRUE(aliases.is_ok());
    EXPECT_EQ(aliases->optimization, lito::CppOptimization::SizeMin);
    EXPECT_EQ(aliases->debug_info, lito::CppDebugInfo::Limited);
    EXPECT_EQ(aliases->strip, lito::StripMode::Symbols);
    EXPECT_EQ(aliases->lto, lito::CppLto::Fat);
}

TEST_F(BuildProfile, BuildProfileCatalogRejectsUnknownParentsAndCycles) {
    auto unknown = lito::ProjectProfile {};
    unknown.build_profiles.push(lito::BuildProfileDefinition {
        .name     = build_profile("custom"_str),
        .inherits = Some(build_profile("missing"_str)),
    });
    auto unknown_result = lito::validate_build_profiles(unknown);
    ASSERT_TRUE(unknown_result.is_err());
    auto unknown_error = rstd::move(unknown_result).unwrap_err();
    ASSERT_TRUE(unknown_error.is_Message());
    EXPECT_TRUE(unknown_error.as_Message().message.as_str().contains("unknown profile"_str));

    auto cycle = lito::ProjectProfile {};
    cycle.build_profiles.push(lito::BuildProfileDefinition {
        .name     = build_profile("first"_str),
        .inherits = Some(build_profile("second"_str)),
    });
    cycle.build_profiles.push(lito::BuildProfileDefinition {
        .name     = build_profile("second"_str),
        .inherits = Some(build_profile("first"_str)),
    });
    auto cycle_result = lito::validate_build_profiles(cycle);
    ASSERT_TRUE(cycle_result.is_err());
    auto cycle_error = rstd::move(cycle_result).unwrap_err();
    ASSERT_TRUE(cycle_error.is_Message());
    EXPECT_TRUE(cycle_error.as_Message().message.as_str().contains("inheritance cycle"_str));
}

TEST_F(BuildProfile, CodegenProfilesMaterializeTypedClangOptionsAndCacheIdentities) {
    EXPECT_EQ(lito::cpp::cpp_optimization_option(lito::CppOptimization::Level2), "-O2"_str);
    EXPECT_EQ(lito::cpp::cpp_optimization_option(lito::CppOptimization::Size), "-Os"_str);
    EXPECT_EQ(lito::cpp::cpp_optimization_option(lito::CppOptimization::SizeMin), "-Oz"_str);
    EXPECT_EQ(lito::cpp::cpp_debug_option(lito::CppDebugInfo::LineDirectivesOnly),
              "-gline-directives-only"_str);
    EXPECT_EQ(lito::cpp::cpp_debug_option(lito::CppDebugInfo::LineTablesOnly),
              "-gline-tables-only"_str);
    EXPECT_EQ(lito::cpp::cpp_debug_option(lito::CppDebugInfo::Limited), "-g1"_str);
    EXPECT_EQ(lito::cpp::cpp_lto_option(lito::CppLto::Off), "-fno-lto"_str);
    EXPECT_EQ(lito::cpp::cpp_lto_option(lito::CppLto::Thin), "-flto=thin"_str);
    EXPECT_EQ(lito::cpp::cpp_lto_option(lito::CppLto::Fat), "-flto=full"_str);

    auto project = profile_project("profile-codegen"_str);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto perf = lito::cpp::make_profile_spec(
        configuration(), graph->profile, build_profile("perf"_str), *parser);
    auto variant = lito::cpp::make_profile_spec(
        configuration(), graph->profile, build_profile("codegen-variant"_str), *parser);
    ASSERT_TRUE(perf.is_ok());
    ASSERT_TRUE(variant.is_ok());
    EXPECT_NE(lito::cpp::cpp_compile_identity(perf->cpp).as_str(),
              lito::cpp::cpp_compile_identity(variant->cpp).as_str());
    EXPECT_EQ(lito::cpp::cpp_scan_identity(perf->cpp).as_str(),
              lito::cpp::cpp_scan_identity(variant->cpp).as_str());
    EXPECT_EQ(lito::cpp::cpp_bmi_compatibility_identity(perf->cpp).as_str(),
              lito::cpp::cpp_bmi_compatibility_identity(variant->cpp).as_str());

    auto aliases = lito::cpp::make_profile_spec(
        configuration(), graph->profile, build_profile("aliases"_str), *parser);
    ASSERT_TRUE(aliases.is_ok());
    EXPECT_NE(lito::cpp::cpp_scan_identity(perf->cpp).as_str(),
              lito::cpp::cpp_scan_identity(aliases->cpp).as_str());
}

TEST_F(BuildProfile, RawCompilerAndLinkerOptionsCannotOverrideOwnedSettings) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());

    auto compiler_configuration = configuration();
    compiler_configuration.options.push(String::make("-flto=auto"_str));
    auto compiler = lito::cpp::make_profile_spec(
        compiler_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(compiler.is_err());
    auto compiler_error = rstd::move(compiler).unwrap_err();
    ASSERT_TRUE(compiler_error.is_Message());
    EXPECT_TRUE(compiler_error.as_Message().message.as_str().contains("selected profile"_str));

    auto linker_configuration = configuration();
    linker_configuration.linker_options.push(String::make("-Wl,--strip-debug"_str));
    auto linker = lito::cpp::make_profile_spec(
        linker_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(linker.is_err());
    auto linker_error = rstd::move(linker).unwrap_err();
    ASSERT_TRUE(linker_error.is_Message());
    EXPECT_TRUE(linker_error.as_Message().message.as_str().contains("selected profile"_str));

    auto stdlib_configuration = configuration();
    stdlib_configuration.linker_options.push(String::make("-nostdlib++"_str));
    auto stdlib = lito::cpp::make_profile_spec(
        stdlib_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(stdlib.is_err());
    auto stdlib_error = rstd::move(stdlib).unwrap_err();
    ASSERT_TRUE(stdlib_error.is_Message());
    EXPECT_TRUE(stdlib_error.as_Message().message.as_str().contains("Lito-owned setting"_str));

    auto pthread_configuration = configuration();
    pthread_configuration.linker_options.push(String::make("-pthread"_str));
    auto pthread = lito::cpp::make_profile_spec(
        pthread_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(pthread.is_err());
    EXPECT_TRUE(rstd::format("{}", pthread.unwrap_err()).as_str().contains("build.options"_str));
}

TEST_F(BuildProfile, CompilerOptionsAreValidatedAfterToolchainParsing) {
    auto project = manifest_project("owned-option"_str, R"toml([package]
name = "fixture-profile-owned_option"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-owned-option"
sources = ["main.cpp"]

[usage]
options = ["-O2"]
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto packages = strings("fixture-profile-owned_option"_str);
    auto targets  = Vec<lito::PackageTargetId>::make();
    targets.push(lito::PackageTargetId {
        .package = String::make("fixture-profile-owned_option"_str),
        .kind    = lito::PackageTargetKind::Binary,
        .name    = String::make("profile-owned-option"_str),
    });
    auto build_configuration = configuration();
    auto build_arguments     = lito::cpp::parse_build_arguments(build_configuration, *parser);
    ASSERT_TRUE(build_arguments.is_ok());
    auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                graph->profile,
                                                build_profile("debug"_str),
                                                rstd::move(build_arguments).unwrap());
    ASSERT_TRUE(profile.is_ok());
    auto external_usage = lito::cpp::ExternalUsageCatalog {};
    external_usage.packages.push(lito::cpp::ExternalPackageUsage {
        .package = String::make("fixture-profile-owned_option"_str),
    });
    auto metadata = lito::cpp::adapt_package_graph_metadata(rstd::move(graph).unwrap(),
                                                            packages,
                                                            targets,
                                                            build_configuration,
                                                            rstd::move(profile).unwrap(),
                                                            native_platform(),
                                                            rstd::move(external_usage),
                                                            *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto planned = lito::cpp::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_err());
    auto planned_error = rstd::move(planned).unwrap_err();
    ASSERT_TRUE(planned_error.is_Configuration());
    EXPECT_TRUE(rstd::format("{}", planned_error.as_Configuration().source.as_ref())
                    .as_str()
                    .contains("optimization"_str));
}

TEST_F(BuildProfile, UsageSettingsAreValidatedByCppAdapter) {
    struct Case {
        ref<str> manifest;
        ref<str> package;
        ref<str> target;
    };
    constexpr Case cases[] = {
        {
            R"toml([package]
name = "fixture-profile-linker-owned"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-linker-owned"
sources = ["main.cpp"]

[usage]
linker-options = ["-Wl,--strip-all"]
)toml"_str,
            "fixture-profile-linker-owned"_str,
            "profile-linker-owned"_str,
        },
        {
            R"toml([package]
name = "fixture-profile-owned_definition"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-owned-definition"
sources = ["main.cpp"]

[usage]
private-definitions = ["NDEBUG"]
)toml"_str,
            "fixture-profile-owned_definition"_str,
            "profile-owned-definition"_str,
        },
    };

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    for (auto index = usize {}; index < usize(2); ++index) {
        const auto& item = cases[index.to_primitive()];
        auto project = manifest_project(rstd::format("usage-{}", index).as_str(), item.manifest);
        ASSERT_TRUE(project.is_ok());
        auto graph = lito::resolve_package_graph(project->root.as_path());
        ASSERT_TRUE(graph.is_ok());
        auto packages = strings(item.package);
        auto targets  = Vec<lito::PackageTargetId>::make();
        targets.push(lito::PackageTargetId {
            .package = String::make(item.package),
            .kind    = lito::PackageTargetKind::Binary,
            .name    = String::make(item.target),
        });
        auto build_configuration = configuration();
        auto build_arguments     = lito::cpp::parse_build_arguments(build_configuration, *parser);
        ASSERT_TRUE(build_arguments.is_ok());
        auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                    graph->profile,
                                                    build_profile("debug"_str),
                                                    rstd::move(build_arguments).unwrap());
        ASSERT_TRUE(profile.is_ok());
        auto external_usage = lito::cpp::ExternalUsageCatalog {};
        external_usage.packages.push(lito::cpp::ExternalPackageUsage {
            .package = String::make(item.package),
        });
        auto metadata = lito::cpp::adapt_package_graph_metadata(rstd::move(graph).unwrap(),
                                                                packages,
                                                                targets,
                                                                build_configuration,
                                                                rstd::move(profile).unwrap(),
                                                                native_platform(),
                                                                rstd::move(external_usage),
                                                                *parser);
        ASSERT_TRUE(metadata.is_err());
        auto error = rstd::move(metadata).unwrap_err();
        ASSERT_TRUE(error.is_Message());
        EXPECT_TRUE(error.as_Message().message.as_str().contains("Lito-owned setting"_str));
    }
}
