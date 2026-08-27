module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.external_source;

export import :dependency.preparation;

import rstd;
import lito.tools;
import lito.core;
import :dependency.cmake;
import :build.event;
import :source;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

struct AcquiredExternalDependencySource {
    usize                                package {};
    usize                                declaration {};
    bool                                 installed_override { false };
    Option<lito::source::AcquiredSource> acquired;
};

struct AcquiredExternalDependencySources {
    Vec<AcquiredExternalDependencySource> cmake_dependencies;
    struct AcquiredPackageExternalSource {
        usize                                package {};
        usize                                declaration {};
        Option<lito::source::AcquiredSource> acquired;
    };
    Vec<AcquiredPackageExternalSource> sources;
};

auto resolve_declared_external_dependency_sources(lito::package::ResolvedPackageGraph&  graph,
                                                  lito::source::SourceResolutionOptions options,
                                                  lito::tools::ToolResolver&            resolver,
                                                  const ResolvedProcessEnvironment&     environment,
                                                  lito::source::SourceEventSink         observer)
    -> lito::dependency::DependencyResult<DeclaredExternalDependencySources>;

auto acquire_external_dependency_sources(lito::package::ResolvedPackageGraph& graph,
                                         const Vec<String>&                   selected_packages,
                                         DeclaredExternalDependencySources    declared,
                                         const lito::dependency::CMakeBuildOverrideSet& overrides,
                                         lito::tools::ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment&              environment,
                                         usize                                          jobs,
                                         lito::source::SourceEventSink                  observer)
    -> lito::dependency::DependencyResult<AcquiredExternalDependencySources>;

struct ExternalSourceTask {
    usize                                               package {};
    usize                                               declaration {};
    lito::dependency::CMakeDependencyRequirement        requirement;
    Option<lito::dependency::ExternalSourceRequirement> source;
    PathBuf                                             source_root;
    bool                                                installed_override { false };
    Option<lito::source::AcquiredSource>                acquired;
};

struct PreparedExternalSourceTask {
    usize                              package {};
    usize                              declaration {};
    bool                               installed_override { false };
    PreparedCMakeDependencyRequirement requirement;
};

auto prepare_external_source_task(ExternalSourceTask task)
    -> lito::dependency::DependencyResult<PreparedExternalSourceTask> {
    auto        source      = PreparedCMakeDependencySource::Find();
    const auto& declaration = task.requirement;
    if (! task.installed_override && task.source.is_some()) {
        const auto& source_declaration = *task.source;
        if (source_declaration.is_Archive()) {
            source = PreparedCMakeDependencySource::Archive(
                source_declaration.as_Archive().url.clone(),
                source_declaration.as_Archive().sha256.clone());
        } else if (source_declaration.is_ArchitectureArchives()) {
            auto variants = Vec<lito::dependency::ExternalArchiveVariant>::with_capacity(
                source_declaration.as_ArchitectureArchives().variants.len());
            for (const auto& variant : source_declaration.as_ArchitectureArchives().variants) {
                variants.push(lito::dependency::ExternalArchiveVariant {
                    .architecture = variant.architecture,
                    .url          = variant.url.clone(),
                    .sha256       = variant.sha256.clone(),
                });
            }
            source = PreparedCMakeDependencySource::ArchitectureArchives(rstd::move(variants));
        } else {
            if (task.acquired.is_none()) {
                return lito::dependency::dependency_failure<PreparedExternalSourceTask>(
                    rstd::format("external source for CMake dependency '{}' was not fetched",
                                 declaration.alias.as_str()));
            }
            auto value = rstd::move(task.acquired).unwrap();
            source     = PreparedCMakeDependencySource::Directory(
                rstd::move(value.root), rstd::move(value.identity), value.cacheable);
        }
    }

    auto cache = Vec<lito::dependency::CMakeCacheEntry>::make();
    if (! task.installed_override) {
        cache.reserve(declaration.cache.len());
        for (const auto& entry : declaration.cache) {
            cache.push(lito::dependency::CMakeCacheEntry {
                .name  = entry.name.clone(),
                .value = entry.value.clone(),
            });
        }
    }
    auto targets =
        Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(declaration.targets.len());
    for (const auto& target : declaration.targets) {
        targets.push(lito::dependency::CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto host_tools = Vec<lito::dependency::CMakeHostToolRequirement>::make();
    for (const auto& tool : declaration.host_tools) host_tools.push(tool.clone());
    auto components       = as<Clone>(declaration.components).clone();
    auto config_directory = Option<PathBuf> {};
    if (! task.installed_override && declaration.config_directory.is_some()) {
        config_directory = Some(declaration.config_directory->clone());
    }
    auto source_name = Option<String> {};
    if (! task.installed_override) {
        source_name = as<Clone>(declaration.source).clone();
    }
    auto adapter          = Option<PathBuf> {};
    auto adapter_identity = String::make();
    if (declaration.adapter.is_some()) {
        auto adapter_root = task.source_root.as_path();
        if (declaration.adapter_root.is_some()) adapter_root = declaration.adapter_root->as_path();
        auto path      = PathBuf::from(adapter_root).join(declaration.adapter->as_path());
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return Err(
                lito::dependency::DependencyError::Io(String::make("resolve CMake adapter"_str),
                                                      rstd::move(path),
                                                      rstd::move(canonical).unwrap_err()));
        }
        if (! canonical->as_path().starts_with(adapter_root)) {
            return lito::dependency::dependency_failure<PreparedExternalSourceTask>(
                rstd::format("CMake adapter '{}' escapes declaration root '{}'",
                             canonical->as_path(),
                             adapter_root));
        }
        auto contents = rstd::fs::read_to_string(canonical->as_path());
        if (contents.is_err()) {
            return Err(lito::dependency::DependencyError::Io(String::make("read CMake adapter"_str),
                                                             canonical->clone(),
                                                             rstd::move(contents).unwrap_err()));
        }
        adapter_identity = rstd::format("{}\n{}", canonical->as_path(), contents->as_str());
        adapter          = Some(rstd::move(canonical).unwrap());
    }
    return Ok(PreparedExternalSourceTask {
        .package            = task.package,
        .declaration        = task.declaration,
        .installed_override = task.installed_override,
        .requirement =
            PreparedCMakeDependencyRequirement {
                .alias            = declaration.alias.clone(),
                .package          = declaration.package.clone(),
                .components       = rstd::move(components),
                .source_name      = rstd::move(source_name),
                .source           = rstd::move(source),
                .adapter          = rstd::move(adapter),
                .adapter_identity = rstd::move(adapter_identity),
                .config_directory = rstd::move(config_directory),
                .cache            = rstd::move(cache),
                .targets          = rstd::move(targets),
                .host_tools       = rstd::move(host_tools),
            },
    });
}

} // namespace lito

export namespace lito
{

auto resolve_external_dependency_sources(lito::package::ResolvedPackageGraph&  graph,
                                         lito::source::SourceResolutionOptions options,
                                         lito::tools::ToolResolver&            resolver,
                                         const ResolvedProcessEnvironment&     environment,
                                         BuildEventSink                        observer = {})
    -> lito::dependency::DependencyResult<DeclaredExternalDependencySources> {
    return resolve_declared_external_dependency_sources(
        graph, rstd::move(options), resolver, environment, source_observer(observer));
}

auto prepare_external_dependency_sources(lito::package::ResolvedPackageGraph& graph,
                                         const Vec<String>&                   selected_packages,
                                         DeclaredExternalDependencySources    declared,
                                         const lito::dependency::CMakeBuildOverrideSet& overrides,
                                         lito::tools::ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment&              environment,
                                         usize          jobs     = usize(1),
                                         BuildEventSink observer = {})
    -> lito::dependency::DependencyResult<PreparedExternalDependencySources> {
    auto acquired = acquire_external_dependency_sources(graph,
                                                        selected_packages,
                                                        rstd::move(declared),
                                                        overrides,
                                                        resolver,
                                                        environment,
                                                        jobs,
                                                        source_observer(observer));
    if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());

    auto tasks = Vec<ExternalSourceTask>::with_capacity(acquired->cmake_dependencies.len());
    for (auto& source : acquired->cmake_dependencies) {
        auto& package     = graph.packages[source.package];
        auto& declaration = package.manifest.cmake_external_dependencies[source.declaration];
        tasks.push(ExternalSourceTask {
            .package            = source.package,
            .declaration        = source.declaration,
            .requirement        = declaration.clone(),
            .source             = {},
            .source_root        = package.manifest.source_root.clone(),
            .installed_override = source.installed_override,
            .acquired           = rstd::move(source.acquired),
        });
        if (declaration.source.is_some()) {
            for (const auto& external : package.manifest.external_sources) {
                if (external.name == declaration.source->as_str()) {
                    tasks[tasks.len() - usize(1)].source = Some(external.source.clone());
                    break;
                }
            }
        }
    }
    auto result = PreparedExternalDependencySources {};
    result.sources.reserve(acquired->sources.len());
    for (auto& source : acquired->sources) {
        const auto& declaration =
            graph.packages[source.package].manifest.external_sources[source.declaration];
        result.sources.push(PreparedPackageExternalSource {
            .package  = source.package,
            .name     = declaration.name.clone(),
            .source   = declaration.source.clone(),
            .acquired = rstd::move(source.acquired),
        });
    }
    if (tasks.is_empty()) return Ok(rstd::move(result));

    auto worker_count = jobs < tasks.len() ? jobs : tasks.len();
    auto created      = rstd::thread::BlockingTaskGroup<
        lito::dependency::DependencyResult<PreparedExternalSourceTask>>::make(worker_count,
                                                                                   tasks.len());
    if (created.is_err()) {
        return Err(lito::dependency::DependencyError::System(
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
            return lito::dependency::dependency_failure<PreparedExternalDependencySources>(
                "cannot submit external source fetch task"_str);
        }
    }
    auto outcomes = rstd::move(group).join();
    result.cmake_dependencies.reserve(outcomes.len());
    for (auto& outcome : outcomes) {
        auto value = rstd::move(outcome).into_value();
        if (value.is_none()) {
            return lito::dependency::dependency_failure<PreparedExternalDependencySources>(
                "external source fetch task was cancelled"_str);
        }
        auto prepared = rstd::move(value).unwrap_unchecked();
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
        auto task = rstd::move(prepared).unwrap();
        result.cmake_dependencies.push(PreparedCMakeDependency {
            .package            = task.package,
            .installed_override = task.installed_override,
            .requirement        = rstd::move(task.requirement),
        });
    }
    rstd::slice_::sort_unstable_by(
        result.cmake_dependencies.as_mut_slice().as_mut_ref(),
        [](const PreparedCMakeDependency& left, const PreparedCMakeDependency& right) {
            if (left.package != right.package) return left.package < right.package;
            if (left.requirement.alias != right.requirement.alias) {
                return left.requirement.alias < right.requirement.alias;
            }
            return left.requirement.package < right.requirement.package;
        });
    return Ok(rstd::move(result));
}

auto prepare_external_dependency_sources(lito::package::ResolvedPackageGraph&  graph,
                                         const Vec<String>&                    selected_packages,
                                         lito::source::SourceResolutionOptions options,
                                         lito::tools::ToolResolver&            resolver,
                                         const ResolvedProcessEnvironment&     environment,
                                         usize                                 jobs     = usize(1),
                                         BuildEventSink                        observer = {})
    -> lito::dependency::DependencyResult<PreparedExternalDependencySources> {
    auto declared = resolve_external_dependency_sources(
        graph, rstd::move(options), resolver, environment, observer);
    if (declared.is_err()) return Err(rstd::move(declared).unwrap_err());
    return prepare_external_dependency_sources(graph,
                                               selected_packages,
                                               rstd::move(declared).unwrap(),
                                               lito::dependency::CMakeBuildOverrideSet {},
                                               resolver,
                                               environment,
                                               jobs,
                                               observer);
}

auto prepare_external_dependency_sources(lito::package::ResolvedPackageGraph&  graph,
                                         lito::source::SourceResolutionOptions options,
                                         lito::tools::ToolResolver&            resolver,
                                         const ResolvedProcessEnvironment&     environment,
                                         usize                                 jobs     = usize(1),
                                         BuildEventSink                        observer = {})
    -> lito::dependency::DependencyResult<PreparedExternalDependencySources> {
    auto selected = Vec<String>::with_capacity(graph.packages.len());
    for (const auto& package : graph.packages) selected.push(package.manifest.name.clone());
    return prepare_external_dependency_sources(
        graph, selected, rstd::move(options), resolver, environment, jobs, observer);
}

auto prepare_external_dependency_sources(lito::package::ResolvedPackageGraph&  graph,
                                         lito::source::SourceResolutionOptions options,
                                         usize                                 jobs     = usize(1),
                                         BuildEventSink                        observer = {})
    -> lito::dependency::DependencyResult<PreparedExternalDependencySources> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(
            rstd::into<lito::dependency::DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::tools::ToolResolver(*environment);
    return prepare_external_dependency_sources(
        graph, rstd::move(options), resolver, *environment, jobs, observer);
}

auto resolve_cmake_requirement_for_platform(const PreparedCMakeDependencyRequirement& requirement,
                                            const BuildPlatform&                      platform)
    -> lito::dependency::DependencyResult<SelectedCMakeDependencyRequirement> {
    auto source = SelectedCMakeDependencySource::Find();
    if (requirement.source.is_Directory()) {
        source = SelectedCMakeDependencySource::Directory(
            requirement.source.as_Directory().root.clone(),
            requirement.source.as_Directory().identity.clone(),
            requirement.source.as_Directory().cacheable);
    } else if (requirement.source.is_Archive()) {
        source =
            SelectedCMakeDependencySource::Archive(requirement.source.as_Archive().url.clone(),
                                                   requirement.source.as_Archive().sha256.clone());
    } else if (requirement.source.is_ArchitectureArchives()) {
        const lito::dependency::ExternalArchiveVariant* selected  = nullptr;
        auto                                            available = String::make();
        for (const auto& variant : requirement.source.as_ArchitectureArchives().variants) {
            if (! available.is_empty()) available.push_str(", "_str);
            available.push_str(architecture_name(variant.architecture));
            if (variant.architecture == platform.effective_target.architecture) {
                selected = rstd::addressof(variant);
            }
        }
        if (selected == nullptr) {
            return lito::dependency::dependency_failure<SelectedCMakeDependencyRequirement>(
                rstd::format(
                    "CMake dependency '{}' has no archive for target '{}' (architecture '{}'); "
                    "available architectures: {}",
                    requirement.alias.as_str(),
                    platform.effective_target.triple.as_str(),
                    architecture_name(platform.effective_target.architecture),
                    available.as_str()));
        }
        source =
            SelectedCMakeDependencySource::Archive(selected->url.clone(), selected->sha256.clone());
    }
    auto adapter = Option<PathBuf> {};
    if (requirement.adapter.is_some()) adapter = Some(requirement.adapter->clone());
    auto config_directory = Option<PathBuf> {};
    if (requirement.config_directory.is_some()) {
        config_directory = Some(requirement.config_directory->clone());
    }
    auto cache = Vec<lito::dependency::CMakeCacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(lito::dependency::CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets =
        Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(lito::dependency::CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto host_tools = Vec<lito::dependency::CMakeHostToolRequirement>::make();
    for (const auto& tool : requirement.host_tools) host_tools.push(tool.clone());
    auto components = as<Clone>(requirement.components).clone();
    return Ok(SelectedCMakeDependencyRequirement {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .components       = rstd::move(components),
        .source           = rstd::move(source),
        .adapter          = rstd::move(adapter),
        .adapter_identity = requirement.adapter_identity.clone(),
        .config_directory = rstd::move(config_directory),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
        .host_tools       = rstd::move(host_tools),
    });
}

auto materialize_cmake_requirement(const SelectedCMakeDependencyRequirement& requirement)
    -> lito::dependency::DependencyResult<ResolvedCMakeDependencyRequirement> {
    if (requirement.source.is_Archive()) {
        return lito::dependency::dependency_failure<ResolvedCMakeDependencyRequirement>(
            rstd::format("CMake dependency '{}' archive source has not been materialized",
                         requirement.alias.as_str()));
    }
    auto source = ResolvedCMakeDependencySource::Find();
    if (requirement.source.is_Directory()) {
        source = ResolvedCMakeDependencySource::Directory(
            requirement.source.as_Directory().root.clone(),
            requirement.source.as_Directory().identity.clone(),
            requirement.source.as_Directory().cacheable);
    }
    auto adapter = Option<PathBuf> {};
    if (requirement.adapter.is_some()) adapter = Some(requirement.adapter->clone());
    auto config_directory = Option<PathBuf> {};
    if (requirement.config_directory.is_some()) {
        config_directory = Some(requirement.config_directory->clone());
    }
    auto cache = Vec<lito::dependency::CMakeCacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(lito::dependency::CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets =
        Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(lito::dependency::CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto host_tools = Vec<lito::dependency::CMakeHostToolRequirement>::make();
    for (const auto& tool : requirement.host_tools) host_tools.push(tool.clone());
    auto components = as<Clone>(requirement.components).clone();
    return Ok(ResolvedCMakeDependencyRequirement {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .components       = rstd::move(components),
        .source           = rstd::move(source),
        .adapter          = rstd::move(adapter),
        .adapter_identity = requirement.adapter_identity.clone(),
        .config_directory = rstd::move(config_directory),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
        .host_tools       = rstd::move(host_tools),
    });
}

} // namespace lito
