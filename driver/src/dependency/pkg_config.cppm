module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.pkg_config;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.clang;
import lito.toolchain.cmake;
import lito.toolchain.pkg_config;
import :dependency.external_source;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

auto resolve_pkg_config_dependencies(
    const Vec<lito::dependency::PkgConfigExternalDependency>& pkg_config_declarations,
    const lito::dependency::PkgConfigProviderConfig&          pkg_config,
    ref<str>                                                  owner,
    const BuildPlatform&                                      platform,
    ToolResolver&                                             tool_resolver,
    const ResolvedProcessEnvironment&                         process_environment)
    -> lito::dependency::DependencyResult<Vec<cpp::ExternalDependencyUsage>> {
    auto result              = Vec<cpp::ExternalDependencyUsage>::make();
    auto environment         = CommandEnvironment {};
    auto provider_id         = String::make();
    auto resolved_pkg_config = pkg_config.clone();
    if (! pkg_config_declarations.is_empty()) {
        if (platform.effective_target.triple != platform.compiler_default.triple.as_str() &&
            ! pkg_config.target_configured) {
            return lito::dependency::dependency_failure<Vec<cpp::ExternalDependencyUsage>>(
                rstd::format(
                    "target '{}' requires explicit pkg-config executable, library-path, or "
                    "sysroot configuration",
                    platform.effective_target.triple.as_str()));
        }
        auto requested_provider = pkg_config.executable.is_empty()
                                      ? tool_resolver.tools().pkg_config.as_path()
                                      : pkg_config.executable.as_path();
        auto requirement_subject =
            rstd::format("{} ({})",
                         pkg_config_declarations[usize {}].alias.as_str(),
                         pkg_config_declarations[usize {}].requirement.module.as_str());
        const auto tool_requirement = external_dependency_tool_requirement(
            HostToolCapability::PkgConfigQuery, owner, requirement_subject.as_str());
        auto resolved =
            pkg_config.executable.is_empty()
                ? tool_resolver.require(Tool::PkgConfig, tool_requirement)
                : tool_resolver.resolve(requested_provider, "pkg-config executable"_str);
        if (resolved.is_err()) {
            const auto& declaration = pkg_config_declarations[usize {}];
            return Err(lito::dependency::DependencyError::Operation(
                rstd::format("pkg-config dependency '{}' module '{}' provider '{}' resolution",
                             declaration.alias.as_str(),
                             declaration.requirement.module.as_str(),
                             requested_provider),
                rstd::move(resolved).unwrap_err()));
        }
        resolved_pkg_config.executable = rstd::move(resolved).unwrap().executable;
        auto configured_environment =
            provider_environment(resolved_pkg_config, platform.compiler_default);
        if (configured_environment.is_err()) {
            return Err(rstd::move(configured_environment).unwrap_err());
        }
        environment   = rstd::move(configured_environment).unwrap();
        auto provider = provider_version(resolved_pkg_config,
                                         environment,
                                         process_environment,
                                         pkg_config_declarations[usize {}]);
        if (provider.is_err()) return Err(rstd::move(provider).unwrap_err());
        auto identity = provider_identity(
            resolved_pkg_config, platform.effective_target.triple.as_str(), provider->as_str());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        provider_id = rstd::move(identity).unwrap();
    }
    auto snapshots = rstd::collections::BTreeMap<String, PkgConfigSnapshot>::make();
    for (const auto& declaration : pkg_config_declarations) {
        const auto& requirement = declaration.requirement;
        auto        key         = provider_id.clone();
        key.push_str(module_spec(requirement).as_str());
        key.push_str(requirement.mode == lito::dependency::PkgConfigQueryMode::Static
                         ? "\nstatic"_str
                         : "\nshared"_str);
        auto cached   = snapshots.get(key.as_str());
        auto snapshot = PkgConfigSnapshot {};
        if (cached.is_some()) {
            snapshot = (**cached).clone();
        } else {
            auto version = query_pkg_config(resolved_pkg_config,
                                            requirement,
                                            declaration.alias.as_str(),
                                            "modversion"_str,
                                            environment,
                                            process_environment);
            if (version.is_err()) return Err(rstd::move(version).unwrap_err());
            auto normalized_version = String::make(version->as_str().trim_ascii());
            if (normalized_version.is_empty()) {
                return lito::dependency::dependency_failure<Vec<cpp::ExternalDependencyUsage>>(
                    rstd::format("pkg-config dependency '{}' module '{}' returned an empty version",
                                 declaration.alias.as_str(),
                                 requirement.module.as_str()));
            }
            auto cflags = query_pkg_config(resolved_pkg_config,
                                           requirement,
                                           declaration.alias.as_str(),
                                           "cflags"_str,
                                           environment,
                                           process_environment);
            if (cflags.is_err()) return Err(rstd::move(cflags).unwrap_err());
            auto libs = query_pkg_config(resolved_pkg_config,
                                         requirement,
                                         declaration.alias.as_str(),
                                         "libs"_str,
                                         environment,
                                         process_environment);
            if (libs.is_err()) return Err(rstd::move(libs).unwrap_err());
            auto compile_tokens = tokenize_pkg_config_fragments(cflags->as_str());
            if (compile_tokens.is_err()) return Err(rstd::move(compile_tokens).unwrap_err());
            auto link_tokens = tokenize_pkg_config_fragments(libs->as_str());
            if (link_tokens.is_err()) return Err(rstd::move(link_tokens).unwrap_err());
            auto source   = rstd::format("pkg-config dependency '{}' module '{}'",
                                         declaration.alias.as_str(),
                                         requirement.module.as_str());
            auto identity = snapshot_identity(provider_id.as_str(),
                                              requirement,
                                              normalized_version.as_str(),
                                              *compile_tokens,
                                              *link_tokens);
            snapshot      = PkgConfigSnapshot {
                .module          = requirement.module.clone(),
                .version         = rstd::move(normalized_version),
                .compile_options = rstd::move(compile_tokens).unwrap(),
                .compile_source  = source.clone(),
                .link_arguments =
                    lito::link::ArgumentSequence {
                        .tokens   = rstd::move(link_tokens).unwrap(),
                        .source   = source.clone(),
                        .identity = identity.clone(),
                    },
                .identity = rstd::move(identity),
            };
            snapshots.insert(rstd::move(key), snapshot.clone());
        }
        auto targets = Vec<cpp::ExternalTargetUsage>::make();
        targets.push(cpp::ExternalTargetUsage {
            .name            = snapshot.module.clone(),
            .visibility      = declaration.visibility,
            .compile_options = as<Clone>(snapshot.compile_options).clone(),
            .compile_source  = snapshot.compile_source.clone(),
            .identity        = snapshot.identity.clone(),
        });
        auto normalized = normalize_clang_link_arguments(snapshot.link_arguments.clone());
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
    ToolResolver&                     tool_resolver,
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
    auto resolver = ToolResolver(*environment);
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
