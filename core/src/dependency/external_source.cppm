module;
#include <rstd/macro.hpp>

export module lito.dependency:external_source;

import rstd;
import lito.error;
import lito.cpp;
import lito.dependency.contract;
import lito.dependency.error_contract;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.platform.contract;
import lito.lock.contract;
import lito.package.graph_contract;
import lito.system.environment;
import lito.source;
import lito.dependency.cmake;
import :pkg_config_support;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto append_locked_git_source(PackageResolutionOptions& options,
                              ref<str>                  url,
                              const GitReference&       reference,
                              ref<str>                  commit) -> DependencyResult<empty> {
    for (const auto& existing : options.git_sources) {
        if (existing.git.as_str() != url || ! git_references_equal(existing.reference, reference)) {
            continue;
        }
        if (existing.commit.as_str() != commit) {
            return dependency_failure<empty>(
                rstd::format("Git requirement '{}#{}' resolves to both '{}' and '{}'",
                             url,
                             reference.value.as_str(),
                             existing.commit.as_str(),
                             commit));
        }
        return Ok(empty {});
    }
    options.git_sources.push(LockedGitSource {
        .git = String::make(url),
        .reference =
            GitReference {
                .kind  = reference.kind,
                .value = reference.value.clone(),
            },
        .commit = String::make(commit),
    });
    return Ok(empty {});
}

auto clone_resolved_package_source(const ResolvedPackageSource& source) -> ResolvedPackageSource {
    return ResolvedPackageSource {
        .identity       = source.identity.clone(),
        .kind           = source.kind,
        .root_directory = source.root_directory.clone(),
        .path           = source.path.clone(),
        .git            = source.git.clone(),
        .reference =
            GitReference {
                .kind  = source.reference.kind,
                .value = source.reference.value.clone(),
            },
        .commit = source.commit.clone(),
    };
}

auto collect_external_source_records(ResolvedPackageGraph&             graph,
                                     PackageResolutionOptions          options,
                                     ToolResolver&                     resolver,
                                     const ResolvedProcessEnvironment& environment,
                                     BuildObserver                     observer)
    -> DependencyResult<PackageResolutionOptions> {
    graph.externals.clear();
    for (const auto& package : graph.packages) {
        if (package.source.kind != PackageSourceKind::Git || package.source.git.is_empty())
            continue;
        rstd_try(append_locked_git_source(options,
                                          package.source.git.as_str(),
                                          package.source.reference,
                                          package.source.commit.as_str()));
    }

    auto source_manager = SourceManager(
        graph.root_directory.as_path(), options.clone(), resolver, environment, observer);
    auto git_sources = Vec<ResolvedPackageSource>::make();
    auto git_indices = rstd::collections::BTreeMap<String, usize>::make();
    for (const auto& package : graph.packages) {
        if (package.source.kind != PackageSourceKind::Git || package.source.git.is_empty())
            continue;
        auto key = git_requirement_identity(package.source.git.as_str(), package.source.reference);
        if (git_indices.contains_key(key.as_str())) continue;
        git_indices.insert(rstd::move(key), git_sources.len());
        git_sources.push(clone_resolved_package_source(package.source));
    }
    for (const auto& package : graph.packages) {
        for (const auto& declaration : package.manifest.cmake_external_dependencies) {
            if (declaration.source.is_Installed()) continue;

            auto make_record = [&](ResolvedExternalSource source,
                                   Vec<Architecture>      architectures) -> void {
                graph.externals.push(ResolvedExternalSourceRecord {
                    .package       = package.manifest.name.clone(),
                    .alias         = declaration.alias.clone(),
                    .provider      = String::make("cmake"_str),
                    .architectures = rstd::move(architectures),
                    .source        = rstd::move(source),
                });
            };

            if (declaration.source.is_Archive()) {
                make_record(
                    ResolvedExternalSource::Archive(declaration.source.as_Archive().url.clone(),
                                                    declaration.source.as_Archive().sha256.clone()),
                    {});
                continue;
            }
            if (declaration.source.is_ArchitectureArchives()) {
                for (const auto& variant : declaration.source.as_ArchitectureArchives().variants) {
                    auto architectures = Vec<Architecture>::make();
                    architectures.push(variant.architecture.clone());
                    make_record(ResolvedExternalSource::Archive(variant.url.clone(),
                                                                variant.sha256.clone()),
                                rstd::move(architectures));
                }
                continue;
            }

            auto declaring_root = package.manifest.root.clone();
            if (declaration.declaration_root.is_some()) {
                declaring_root = declaration.declaration_root->clone();
            }
            if (declaration.source.is_Path()) {
                auto resolved = source_manager.resolve_external_source(
                    PackageSourceRequirement::Path(declaration.source.as_Path().path.clone()),
                    declaring_root.as_path());
                if (resolved.is_err()) {
                    return Err(rstd::into<DependencyError>(rstd::move(resolved).unwrap_err()));
                }
                make_record(ResolvedExternalSource::Path(rstd::move(resolved).unwrap().path), {});
                continue;
            }

            const auto& git      = declaration.source.as_Git();
            auto        key      = git_requirement_identity(git.url.as_str(), git.reference);
            auto        existing = git_indices.get(key.as_str());
            auto        resolved = ResolvedPackageSource {};
            if (existing.is_some()) {
                resolved = clone_resolved_package_source(git_sources[**existing]);
            } else {
                auto acquired = source_manager.resolve_external_source(
                    PackageSourceRequirement::Git(git.url.clone(),
                                                  GitReference {
                                                      .kind  = git.reference.kind,
                                                      .value = git.reference.value.clone(),
                                                  }),
                    declaring_root.as_path());
                if (acquired.is_err()) {
                    return Err(rstd::into<DependencyError>(rstd::move(acquired).unwrap_err()));
                }
                resolved = rstd::move(acquired).unwrap();
                git_indices.insert(rstd::move(key), git_sources.len());
                git_sources.push(clone_resolved_package_source(resolved));
            }
            rstd_try(append_locked_git_source(
                options, git.url.as_str(), git.reference, resolved.commit.as_str()));
            make_record(ResolvedExternalSource::Git(git.url.clone(),
                                                    GitReference {
                                                        .kind  = git.reference.kind,
                                                        .value = git.reference.value.clone(),
                                                    },
                                                    rstd::move(resolved.commit)),
                        {});
        }
    }
    options.git = GitResolutionMode::ReuseLocked;
    return Ok(rstd::move(options));
}

struct ExternalSourceTask {
    usize                      package {};
    usize                      declaration {};
    CMakeDependencyRequirement requirement;
    PathBuf                    source_root;
    Option<usize>              source_fetch;
    Option<AcquiredSource>     acquired;
};

struct PreparedExternalSourceTask {
    usize                              package {};
    usize                              declaration {};
    PreparedCMakeDependencyRequirement requirement;
};

auto prepare_external_source_task(ExternalSourceTask task) -> DependencyResult<PreparedExternalSourceTask> {
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
                                         PackageResolutionOptions          options,
                                         ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment& environment,
                                         usize                             jobs = usize(1),
                                         BuildObserver observer = {}) -> DependencyResult<empty> {
    if (jobs == usize {}) {
        return dependency_failure<empty>("source fetch jobs must be greater than zero"_str);
    }
    options             = rstd_try(collect_external_source_records(
        graph, rstd::move(options), resolver, environment, observer));
    auto tasks          = Vec<ExternalSourceTask>::make();
    auto fetch_requests = Vec<PackageSourceFetchRequest>::make();
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        const auto& package  = graph.packages[package_index];
        auto        selected = false;
        for (const auto& name : selected_packages) {
            if (name == package.manifest.name.as_str()) {
                selected = true;
                break;
            }
        }
        if (! selected) continue;
        for (usize declaration_index {};
             declaration_index < package.manifest.cmake_external_dependencies.len();
             ++declaration_index) {
            const auto& declaration =
                package.manifest.cmake_external_dependencies[declaration_index];
            auto source_fetch = Option<usize> {};
            if (declaration.source.is_Git() || declaration.source.is_Path()) {
                auto acquisition =
                    declaration.source.is_Git()
                        ? PackageSourceRequirement::Git(
                              declaration.source.as_Git().url.clone(),
                              GitReference {
                                  .kind  = declaration.source.as_Git().reference.kind,
                                  .value = declaration.source.as_Git().reference.value.clone(),
                              })
                        : PackageSourceRequirement::Path(declaration.source.as_Path().path.clone());
                auto declaring_root = package.manifest.root.clone();
                if (declaration.declaration_root.is_some()) {
                    declaring_root = declaration.declaration_root->clone();
                }
                source_fetch = Some(fetch_requests.len());
                fetch_requests.push(PackageSourceFetchRequest {
                    .source         = rstd::move(acquisition),
                    .declaring_root = rstd::move(declaring_root),
                });
            }
            tasks.push(ExternalSourceTask {
                .package      = package_index,
                .declaration  = declaration_index,
                .requirement  = clone_cmake_declaration(declaration),
                .source_root  = package.manifest.source_root.clone(),
                .source_fetch = rstd::move(source_fetch),
            });
        }
    }
    if (tasks.is_empty()) return Ok(empty {});

    auto source_manager = SourceManager(
        graph.root_directory.as_path(), rstd::move(options), resolver, environment, observer);
    auto fetched_result =
        source_manager.acquire_external_frontier(rstd::move(fetch_requests), jobs);
    if (fetched_result.is_err()) {
        return Err(rstd::into<DependencyError>(rstd::move(fetched_result).unwrap_err()));
    }
    auto fetched = rstd::move(fetched_result).unwrap();
    for (auto& task : tasks) {
        if (task.source_fetch.is_none()) continue;
        task.acquired = Some(rstd::move(fetched[*task.source_fetch].acquired));
    }
    auto prepared_sources = Vec<ResolvedPackageSource>::make();
    for (auto& outcome : fetched) {
        for (auto& source : outcome.sources) {
            auto present = false;
            for (const auto& existing : prepared_sources) {
                if (existing.identity == source.identity.as_str()) {
                    present = true;
                    break;
                }
            }
            if (! present) prepared_sources.push(rstd::move(source));
        }
    }

    auto worker_count = jobs < tasks.len() ? jobs : tasks.len();
    auto created      = rstd::thread::BlockingTaskGroup<DependencyResult<PreparedExternalSourceTask>>::make(
        worker_count, tasks.len());
    if (created.is_err()) {
        return Err(DependencyError::System(SystemError::Io(
            String::make("create external source fetch executor"_str),
            PathBuf::make(),
            rstd::move(created).unwrap_err_unchecked())));
    }
    auto group = rstd::move(created).unwrap_unchecked();
    for (auto& task : tasks) {
        auto submitted = group.submit([task = rstd::move(task)]() mutable {
            return prepare_external_source_task(rstd::move(task));
        });
        if (submitted.is_err()) {
            return dependency_failure<empty>("cannot submit external source fetch task"_str);
        }
    }
    auto outcomes = rstd::move(group).join();
    auto prepared = Vec<PreparedExternalSourceTask>::with_capacity(outcomes.len());
    for (auto& outcome : outcomes) {
        auto value = rstd::move(outcome).into_value();
        if (value.is_none()) {
            return dependency_failure<empty>("external source fetch task was cancelled"_str);
        }
        auto result = rstd::move(value).unwrap_unchecked();
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        prepared.push(rstd::move(result).unwrap());
    }
    for (auto& package : graph.packages) package.cmake_external_dependencies.clear();
    for (auto& source : prepared_sources) {
        auto present = false;
        for (const auto& existing : graph.sources) {
            if (existing.identity == source.identity.as_str()) {
                present = true;
                break;
            }
        }
        if (! present) graph.sources.push(rstd::move(source));
    }
    for (auto& task : prepared) {
        graph.packages[task.package].cmake_external_dependencies.push(rstd::move(task.requirement));
    }
    rstd::slice_::sort_unstable_by(
        graph.sources.as_mut_slice().as_mut_ref(),
        [](const ResolvedPackageSource& left, const ResolvedPackageSource& right) {
            return left.identity < right.identity;
        });
    return Ok(empty {});
}

auto prepare_external_dependency_sources(ResolvedPackageGraph&             graph,
                                         PackageResolutionOptions          options,
                                         ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment& environment,
                                         usize                             jobs = usize(1),
                                         BuildObserver observer = {}) -> DependencyResult<empty> {
    auto selected = Vec<String>::with_capacity(graph.packages.len());
    for (const auto& package : graph.packages) {
        selected.push(package.manifest.name.clone());
    }
    return prepare_external_dependency_sources(
        graph, selected, rstd::move(options), resolver, environment, jobs, observer);
}

auto prepare_external_dependency_sources(ResolvedPackageGraph&    graph,
                                         PackageResolutionOptions options,
                                         usize                    jobs     = usize(1),
                                         BuildObserver            observer = {}) -> DependencyResult<empty> {
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
