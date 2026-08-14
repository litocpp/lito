#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(BuildProfile, ProjectProfileDefaultsAndRootOwnershipAreTyped) {
    auto defaults = lito::resolve_package_graph(fixture_path("build/profile"_str).as_path());
    ASSERT_TRUE(defaults.is_ok());
    EXPECT_TRUE(defaults->profile.exceptions);
    EXPECT_TRUE(defaults->profile.rtti);

    auto disabled =
        lito::resolve_package_graph(fixture_path("build/profile/disabled"_str).as_path());
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_FALSE(disabled->profile.exceptions);
    EXPECT_FALSE(disabled->profile.rtti);

    auto workspace = lito::resolve_package_graph(fixture_path("workspace/profile"_str).as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->profile.exceptions);
    EXPECT_FALSE(workspace->profile.rtti);

    auto dependency =
        lito::resolve_package_graph(fixture_path("build/profile/dependency/root"_str).as_path());
    ASSERT_TRUE(dependency.is_ok());
    EXPECT_TRUE(dependency->profile.exceptions);
    EXPECT_TRUE(dependency->profile.rtti);
}

TEST(BuildProfile, ProjectProfileMaterializesIdenticalLanguageSemanticsAcrossBuildModes) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto project = lito::ProjectProfile {
        .exceptions = false,
        .rtti       = false,
    };
    auto debug =
        lito::make_profile_spec(configuration(), project, build_profile("debug"_str), *parser);
    auto release =
        lito::make_profile_spec(configuration(), project, build_profile("release"_str), *parser);
    ASSERT_TRUE(debug.is_ok());
    ASSERT_TRUE(release.is_ok());
    EXPECT_FALSE(debug->cpp.language.exceptions);
    EXPECT_FALSE(debug->cpp.language.rtti);
    EXPECT_FALSE(release->cpp.language.exceptions);
    EXPECT_FALSE(release->cpp.language.rtti);
}

TEST(BuildProfile, BuildProfilesResolveCargoStyleValuesAndInheritance) {
    auto graph = lito::resolve_package_graph(fixture_path("build/profile"_str).as_path());
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

TEST(BuildProfile, BuildProfileCatalogRejectsUnknownParentsAndCycles) {
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

TEST(BuildProfile, CodegenProfilesMaterializeTypedClangOptionsAndCacheIdentities) {
    EXPECT_EQ(lito::cpp_optimization_option(lito::CppOptimization::Level2), "-O2"_str);
    EXPECT_EQ(lito::cpp_optimization_option(lito::CppOptimization::Size), "-Os"_str);
    EXPECT_EQ(lito::cpp_optimization_option(lito::CppOptimization::SizeMin), "-Oz"_str);
    EXPECT_EQ(lito::cpp_debug_option(lito::CppDebugInfo::LineDirectivesOnly),
              "-gline-directives-only"_str);
    EXPECT_EQ(lito::cpp_debug_option(lito::CppDebugInfo::LineTablesOnly), "-gline-tables-only"_str);
    EXPECT_EQ(lito::cpp_debug_option(lito::CppDebugInfo::Limited), "-g1"_str);
    EXPECT_EQ(lito::cpp_lto_option(lito::CppLto::Off), "-fno-lto"_str);
    EXPECT_EQ(lito::cpp_lto_option(lito::CppLto::Thin), "-flto=thin"_str);
    EXPECT_EQ(lito::cpp_lto_option(lito::CppLto::Fat), "-flto=full"_str);

    auto graph = lito::resolve_package_graph(fixture_path("build/profile"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto perf = lito::make_profile_spec(
        configuration(), graph->profile, build_profile("perf"_str), *parser);
    auto variant = lito::make_profile_spec(
        configuration(), graph->profile, build_profile("codegen-variant"_str), *parser);
    ASSERT_TRUE(perf.is_ok());
    ASSERT_TRUE(variant.is_ok());
    EXPECT_NE(lito::cpp_compile_identity(perf->cpp).as_str(),
              lito::cpp_compile_identity(variant->cpp).as_str());
    EXPECT_EQ(lito::cpp_scan_identity(perf->cpp).as_str(),
              lito::cpp_scan_identity(variant->cpp).as_str());
    EXPECT_EQ(lito::cpp_bmi_compatibility_identity(perf->cpp).as_str(),
              lito::cpp_bmi_compatibility_identity(variant->cpp).as_str());

    auto aliases = lito::make_profile_spec(
        configuration(), graph->profile, build_profile("aliases"_str), *parser);
    ASSERT_TRUE(aliases.is_ok());
    EXPECT_NE(lito::cpp_scan_identity(perf->cpp).as_str(),
              lito::cpp_scan_identity(aliases->cpp).as_str());
}

TEST(BuildProfile, RawCompilerAndLinkerOptionsCannotOverrideOwnedSettings) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());

    auto compiler_configuration = configuration();
    compiler_configuration.options.push(String::make("-flto=auto"_str));
    auto compiler = lito::make_profile_spec(
        compiler_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(compiler.is_err());
    auto compiler_error = rstd::move(compiler).unwrap_err();
    ASSERT_TRUE(compiler_error.is_Message());
    EXPECT_TRUE(compiler_error.as_Message().message.as_str().contains("selected profile"_str));

    auto linker_configuration = configuration();
    linker_configuration.linker_options.push(String::make("-Wl,--strip-debug"_str));
    auto linker = lito::make_profile_spec(
        linker_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(linker.is_err());
    auto linker_error = rstd::move(linker).unwrap_err();
    ASSERT_TRUE(linker_error.is_Message());
    EXPECT_TRUE(linker_error.as_Message().message.as_str().contains("selected profile"_str));

    auto stdlib_configuration = configuration();
    stdlib_configuration.linker_options.push(String::make("-nostdlib++"_str));
    auto stdlib = lito::make_profile_spec(
        stdlib_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(stdlib.is_err());
    auto stdlib_error = rstd::move(stdlib).unwrap_err();
    ASSERT_TRUE(stdlib_error.is_Message());
    EXPECT_TRUE(stdlib_error.as_Message().message.as_str().contains("Lito-owned setting"_str));
}

TEST(BuildProfile, CompilerOptionsAreValidatedAfterToolchainParsing) {
    auto directory = fixture_path("build/profile/owned-option"_str);
    auto graph     = lito::resolve_package_graph(directory.as_path());
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
    auto build_arguments     = lito::parse_build_arguments(build_configuration, *parser);
    ASSERT_TRUE(build_arguments.is_ok());
    auto profile = lito::make_profile_spec(build_configuration,
                                           graph->profile,
                                           build_profile("debug"_str),
                                           rstd::move(build_arguments).unwrap());
    ASSERT_TRUE(profile.is_ok());
    auto external_usage = lito::ExternalUsageCatalog {};
    external_usage.packages.push(lito::ExternalPackageUsage {
        .package = String::make("fixture-profile-owned_option"_str),
    });
    auto metadata = lito::adapt_package_graph_metadata(rstd::move(graph).unwrap(),
                                                       packages,
                                                       targets,
                                                       build_configuration,
                                                       rstd::move(profile).unwrap(),
                                                       native_platform(),
                                                       rstd::move(external_usage),
                                                       *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto planned = lito::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_err());
    auto planned_error = rstd::move(planned).unwrap_err();
    ASSERT_TRUE(planned_error.is_Cpp());
    ASSERT_TRUE(planned_error.as_Cpp().source.is_Message());
    EXPECT_TRUE(
        planned_error.as_Cpp().source.as_Message().message.as_str().contains("optimization"_str));
}
