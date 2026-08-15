module;

#include <rstd/macro.hpp>

export module lito.test.support.dependency;

import rstd;
import lito.cpp;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.base_support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto has_external_macro(const lito::cpp::CompileContext& context) -> bool {
    for (const auto& macro : context.cpp.preprocessor.macros) {
        if (macro.value.as_str() == "LITO_EXTERNAL_USAGE=1"_str) return true;
    }
    return false;
}

auto pkg_config_target() -> lito::system::TargetInfo {
    return lito::system::TargetInfo {
        .triple = String::make("x86_64-unknown-linux-gnu"_str),
        .architecture =
            lito::system::Architecture {
                .name = String::make("x86_64"_str),
            },
        .os     = String::make("linux"_str),
        .family = lito::system::TargetFamily::Unix,
    };
}

auto native_platform() -> lito::system::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::system::resolve_build_platform(
        lito::system::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        None());
    return rstd::move(resolved).unwrap();
}

auto explicit_platform(ref<str> target_triple) -> lito::system::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::system::resolve_build_platform(
        lito::system::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        Some(target_triple));
    return rstd::move(resolved).unwrap();
}

auto default_profile(const lito::cpp::CppArgumentParser& parser) -> lito::cpp::ProfileSpec {
    auto profile = lito::cpp::make_profile_spec(
        configuration(), lito::ProjectProfile {}, build_profile("debug"_str), parser);
    return rstd::move(profile).unwrap();
}

auto fixture_pkg_config() -> lito::PkgConfigProviderConfig {
    auto library_paths = Vec<rstd::path::PathBuf>::make();
    library_paths.push(fixture_path("dependency/pkg-config/provider"_str));
    return lito::PkgConfigProviderConfig {
        .executable    = rstd::path::PathBuf::from("pkg-config"_str),
        .library_paths = rstd::move(library_paths),
    };
}

auto fixture_cmake() -> lito::CMakeProviderConfig {
    return lito::CMakeProviderConfig {
        .executable = rstd::path::PathBuf::from("cmake"_str),
        .generator  = String::make("Ninja"_str),
    };
}

auto resolve_cmake_fixtures(const Vec<lito::PreparedCMakeDependencyRequirement>& declarations,
                            const lito::cpp::ProfileSpec&                        profile,
                            const lito::system::BuildPlatform&                   platform,
                            const lito::cpp::CppArgumentParser&                  parser,
                            usize                                                jobs   = usize(1),
                            Vec<lito::ExternalAssetSet>*                         assets = nullptr)
    -> lito::DependencyResult<Vec<lito::cpp::ResolvedExternalDependency>> {
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<lito::DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::system::ToolResolver(*environment);
    auto provider = fixture_cmake();
    auto tool     = resolver.resolve(provider.executable.as_path(), "CMake executable"_str);
    if (tool.is_err()) {
        return Err(rstd::into<lito::DependencyError>(rstd::move(tool).unwrap_err()));
    }
    provider.executable = rstd::move(tool).unwrap().executable;
    auto identified     = lito::identify_cmake_provider(rstd::move(provider), *environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    provider       = rstd::move(identified).unwrap();
    auto result    = Vec<lito::cpp::ResolvedExternalDependency>::make();
    auto work_root = output_root("cmake-fixture-work"_str);
    for (const auto& declaration : declarations) {
        auto requirement = lito::resolve_cmake_requirement_for_platform(declaration, platform);
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        if (requirement->adapter.is_some() && requirement->adapter_identity.is_empty()) {
            auto contents = rstd::fs::read_to_string(requirement->adapter->as_path());
            if (contents.is_err()) {
                return Err(lito::DependencyError::Message(
                    rstd::format("cannot read CMake adapter '{}': {}",
                                 requirement->adapter->as_path(),
                                 rstd::move(contents).unwrap_err())));
            }
            requirement->adapter_identity =
                rstd::format("{}\n{}", requirement->adapter->as_path(), contents->as_str());
        }
        auto plan = lito::plan_cmake_package(*requirement,
                                             provider,
                                             configuration(),
                                             profile,
                                             platform.compiler_default,
                                             platform.effective_target.triple.as_str(),
                                             work_root.as_path(),
                                             jobs);
        if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
        auto snapshot = lito::execute_cmake_package(*plan, *environment);
        if (snapshot.is_err()) return Err(rstd::move(snapshot).unwrap_err());
        if (assets != nullptr) {
            for (const auto& set : snapshot->assets) assets->push(set.clone());
        }
        auto usage = lito::materialize_cmake_usage(*plan, *snapshot, parser);
        if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
        result.push(rstd::move(usage).unwrap());
    }
    return Ok(rstd::move(result));
}
auto versioned_fixture(ref<str>                       alias,
                       lito::PkgConfigVersionOperator comparison,
                       ref<str>                       version,
                       lito::PkgConfigQueryMode       mode = lito::PkgConfigQueryMode::Shared)
    -> lito::PkgConfigExternalDependency {
    return lito::PkgConfigExternalDependency {
        .alias = String::make(alias),
        .requirement =
            lito::PkgConfigDependencyRequirement {
                .module  = String::make("lito-fixture"_str),
                .version = Some(lito::PkgConfigVersionRequirement {
                    .comparison = comparison,
                    .value      = String::make(version),
                }),
                .mode    = mode,
            },
    };
}

auto external_usage_metadata(lito::DependencyVisibility          visibility,
                             const lito::cpp::CppArgumentParser& parser)
    -> lito::PackageResult<lito::cpp::PackageMetadata> {
    auto raw       = strings("-DLITO_EXTERNAL_USAGE=1"_str);
    auto arguments = parser.parse(raw, "pkg-config test fixture"_str);
    if (arguments.is_err()) {
        return Err(lito::PackageError::Message(
            rstd::format("pkg-config test fixture compiler arguments are invalid: {}",
                         rstd::move(arguments).unwrap_err())));
    }
    auto external         = Vec<lito::cpp::ResolvedExternalDependency>::make();
    auto external_targets = Vec<lito::cpp::ResolvedExternalTargetUsage>::make();
    external_targets.push(lito::cpp::ResolvedExternalTargetUsage {
        .name              = String::make("lito-fixture"_str),
        .visibility        = visibility,
        .compile_arguments = rstd::move(arguments).unwrap(),
        .identity          = String::make("fixture-resolution-v1"_str),
    });
    external.push(lito::cpp::ResolvedExternalDependency {
        .alias    = String::make("fixture"_str),
        .provider = String::make("pkg-config"_str),
        .version  = String::make("2.3.4"_str),
        .targets  = rstd::move(external_targets),
        .link_arguments =
            lito::cpp::LinkArgumentSequence {
                .tokens   = strings("-llito_fixture"_str),
                .source   = String::make("pkg-config fixture"_str),
                .identity = String::make("fixture-link-v1"_str),
            },
        .identity = String::make("fixture-resolution-v1"_str),
    });
    auto dependencies = Vec<lito::cpp::DependencySpec>::make();
    dependencies.push(lito::cpp::DependencySpec {
        .target =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .visibility = lito::DependencyVisibility::Private,
    });
    auto targets = Vec<lito::cpp::ResolvedTarget>::make();
    targets.push(lito::cpp::ResolvedTarget {
        .id =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .artifact_kind         = lito::cpp::ArtifactKind::StaticLibrary,
        .artifact_name         = String::make("library"_str),
        .external_dependencies = rstd::move(external),
    });
    targets.push(lito::cpp::ResolvedTarget {
        .id =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Binary,
                .name    = String::make("app"_str),
            },
        .artifact_kind = lito::cpp::ArtifactKind::Executable,
        .artifact_name = String::make("app"_str),
        .dependencies  = rstd::move(dependencies),
    });
    auto default_targets = Vec<lito::PackageTargetId>::make();
    default_targets.push(targets[usize(1)].id.clone());
    auto profiles = Vec<lito::cpp::ProfileSpec>::make();
    profiles.push(default_profile(parser));
    return Ok(lito::cpp::PackageMetadata {
        .name            = String::make("external-usage"_str),
        .default_profile = String::make("debug"_str),
        .default_targets = rstd::move(default_targets),
        .profiles        = rstd::move(profiles),
        .targets         = rstd::move(targets),
    });
}
} // namespace lito_test
