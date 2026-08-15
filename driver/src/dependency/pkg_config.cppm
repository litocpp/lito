module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.pkg_config;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain.pkg_config;
import :dependency.external_source;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

auto resolve_pkg_config_dependencies(
    const Vec<PkgConfigExternalDependency>& pkg_config_declarations,
    const PkgConfigProviderConfig&          pkg_config,
    const BuildPlatform&                    platform,
    const cpp::CppArgumentParser&           parser,
    ToolResolver&                           tool_resolver,
    const ResolvedProcessEnvironment&       process_environment)
    -> DependencyResult<Vec<cpp::ResolvedExternalDependency>> {
    auto result              = Vec<cpp::ResolvedExternalDependency>::make();
    auto environment         = CommandEnvironment {};
    auto provider_id         = String::make();
    auto resolved_pkg_config = pkg_config.clone();
    if (! pkg_config_declarations.is_empty()) {
        if (platform.effective_target.triple != platform.compiler_default.triple.as_str() &&
            ! pkg_config.target_configured) {
            return dependency_failure<Vec<cpp::ResolvedExternalDependency>>(rstd::format(
                "target '{}' requires explicit pkg-config executable, library-path, or "
                "sysroot configuration",
                platform.effective_target.triple.as_str()));
        }
        auto resolved =
            tool_resolver.resolve(pkg_config.executable.as_path(), "pkg-config executable"_str);
        if (resolved.is_err()) {
            const auto& declaration = pkg_config_declarations[usize {}];
            return Err(DependencyError::Operation(
                rstd::format("pkg-config dependency '{}' module '{}' provider '{}' resolution",
                             declaration.alias.as_str(),
                             declaration.requirement.module.as_str(),
                             pkg_config.executable.as_path()),
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
        key.push_str(requirement.mode == PkgConfigQueryMode::Static ? "\nstatic"_str
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
                return dependency_failure<Vec<cpp::ResolvedExternalDependency>>(
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
            auto source  = rstd::format("pkg-config dependency '{}' module '{}'",
                                        declaration.alias.as_str(),
                                        requirement.module.as_str());
            auto compile = parser.parse(*compile_tokens, source.as_str());
            if (compile.is_err()) {
                return Err(DependencyError::Configuration(
                    source.clone(), erase_error(rstd::move(compile).unwrap_err())));
            }
            auto identity = snapshot_identity(provider_id.as_str(),
                                              requirement,
                                              normalized_version.as_str(),
                                              *compile_tokens,
                                              *link_tokens);
            snapshot      = PkgConfigSnapshot {
                .module            = requirement.module.clone(),
                .version           = rstd::move(normalized_version),
                .compile_arguments = rstd::move(compile).unwrap(),
                .link_arguments =
                    cpp::LinkArgumentSequence {
                        .tokens   = rstd::move(link_tokens).unwrap(),
                        .source   = source.clone(),
                        .identity = identity.clone(),
                    },
                .identity = rstd::move(identity),
            };
            snapshots.insert(rstd::move(key), snapshot.clone());
        }
        auto targets = Vec<cpp::ResolvedExternalTargetUsage>::make();
        targets.push(cpp::ResolvedExternalTargetUsage {
            .name              = snapshot.module.clone(),
            .visibility        = declaration.visibility,
            .compile_arguments = as<Clone>(snapshot.compile_arguments).clone(),
            .identity          = snapshot.identity.clone(),
        });
        result.push(cpp::ResolvedExternalDependency {
            .alias          = declaration.alias.clone(),
            .provider       = String::make("pkg-config"_str),
            .version        = snapshot.version.clone(),
            .targets        = rstd::move(targets),
            .link_arguments = snapshot.link_arguments.clone(),
            .identity       = snapshot.identity.clone(),
        });
    }
    return Ok(rstd::move(result));
}

auto resolve_external_dependencies(const Vec<PkgConfigExternalDependency>& declarations,
                                   const PkgConfigProviderConfig&          pkg_config,
                                   const CMakeProviderConfig&,
                                   const cpp::BuildConfiguration&,
                                   const cpp::ProfileSpec&,
                                   const BuildPlatform&              platform,
                                   const cpp::CppArgumentParser&     parser,
                                   ToolResolver&                     tool_resolver,
                                   const ResolvedProcessEnvironment& process_environment)
    -> DependencyResult<Vec<cpp::ResolvedExternalDependency>> {
    return resolve_pkg_config_dependencies(
        declarations, pkg_config, platform, parser, tool_resolver, process_environment);
}

auto resolve_external_dependencies(const Vec<PkgConfigExternalDependency>& declarations,
                                   const PkgConfigProviderConfig&          pkg_config,
                                   const CMakeProviderConfig&              cmake_config,
                                   const cpp::BuildConfiguration&          configuration,
                                   const cpp::ProfileSpec&                 profile,
                                   const BuildPlatform&                    platform,
                                   const cpp::CppArgumentParser&           parser)
    -> DependencyResult<Vec<cpp::ResolvedExternalDependency>> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = ToolResolver(*environment);
    return resolve_external_dependencies(declarations,
                                         pkg_config,
                                         cmake_config,
                                         configuration,
                                         profile,
                                         platform,
                                         parser,
                                         resolver,
                                         *environment);
}

} // namespace lito
