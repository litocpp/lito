module;

#include <rstd/macro.hpp>

export module lito.test.support.dependency;

import rstd;
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
import lito.test.base_support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto has_external_macro(const lito::CompileContext& context) -> bool {
    for (const auto& macro : context.cpp.preprocessor.macros) {
        if (macro.value.as_str() == "LITO_EXTERNAL_USAGE=1"_str) return true;
    }
    return false;
}

auto pkg_config_target() -> lito::TargetInfo {
    return lito::TargetInfo {
        .triple = String::make("x86_64-unknown-linux-gnu"_str),
        .architecture =
            lito::Architecture {
                .name = String::make("x86_64"_str),
            },
        .os     = String::make("linux"_str),
        .family = lito::TargetFamily::Unix,
    };
}

auto native_platform() -> lito::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::resolve_build_platform(
        lito::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        None());
    return rstd::move(resolved).unwrap();
}

auto explicit_platform(ref<str> target_triple) -> lito::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::resolve_build_platform(
        lito::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        Some(target_triple));
    return rstd::move(resolved).unwrap();
}

auto default_profile(const lito::CppArgumentParser& parser) -> lito::ProfileSpec {
    auto profile = lito::make_profile_spec(
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
                            const lito::ProfileSpec&                             profile,
                            const lito::BuildPlatform&                           platform,
                            const lito::CppArgumentParser&                       parser,
                            usize                                                jobs   = usize(1),
                            Vec<lito::ExternalAssetSet>*                         assets = nullptr)
    -> lito::DependencyResult<Vec<lito::ResolvedExternalDependency>> {
    auto environment = lito::ResolvedProcessEnvironment::resolve(lito::ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<lito::DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::ToolResolver(*environment);
    auto provider = fixture_cmake();
    auto tool     = resolver.resolve(provider.executable.as_path(), "CMake executable"_str);
    if (tool.is_err()) {
        return Err(rstd::into<lito::DependencyError>(rstd::move(tool).unwrap_err()));
    }
    provider.executable = rstd::move(tool).unwrap().executable;
    auto identified     = lito::identify_cmake_provider(rstd::move(provider), *environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    provider       = rstd::move(identified).unwrap();
    auto result    = Vec<lito::ResolvedExternalDependency>::make();
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

auto external_usage_metadata(lito::DependencyVisibility     visibility,
                             const lito::CppArgumentParser& parser)
    -> lito::PackageResult<lito::PackageMetadata> {
    auto raw       = strings("-DLITO_EXTERNAL_USAGE=1"_str);
    auto arguments = parser.parse(raw, "pkg-config test fixture"_str);
    if (arguments.is_err()) {
        return Err(lito::PackageError::Message(
            rstd::format("pkg-config test fixture compiler arguments are invalid: {}",
                         rstd::move(arguments).unwrap_err())));
    }
    auto external         = Vec<lito::ResolvedExternalDependency>::make();
    auto external_targets = Vec<lito::ResolvedExternalTargetUsage>::make();
    external_targets.push(lito::ResolvedExternalTargetUsage {
        .name              = String::make("lito-fixture"_str),
        .visibility        = visibility,
        .compile_arguments = rstd::move(arguments).unwrap(),
        .identity          = String::make("fixture-resolution-v1"_str),
    });
    external.push(lito::ResolvedExternalDependency {
        .alias    = String::make("fixture"_str),
        .provider = String::make("pkg-config"_str),
        .version  = String::make("2.3.4"_str),
        .targets  = rstd::move(external_targets),
        .link_arguments =
            lito::LinkArgumentSequence {
                .tokens   = strings("-llito_fixture"_str),
                .source   = String::make("pkg-config fixture"_str),
                .identity = String::make("fixture-link-v1"_str),
            },
        .identity = String::make("fixture-resolution-v1"_str),
    });
    auto dependencies = Vec<lito::DependencySpec>::make();
    dependencies.push(lito::DependencySpec {
        .target =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .visibility = lito::DependencyVisibility::Private,
    });
    auto targets = Vec<lito::ResolvedTarget>::make();
    targets.push(lito::ResolvedTarget {
        .id =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .artifact_kind         = lito::ArtifactKind::StaticLibrary,
        .artifact_name         = String::make("library"_str),
        .external_dependencies = rstd::move(external),
    });
    targets.push(lito::ResolvedTarget {
        .id =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Binary,
                .name    = String::make("app"_str),
            },
        .artifact_kind = lito::ArtifactKind::Executable,
        .artifact_name = String::make("app"_str),
        .dependencies  = rstd::move(dependencies),
    });
    auto default_targets = Vec<lito::PackageTargetId>::make();
    default_targets.push(targets[usize(1)].id.clone());
    auto profiles = Vec<lito::ProfileSpec>::make();
    profiles.push(default_profile(parser));
    return Ok(lito::PackageMetadata {
        .name            = String::make("external-usage"_str),
        .default_profile = String::make("debug"_str),
        .default_targets = rstd::move(default_targets),
        .profiles        = rstd::move(profiles),
        .targets         = rstd::move(targets),
    });
}
} // namespace lito_test
