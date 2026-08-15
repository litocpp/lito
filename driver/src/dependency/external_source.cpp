module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

auto append_locked_git_source(SourceResolutionOptions& options,
                              ref<str>                 url,
                              const GitReference&      reference,
                              ref<str>                 commit) -> DependencyResult<empty> {
    for (auto& existing : options.git_sources) {
        if (existing.git.as_str() != url || ! git_references_equal(existing.reference, reference)) {
            continue;
        }
        if (existing.commit.as_str() != commit) {
            if (options.git == GitResolutionMode::Refresh &&
                reference.kind != GitReferenceKind::Commit) {
                existing.commit = String::make(commit);
                return Ok(empty {});
            }
            return dependency_failure<empty>(
                rstd::format("Git requirement '{}#{}' resolves to both '{}' and '{}'",
                             url,
                             reference.value.as_str(),
                             existing.commit.as_str(),
                             commit));
        }
        return Ok(empty {});
    }
    options.git_sources.push(GitSourcePin {
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
    return source.clone();
}

auto external_relation_key(ref<str> package, ref<str> alias) -> String {
    return rstd::format("{}\n{}", package, alias);
}

struct PackageOwnedExternalSourceResolution {
    PathBuf        relative_path;
    AcquiredSource acquired;
};

auto resolve_package_owned_external(const ResolvedPackage&            package,
                                    const CMakeDependencyRequirement& declaration,
                                    ref<rstd::path::Path>             declaring_root)
    -> DependencyResult<Option<PackageOwnedExternalSourceResolution>> {
    auto requested =
        PathBuf::from(declaring_root).join(declaration.source.as_Path().path.as_path());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return Err(DependencyError::Io(String::make("resolve CMake external source"_str),
                                       rstd::move(requested),
                                       rstd::move(canonical).unwrap_err()));
    }
    auto physical = rstd::move(canonical).unwrap();
    auto relative = physical.as_path().strip_prefix(package.source.root_directory.as_path());
    if (relative.is_none()) return Ok(None());

    auto normalized = relative->is_empty() ? PathBuf::from("."_str) : PathBuf::from(*relative);
    if (! normalized.as_path().is_safe_relative()) {
        return dependency_failure<Option<PackageOwnedExternalSourceResolution>>(
            rstd::format("CMake external dependency '{}:{}' has unsafe package source path '{}'",
                         package.manifest.name.as_str(),
                         declaration.alias.as_str(),
                         normalized.as_path()));
    }
    auto metadata = rstd::fs::metadata(physical.as_path());
    if (metadata.is_err()) {
        return Err(DependencyError::Io(String::make("inspect CMake external source"_str),
                                       physical.clone(),
                                       rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return dependency_failure<Option<PackageOwnedExternalSourceResolution>>(
            rstd::format("CMake external dependency '{}:{}' source '{}' is not a directory",
                         package.manifest.name.as_str(),
                         declaration.alias.as_str(),
                         physical.as_path()));
    }
    auto identity = rstd::format(
        "lito-package-external-v1\n{}\n{}", package.source.identity.as_str(), normalized.as_path());
    return Ok(Some(PackageOwnedExternalSourceResolution {
        .relative_path = rstd::move(normalized),
        .acquired =
            AcquiredSource {
                .root      = rstd::move(physical),
                .identity  = rstd::move(identity),
                .cacheable = package.source.kind == PackageSourceKind::Git,
            },
    }));
}

struct CollectedExternalSources {
    SourceResolutionOptions                             options;
    rstd::collections::BTreeMap<String, AcquiredSource> package_owned;
};

auto collect_external_source_records(ResolvedPackageGraph&             graph,
                                     SourceResolutionOptions           options,
                                     ToolResolver&                     resolver,
                                     const ResolvedProcessEnvironment& environment,
                                     SourceEventSink                   observer)
    -> DependencyResult<CollectedExternalSources> {
    graph.externals.clear();
    auto package_owned = rstd::collections::BTreeMap<String, AcquiredSource>::make();
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
        for (const auto& tool : package.manifest.build_tools) {
            for (const auto& archive : tool.archives) {
                auto architectures = Vec<Architecture>::make();
                architectures.push(archive.host.architecture.clone());
                graph.externals.push(ResolvedExternalSourceRecord {
                    .package       = package.manifest.name.clone(),
                    .alias         = tool.alias.clone(),
                    .provider      = rstd::format("build-tool:{}", archive.host.os.as_str()),
                    .architectures = rstd::move(architectures),
                    .build_tool    = Some(ResolvedBuildToolSourceMetadata {
                        .version          = tool.version.clone(),
                        .executable       = tool.executable.clone(),
                        .operating_system = archive.host.os.clone(),
                    }),
                    .source        = ResolvedExternalSource::Archive(archive.url.clone(),
                                                                     archive.sha256.clone()),
                });
            }
        }
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
                auto owned =
                    resolve_package_owned_external(package, declaration, declaring_root.as_path());
                if (owned.is_err()) return Err(rstd::move(owned).unwrap_err());
                if (owned->is_some()) {
                    auto resolved = rstd::move(owned).unwrap().unwrap();
                    auto key      = external_relation_key(package.manifest.name.as_str(),
                                                          declaration.alias.as_str());
                    if (package_owned.contains_key(key.as_str())) {
                        return dependency_failure<CollectedExternalSources>(
                            rstd::format("package '{}' repeats CMake external dependency '{}'",
                                         package.manifest.name.as_str(),
                                         declaration.alias.as_str()));
                    }
                    make_record(ResolvedExternalSource::Package(resolved.relative_path.clone()),
                                {});
                    package_owned.insert(rstd::move(key), rstd::move(resolved.acquired));
                    continue;
                }
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
                    PackageSourceRequirement::Git(git.url.clone(), git.reference.clone()),
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
            make_record(ResolvedExternalSource::Git(
                            git.url.clone(), git.reference.clone(), rstd::move(resolved.commit)),
                        {});
        }
    }
    options.git = GitResolutionMode::ReuseLocked;
    return Ok(CollectedExternalSources {
        .options       = rstd::move(options),
        .package_owned = rstd::move(package_owned),
    });
}

struct ExternalAcquisitionTask {
    usize                  package {};
    usize                  declaration {};
    Option<usize>          source_fetch;
    Option<AcquiredSource> acquired;
};

auto package_selected(const Vec<String>& selected, ref<str> package) -> bool {
    for (const auto& name : selected) {
        if (name == package) return true;
    }
    return false;
}

} // namespace lito

namespace lito
{

auto acquire_external_dependency_sources(ResolvedPackageGraph&             graph,
                                         const Vec<String>&                selected_packages,
                                         SourceResolutionOptions           options,
                                         ToolResolver&                     resolver,
                                         const ResolvedProcessEnvironment& environment,
                                         usize                             jobs     = usize(1),
                                         SourceEventSink                   observer = {})
    -> DependencyResult<AcquiredExternalDependencySources> {
    if (jobs == usize {}) {
        return dependency_failure<AcquiredExternalDependencySources>(
            "source fetch jobs must be greater than zero"_str);
    }
    auto collected      = rstd_try(collect_external_source_records(
        graph, rstd::move(options), resolver, environment, observer));
    options             = rstd::move(collected.options);
    auto package_owned  = rstd::move(collected.package_owned);
    auto tasks          = Vec<ExternalAcquisitionTask>::make();
    auto fetch_requests = Vec<PackageSourceFetchRequest>::make();
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        const auto& package = graph.packages[package_index];
        if (! package_selected(selected_packages, package.manifest.name.as_str())) continue;
        for (usize declaration_index {};
             declaration_index < package.manifest.cmake_external_dependencies.len();
             ++declaration_index) {
            const auto& declaration =
                package.manifest.cmake_external_dependencies[declaration_index];
            auto source_fetch = Option<usize> {};
            auto acquired     = Option<AcquiredSource> {};
            if (declaration.source.is_Git() || declaration.source.is_Path()) {
                if (declaration.source.is_Path()) {
                    auto key = external_relation_key(package.manifest.name.as_str(),
                                                     declaration.alias.as_str());
                    acquired = package_owned.remove(key.as_str());
                }
                if (acquired.is_none()) {
                    auto acquisition    = declaration.source.is_Git()
                                              ? PackageSourceRequirement::Git(
                                                    declaration.source.as_Git().url.clone(),
                                                    declaration.source.as_Git().reference.clone())
                                              : PackageSourceRequirement::Path(
                                                    declaration.source.as_Path().path.clone());
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
            }
            tasks.push(ExternalAcquisitionTask {
                .package      = package_index,
                .declaration  = declaration_index,
                .source_fetch = rstd::move(source_fetch),
                .acquired     = rstd::move(acquired),
            });
        }
    }

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
    for (auto& outcome : fetched) {
        for (auto& source : outcome.sources) {
            auto present = false;
            for (const auto& existing : graph.sources) {
                if (existing.identity == source.identity.as_str()) {
                    present = true;
                    break;
                }
            }
            if (! present) graph.sources.push(rstd::move(source));
        }
    }
    rstd::slice_::sort_unstable_by(
        graph.sources.as_mut_slice().as_mut_ref(),
        [](const ResolvedPackageSource& left, const ResolvedPackageSource& right) {
            return left.identity < right.identity;
        });

    auto result = AcquiredExternalDependencySources {};
    result.dependencies.reserve(tasks.len());
    for (auto& task : tasks) {
        result.dependencies.push(AcquiredExternalDependencySource {
            .package     = task.package,
            .declaration = task.declaration,
            .acquired    = rstd::move(task.acquired),
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito
