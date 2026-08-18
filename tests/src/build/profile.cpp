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
    auto profile_project(ref<str> name)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        auto tree = build_profile_project_tree();
        if (tree.is_err()) return Err(rstd::move(tree).unwrap_err());
        return materialize(name, *tree);
    }

    auto manifest_project(ref<str> name, ref<str> contents)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        const ProjectFile files[] = {
            { "lito.toml"_str, contents },
        };
        return materialize(name, files);
    }
};

TEST_F(BuildProfile, ProjectProfileDefaultsAndRootOwnershipAreTyped) {
    auto default_project = profile_project("defaults"_str);
    ASSERT_TRUE(default_project.is_ok());
    auto defaults = lito::package::resolve_package_graph(default_project->root.as_path());
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
    auto disabled = lito::package::resolve_package_graph(disabled_project->root.as_path());
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
    auto workspace = lito::package::resolve_package_graph(workspace_project->root.as_path());
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
    auto dependency      = lito::package::resolve_package_graph(dependency_root.as_path());
    ASSERT_TRUE(dependency.is_ok());
    EXPECT_TRUE(dependency->profile.exceptions);
    EXPECT_TRUE(dependency->profile.rtti);
}

TEST_F(BuildProfile, ProjectProfileKeepsCppPolicyAndOneCommonCodegenPolicy) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto project = lito::manifest::ProjectProfile {
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
    EXPECT_EQ(debug->c.standard, lito::manifest::CStandard::C99);
    EXPECT_EQ(release->c.standard, lito::manifest::CStandard::C99);
    ASSERT_EQ(debug->c.diagnostics.warnings.len(), usize(3));
    EXPECT_EQ(debug->c.diagnostics.warnings[usize {}].warning,
              lito::compiler::CompilerWarning::All);
    EXPECT_NE(debug->cpp.common.codegen.optimization, release->cpp.common.codegen.optimization);
    EXPECT_TRUE(debug->cpp.common.codegen.position_independent_code);
    EXPECT_TRUE(release->cpp.common.codegen.position_independent_code);
}

TEST_F(BuildProfile, PthreadBuildOptionOwnsCompileAndLinkRequirements) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-pthread"_str),
        .source    = String::make("config.build.options"_str),
    });
    auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                lito::manifest::ProjectProfile {},
                                                build_profile("debug"_str),
                                                *parser);
    ASSERT_TRUE(profile.is_ok());
    EXPECT_TRUE(lito::compiler::uses_posix_threads(profile->cpp.common));
    EXPECT_FALSE(lito::compiler::uses_posix_threads(profile->c.common));
    EXPECT_TRUE(profile->cpp_link_requirements.posix_threads);
    EXPECT_FALSE(profile->c_link_requirements.posix_threads);
    ASSERT_EQ(profile->cpp_link_requirements.thread_sources.len(), usize(1));
    EXPECT_EQ(profile->cpp_link_requirements.thread_sources[usize {}].as_str(),
              "config.build.options"_str);
}

TEST_F(BuildProfile, GlobalVendorOptionsRemainInTheCppLanguageDomain) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-fno-builtin"_str),
        .source    = String::make("config.build.options"_str),
    });
    auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                lito::manifest::ProjectProfile {},
                                                build_profile("debug"_str),
                                                *parser);
    ASSERT_TRUE(profile.is_ok());
    ASSERT_EQ(profile->cpp.vendor.len(), usize(1));
    EXPECT_EQ(profile->cpp.vendor[usize {}].value.as_str(), "-fno-builtin"_str);
    EXPECT_TRUE(profile->c.vendor.is_empty());
}

TEST_F(BuildProfile, GlobalCOptionsRemainInTheCLanguageDomain) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("-pthread"_str),
        .source    = String::make("CFLAGS"_str),
    });
    auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                lito::manifest::ProjectProfile {},
                                                build_profile("debug"_str),
                                                *parser);
    ASSERT_TRUE(profile.is_ok());
    EXPECT_TRUE(lito::compiler::uses_posix_threads(profile->c.common));
    EXPECT_FALSE(lito::compiler::uses_posix_threads(profile->cpp.common));
    EXPECT_TRUE(profile->c_link_requirements.posix_threads);
    EXPECT_FALSE(profile->cpp_link_requirements.posix_threads);
    ASSERT_EQ(profile->c_link_requirements.thread_sources.len(), usize(1));
    EXPECT_EQ(profile->c_link_requirements.thread_sources[usize {}].as_str(), "CFLAGS"_str);
}

TEST_F(BuildProfile, GlobalLanguageAndLinkOptionsHaveIndependentIdentities) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto baseline = lito::cpp::make_profile_spec(
        configuration(), lito::manifest::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(baseline.is_ok());

    auto cpp_configuration = configuration();
    cpp_configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-DCPP_IDENTITY=1"_str),
        .source    = String::make("CXXFLAGS"_str),
    });
    auto cpp = lito::cpp::make_profile_spec(
        cpp_configuration, lito::manifest::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(cpp.is_ok());
    EXPECT_NE(lito::cpp::cpp_compile_identity(cpp->cpp).as_str(),
              lito::cpp::cpp_compile_identity(baseline->cpp).as_str());
    EXPECT_NE(lito::cpp::cpp_scan_identity(cpp->cpp).as_str(),
              lito::cpp::cpp_scan_identity(baseline->cpp).as_str());
    EXPECT_EQ(lito::c::c_compile_identity(cpp->c).as_str(),
              lito::c::c_compile_identity(baseline->c).as_str());

    auto c_configuration = configuration();
    c_configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("-DC_IDENTITY=1"_str),
        .source    = String::make("CFLAGS"_str),
    });
    auto c = lito::cpp::make_profile_spec(
        c_configuration, lito::manifest::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(c.is_ok());
    EXPECT_NE(lito::c::c_compile_identity(c->c).as_str(),
              lito::c::c_compile_identity(baseline->c).as_str());
    EXPECT_NE(lito::c::c_scan_identity(c->c).as_str(),
              lito::c::c_scan_identity(baseline->c).as_str());
    EXPECT_EQ(lito::cpp::cpp_compile_identity(c->cpp).as_str(),
              lito::cpp::cpp_compile_identity(baseline->cpp).as_str());

    auto link_configuration = configuration();
    link_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-Wl,--as-needed"_str),
        .source    = String::make("LDFLAGS"_str),
    });
    auto link = lito::cpp::make_profile_spec(
        link_configuration, lito::manifest::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(link.is_ok());
    EXPECT_EQ(lito::cpp::cpp_compile_identity(link->cpp).as_str(),
              lito::cpp::cpp_compile_identity(baseline->cpp).as_str());
    EXPECT_EQ(lito::c::c_compile_identity(link->c).as_str(),
              lito::c::c_compile_identity(baseline->c).as_str());
}

TEST_F(BuildProfile, BuildProfilesResolveCargoStyleValuesAndInheritance) {
    auto project = profile_project("profile-values"_str);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto perf = lito::manifest::resolve_build_profile(graph->profile, build_profile("perf"_str));
    ASSERT_TRUE(perf.is_ok());
    EXPECT_EQ(perf->family, lito::manifest::BuildProfileFamily::Release);
    EXPECT_EQ(perf->optimization, lito::manifest::Optimization::Level2);
    EXPECT_EQ(perf->debug_info, lito::manifest::DebugInfo::LineTablesOnly);
    EXPECT_EQ(perf->strip, lito::manifest::StripMode::None);
    EXPECT_EQ(perf->lto, lito::manifest::Lto::Off);
    EXPECT_TRUE(perf->ndebug);

    auto aliases =
        lito::manifest::resolve_build_profile(graph->profile, build_profile("aliases"_str));
    ASSERT_TRUE(aliases.is_ok());
    EXPECT_EQ(aliases->optimization, lito::manifest::Optimization::SizeMin);
    EXPECT_EQ(aliases->debug_info, lito::manifest::DebugInfo::Limited);
    EXPECT_EQ(aliases->strip, lito::manifest::StripMode::Symbols);
    EXPECT_EQ(aliases->lto, lito::manifest::Lto::Fat);
}

TEST_F(BuildProfile, BuildProfileCatalogRejectsUnknownParentsAndCycles) {
    auto unknown = lito::manifest::ProjectProfile {};
    unknown.build_profiles.push(lito::manifest::BuildProfileDefinition {
        .name     = build_profile("custom"_str),
        .inherits = Some(build_profile("missing"_str)),
    });
    auto unknown_result = lito::manifest::validate_build_profiles(unknown);
    ASSERT_TRUE(unknown_result.is_err());
    auto unknown_error = rstd::move(unknown_result).unwrap_err();
    ASSERT_TRUE(unknown_error.is_Message());
    EXPECT_TRUE(unknown_error.as_Message().message.as_str().contains("unknown profile"_str));

    auto cycle = lito::manifest::ProjectProfile {};
    cycle.build_profiles.push(lito::manifest::BuildProfileDefinition {
        .name     = build_profile("first"_str),
        .inherits = Some(build_profile("second"_str)),
    });
    cycle.build_profiles.push(lito::manifest::BuildProfileDefinition {
        .name     = build_profile("second"_str),
        .inherits = Some(build_profile("first"_str)),
    });
    auto cycle_result = lito::manifest::validate_build_profiles(cycle);
    ASSERT_TRUE(cycle_result.is_err());
    auto cycle_error = rstd::move(cycle_result).unwrap_err();
    ASSERT_TRUE(cycle_error.is_Message());
    EXPECT_TRUE(cycle_error.as_Message().message.as_str().contains("inheritance cycle"_str));
}

TEST_F(BuildProfile, CodegenProfilesMaterializeTypedClangOptionsAndCacheIdentities) {
    EXPECT_EQ(lito::cpp::cpp_optimization_option(lito::manifest::Optimization::Level2), "-O2"_str);
    EXPECT_EQ(lito::cpp::cpp_optimization_option(lito::manifest::Optimization::Size), "-Os"_str);
    EXPECT_EQ(lito::cpp::cpp_optimization_option(lito::manifest::Optimization::SizeMin), "-Oz"_str);
    EXPECT_EQ(lito::cpp::cpp_debug_option(lito::manifest::DebugInfo::LineDirectivesOnly),
              "-gline-directives-only"_str);
    EXPECT_EQ(lito::cpp::cpp_debug_option(lito::manifest::DebugInfo::LineTablesOnly),
              "-gline-tables-only"_str);
    EXPECT_EQ(lito::cpp::cpp_debug_option(lito::manifest::DebugInfo::Limited), "-g1"_str);
    EXPECT_EQ(lito::cpp::cpp_lto_option(lito::manifest::Lto::Off), "-fno-lto"_str);
    EXPECT_EQ(lito::cpp::cpp_lto_option(lito::manifest::Lto::Thin), "-flto=thin"_str);
    EXPECT_EQ(lito::cpp::cpp_lto_option(lito::manifest::Lto::Fat), "-flto=full"_str);

    auto project = profile_project("profile-codegen"_str);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
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
    compiler_configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-flto=auto"_str),
        .source    = String::make("config.build.options"_str),
    });
    auto compiler = lito::cpp::make_profile_spec(compiler_configuration,
                                                 lito::manifest::ProjectProfile {},
                                                 build_profile("debug"_str),
                                                 *parser);
    ASSERT_TRUE(compiler.is_err());
    auto compiler_error = rstd::move(compiler).unwrap_err();
    ASSERT_TRUE(compiler_error.is_Message());
    EXPECT_TRUE(compiler_error.as_Message().message.as_str().contains("selected profile"_str));

    auto linker_configuration = configuration();
    linker_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-Wl,--strip-debug"_str),
        .source    = String::make("config.build.linker-options"_str),
    });
    auto linker = lito::cpp::make_profile_spec(linker_configuration,
                                               lito::manifest::ProjectProfile {},
                                               build_profile("debug"_str),
                                               *parser);
    ASSERT_TRUE(linker.is_err());
    auto linker_error = rstd::move(linker).unwrap_err();
    ASSERT_TRUE(linker_error.is_Message());
    EXPECT_TRUE(linker_error.as_Message().message.as_str().contains("selected profile"_str));

    auto stdlib_configuration = configuration();
    stdlib_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-nostdlib++"_str),
        .source    = String::make("config.build.linker-options"_str),
    });
    auto stdlib = lito::cpp::make_profile_spec(stdlib_configuration,
                                               lito::manifest::ProjectProfile {},
                                               build_profile("debug"_str),
                                               *parser);
    ASSERT_TRUE(stdlib.is_err());
    auto stdlib_error = rstd::move(stdlib).unwrap_err();
    ASSERT_TRUE(stdlib_error.is_Message());
    EXPECT_TRUE(stdlib_error.as_Message().message.as_str().contains("Lito-owned setting"_str));

    auto pthread_configuration = configuration();
    pthread_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-pthread"_str),
        .source    = String::make("LDFLAGS"_str),
    });
    auto pthread = lito::cpp::make_profile_spec(pthread_configuration,
                                                lito::manifest::ProjectProfile {},
                                                build_profile("debug"_str),
                                                *parser);
    ASSERT_TRUE(pthread.is_ok());
    EXPECT_TRUE(pthread->c_link_requirements.posix_threads);
    EXPECT_TRUE(pthread->cpp_link_requirements.posix_threads);
    EXPECT_TRUE(pthread->linker_options.is_empty());
}

TEST_F(BuildProfile, GlobalAndPackageLinkerOptionsPreserveOrderAndDuplicates) {
    auto project = manifest_project("linker-option-order"_str, R"toml([package]
name = "fixture-linker-option-order"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "linker-option-order"
sources = ["main.cpp"]

[usage]
linker-options = ["-Wl,--pop-state"]
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-Wl,--push-state"_str, "-Wl,--as-needed"_str),
        .source    = String::make("config.build.linker-options"_str),
    });
    build_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-Wl,--as-needed"_str),
        .source    = String::make("LDFLAGS"_str),
    });
    auto profile = lito::cpp::make_profile_spec(
        build_configuration, graph->profile, build_profile("debug"_str), *parser);
    ASSERT_TRUE(profile.is_ok());
    auto packages = strings("fixture-linker-option-order"_str);
    auto targets  = Vec<lito::package::PackageTargetId>::make();
    targets.push(lito::package::PackageTargetId {
        .package = String::make("fixture-linker-option-order"_str),
        .kind    = lito::package::PackageTargetKind::Binary,
        .name    = String::make("linker-option-order"_str),
    });
    auto external_usage = lito::cpp::ExternalUsageCatalog {};
    external_usage.packages.push(lito::cpp::ExternalPackageUsage {
        .package = String::make("fixture-linker-option-order"_str),
    });
    auto metadata = lito::cpp::adapt_package_graph_metadata(rstd::move(graph).unwrap(),
                                                            packages,
                                                            targets,
                                                            build_configuration,
                                                            rstd::move(profile).unwrap(),
                                                            native_platform(),
                                                            rstd::move(external_usage),
                                                            lito::cpp::ExternalSourceRootCatalog {},
                                                            *parser);
    ASSERT_TRUE(metadata.is_ok());
    auto planned = lito::cpp::resolve_native_targets(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_ok());
    ASSERT_EQ(planned->linker_options.len(), usize(1));
    ASSERT_EQ(planned->linker_options[usize {}].len(), usize(4));
    EXPECT_EQ(planned->linker_options[usize {}][usize {}].as_str(), "-Wl,--push-state"_str);
    EXPECT_EQ(planned->linker_options[usize {}][usize(1)].as_str(), "-Wl,--as-needed"_str);
    EXPECT_EQ(planned->linker_options[usize {}][usize(2)].as_str(), "-Wl,--as-needed"_str);
    EXPECT_EQ(planned->linker_options[usize {}][usize(3)].as_str(), "-Wl,--pop-state"_str);
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
    auto graph = lito::package::resolve_package_graph(project->root.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto packages = strings("fixture-profile-owned_option"_str);
    auto targets  = Vec<lito::package::PackageTargetId>::make();
    targets.push(lito::package::PackageTargetId {
        .package = String::make("fixture-profile-owned_option"_str),
        .kind    = lito::package::PackageTargetKind::Binary,
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
                                                            lito::cpp::ExternalSourceRootCatalog {},
                                                            *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto planned = lito::cpp::resolve_native_targets(*metadata, "debug"_str, Vec<String>::make());
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
        {
            R"toml([package]
name = "fixture-package-version-definition"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "package-version-definition"
sources = ["main.cpp"]

[usage]
private-definitions = ["LITO_PKG_VERSION=\"override\""]
)toml"_str,
            "fixture-package-version-definition"_str,
            "package-version-definition"_str,
        },
        {
            R"toml([package]
name = "fixture-package-feature-option"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "package-feature-option"
sources = ["main.cpp"]

[usage]
options = ["-ULITO_FEAT_API"]
)toml"_str,
            "fixture-package-feature-option"_str,
            "package-feature-option"_str,
        },
    };

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    for (auto index = usize {}; index < usize(4); ++index) {
        const auto& item = cases[index.to_primitive()];
        auto project = manifest_project(rstd::format("usage-{}", index).as_str(), item.manifest);
        ASSERT_TRUE(project.is_ok());
        auto graph = lito::package::resolve_package_graph(project->root.as_path());
        ASSERT_TRUE(graph.is_ok());
        auto packages = strings(item.package);
        auto targets  = Vec<lito::package::PackageTargetId>::make();
        targets.push(lito::package::PackageTargetId {
            .package = String::make(item.package),
            .kind    = lito::package::PackageTargetKind::Binary,
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
        auto metadata =
            lito::cpp::adapt_package_graph_metadata(rstd::move(graph).unwrap(),
                                                    packages,
                                                    targets,
                                                    build_configuration,
                                                    rstd::move(profile).unwrap(),
                                                    native_platform(),
                                                    rstd::move(external_usage),
                                                    lito::cpp::ExternalSourceRootCatalog {},
                                                    *parser);
        ASSERT_TRUE(metadata.is_err());
        auto error = rstd::move(metadata).unwrap_err();
        ASSERT_TRUE(error.is_Message());
        EXPECT_TRUE(error.as_Message().message.as_str().contains("Lito-owned"_str));
    }
}
