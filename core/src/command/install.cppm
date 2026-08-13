module;
#include <rstd/macro.hpp>

export module lito.command.install;

import rstd;
import lito.error;
import lito.build;
import lito.build.profile;
import lito.cpp;
import lito.install;
import lito.package.identity;
import lito.package.target_contract;
import lito.workspace.contract;
import lito.build.profile_contract;
import lito.platform;
import lito.toolchain;
import lito.system.environment;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto install_failure(String message) -> InstallResult<T> {
    return Err(InstallError::Message(rstd::move(message)));
}

template<typename T>
auto install_failure(ref<str> message) -> InstallResult<T> {
    return Err(InstallError::Message(String::make(message)));
}

} // namespace lito

export namespace lito
{

auto install(InstallRequest request) -> InstallResult<InstallSummary>;

auto install(InstallRequest request) -> InstallResult<InstallSummary> {
    request.build.selection.root = request.source.project.root.clone();
    request.build.purpose        = PackageSelectionPurpose::Install;
    if (request.build.profile.is_none()) {
        request.build.profile = Some(BuildProfileName { .value = String::make("release"_str) });
    }
    if (! request.build.targets.is_empty()) {
        return install_failure<InstallSummary>(
            "install build targets must be selected through install binaries"_str);
    }
    auto owners = request.source.project.catalog.install_packages(
        request.build.selection.packages);
    if (owners.is_err()) {
        return Err(InstallError::Selection(rstd::move(owners).unwrap_err()));
    }
    auto selected_owners = rstd::move(owners).unwrap();
    auto environment = ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err()) {
        return install_failure<InstallSummary>(
            rstd::format("cannot resolve install environment: {}", environment.unwrap_err()));
    }
    auto resolver = ToolResolver(*environment);
    auto toolchain = ClangToolchain::create(
        request.build.configuration.toolchain, resolver, *environment);
    if (toolchain.is_err()) {
        return install_failure<InstallSummary>(
            rstd::format("cannot resolve install target: {}", toolchain.unwrap_err()));
    }
    auto build_arguments = parse_build_arguments(
        request.build.configuration, toolchain->argument_parser());
    if (build_arguments.is_err()) {
        return install_failure<InstallSummary>(rstd::format(
            "cannot resolve install target: {}", rstd::move(build_arguments).unwrap_err()));
    }
    auto host = detect_host_info();
    if (host.is_err()) {
        return install_failure<InstallSummary>(rstd::format(
            "cannot resolve install target: {}", rstd::move(host).unwrap_err()));
    }
    auto platform = resolve_build_platform(
        *host, toolchain->target_info(), explicit_cpp_target(*build_arguments));
    if (platform.is_err()) {
        return install_failure<InstallSummary>(rstd::format(
            "cannot resolve install target: {}", rstd::move(platform).unwrap_err()));
    }
    auto effective_target = rstd::move(platform).unwrap().effective_target;
    auto profile = request.build.profile->as_str();
    auto recipes = Vec<InstallRecipe>::make();
    for (const auto& owner : selected_owners) {
        if (owner.script.is_some() && ! request.binaries.is_empty()) {
            return install_failure<InstallSummary>(rstd::format(
                "--bin cannot filter install recipe package '{}'", owner.name.as_str()));
        }
        if (owner.script.is_some()) {
            recipes.push(rstd_try(execute_install_script(
                owner,
                InstallScriptContext {
                    .profile     = String::make(profile),
                    .target      = effective_target.triple.clone(),
                    .target_arch = effective_target.architecture.name.clone(),
                })));
            continue;
        }
        auto recipe = InstallRecipe {
            .owner   = owner.name.clone(),
            .version = owner.version.clone(),
            .root    = owner.root.clone(),
        };
        for (const auto& target : owner.binaries) {
            if (! request.binaries.is_empty()) {
                auto matched = false;
                for (const auto& requested : request.binaries) {
                    if (requested == target.target.name.as_str()) matched = true;
                }
                if (! matched) continue;
            }
            auto destination = PathBuf::from("bin"_str);
            destination.push(PathBuf::from(target.artifact_name.as_str()).as_path());
            recipe.artifacts.push(InstallArtifactRecipe {
                .target      = target.target.clone(),
                .destination = rstd::move(destination),
            });
        }
        if (recipe.artifacts.is_empty()) {
            return install_failure<InstallSummary>(rstd::format(
                "package '{}' has no selected installable binaries", owner.name.as_str()));
        }
        recipes.push(rstd::move(recipe));
    }
    auto requirements = request.source.project.catalog.install_build_requirements(
        recipes, effective_target);
    if (requirements.is_err()) {
        return Err(InstallError::Selection(rstd::move(requirements).unwrap_err()));
    }
    auto build_requirements          = rstd::move(requirements).unwrap();
    request.build.selection.packages = rstd::move(build_requirements.packages);
    request.build.exact_targets      = rstd::move(build_requirements.targets);
    auto summary = rstd_try(
        build_resolved_project(rstd::move(request.build), rstd::move(request.source.project)));
    auto plan = rstd_try(materialize_install_plan(rstd::move(recipes),
                                                  summary,
                                                  summary.profile.as_str(),
                                                  summary.target.as_str()));
    auto stored = rstd_try(install_artifacts(InstallStoreRequest {
        .root       = InstallRoot { .path = rstd::move(request.root.path) },
        .provenance = rstd::move(request.source.provenance),
        .packages   = rstd::move(plan.packages),
        .force      = request.force,
    }));
    return Ok(InstallSummary {
        .build    = rstd::move(summary),
        .root     = InstallRoot { .path = stored.layout.root.path.clone() },
        .packages = rstd::move(stored.packages),
        .binaries = rstd::move(stored.binaries),
        .entries  = rstd::move(stored.entries),
    });
}

} // namespace lito
