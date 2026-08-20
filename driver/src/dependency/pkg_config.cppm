module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.pkg_config;

import rstd;
import lito.tools;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.clang;
import :dependency.external_source;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

auto pkg_config_error(ref<str> context, lito::tools::ToolError error)
    -> lito::dependency::DependencyError {
    return lito::dependency::DependencyError::Provider(
        String::make(context), Box<dyn<rstd::error::Error>>::make(rstd::move(error)));
}

auto pkg_config_version_operator(lito::dependency::PkgConfigVersionOperator value)
    -> lito::tools::pkg_config::VersionOperator {
    using Source = lito::dependency::PkgConfigVersionOperator;
    using Target = lito::tools::pkg_config::VersionOperator;
    switch (value) {
    case Source::Equal: return Target::Equal;
    case Source::Less: return Target::Less;
    case Source::Greater: return Target::Greater;
    case Source::LessEqual: return Target::LessEqual;
    case Source::GreaterEqual: return Target::GreaterEqual;
    }
    return Target::Equal;
}

auto pkg_config_request(const lito::dependency::PkgConfigExternalDependency& declaration)
    -> lito::tools::pkg_config::Request {
    auto version = Option<lito::tools::pkg_config::VersionRequirement> {};
    if (declaration.requirement.version.is_some()) {
        version = Some(lito::tools::pkg_config::VersionRequirement {
            .comparison = pkg_config_version_operator(declaration.requirement.version->comparison),
            .value      = declaration.requirement.version->value.clone(),
        });
    }
    return lito::tools::pkg_config::Request {
        .alias   = declaration.alias.clone(),
        .module  = declaration.requirement.module.clone(),
        .version = rstd::move(version),
        .mode    = declaration.requirement.mode == lito::dependency::PkgConfigQueryMode::Static
                       ? lito::tools::pkg_config::QueryMode::Static
                       : lito::tools::pkg_config::QueryMode::Shared,
    };
}

auto pkg_config_provider(const lito::dependency::PkgConfigProviderConfig& config,
                         PathBuf                                          executable,
                         const BuildPlatform& platform) -> lito::tools::pkg_config::Provider {
    auto sysroot = config.sysroot.is_some() ? Some(config.sysroot->clone()) : Option<PathBuf> {};
    return lito::tools::pkg_config::Provider {
        .executable    = rstd::move(executable),
        .search_paths  = as<Clone>(config.search_paths).clone(),
        .library_paths = as<Clone>(config.library_paths).clone(),
        .sysroot       = rstd::move(sysroot),
        .path_separator =
            platform.compiler_default.family == TargetFamily::Windows ? u8(';') : u8(':'),
        .effective_target = platform.effective_target.triple.clone(),
    };
}

} // namespace lito

export namespace lito
{

auto resolve_pkg_config_dependencies(
    const Vec<lito::dependency::PkgConfigExternalDependency>& declarations,
    const lito::dependency::PkgConfigProviderConfig&          config,
    ref<str>                                                  owner,
    const BuildPlatform&                                      platform,
    lito::tools::ToolResolver&                                tool_resolver,
    const ResolvedProcessEnvironment&                         process_environment)
    -> lito::dependency::DependencyResult<Vec<cpp::ExternalDependencyUsage>> {
    auto result = Vec<cpp::ExternalDependencyUsage>::make();
    if (declarations.is_empty()) return Ok(rstd::move(result));
    if (platform.effective_target.triple != platform.compiler_default.triple.as_str() &&
        ! config.target_configured) {
        return lito::dependency::dependency_failure<Vec<cpp::ExternalDependencyUsage>>(
            rstd::format("target '{}' requires explicit pkg-config executable, library-path, or "
                         "sysroot configuration",
                         platform.effective_target.triple.as_str()));
    }
    auto       requested = config.executable.is_empty() ? tool_resolver.tools().pkg_config.as_path()
                                                        : config.executable.as_path();
    auto       subject   = rstd::format("{} ({})",
                                        declarations[usize {}].alias.as_str(),
                                        declarations[usize {}].requirement.module.as_str());
    const auto requirement = lito::tools::external_dependency_tool_requirement(
        lito::tools::HostToolCapability::PkgConfigQuery, owner, subject.as_str());
    auto resolved = config.executable.is_empty()
                        ? tool_resolver.require(lito::tools::Tool::PkgConfig, requirement)
                        : tool_resolver.resolve(requested, "pkg-config executable"_str);
    if (resolved.is_err()) {
        return Err(pkg_config_error(
            rstd::format("resolve pkg-config provider for dependency '{}' module '{}'",
                         declarations[usize {}].alias.as_str(),
                         declarations[usize {}].requirement.module.as_str())
                .as_str(),
            rstd::move(resolved).unwrap_err()));
    }
    auto provider = pkg_config_provider(config, rstd::move(resolved).unwrap().executable, platform);
    auto snapshots = rstd::collections::BTreeMap<String, lito::tools::pkg_config::Snapshot>::make();
    for (const auto& declaration : declarations) {
        auto request = pkg_config_request(declaration);
        auto key     = lito::tools::pkg_config::module_spec(request);
        key.push_str(request.mode == lito::tools::pkg_config::QueryMode::Static ? "\nstatic"_str
                                                                                : "\nshared"_str);
        auto cached   = snapshots.get(key.as_str());
        auto snapshot = lito::tools::pkg_config::Snapshot {};
        if (cached.is_some()) {
            snapshot = (**cached).clone();
        } else {
            auto queried = lito::tools::pkg_config::query(provider, request, process_environment);
            if (queried.is_err()) {
                return Err(pkg_config_error(
                    rstd::format("query pkg-config dependency '{}'", declaration.alias.as_str())
                        .as_str(),
                    rstd::move(queried).unwrap_err()));
            }
            snapshot = rstd::move(queried).unwrap();
            snapshots.insert(rstd::move(key), snapshot.clone());
        }
        auto source  = rstd::format("pkg-config dependency '{}' module '{}'",
                                    declaration.alias.as_str(),
                                    snapshot.module.as_str());
        auto targets = Vec<cpp::ExternalTargetUsage>::make();
        targets.push(cpp::ExternalTargetUsage {
            .name            = snapshot.module.clone(),
            .visibility      = declaration.visibility,
            .compile_options = as<Clone>(snapshot.compile_fragments).clone(),
            .compile_source  = source.clone(),
            .identity        = snapshot.identity.clone(),
        });
        auto link_arguments = lito::link::ArgumentSequence {
            .tokens   = as<Clone>(snapshot.link_fragments).clone(),
            .source   = rstd::move(source),
            .identity = snapshot.identity.clone(),
        };
        auto normalized = normalize_clang_link_arguments(rstd::move(link_arguments));
        if (normalized.is_err()) {
            return lito::dependency::dependency_failure<Vec<cpp::ExternalDependencyUsage>>(
                rstd::format("{}", rstd::move(normalized).unwrap_err()));
        }
        result.push(cpp::ExternalDependencyUsage {
            .alias             = declaration.alias.clone(),
            .provider          = String::make("pkg-config"_str),
            .version           = snapshot.version.clone(),
            .targets           = rstd::move(targets),
            .link_arguments    = rstd::move(normalized->arguments),
            .link_requirements = rstd::move(normalized->requirements),
            .identity          = snapshot.identity.clone(),
        });
    }
    return Ok(rstd::move(result));
}

auto resolve_external_dependencies(
    const Vec<lito::dependency::PkgConfigExternalDependency>& declarations,
    const lito::dependency::PkgConfigProviderConfig&          pkg_config,
    const lito::dependency::CMakeProviderConfig&,
    const cpp::BuildConfiguration&,
    const cpp::ProfileSpec&,
    const BuildPlatform&              platform,
    lito::tools::ToolResolver&        tool_resolver,
    const ResolvedProcessEnvironment& process_environment)
    -> lito::dependency::DependencyResult<Vec<cpp::ExternalDependencyUsage>> {
    return resolve_pkg_config_dependencies(declarations,
                                           pkg_config,
                                           "selected package"_str,
                                           platform,
                                           tool_resolver,
                                           process_environment);
}

auto resolve_external_dependencies(
    const Vec<lito::dependency::PkgConfigExternalDependency>& declarations,
    const lito::dependency::PkgConfigProviderConfig&          pkg_config,
    const lito::dependency::CMakeProviderConfig&              cmake_config,
    const cpp::BuildConfiguration&                            configuration,
    const cpp::ProfileSpec&                                   profile,
    const BuildPlatform&                                      platform)
    -> lito::dependency::DependencyResult<Vec<cpp::ExternalDependencyUsage>> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(
            rstd::into<lito::dependency::DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::tools::ToolResolver(*environment);
    return resolve_external_dependencies(declarations,
                                         pkg_config,
                                         cmake_config,
                                         configuration,
                                         profile,
                                         platform,
                                         resolver,
                                         *environment);
}

} // namespace lito
