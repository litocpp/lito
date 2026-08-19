module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import :build;
import :build.error;
import :build.event;
import :build.setup_report;
import :build.prepared_project;
import lito.cpp;
import :install;
import :install.package;
import :project;
import :project.error;
import lito.system;
import lito.toolchain;

using namespace rstd::prelude;
using namespace lito::system;
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

template<typename T>
auto install_project_failure(ProjectResult<T> result) -> InstallResult<T> {
    if (result.is_ok()) return Ok(rstd::move(result).unwrap());
    return Err(InstallError::Build(BuildError::Project(rstd::move(result).unwrap_err())));
}

struct InstallStripContext {
    const LlvmStrip*              provider {};
    const Option<BuildEventSink>* observer {};
};

auto apply_install_strip(void* raw_context, const InstallStripRequest& request)
    -> ToolchainResult<rstd::time::Duration> {
    auto& context = *static_cast<InstallStripContext*>(raw_context);
    auto  target  = String::make(request.package);
    if (request.origin != nullptr && request.origin->is_ExternalAsset()) {
        const auto& external = request.origin->as_ExternalAsset();
        target.push_str("::"_str);
        target.push_str(external.dependency.as_str());
        target.push_ascii(':');
        target.push_str(external.set.as_str());
        target.push_ascii('/');
        target.push_str(external.path.as_path().to_string_lossy().as_str());
    }
    if (context.observer != nullptr && context.observer->is_some()) {
        const auto& observer = **context.observer;
        if (observer.notify != nullptr) {
            observer.notify(
                observer.context,
                BuildEvent { BuildEventKind::Strip, target.as_str(), request.destination });
        }
    }
    return context.provider->strip_in_place(
        request.staged, request.mode, request.working_directory);
}

} // namespace lito

namespace lito
{

auto install(InstallRequest request) -> InstallResult<InstallSummary> {
    request.build.selection.root = request.source.project.root.clone();
    request.build.purpose        = lito::package::PackageSelectionPurpose::Install;
    if (request.build.profile.is_none()) {
        request.build.profile =
            Some(lito::manifest::BuildProfileName { .value = String::make("release"_str) });
    }
    if (! request.build.targets.is_empty()) {
        return install_failure<InstallSummary>(
            "install build targets must be selected through install binaries"_str);
    }
    auto environment = ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err()) {
        return install_failure<InstallSummary>(
            rstd::format("cannot resolve install environment: {}", environment.unwrap_err()));
    }
    auto resolver =
        ToolResolver(*environment, request.build.tools.clone(), request.build.tool_reporter);
    auto toolchain =
        ClangToolchain::create(request.build.configuration.toolchain, resolver, *environment);
    if (toolchain.is_err()) {
        return install_failure<InstallSummary>(
            rstd::format("cannot resolve install target: {}", toolchain.unwrap_err()));
    }
    auto resolved_toolchain = rstd::move(toolchain).unwrap();
    auto jobs =
        request.build.execution.scan.jobs.is_some() ? *request.build.execution.scan.jobs : usize(1);
    if (request.build.execution.scan.jobs.is_none()) {
        auto available = rstd::thread::available_parallelism();
        if (available.is_ok()) jobs = available->get();
    }
    auto session          = rstd_try(install_project_failure(
        resolve_project_session(request.build.selection,
                                request.build.configuration,
                                request.build.sources,
                                request.build.lock,
                                resolved_toolchain,
                                resolver,
                                *environment,
                                request.build.cmake_build_overrides,
                                request.build.locked,
                                lito::package::PackageSelectionPurpose::Install,
                                jobs,
                                request.build.observer,
                                request.build.setup_reporter,
                                Some(rstd::move(request.source.project.catalog)))));
    auto effective_target = session.platform.effective_target.clone();
    auto selected_owners =
        rstd_try(resolve_install_packages(session.project.selection, effective_target));
    auto profile = request.build.profile->as_str();
    auto recipes = Vec<InstallRecipe>::make();
    for (const auto& owner : selected_owners) {
        if (owner.direct && owner.script.is_some() && ! request.binaries.is_empty()) {
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
            .source  = owner.source.clone(),
        };
        for (const auto& dependency : owner.runtime_dependencies) {
            recipe.runtime_dependencies.push(InstallRuntimeDependency {
                .name            = dependency.name.clone(),
                .source_identity = dependency.source_identity.clone(),
            });
        }
        for (const auto& target : owner.binaries) {
            if (owner.direct && ! request.binaries.is_empty()) {
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
    auto requirements = rstd_try(
        resolve_install_build_requirements(session.project.selection, recipes, effective_target));
    for (const auto& target : requirements.targets) {
        request.build.exact_targets.push(target.clone());
    }
    auto profile_name = request.build.profile->clone();
    auto prepared     = rstd_try(
        install_project_failure(prepare_resolved_build_project(rstd::move(session),
                                                               request.build.configuration,
                                                               profile_name,
                                                               request.build.output.as_path(),
                                                               request.build.sources,
                                                               request.build.pkg_config,
                                                               request.build.cmake,
                                                               rstd::move(resolved_toolchain),
                                                               resolver,
                                                               *environment,
                                                               jobs,
                                                               request.build.observer)));
    rstd_try(resolve_install_artifact_link_variants(requirements, prepared.external_assets));
    for (const auto& variant : requirements.artifact_link_variants) {
        request.build.artifact_link_variants.push(variant.clone());
    }
    auto summary =
        rstd_try(build_prepared_project(request.build, *environment, rstd::move(prepared)));
    auto plan              = rstd_try(materialize_install_plan(rstd::move(recipes),
                                                               requirements,
                                                               summary,
                                                               summary.profile.as_str(),
                                                               summary.target.as_str()));
    auto strip_requirement = Option<HostToolRequirement> {};
    for (const auto& package : plan.packages) {
        for (const auto& entry : package.entries) {
            if (entry.transforms.is_empty() || strip_requirement.is_some()) continue;
            strip_requirement = Some(install_entry_tool_requirement(
                HostToolCapability::ArtifactStripping,
                package.name.as_str(),
                entry.relative_destination.as_path().to_string_lossy().as_str()));
        }
    }
    auto strip_provider = Option<LlvmStrip> {};
    if (strip_requirement.is_some()) {
        auto resolved_strip = resolver.require(Tool::Strip, *strip_requirement);
        if (resolved_strip.is_err()) {
            return install_failure<InstallSummary>(
                rstd::format("cannot resolve LLVM strip executable: {}",
                             rstd::move(resolved_strip).unwrap_err()));
        }
        strip_provider =
            Some(LlvmStrip(rstd::move(resolved_strip).unwrap().executable, *environment));
    }
    auto strip_context = InstallStripContext {
        .provider = strip_provider.is_some() ? rstd::addressof(*strip_provider) : nullptr,
        .observer = rstd::addressof(request.build.observer),
    };
    auto strip_executor = Option<InstallStripExecutor> {};
    if (strip_requirement.is_some()) {
        strip_executor = Some(InstallStripExecutor {
            .context = rstd::addressof(strip_context),
            .apply   = apply_install_strip,
        });
    }
    auto stored = rstd_try(install_artifacts(InstallStoreRequest {
        .destination = rstd::move(request.destination),
        .packages    = rstd::move(plan.packages),
        .strip       = rstd::move(strip_executor),
        .force       = request.force,
    }));
    return Ok(InstallSummary {
        .build       = rstd::move(summary),
        .destination = rstd::move(stored.destination),
        .packages    = rstd::move(stored.packages),
        .binaries    = rstd::move(stored.binaries),
        .entries     = rstd::move(stored.entries),
        .links       = rstd::move(stored.links),
    });
}

} // namespace lito
