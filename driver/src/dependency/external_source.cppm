module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.external_source;

export import :dependency.preparation;

import rstd;
import lito.core;
import lito.toolchain.cmake;
import :build.event;
import :build.source_observer;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

struct AcquiredExternalDependencySource {
    usize                  package {};
    usize                  declaration {};
    Option<AcquiredSource> acquired;
};

struct AcquiredExternalDependencySources {
    Vec<AcquiredExternalDependencySource> dependencies;
};

auto acquire_external_dependency_sources(ResolvedPackageGraph&             graph,
                                         const Vec<String>&                selected_packages,
                                         SourceResolutionOptions           options,
                                         ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment& environment,
                                         usize                             jobs,
                                         SourceEventSink                   observer)
    -> DependencyResult<AcquiredExternalDependencySources>;

struct ExternalSourceTask {
    usize                      package {};
    usize                      declaration {};
    CMakeDependencyRequirement requirement;
    PathBuf                    source_root;
    Option<AcquiredSource>     acquired;
};

struct PreparedExternalSourceTask {
    usize                              package {};
    usize                              declaration {};
    PreparedCMakeDependencyRequirement requirement;
};

auto prepare_external_source_task(ExternalSourceTask task)
    -> DependencyResult<PreparedExternalSourceTask> {
    auto        source      = PreparedCMakeDependencySource::Installed();
    const auto& declaration = task.requirement;
    if (declaration.source.is_Archive()) {
        source =
            PreparedCMakeDependencySource::Archive(declaration.source.as_Archive().url.clone(),
                                                   declaration.source.as_Archive().sha256.clone());
    } else if (declaration.source.is_ArchitectureArchives()) {
        auto variants = Vec<CMakeArchiveVariant>::with_capacity(
            declaration.source.as_ArchitectureArchives().variants.len());
        for (const auto& variant : declaration.source.as_ArchitectureArchives().variants) {
            variants.push(CMakeArchiveVariant {
                .architecture = variant.architecture.clone(),
                .url          = variant.url.clone(),
                .sha256       = variant.sha256.clone(),
            });
        }
        source = PreparedCMakeDependencySource::ArchitectureArchives(rstd::move(variants));
    } else if (! declaration.source.is_Installed()) {
        if (task.acquired.is_none()) {
            return dependency_failure<PreparedExternalSourceTask>(
                rstd::format("external source for CMake dependency '{}' was not fetched",
                             declaration.alias.as_str()));
        }
        auto value = rstd::move(task.acquired).unwrap();
        source     = PreparedCMakeDependencySource::Directory(
            rstd::move(value.root), rstd::move(value.identity), value.cacheable);
    }

    auto cache = Vec<CMakeCacheEntry>::with_capacity(declaration.cache.len());
    for (const auto& entry : declaration.cache) {
        cache.push(CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets = Vec<CMakeTargetRequirement>::with_capacity(declaration.targets.len());
    for (const auto& target : declaration.targets) {
        targets.push(CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto config_directory = Option<PathBuf> {};
    if (declaration.config_directory.is_some()) {
        config_directory = Some(declaration.config_directory->clone());
    }
    auto adapter          = Option<PathBuf> {};
    auto adapter_identity = String::make();
    if (declaration.adapter.is_some()) {
        auto adapter_root = task.source_root.as_path();
        if (declaration.adapter_root.is_some()) adapter_root = declaration.adapter_root->as_path();
        auto path      = PathBuf::from(adapter_root).join(declaration.adapter->as_path());
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return Err(DependencyError::Io(String::make("resolve CMake adapter"_str),
                                           rstd::move(path),
                                           rstd::move(canonical).unwrap_err()));
        }
        if (! canonical->as_path().starts_with(adapter_root)) {
            return dependency_failure<PreparedExternalSourceTask>(
                rstd::format("CMake adapter '{}' escapes declaration root '{}'",
                             canonical->as_path(),
                             adapter_root));
        }
        auto contents = rstd::fs::read_to_string(canonical->as_path());
        if (contents.is_err()) {
            return Err(DependencyError::Io(String::make("read CMake adapter"_str),
                                           canonical->clone(),
                                           rstd::move(contents).unwrap_err()));
        }
        adapter_identity = rstd::format("{}\n{}", canonical->as_path(), contents->as_str());
        adapter          = Some(rstd::move(canonical).unwrap());
    }
    return Ok(PreparedExternalSourceTask {
        .package     = task.package,
        .declaration = task.declaration,
        .requirement =
            PreparedCMakeDependencyRequirement {
                .alias            = declaration.alias.clone(),
                .package          = declaration.package.clone(),
                .source           = rstd::move(source),
                .integration      = declaration.integration,
                .adapter          = rstd::move(adapter),
                .adapter_identity = rstd::move(adapter_identity),
                .config_directory = rstd::move(config_directory),
                .cache            = rstd::move(cache),
                .targets          = rstd::move(targets),
            },
    });
}

} // namespace lito

export namespace lito
{

auto tokenize_pkg_config_fragments(ref<str> input) -> DependencyResult<Vec<String>> {
    auto tokens = tokenize_command_fragments(input, "pkg-config output"_str);
    if (tokens.is_err()) {
        return Err(rstd::into<DependencyError>(rstd::move(tokens).unwrap_err()));
    }
    return Ok(rstd::move(tokens).unwrap());
}

auto prepare_external_dependency_sources(ResolvedPackageGraph&             graph,
                                         const Vec<String>&                selected_packages,
                                         SourceResolutionOptions           options,
                                         ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment& environment,
                                         usize                             jobs     = usize(1),
                                         BuildEventSink                    observer = {})
    -> DependencyResult<PreparedExternalDependencySources> {
    auto acquired = acquire_external_dependency_sources(graph,
                                                        selected_packages,
                                                        rstd::move(options),
                                                        resolver,
                                                        environment,
                                                        jobs,
                                                        source_observer(observer));
    if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());

    auto tasks = Vec<ExternalSourceTask>::with_capacity(acquired->dependencies.len());
    for (auto& source : acquired->dependencies) {
        auto& package     = graph.packages[source.package];
        auto& declaration = package.manifest.cmake_external_dependencies[source.declaration];
        tasks.push(ExternalSourceTask {
            .package     = source.package,
            .declaration = source.declaration,
            .requirement = declaration.clone(),
            .source_root = package.manifest.source_root.clone(),
            .acquired    = rstd::move(source.acquired),
        });
    }
    auto result = PreparedExternalDependencySources {};
    if (tasks.is_empty()) return Ok(rstd::move(result));

    auto worker_count = jobs < tasks.len() ? jobs : tasks.len();
    auto created =
        rstd::thread::BlockingTaskGroup<DependencyResult<PreparedExternalSourceTask>>::make(
            worker_count, tasks.len());
    if (created.is_err()) {
        return Err(DependencyError::System(
            SystemError::Io(String::make("create external source fetch executor"_str),
                            PathBuf::make(),
                            rstd::move(created).unwrap_err_unchecked())));
    }
    auto group = rstd::move(created).unwrap_unchecked();
    for (auto& task : tasks) {
        auto submitted = group.submit([task = rstd::move(task)]() mutable {
            return prepare_external_source_task(rstd::move(task));
        });
        if (submitted.is_err()) {
            return dependency_failure<PreparedExternalDependencySources>(
                "cannot submit external source fetch task"_str);
        }
    }
    auto outcomes = rstd::move(group).join();
    result.dependencies.reserve(outcomes.len());
    for (auto& outcome : outcomes) {
        auto value = rstd::move(outcome).into_value();
        if (value.is_none()) {
            return dependency_failure<PreparedExternalDependencySources>(
                "external source fetch task was cancelled"_str);
        }
        auto prepared = rstd::move(value).unwrap_unchecked();
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
        auto task = rstd::move(prepared).unwrap();
        result.dependencies.push(PreparedExternalDependency {
            .package     = task.package,
            .requirement = rstd::move(task.requirement),
        });
    }
    rstd::slice_::sort_unstable_by(
        result.dependencies.as_mut_slice().as_mut_ref(),
        [](const PreparedExternalDependency& left, const PreparedExternalDependency& right) {
            if (left.package != right.package) return left.package < right.package;
            if (left.requirement.alias != right.requirement.alias) {
                return left.requirement.alias < right.requirement.alias;
            }
            return left.requirement.package < right.requirement.package;
        });
    return Ok(rstd::move(result));
}

auto prepare_external_dependency_sources(ResolvedPackageGraph&             graph,
                                         SourceResolutionOptions           options,
                                         ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment& environment,
                                         usize                             jobs     = usize(1),
                                         BuildEventSink                    observer = {})
    -> DependencyResult<PreparedExternalDependencySources> {
    auto selected = Vec<String>::with_capacity(graph.packages.len());
    for (const auto& package : graph.packages) selected.push(package.manifest.name.clone());
    return prepare_external_dependency_sources(
        graph, selected, rstd::move(options), resolver, environment, jobs, observer);
}

auto prepare_external_dependency_sources(ResolvedPackageGraph&   graph,
                                         SourceResolutionOptions options,
                                         usize                   jobs     = usize(1),
                                         BuildEventSink          observer = {})
    -> DependencyResult<PreparedExternalDependencySources> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = ToolResolver(*environment);
    return prepare_external_dependency_sources(
        graph, rstd::move(options), resolver, *environment, jobs, observer);
}

auto resolve_cmake_requirement_for_platform(const PreparedCMakeDependencyRequirement& requirement,
                                            const BuildPlatform&                      platform)
    -> DependencyResult<ResolvedCMakeDependencyRequirement> {
    auto source = ResolvedCMakeDependencySource::Installed();
    if (requirement.source.is_Directory()) {
        source = ResolvedCMakeDependencySource::Directory(
            requirement.source.as_Directory().root.clone(),
            requirement.source.as_Directory().identity.clone(),
            true,
            requirement.source.as_Directory().cacheable);
    } else if (requirement.source.is_Archive()) {
        source =
            ResolvedCMakeDependencySource::Archive(requirement.source.as_Archive().url.clone(),
                                                   requirement.source.as_Archive().sha256.clone());
    } else if (requirement.source.is_ArchitectureArchives()) {
        const CMakeArchiveVariant* selected  = nullptr;
        auto                       available = String::make();
        for (const auto& variant : requirement.source.as_ArchitectureArchives().variants) {
            if (! available.is_empty()) available.push_str(", "_str);
            available.push_str(variant.architecture.as_str());
            if (variant.architecture == platform.effective_target.architecture) {
                selected = rstd::addressof(variant);
            }
        }
        if (selected == nullptr) {
            return dependency_failure<ResolvedCMakeDependencyRequirement>(rstd::format(
                "CMake dependency '{}' has no archive for target '{}' (architecture '{}'); "
                "available architectures: {}",
                requirement.alias.as_str(),
                platform.effective_target.triple.as_str(),
                platform.effective_target.architecture.as_str(),
                available.as_str()));
        }
        source =
            ResolvedCMakeDependencySource::Archive(selected->url.clone(), selected->sha256.clone());
    }
    auto adapter = Option<PathBuf> {};
    if (requirement.adapter.is_some()) adapter = Some(requirement.adapter->clone());
    auto config_directory = Option<PathBuf> {};
    if (requirement.config_directory.is_some()) {
        config_directory = Some(requirement.config_directory->clone());
    }
    auto cache = Vec<CMakeCacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets = Vec<CMakeTargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    return Ok(ResolvedCMakeDependencyRequirement {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .source           = rstd::move(source),
        .integration      = requirement.integration,
        .adapter          = rstd::move(adapter),
        .adapter_identity = requirement.adapter_identity.clone(),
        .config_directory = rstd::move(config_directory),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
    });
}

} // namespace lito
