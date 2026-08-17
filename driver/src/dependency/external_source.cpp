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

auto append_locked_git_source(lito::source::SourceResolutionOptions& options,
                              ref<str>                               url,
                              const lito::source::GitReference&      reference,
                              ref<str> commit) -> lito::dependency::DependencyResult<empty> {
    for (auto& existing : options.git_sources) {
        if (existing.git.as_str() != url ||
            ! lito::source::git_references_equal(existing.reference, reference)) {
            continue;
        }
        if (existing.commit.as_str() != commit) {
            if (options.git == lito::source::GitResolutionMode::Refresh &&
                reference.kind != lito::source::GitReferenceKind::Commit) {
                existing.commit = String::make(commit);
                return Ok(empty {});
            }
            return lito::dependency::dependency_failure<empty>(
                rstd::format("Git requirement '{}#{}' resolves to both '{}' and '{}'",
                             url,
                             reference.value.as_str(),
                             existing.commit.as_str(),
                             commit));
        }
        return Ok(empty {});
    }
    options.git_sources.push(lito::source::GitSourcePin {
        .git = String::make(url),
        .reference =
            lito::source::GitReference {
                .kind  = reference.kind,
                .value = reference.value.clone(),
            },
        .commit = String::make(commit),
    });
    return Ok(empty {});
}

auto clone_resolved_package_source(const lito::source::ResolvedPackageSource& source)
    -> lito::source::ResolvedPackageSource {
    return source.clone();
}

auto external_relation_key(ref<str> package, ref<str> alias) -> String {
    return rstd::format("{}\n{}", package, alias);
}

struct PackageOwnedExternalSourceResolution {
    PathBuf                      relative_path;
    lito::source::AcquiredSource acquired;
};

auto resolve_package_owned_external(const lito::package::ResolvedPackage&               package,
                                    const lito::dependency::CMakeDependencyRequirement& declaration,
                                    ref<rstd::path::Path> declaring_root)
    -> lito::dependency::DependencyResult<Option<PackageOwnedExternalSourceResolution>> {
    auto requested =
        PathBuf::from(declaring_root).join(declaration.source.as_Path().path.as_path());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return Err(
            lito::dependency::DependencyError::Io(String::make("resolve CMake external source"_str),
                                                  rstd::move(requested),
                                                  rstd::move(canonical).unwrap_err()));
    }
    auto physical = rstd::move(canonical).unwrap();
    auto relative = physical.as_path().strip_prefix(package.source.root_directory.as_path());
    if (relative.is_none()) return Ok(None());

    auto normalized = relative->is_empty() ? PathBuf::from("."_str) : PathBuf::from(*relative);
    if (! normalized.as_path().is_safe_relative()) {
        return lito::dependency::dependency_failure<Option<PackageOwnedExternalSourceResolution>>(
            rstd::format("CMake external dependency '{}:{}' has unsafe package source path '{}'",
                         package.manifest.name.as_str(),
                         declaration.alias.as_str(),
                         normalized.as_path()));
    }
    auto metadata = rstd::fs::metadata(physical.as_path());
    if (metadata.is_err()) {
        return Err(
            lito::dependency::DependencyError::Io(String::make("inspect CMake external source"_str),
                                                  physical.clone(),
                                                  rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return lito::dependency::dependency_failure<Option<PackageOwnedExternalSourceResolution>>(
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
            lito::source::AcquiredSource {
                .root      = rstd::move(physical),
                .identity  = rstd::move(identity),
                .cacheable = package.source.kind == lito::source::PackageSourceKind::Git,
            },
    }));
}

auto resolve_declared_external_dependency_sources(lito::package::ResolvedPackageGraph&  graph,
                                                  lito::source::SourceResolutionOptions options,
                                                  ToolResolver&                         resolver,
                                                  const ResolvedProcessEnvironment&     environment,
                                                  lito::source::SourceEventSink         observer)
    -> lito::dependency::DependencyResult<DeclaredExternalDependencySources> {
    graph.externals.clear();
    auto package_owned = rstd::collections::BTreeMap<String, lito::source::AcquiredSource>::make();
    for (const auto& package : graph.packages) {
        if (package.source.kind != lito::source::PackageSourceKind::Git ||
            package.source.git.is_empty())
            continue;
        rstd_try(append_locked_git_source(options,
                                          package.source.git.as_str(),
                                          package.source.reference,
                                          package.source.commit.as_str()));
    }

    auto source_manager = lito::source::SourceManager(
        graph.root_directory.as_path(), options.clone(), resolver, environment, observer);
    auto git_sources = Vec<lito::source::ResolvedPackageSource>::make();
    auto git_indices = rstd::collections::BTreeMap<String, usize>::make();
    for (const auto& package : graph.packages) {
        if (package.source.kind != lito::source::PackageSourceKind::Git ||
            package.source.git.is_empty())
            continue;
        auto key = lito::source::git_requirement_identity(package.source.git.as_str(),
                                                          package.source.reference);
        if (git_indices.contains_key(key.as_str())) continue;
        git_indices.insert(rstd::move(key), git_sources.len());
        git_sources.push(clone_resolved_package_source(package.source));
    }
    for (const auto& package : graph.packages) {
        for (const auto& tool : package.manifest.build_tools) {
            for (const auto& archive : tool.archives) {
                auto architectures = Vec<Architecture>::make();
                architectures.push(archive.host.architecture.clone());
                graph.externals.push(lito::dependency::ResolvedExternalSourceRecord {
                    .package       = package.manifest.name.clone(),
                    .alias         = tool.alias.clone(),
                    .provider      = rstd::format("build-tool:{}", archive.host.os.as_str()),
                    .architectures = rstd::move(architectures),
                    .build_tool    = Some(lito::dependency::ResolvedBuildToolSourceMetadata {
                        .version          = tool.version.clone(),
                        .executable       = tool.executable.clone(),
                        .operating_system = archive.host.os.clone(),
                    }),
                    .source        = lito::dependency::ResolvedExternalSource::Archive(
                        archive.url.clone(), archive.sha256.clone()),
                });
            }
        }
        for (const auto& declaration : package.manifest.cmake_external_dependencies) {
            if (declaration.source.is_Installed()) continue;

            auto make_record = [&](lito::dependency::ResolvedExternalSource source,
                                   Vec<Architecture>                        architectures) -> void {
                graph.externals.push(lito::dependency::ResolvedExternalSourceRecord {
                    .package       = package.manifest.name.clone(),
                    .alias         = declaration.alias.clone(),
                    .provider      = String::make("cmake"_str),
                    .architectures = rstd::move(architectures),
                    .source        = rstd::move(source),
                });
            };

            if (declaration.source.is_Archive()) {
                make_record(lito::dependency::ResolvedExternalSource::Archive(
                                declaration.source.as_Archive().url.clone(),
                                declaration.source.as_Archive().sha256.clone()),
                            {});
                continue;
            }
            if (declaration.source.is_ArchitectureArchives()) {
                for (const auto& variant : declaration.source.as_ArchitectureArchives().variants) {
                    auto architectures = Vec<Architecture>::make();
                    architectures.push(variant.architecture.clone());
                    make_record(lito::dependency::ResolvedExternalSource::Archive(
                                    variant.url.clone(), variant.sha256.clone()),
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
                        return lito::dependency::dependency_failure<
                            DeclaredExternalDependencySources>(
                            rstd::format("package '{}' repeats CMake external dependency '{}'",
                                         package.manifest.name.as_str(),
                                         declaration.alias.as_str()));
                    }
                    make_record(lito::dependency::ResolvedExternalSource::Package(
                                    resolved.relative_path.clone()),
                                {});
                    package_owned.insert(rstd::move(key), rstd::move(resolved.acquired));
                    continue;
                }
                auto resolved = source_manager.resolve_external_source(
                    lito::source::PackageSourceRequirement::Path(
                        declaration.source.as_Path().path.clone()),
                    declaring_root.as_path());
                if (resolved.is_err()) {
                    return Err(rstd::into<lito::dependency::DependencyError>(
                        rstd::move(resolved).unwrap_err()));
                }
                make_record(lito::dependency::ResolvedExternalSource::Path(
                                rstd::move(resolved).unwrap().path),
                            {});
                continue;
            }

            const auto& git = declaration.source.as_Git();
            auto key      = lito::source::git_requirement_identity(git.url.as_str(), git.reference);
            auto existing = git_indices.get(key.as_str());
            auto resolved = lito::source::ResolvedPackageSource {};
            if (existing.is_some()) {
                resolved = clone_resolved_package_source(git_sources[**existing]);
            } else {
                auto selection =
                    lito::source::select_git_source(options, git.url.as_str(), git.reference);
                if (selection.is_err()) {
                    return Err(rstd::into<lito::dependency::DependencyError>(
                        rstd::move(selection).unwrap_err()));
                }
                if (selection->exact_commit.is_some()) {
                    resolved = lito::source::ResolvedPackageSource {
                        .identity = lito::source::git_source_identity(
                            git.url.as_str(), selection->exact_commit->as_str()),
                        .kind      = lito::source::PackageSourceKind::Git,
                        .git       = git.url.clone(),
                        .reference = git.reference.clone(),
                        .commit    = selection->exact_commit->clone(),
                    };
                } else {
                    auto acquired = source_manager.resolve_external_source(
                        lito::source::PackageSourceRequirement::Git(git.url.clone(),
                                                                    git.reference.clone()),
                        declaring_root.as_path());
                    if (acquired.is_err()) {
                        return Err(rstd::into<lito::dependency::DependencyError>(
                            rstd::move(acquired).unwrap_err()));
                    }
                    resolved = rstd::move(acquired).unwrap();
                }
                git_indices.insert(rstd::move(key), git_sources.len());
                git_sources.push(clone_resolved_package_source(resolved));
            }
            rstd_try(append_locked_git_source(
                options, git.url.as_str(), git.reference, resolved.commit.as_str()));
            make_record(lito::dependency::ResolvedExternalSource::Git(
                            git.url.clone(), git.reference.clone(), rstd::move(resolved.commit)),
                        {});
        }
    }
    options.git = lito::source::GitResolutionMode::ReuseLocked;
    return Ok(DeclaredExternalDependencySources {
        .options       = rstd::move(options),
        .package_owned = rstd::move(package_owned),
    });
}

struct ExternalAcquisitionTask {
    usize                                package {};
    usize                                declaration {};
    bool                                 installed_override { false };
    Option<usize>                        source_fetch;
    Option<lito::source::AcquiredSource> acquired;
};

auto package_selected(const Vec<String>& selected, ref<str> package) -> bool {
    for (const auto& name : selected) {
        if (name == package) return true;
    }
    return false;
}

auto validate_cmake_build_overrides(const lito::package::ResolvedPackageGraph&     graph,
                                    const lito::dependency::CMakeBuildOverrideSet& overrides)
    -> lito::dependency::DependencyResult<empty> {
    for (const auto& entry : overrides.entries) {
        auto matched = false;
        for (const auto& package : graph.packages) {
            for (const auto& declaration : package.manifest.cmake_external_dependencies) {
                if (declaration.package != entry.package) continue;
                matched = true;
            }
        }
        if (! matched) {
            return lito::dependency::dependency_failure<empty>(rstd::format(
                "cmake.overrides.{}.source = 'installed' does not match any find-package in the "
                "active package graph",
                entry.package.as_str()));
        }
    }
    return Ok(empty {});
}

} // namespace lito

namespace lito
{

auto acquire_external_dependency_sources(lito::package::ResolvedPackageGraph& graph,
                                         const Vec<String>&                   selected_packages,
                                         DeclaredExternalDependencySources    declared,
                                         const lito::dependency::CMakeBuildOverrideSet& overrides,
                                         ToolResolver&                                  resolver,
                                         const ResolvedProcessEnvironment&              environment,
                                         usize                         jobs     = usize(1),
                                         lito::source::SourceEventSink observer = {})
    -> lito::dependency::DependencyResult<AcquiredExternalDependencySources> {
    if (jobs == usize {}) {
        return lito::dependency::dependency_failure<AcquiredExternalDependencySources>(
            "source fetch jobs must be greater than zero"_str);
    }
    rstd_try(validate_cmake_build_overrides(graph, overrides));
    auto options        = rstd::move(declared.options);
    auto package_owned  = rstd::move(declared.package_owned);
    auto tasks          = Vec<ExternalAcquisitionTask>::make();
    auto fetch_requests = Vec<lito::source::PackageSourceFetchRequest>::make();
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        const auto& package = graph.packages[package_index];
        if (! package_selected(selected_packages, package.manifest.name.as_str())) continue;
        for (usize declaration_index {};
             declaration_index < package.manifest.cmake_external_dependencies.len();
             ++declaration_index) {
            const auto& declaration =
                package.manifest.cmake_external_dependencies[declaration_index];
            const auto installed_override = overrides.contains(declaration.package.as_str());
            if (installed_override &&
                declaration.integration == lito::dependency::CMakeIntegration::BuildTree) {
                return lito::dependency::dependency_failure<AcquiredExternalDependencySources>(
                    rstd::format(
                        "cmake.overrides.{}.source = 'installed' cannot replace build-tree "
                        "dependency '{}:{}'",
                        declaration.package.as_str(),
                        package.manifest.name.as_str(),
                        declaration.alias.as_str()));
            }
            auto source_fetch = Option<usize> {};
            auto acquired     = Option<lito::source::AcquiredSource> {};
            if (! installed_override &&
                (declaration.source.is_Git() || declaration.source.is_Path())) {
                if (declaration.source.is_Path()) {
                    auto key = external_relation_key(package.manifest.name.as_str(),
                                                     declaration.alias.as_str());
                    acquired = package_owned.remove(key.as_str());
                }
                if (acquired.is_none()) {
                    auto acquisition    = declaration.source.is_Git()
                                              ? lito::source::PackageSourceRequirement::Git(
                                                    declaration.source.as_Git().url.clone(),
                                                    declaration.source.as_Git().reference.clone())
                                              : lito::source::PackageSourceRequirement::Path(
                                                    declaration.source.as_Path().path.clone());
                    auto declaring_root = package.manifest.root.clone();
                    if (declaration.declaration_root.is_some()) {
                        declaring_root = declaration.declaration_root->clone();
                    }
                    source_fetch = Some(fetch_requests.len());
                    fetch_requests.push(lito::source::PackageSourceFetchRequest {
                        .source         = rstd::move(acquisition),
                        .declaring_root = rstd::move(declaring_root),
                    });
                }
            }
            tasks.push(ExternalAcquisitionTask {
                .package            = package_index,
                .declaration        = declaration_index,
                .installed_override = installed_override,
                .source_fetch       = rstd::move(source_fetch),
                .acquired           = rstd::move(acquired),
            });
        }
    }

    auto source_manager = lito::source::SourceManager(
        graph.root_directory.as_path(), rstd::move(options), resolver, environment, observer);
    auto fetched_result =
        source_manager.acquire_external_frontier(rstd::move(fetch_requests), jobs);
    if (fetched_result.is_err()) {
        return Err(
            rstd::into<lito::dependency::DependencyError>(rstd::move(fetched_result).unwrap_err()));
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
    rstd::slice_::sort_unstable_by(graph.sources.as_mut_slice().as_mut_ref(),
                                   [](const lito::source::ResolvedPackageSource& left,
                                      const lito::source::ResolvedPackageSource& right) {
                                       return left.identity < right.identity;
                                   });

    auto result = AcquiredExternalDependencySources {};
    result.dependencies.reserve(tasks.len());
    for (auto& task : tasks) {
        result.dependencies.push(AcquiredExternalDependencySource {
            .package            = task.package,
            .declaration        = task.declaration,
            .installed_override = task.installed_override,
            .acquired           = rstd::move(task.acquired),
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito
