module;
#include <rstd/macro.hpp>

export module lito.package:resolver;

import rstd;
import lito.model;
import lito.source;
import lito.environment;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace lito
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

auto clone_package_source(const PackageSourceRequirement& source) -> PackageSourceRequirement {
    if (source.is_Path()) {
        return PackageSourceRequirement::Path(source.as_Path().path.clone());
    }
    return PackageSourceRequirement::Git(source.as_Git().url.clone(),
                                         GitReference {
                                             .kind  = source.as_Git().reference.kind,
                                             .value = source.as_Git().reference.value.clone(),
                                         });
}

struct PackageCoordinate {
    Option<String> version;
    String         source_identity;
    PathBuf        manifest;
};

using CoordinateMap = rstd::collections::BTreeMap<String, PackageCoordinate>;

auto package_coordinate(const SelectedSourcePackage& selected) -> Result<PackageCoordinate> {
    if (selected.package.version.source == PackageVersionSource::Workspace &&
        selected.package.version.value.is_none()) {
        return failure<PackageCoordinate>(rstd::format(
            "package '{}' has an unresolved workspace version", selected.package.name.as_str()));
    }
    auto requires_version = false;
    for (const auto& target : selected.package.targets) {
        const auto kind = package_target_kind(target);
        if (kind == PackageTargetKind::Library || kind == PackageTargetKind::Binary ||
            kind == PackageTargetKind::Benchmark) {
            requires_version = true;
            break;
        }
    }
    if (selected.package.version.value.is_none() && requires_version) {
        return failure<PackageCoordinate>(
            rstd::format("package '{}' has no version", selected.package.name.as_str()));
    }
    return Ok(PackageCoordinate {
        .version         = selected.package.version.value.clone(),
        .source_identity = selected.source_identity.clone(),
        .manifest        = selected.manifest.clone(),
    });
}

auto package_conflict(ref<str>                 name,
                      const PackageCoordinate& existing,
                      const PackageCoordinate& candidate) -> Error {
    auto existing_version = existing.version.is_some() ? existing.version->as_str() : "<none>"_str;
    auto candidate_version =
        candidate.version.is_some() ? candidate.version->as_str() : "<none>"_str;
    return Error::make(
        ErrorKind::Dependency,
        rstd::format("package conflict for '{}': version '{}' at '{}' from source '{}' conflicts "
                     "with version '{}' at '{}' from source '{}'",
                     name,
                     existing_version,
                     existing.manifest.as_path(),
                     existing.source_identity.as_str(),
                     candidate_version,
                     candidate.manifest.as_path(),
                     candidate.source_identity.as_str()));
}

class Resolver {
    PathBuf              root_directory_;
    SourceManager        sources_;
    Vec<ResolvedPackage> packages_;
    CoordinateMap        coordinates_ { CoordinateMap::make() };
    StringSet            active_ { StringSet::make() };
    usize                jobs_ { usize(1) };

public:
    explicit Resolver(ref<rstd::path::Path>             root_directory,
                      PackageResolutionOptions          options,
                      ToolResolver&                     resolver,
                      const ResolvedProcessEnvironment& environment,
                      usize                             jobs)
        : root_directory_(PathBuf::from(root_directory)),
          sources_(root_directory, rstd::move(options), resolver, environment),
          jobs_(jobs) {}

    auto acquire_root(ref<rstd::path::Path> root) -> Result<AcquiredProjectSources> {
        return sources_.acquire_root(root);
    }

    auto package_names(usize source) const -> Vec<String> { return sources_.package_names(source); }

    auto source_name(usize source) const noexcept -> ref<str> {
        return sources_.source_name(source);
    }

    auto source_identity(usize source) const noexcept -> ref<str> {
        return sources_.source_identity(source);
    }

    auto source_manifest(usize source) const -> PathBuf { return sources_.source_manifest(source); }

    auto source_is_workspace(usize source) const noexcept -> bool {
        return sources_.source_is_workspace(source);
    }

    auto source_profile(usize source) const -> ProjectProfile {
        return sources_.source_profile(source);
    }

    auto resolve(usize source, ref<str> expected_name) -> Result<String> {
        auto source_identity = String::make(sources_.source_identity(source));
        auto existing        = coordinates_.get(expected_name);
        if (existing.is_some() && (**existing).source_identity == source_identity) {
            if (active_.contains_key(expected_name)) {
                return failure<String>(
                    rstd::format("dependency cycle reaches package '{}' from source '{}'",
                                 expected_name,
                                 source_identity.as_str()));
            }
            return Ok(String::make(expected_name));
        }

        auto selected = sources_.take_package(source, expected_name);
        if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
        auto loaded = rstd::move(selected).unwrap();
        if (loaded.package.name.as_str() != expected_name) {
            return failure<String>(
                rstd::format("dependency '{}' resolves to package '{}' from source '{}'",
                             expected_name,
                             loaded.package.name.as_str(),
                             source_identity.as_str()));
        }
        auto coordinate = package_coordinate(loaded);
        if (coordinate.is_err()) return Err(rstd::move(coordinate).unwrap_err());
        auto candidate = rstd::move(coordinate).unwrap();
        if (existing.is_some()) {
            return Err(package_conflict(expected_name, **existing, candidate));
        }

        coordinates_.insert(loaded.package.name.clone(),
                            PackageCoordinate {
                                .version         = candidate.version.clone(),
                                .source_identity = candidate.source_identity.clone(),
                                .manifest        = candidate.manifest.clone(),
                            });
        active_.insert(loaded.package.name.clone(), empty {});

        auto fetch_requests = Vec<PackageSourceFetchRequest>::with_capacity(
            loaded.package.dependencies.len() + loaded.package.dev_dependencies.len());
        const auto append_fetch_requests =
            [&](const Vec<DeclaredDependency>& declarations) -> void {
            for (const auto& dependency : declarations) {
                auto declaring_root = loaded.package.root.clone();
                if (dependency.declaration_root.is_some()) {
                    declaring_root = dependency.declaration_root->clone();
                }
                fetch_requests.push(PackageSourceFetchRequest {
                    .source         = clone_package_source(dependency.source),
                    .declaring_root = rstd::move(declaring_root),
                });
            }
        };
        append_fetch_requests(loaded.package.dependencies);
        append_fetch_requests(loaded.package.dev_dependencies);
        auto fetched_sources =
            rstd_try(sources_.acquire_frontier(rstd::move(fetch_requests), jobs_));
        auto       source_offset = usize {};
        const auto resolve_dependencies =
            [&](const Vec<DeclaredDependency>& declarations) -> Result<Vec<ResolvedDependency>> {
            auto dependencies = Vec<ResolvedDependency>::with_capacity(declarations.len());
            for (const auto& dependency : declarations) {
                auto dependency_name =
                    resolve(fetched_sources[source_offset++], dependency.name.as_str());
                if (dependency_name.is_err()) {
                    return Err(rstd::move(dependency_name).unwrap_err());
                }
                dependencies.push(ResolvedDependency {
                    .name       = rstd::move(dependency_name).unwrap(),
                    .visibility = dependency.visibility,
                });
            }
            rstd::slice_::sort_unstable_by(
                dependencies.as_mut_slice().as_mut_ref(),
                [](const ResolvedDependency& left, const ResolvedDependency& right) {
                    return left.name < right.name;
                });
            return Ok(rstd::move(dependencies));
        };
        auto dependencies     = rstd_try(resolve_dependencies(loaded.package.dependencies));
        auto dev_dependencies = rstd_try(resolve_dependencies(loaded.package.dev_dependencies));

        active_.remove(loaded.package.name.as_str());
        packages_.push(ResolvedPackage {
            .source_identity             = rstd::move(loaded.source_identity),
            .source_manifest             = rstd::move(loaded.manifest),
            .manifest                    = rstd::move(loaded.package),
            .dependencies                = rstd::move(dependencies),
            .dev_dependencies            = rstd::move(dev_dependencies),
            .cmake_external_dependencies = {},
        });
        return Ok(String::make(expected_name));
    }

    auto finish(String                   name,
                Vec<ResolvedProjectRoot> roots,
                PathBuf                  manifest_path,
                bool                     root_is_workspace,
                ProjectProfile           profile) -> ResolvedPackageGraph {
        rstd::slice_::sort_unstable_by(
            packages_.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
                return left.manifest.name < right.manifest.name;
            });
        rstd::slice_::sort_unstable_by(
            roots.as_mut_slice().as_mut_ref(),
            [](const ResolvedProjectRoot& left, const ResolvedProjectRoot& right) {
                return left.name < right.name;
            });
        return ResolvedPackageGraph {
            .name              = rstd::move(name),
            .roots             = rstd::move(roots),
            .root_directory    = rstd::move(root_directory_),
            .manifest_path     = rstd::move(manifest_path),
            .root_is_workspace = root_is_workspace,
            .profile           = rstd::move(profile),
            .sources           = sources_.finish(),
            .packages          = rstd::move(packages_),
        };
    }
};

} // namespace lito

export namespace lito
{

auto resolve_package_graph_with_environment(ref<rstd::path::Path>             requested_root,
                                            PackageResolutionOptions          options,
                                            ToolResolver&                     tool_resolver,
                                            const ResolvedProcessEnvironment& environment,
                                            usize jobs = usize(1)) -> Result<ResolvedPackageGraph> {
    if (jobs == usize {}) {
        return failure<ResolvedPackageGraph>(
            String::make("source fetch jobs must be greater than zero"_str));
    }
    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return failure<ResolvedPackageGraph>(
            rstd::format("cannot resolve graph root directory '{}': {}",
                         requested_root,
                         rstd::move(canonical).unwrap_err()));
    }
    auto root     = rstd::move(canonical).unwrap();
    auto resolver = Resolver(root.as_path(), rstd::move(options), tool_resolver, environment, jobs);
    auto source   = resolver.acquire_root(root.as_path());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto project_sources   = rstd::move(source).unwrap();
    auto root_source       = project_sources.primary;
    auto project_name      = String::make(resolver.source_name(root_source));
    auto manifest_path     = resolver.source_manifest(root_source);
    auto root_is_workspace = resolver.source_is_workspace(root_source);
    auto profile           = resolver.source_profile(root_source);
    auto roots             = Vec<ResolvedProjectRoot>::make();
    auto resolve_roots     = [&](usize source_index, ProjectRootRole role) -> Result<empty> {
        auto names = resolver.package_names(source_index);
        for (const auto& name : names) {
            auto resolved_name = resolver.resolve(source_index, name.as_str());
            if (resolved_name.is_err()) return Err(rstd::move(resolved_name).unwrap_err());
            roots.push(ResolvedProjectRoot {
                .name            = rstd::move(resolved_name).unwrap(),
                .source_identity = String::make(resolver.source_identity(source_index)),
                .role            = role,
            });
        }
        return Ok(empty {});
    };
    auto primary_role =
        root_is_workspace ? ProjectRootRole::WorkspaceMember : ProjectRootRole::PrimaryPackage;
    auto resolved_primary = resolve_roots(root_source, primary_role);
    if (resolved_primary.is_err()) return Err(rstd::move(resolved_primary).unwrap_err());
    if (project_sources.tests.is_some()) {
        auto resolved_tests =
            resolve_roots(*project_sources.tests, ProjectRootRole::AssociatedTest);
        if (resolved_tests.is_err()) return Err(rstd::move(resolved_tests).unwrap_err());
    }
    return Ok(resolver.finish(rstd::move(project_name),
                              rstd::move(roots),
                              rstd::move(manifest_path),
                              root_is_workspace,
                              rstd::move(profile)));
}

auto resolve_package_graph(ref<rstd::path::Path>    requested_root,
                           PackageResolutionOptions options = {}) -> Result<ResolvedPackageGraph> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto resolver = ToolResolver(*environment);
    return resolve_package_graph_with_environment(
        requested_root, rstd::move(options), resolver, *environment);
}

} // namespace lito
