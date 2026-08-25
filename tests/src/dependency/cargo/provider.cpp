#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.driver;
import lito.system;
import lito.tools;
import lito.tools.cargo;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class CargoProvider : public ProjectFixture {};

auto cargo_profile(ref<str> selected, ref<str> inherited)
    -> lito::tools::cargo::ProfileConfiguration {
    return lito::tools::cargo::ProfileConfiguration {
        .selected = lito::dependency::CargoProfileName { .value = String::make(selected) },
        .inherits = lito::dependency::CargoProfileName { .value = String::make(inherited) },
    };
}

TEST(CargoProviderProtocol, NativeLinkTokensPreserveOrderAndRejectCommandInjection) {
    auto unix =
        lito::tools::cargo::parse_native_arguments("native-static-libs: -ldl -lpthread -ldl"_str);
    ASSERT_TRUE(unix.is_ok());
    ASSERT_EQ(unix->len(), usize(3));
    EXPECT_EQ((*unix)[usize {}].as_str(), "-ldl"_str);
    EXPECT_EQ((*unix)[usize(1)].as_str(), "-lpthread"_str);
    EXPECT_EQ((*unix)[usize(2)].as_str(), "-ldl"_str);

    auto macos = lito::tools::cargo::parse_native_arguments(
        "native-static-libs: -framework Security -lSystem"_str);
    ASSERT_TRUE(macos.is_ok());
    ASSERT_EQ(macos->len(), usize(3));

    auto msvc = lito::tools::cargo::parse_native_arguments(
        "native-static-libs: ws2_32.lib userenv.lib"_str);
    ASSERT_TRUE(msvc.is_ok());
    ASSERT_EQ(msvc->len(), usize(2));

    EXPECT_TRUE(lito::tools::cargo::parse_native_arguments("native-static-libs: @response.rsp"_str)
                    .is_err());
    EXPECT_TRUE(
        lito::tools::cargo::parse_native_arguments("native-static-libs: -Wl,--whole-archive"_str)
            .is_err());
    EXPECT_TRUE(
        lito::tools::cargo::parse_native_arguments("native-static-libs: relative/libnative.a"_str)
            .is_err());
}

TEST_F(CargoProvider, EffectivePlainProfileProjectsTypedCargoConfigurationByLanguage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-O2"_str, "-g1"_str, "-flto=thin"_str, "-DNDEBUG"_str),
        .source    = String::make("CXXFLAGS"_str),
    });
    build_configuration.global_options.c.push(lito::config::BuildOptionInput {
        .arguments = strings("-O3"_str, "-g0"_str),
        .source    = String::make("CFLAGS"_str),
    });
    build_configuration.global_options.linker.push(lito::config::BuildOptionInput {
        .arguments = strings("-flto=thin"_str, "-Wl,--strip-debug"_str),
        .source    = String::make("LDFLAGS"_str),
    });
    auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                lito::manifest::ProjectProfile {},
                                                build_profile("plain"_str),
                                                *parser);
    ASSERT_TRUE(profile.is_ok());

    auto declaration = lito::dependency::CargoDependencyRequirement {
        .alias = String::make("fixture"_str),
        .recipe =
            lito::dependency::CargoDependencyRecipe {
                .package = String::make("fixture"_str),
                .source  = String::make("fixture-source"_str),
            },
        .consumption =
            lito::dependency::CargoDependencyConsumption {
                .profile = Some(lito::dependency::CargoProfileName {
                    .value = String::make("packaging"_str),
                }),
                .usage   = lito::dependency::CargoDependencyUsage::Runtime,
            },
    };
    auto cpp = lito::resolve_cargo_profile_configuration(
        "owner"_str, declaration, *profile, lito::manifest::PackageLanguage::Cpp);
    ASSERT_TRUE(cpp.is_ok());
    EXPECT_TRUE(cpp->selected.as_str().starts_with("lito-"_str));
    EXPECT_EQ(cpp->inherits.as_str(), "packaging"_str);
    ASSERT_TRUE(cpp->optimization.is_some());
    ASSERT_TRUE(cpp->debug_info.is_some());
    ASSERT_TRUE(cpp->lto.is_some());
    ASSERT_TRUE(cpp->debug_assertions.is_some());
    ASSERT_TRUE(cpp->strip.is_some());
    EXPECT_EQ(*cpp->optimization, lito::tools::cargo::ProfileOptimization::Level2);
    EXPECT_EQ(*cpp->debug_info, lito::tools::cargo::ProfileDebugInfo::Limited);
    EXPECT_EQ(*cpp->lto, lito::tools::cargo::ProfileLto::Thin);
    EXPECT_FALSE(*cpp->debug_assertions);
    EXPECT_EQ(*cpp->strip, lito::tools::cargo::ProfileStrip::DebugInfo);

    declaration.consumption.profile = None();
    declaration.consumption.usage   = lito::dependency::CargoDependencyUsage::Link;
    auto c                          = lito::resolve_cargo_profile_configuration(
        "owner"_str, declaration, *profile, lito::manifest::PackageLanguage::C);
    ASSERT_TRUE(c.is_ok());
    EXPECT_EQ(c->inherits.as_str(), "dev"_str);
    ASSERT_TRUE(c->optimization.is_some());
    ASSERT_TRUE(c->debug_info.is_some());
    ASSERT_TRUE(c->lto.is_some());
    EXPECT_EQ(*c->optimization, lito::tools::cargo::ProfileOptimization::Level3);
    EXPECT_EQ(*c->debug_info, lito::tools::cargo::ProfileDebugInfo::None);
    EXPECT_EQ(*c->lto, lito::tools::cargo::ProfileLto::Thin);
    EXPECT_TRUE(c->debug_assertions.is_none());
    EXPECT_TRUE(c->strip.is_none());
    EXPECT_NE(c->selected.as_str(), cpp->selected.as_str());
}

TEST_F(CargoProvider, BuiltinProfilesSelectCargoBaseAndAssertionPolicy) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto debug = lito::cpp::make_profile_spec(
        configuration(), lito::manifest::ProjectProfile {}, build_profile("debug"_str), *parser);
    auto release = lito::cpp::make_profile_spec(
        configuration(), lito::manifest::ProjectProfile {}, build_profile("release"_str), *parser);
    ASSERT_TRUE(debug.is_ok());
    ASSERT_TRUE(release.is_ok());
    auto declaration = lito::dependency::CargoDependencyRequirement {
        .alias = String::make("fixture"_str),
    };
    auto debug_cargo = lito::resolve_cargo_profile_configuration(
        "owner"_str, declaration, *debug, lito::manifest::PackageLanguage::Cpp);
    auto release_cargo = lito::resolve_cargo_profile_configuration(
        "owner"_str, declaration, *release, lito::manifest::PackageLanguage::Cpp);
    ASSERT_TRUE(debug_cargo.is_ok());
    ASSERT_TRUE(release_cargo.is_ok());
    EXPECT_EQ(debug_cargo->inherits.as_str(), "dev"_str);
    EXPECT_EQ(release_cargo->inherits.as_str(), "release"_str);
    ASSERT_TRUE(debug_cargo->debug_assertions.is_some());
    ASSERT_TRUE(release_cargo->debug_assertions.is_some());
    EXPECT_TRUE(*debug_cargo->debug_assertions);
    EXPECT_FALSE(*release_cargo->debug_assertions);
}

TEST_F(CargoProvider, UnsupportedNativeOptimizationFailsCargoProjection) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto build_configuration = configuration();
    build_configuration.global_options.cpp.push(lito::config::BuildOptionInput {
        .arguments = strings("-Og"_str),
        .source    = String::make("CXXFLAGS"_str),
    });
    auto profile = lito::cpp::make_profile_spec(build_configuration,
                                                lito::manifest::ProjectProfile {},
                                                build_profile("plain"_str),
                                                *parser);
    ASSERT_TRUE(profile.is_ok());
    auto declaration = lito::dependency::CargoDependencyRequirement {
        .alias = String::make("fixture"_str),
    };
    constexpr lito::manifest::Optimization unsupported[] = {
        lito::manifest::Optimization::Level4,
        lito::manifest::Optimization::Debug,
        lito::manifest::Optimization::Fast,
    };
    for (auto optimization : unsupported) {
        profile->cpp.common.codegen.optimization = Some(optimization);
        auto projected                           = lito::resolve_cargo_profile_configuration(
            "owner"_str, declaration, *profile, lito::manifest::PackageLanguage::Cpp);
        ASSERT_TRUE(projected.is_err());
        auto message = rstd::format("{}", projected.unwrap_err());
        EXPECT_TRUE(message.as_str().contains(lito::cpp::cpp_optimization_option(optimization)));
        EXPECT_TRUE(message.as_str().contains("CXXFLAGS"_str));
    }
}

TEST_F(CargoProvider, CargoBuildReturnsExactStaticlibAndOrderedNativeClosure) {
    constexpr ProjectFile files[] = {
        { "Cargo.toml"_str, R"toml([package]
name = "lito-cargo-provider-fixture"
version = "0.1.0"
edition = "2024"

[lib]
name = "lito_cargo_provider_fixture"
crate-type = ["staticlib"]

[features]
default = ["default-answer"]
default-answer = []
selected = []
)toml"_str },
        { "Cargo.lock"_str, R"lock(# This file is automatically @generated by Cargo.
# It is not intended for manual editing.
version = 4

[[package]]
name = "lito-cargo-provider-fixture"
version = "0.1.0"
)lock"_str },
        { "src/lib.rs"_str, R"rust(#[unsafe(no_mangle)]
pub extern "C" fn lito_cargo_provider_answer(value: i32) -> i32 {
    value + 1
}
)rust"_str },
    };
    auto project = materialize("cargo-provider"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto resolved = resolver.require(
        lito::tools::Tool::Cargo,
        lito::tools::command_tool_requirement(lito::tools::HostToolCapability::CargoBuild,
                                              "Cargo provider test"_str));
    ASSERT_TRUE(resolved.is_ok());
    auto provider = lito::tools::cargo::identify_provider(rstd::move(resolved).unwrap().executable,
                                                          *environment);
    ASSERT_TRUE(provider.is_ok());
    EXPECT_FALSE(provider->identity.is_empty());
    EXPECT_FALSE(provider->host_target.is_empty());

    auto manifest = project->root.join(PathBuf::from("Cargo.toml"_str).as_path());
    auto metadata = lito::tools::cargo::query_metadata(
        *provider,
        lito::tools::cargo::MetadataRequest {
            .source_root = project->root.clone(),
            .manifest    = manifest.clone(),
            .package     = String::make("lito-cargo-provider-fixture"_str),
            .offline     = true,
        },
        *environment);
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_EQ(metadata->name.as_str(), "lito-cargo-provider-fixture"_str);
    ASSERT_TRUE(metadata->library.is_some());
    EXPECT_EQ(metadata->library->name.as_str(), "lito_cargo_provider_fixture"_str);
    EXPECT_TRUE(metadata->binaries.is_empty());
    EXPECT_EQ(metadata->workspace_root.as_path(), project->root.as_path());

    auto work    = build_root("cargo-provider"_str);
    auto target  = work.join(PathBuf::from("target"_str).as_path());
    auto request = lito::tools::cargo::BuildRequest {
        .alias            = String::make("fixture"_str),
        .source_root      = project->root.clone(),
        .manifest         = metadata->manifest.clone(),
        .package          = String::make("lito-cargo-provider-fixture"_str),
        .features         = strings("selected"_str),
        .default_features = false,
        .profile          = cargo_profile("lito-provider-test"_str, "dev"_str),
        .target           = provider->host_target.clone(),
        .request_identity = String::make("cargo-provider-request-v1"_str),
        .work_root        = work.clone(),
        .target_directory = target.clone(),
        .jobs             = usize(1),
        .offline          = true,
    };
    request.profile.optimization     = Some(lito::tools::cargo::ProfileOptimization::Level2);
    request.profile.debug_info       = Some(lito::tools::cargo::ProfileDebugInfo::Limited);
    request.profile.lto              = Some(lito::tools::cargo::ProfileLto::Off);
    request.profile.debug_assertions = Some(false);
#if RSTD_OS_WINDOWS
    constexpr auto archive_suffix = ".lib"_str;
#else
    constexpr auto archive_suffix = ".a"_str;
#endif
    auto built = lito::tools::cargo::build_static_library(
        *provider, *metadata, request, archive_suffix, *environment);
    ASSERT_TRUE(built.is_ok());
    EXPECT_TRUE(built->archive.as_path().starts_with(target.as_path()));
    EXPECT_FALSE(built->archive_digest.is_empty());
    EXPECT_TRUE(built->archive_size > u64 {});
    EXPECT_FALSE(built->native_link_arguments.is_empty());

    auto repeated = lito::tools::cargo::build_static_library(
        *provider, *metadata, request, archive_suffix, *environment);
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_EQ(repeated->identity.as_str(), built->identity.as_str());
    EXPECT_EQ(repeated->archive.as_path(), built->archive.as_path());
    EXPECT_TRUE(repeated->fresh);
}

TEST_F(CargoProvider, CargoBuildReturnsDiscoveredBinaryArtifacts) {
    constexpr ProjectFile files[] = {
        { "Cargo.toml"_str, R"toml([package]
name = "lito-cargo-runtime-fixture"
version = "0.1.0"
edition = "2024"

[[bin]]
name = "runtime-daemon"
path = "src/daemon.rs"

[[bin]]
name = "runtime-helper"
path = "src/helper.rs"
)toml"_str },
        { "Cargo.lock"_str, R"lock(# This file is automatically @generated by Cargo.
# It is not intended for manual editing.
version = 4

[[package]]
name = "lito-cargo-runtime-fixture"
version = "0.1.0"
)lock"_str },
        { "src/daemon.rs"_str, "fn main() { println!(\"daemon\"); }\n"_str },
        { "src/helper.rs"_str, "fn main() { println!(\"helper\"); }\n"_str },
    };
    auto project = materialize("cargo-runtime-provider"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto resolved = resolver.require(
        lito::tools::Tool::Cargo,
        lito::tools::command_tool_requirement(lito::tools::HostToolCapability::CargoBuild,
                                              "Cargo runtime provider test"_str));
    ASSERT_TRUE(resolved.is_ok());
    auto provider = lito::tools::cargo::identify_provider(rstd::move(resolved).unwrap().executable,
                                                          *environment);
    ASSERT_TRUE(provider.is_ok());
    auto metadata = lito::tools::cargo::query_metadata(
        *provider,
        lito::tools::cargo::MetadataRequest {
            .source_root = project->root.clone(),
            .manifest    = project->root.join(PathBuf::from("Cargo.toml"_str).as_path()),
            .package     = String::make("lito-cargo-runtime-fixture"_str),
            .offline     = true,
        },
        *environment);
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->library.is_none());
    ASSERT_EQ(metadata->binaries.len(), usize(2));
    EXPECT_EQ(metadata->binaries[usize {}].name.as_str(), "runtime-daemon"_str);
    EXPECT_EQ(metadata->binaries[usize(1)].name.as_str(), "runtime-helper"_str);

    auto work    = build_root("cargo-runtime-provider"_str);
    auto target  = work.join(PathBuf::from("target"_str).as_path());
    auto request = lito::tools::cargo::BuildRequest {
        .alias            = String::make("runtime"_str),
        .source_root      = project->root.clone(),
        .manifest         = metadata->manifest.clone(),
        .package          = String::make("lito-cargo-runtime-fixture"_str),
        .profile          = cargo_profile("lito-runtime-test"_str, "dev"_str),
        .target           = provider->host_target.clone(),
        .request_identity = String::make("cargo-runtime-request-v1"_str),
        .work_root        = work.clone(),
        .target_directory = target.clone(),
        .jobs             = usize(1),
        .offline          = true,
    };
    request.profile.optimization = Some(lito::tools::cargo::ProfileOptimization::Level1);
    request.profile.strip        = Some(lito::tools::cargo::ProfileStrip::DebugInfo);
    auto built = lito::tools::cargo::build_binaries(*provider, *metadata, request, *environment);
    ASSERT_TRUE(built.is_ok());
    ASSERT_EQ(built->artifacts.len(), usize(2));
    EXPECT_EQ(built->artifacts[usize {}].name.as_str(), "runtime-daemon"_str);
    EXPECT_TRUE(built->artifacts[usize {}].executable.as_path().starts_with(target.as_path()));
    EXPECT_FALSE(built->artifacts[usize {}].executable_digest.is_empty());
    EXPECT_EQ(built->artifacts[usize(1)].name.as_str(), "runtime-helper"_str);

    auto repeated = lito::tools::cargo::build_binaries(*provider, *metadata, request, *environment);
    ASSERT_TRUE(repeated.is_ok());
    ASSERT_EQ(repeated->artifacts.len(), usize(2));
    EXPECT_TRUE(repeated->artifacts[usize {}].fresh);
    EXPECT_TRUE(repeated->artifacts[usize(1)].fresh);
}
