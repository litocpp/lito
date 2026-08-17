module;
#include <rstd/macro.hpp>

export module lito.core:package.resolver;

import rstd;
import :manifest.package;
import :package.identity;
import :manifest.profile;
import :source.event;
import :source.git;
import :source.requirement;
import :source.resolution;
import :package.graph;
import :package.error;
import :workspace;
import :source;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using namespace lito;

using namespace lito::package;

template<typename T>
auto package_resolution_failure(String message) -> PackageResult<T> {
    return Err(PackageError::Message(rstd::move(message)));
}

template<typename T>
auto package_resolution_failure(ref<str> message) -> PackageResult<T> {
    return Err(PackageError::Message(String::make(message)));
}

auto clone_dependency_source(const lito::source::PackageSourceRequirement& source)
    -> lito::source::PackageSourceRequirement {
    if (source.is_Path()) {
        return lito::source::PackageSourceRequirement::Path(source.as_Path().path.clone());
    }
    return lito::source::PackageSourceRequirement::Git(
        source.as_Git().url.clone(),
        lito::source::GitReference {
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

struct SelectedSourcePackage {
    String                              source_identity;
    lito::source::ResolvedPackageSource source;
    PathBuf                             manifest;
    lito::manifest::PackageManifest     package;
};

struct AcquiredProjectSources {
    usize         primary;
    Option<usize> tests;
};

auto same_source_root(ref<rstd::path::Path> left, ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

auto load_package_catalog(ref<rstd::path::Path>                    root,
                          const lito::workspace::WorkspaceCatalog* associated_primary = nullptr)
    -> PackageResult<lito::workspace::WorkspaceCatalog> {
    auto document = lito::manifest::load_manifest_document(root);
    if (document.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(document).unwrap_err()));
    }
    auto loaded = rstd::move(document).unwrap();
    if (loaded.kind == lito::manifest::ManifestKind::Workspace && loaded.workspace.is_some()) {
        auto catalog =
            lito::workspace::load_workspace_catalog(rstd::move(loaded.workspace).unwrap());
        if (catalog.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
        }
        return Ok(rstd::move(catalog).unwrap());
    }
    if (loaded.kind != lito::manifest::ManifestKind::Package || loaded.package.is_none()) {
        return package_resolution_failure<lito::workspace::WorkspaceCatalog>(
            "source manifest has no package or workspace"_str);
    }
    auto package = rstd::move(loaded.package).unwrap();
    if (associated_primary != nullptr) {
        auto catalog = lito::workspace::WorkspaceCatalog::associated_package(rstd::move(package),
                                                                             *associated_primary);
        if (catalog.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
        }
        return Ok(rstd::move(catalog).unwrap());
    }
    auto containing = lito::workspace::try_containing_workspace(package);
    if (containing.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(containing).unwrap_err()));
    }
    if (containing->is_some()) {
        auto catalog = lito::workspace::load_workspace_catalog(
            rstd::move(containing).unwrap().unwrap(), Some(rstd::move(package)));
        if (catalog.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
        }
        return Ok(rstd::move(catalog).unwrap());
    }
    auto catalog = lito::workspace::WorkspaceCatalog::single(rstd::move(package));
    if (catalog.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
    }
    return Ok(rstd::move(catalog).unwrap());
}

auto package_coordinate(const SelectedSourcePackage& selected) -> PackageResult<PackageCoordinate> {
    if (selected.package.version.source == lito::manifest::PackageVersionSource::Workspace &&
        selected.package.version.value.is_none()) {
        return package_resolution_failure<PackageCoordinate>(rstd::format(
            "package '{}' has an unresolved workspace version", selected.package.name.as_str()));
    }
    auto requires_version = false;
    for (const auto& target : selected.package.targets) {
        const auto kind = lito::manifest::package_target_kind(target);
        if (kind == PackageTargetKind::Library || kind == PackageTargetKind::Binary ||
            kind == PackageTargetKind::Benchmark) {
            requires_version = true;
            break;
        }
    }
    if (selected.package.version.value.is_none() && requires_version) {
        return package_resolution_failure<PackageCoordinate>(
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
                      const PackageCoordinate& candidate) -> PackageError {
    auto existing_version = existing.version.is_some() ? existing.version->as_str() : "<none>"_str;
    auto candidate_version =
        candidate.version.is_some() ? candidate.version->as_str() : "<none>"_str;
    return PackageError::Message(
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

class PackageGraphResolver {
    PathBuf                                        root_directory_;
    lito::source::SourceManager                    sources_;
    Vec<Option<lito::workspace::WorkspaceCatalog>> catalogs_;
    Vec<ResolvedPackage>                           packages_;
    CoordinateMap                                  coordinates_ { CoordinateMap::make() };
    StringSet                                      active_ { StringSet::make() };
    Vec<String>                                    active_path_;
    Vec<PackageDependencyKind>                     active_kinds_;
    usize                                          jobs_ { usize(1) };

    auto ensure_catalog_slot(usize source) -> void {
        while (catalogs_.len() <= source) catalogs_.push(None());
    }

    auto store_catalog(usize source, lito::workspace::WorkspaceCatalog catalog) -> void {
        ensure_catalog_slot(source);
        if (catalogs_[source].is_none()) catalogs_[source] = Some(rstd::move(catalog));
    }

    auto catalog(usize source) noexcept -> lito::workspace::WorkspaceCatalog& {
        return *catalogs_[source];
    }

    auto catalog(usize source) const noexcept -> const lito::workspace::WorkspaceCatalog& {
        return *catalogs_[source];
    }

    auto acquire_catalog_root(ref<rstd::path::Path>                     root,
                              Option<lito::workspace::WorkspaceCatalog> preloaded         = None(),
                              const lito::workspace::WorkspaceCatalog* associated_primary = nullptr)
        -> PackageResult<usize> {
        auto loaded = preloaded.is_some() ? Ok(rstd::move(preloaded).unwrap())
                                          : load_package_catalog(root, associated_primary);
        if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
        auto catalog  = rstd::move(loaded).unwrap();
        auto acquired = sources_.acquire_root(catalog.root());
        if (acquired.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(acquired).unwrap_err()));
        }
        auto source = *acquired;
        store_catalog(source, rstd::move(catalog));
        return Ok(source);
    }

    auto ensure_source_catalog(usize source) -> PackageResult<empty> {
        ensure_catalog_slot(source);
        if (catalogs_[source].is_some()) return Ok(empty {});
        auto root    = sources_.source_root(source);
        auto catalog = load_package_catalog(root.as_path());
        if (catalog.is_err()) return Err(rstd::move(catalog).unwrap_err());
        auto resolved = sources_.resolved_source(source);
        if (resolved.kind == lito::source::PackageSourceKind::Git &&
            ! same_source_root(catalog->root(), root.as_path())) {
            return package_resolution_failure<empty>(
                rstd::format("Git source manifest root '{}' does not match checkout root '{}'",
                             catalog->root(),
                             root.as_path()));
        }
        store_catalog(source, rstd::move(catalog).unwrap());
        return Ok(empty {});
    }

    auto acquire_associated_catalog(usize primary_source, ref<str> directory, ProjectRootRole role)
        -> PackageResult<Option<usize>> {
        auto& primary = catalog(primary_source);
        auto  root    = PathBuf::from(primary.root()).join(PathBuf::from(directory).as_path());
        auto  located = lito::manifest::try_locate_manifest(root.as_path());
        if (located.is_err()) {
            return Err(PackageError::Manifest(
                lito::manifest::ManifestError::Locate(rstd::move(located).unwrap_err())));
        }
        if (located->is_none()) return Ok(None());
        const auto& location = **located;
        if (! same_source_root(location.directory.as_path(), root.as_path())) {
            return package_resolution_failure<Option<usize>>(rstd::format(
                "associated manifest directory '{}' must be the exact project directory '{}'",
                location.directory.as_path(),
                root.as_path()));
        }
        if (primary.contains_package_root(location.directory.as_path())) return Ok(None());
        auto source =
            acquire_catalog_root(location.directory.as_path(), None(), rstd::addressof(primary));
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        // Storing the associated catalog can grow `catalogs_` and invalidate `primary`.
        // Resolve it again before validation instead of retaining a reference into the vector.
        auto validated = lito::workspace::validate_associated_catalog(
            catalog(primary_source), catalog(*source), role);
        if (validated.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(validated).unwrap_err()));
        }
        return Ok(Some(*source));
    }

    auto acquire_frontier(Vec<lito::source::PackageSourceFetchRequest> requests)
        -> PackageResult<Vec<usize>> {
        auto prepared = Vec<lito::source::PackageSourceFetchRequest>::with_capacity(requests.len());
        auto catalogs =
            Vec<Option<lito::workspace::WorkspaceCatalog>>::with_capacity(requests.len());
        for (auto& request : requests) {
            if (request.source.is_Git()) {
                prepared.push(rstd::move(request));
                catalogs.push(None());
                continue;
            }
            auto requested = request.declaring_root.join(request.source.as_Path().path.as_path());
            auto catalog   = load_package_catalog(requested.as_path());
            if (catalog.is_err()) return Err(rstd::move(catalog).unwrap_err());
            auto root = PathBuf::from(catalog->root());
            prepared.push(lito::source::PackageSourceFetchRequest {
                .source = lito::source::PackageSourceRequirement::Path(PathBuf::from("."_str)),
                .declaring_root = rstd::move(root),
            });
            catalogs.push(Some(rstd::move(catalog).unwrap()));
        }
        auto acquired = sources_.acquire_frontier(rstd::move(prepared), jobs_);
        if (acquired.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(acquired).unwrap_err()));
        }
        auto result = rstd::move(acquired).unwrap();
        for (usize index {}; index < result.len(); ++index) {
            if (catalogs[index].is_some()) {
                store_catalog(result[index], rstd::move(catalogs[index]).unwrap());
            } else {
                auto ensured = ensure_source_catalog(result[index]);
                if (ensured.is_err()) return Err(rstd::move(ensured).unwrap_err());
            }
        }
        return Ok(rstd::move(result));
    }

    auto take_package(usize source, ref<str> name) -> PackageResult<SelectedSourcePackage> {
        auto manifest = catalog(source).take_package(name);
        if (manifest.is_none()) {
            if (catalog(source).names().len() == usize(1)) {
                return package_resolution_failure<SelectedSourcePackage>(
                    rstd::format("dependency '{}' resolves to package '{}' from source '{}'",
                                 name,
                                 catalog(source).names()[usize {}].as_str(),
                                 sources_.source_identity(source)));
            }
            return package_resolution_failure<SelectedSourcePackage>(rstd::format(
                "source '{}' has no package named '{}'", sources_.source_identity(source), name));
        }
        auto package     = rstd::move(manifest).unwrap();
        auto source_root = sources_.source_root(source);
        auto relative    = package.manifest_path.as_path().strip_prefix(source_root.as_path());
        if (relative.is_none()) {
            return package_resolution_failure<SelectedSourcePackage>(
                rstd::format("package manifest '{}' is outside source '{}'",
                             package.manifest_path.as_path(),
                             sources_.source_identity(source)));
        }
        return Ok(SelectedSourcePackage {
            .source_identity = String::make(sources_.source_identity(source)),
            .source          = sources_.resolved_source(source),
            .manifest        = PathBuf::from(*relative),
            .package         = rstd::move(package),
        });
    }

public:
    explicit PackageGraphResolver(ref<rstd::path::Path>                 root_directory,
                                  lito::source::SourceResolutionOptions options,
                                  ToolResolver&                         resolver,
                                  const ResolvedProcessEnvironment&     environment,
                                  usize                                 jobs,
                                  lito::source::SourceEventSink         observer)
        : root_directory_(PathBuf::from(root_directory)),
          sources_(root_directory, rstd::move(options), resolver, environment, observer),
          jobs_(jobs) {}

    auto acquire_root(ref<rstd::path::Path> root, Option<lito::workspace::WorkspaceCatalog> catalog)
        -> PackageResult<AcquiredProjectSources> {
        auto primary = acquire_catalog_root(root, rstd::move(catalog));
        if (primary.is_err()) return Err(rstd::move(primary).unwrap_err());
        auto tests =
            acquire_associated_catalog(*primary, "tests"_str, ProjectRootRole::AssociatedTest);
        if (tests.is_err()) return Err(rstd::move(tests).unwrap_err());
        return Ok(AcquiredProjectSources {
            .primary = *primary,
            .tests   = rstd::move(tests).unwrap(),
        });
    }

    auto package_names(usize source) const -> Vec<String> {
        auto result = Vec<String>::with_capacity(catalog(source).names().len());
        for (const auto& name : catalog(source).names()) result.push(name.clone());
        return result;
    }

    auto source_name(usize source) const noexcept -> ref<str> { return catalog(source).name(); }

    auto source_identity(usize source) const noexcept -> ref<str> {
        return sources_.source_identity(source);
    }

    auto source_manifest(usize source) const -> PathBuf {
        return PathBuf::from(catalog(source).manifest_path());
    }

    auto source_is_workspace(usize source) const noexcept -> bool {
        return catalog(source).is_workspace();
    }

    auto source_profile(usize source) const -> lito::manifest::ProjectProfile {
        return catalog(source).profile();
    }

    auto resolve(usize                 source,
                 ref<str>              expected_name,
                 PackageDependencyKind incoming = PackageDependencyKind::Normal)
        -> PackageResult<String> {
        auto source_identity = String::make(sources_.source_identity(source));
        auto existing        = coordinates_.get(expected_name);
        if (existing.is_some() && (**existing).source_identity == source_identity) {
            if (active_.contains_key(expected_name)) {
                auto cycle = PackageDependencyCycleError {};
                auto start = usize {};
                while (start < active_path_.len() && active_path_[start] != expected_name) ++start;
                for (auto index = start; index < active_path_.len(); ++index) {
                    cycle.packages.push(active_path_[index].clone());
                    if (index + usize(1) < active_path_.len()) {
                        cycle.edges.push(PackageDependencyCycleEdge {
                            .package    = active_path_[index].clone(),
                            .dependency = active_path_[index + usize(1)].clone(),
                            .kind       = active_kinds_[index + usize(1)],
                        });
                    }
                }
                cycle.packages.push(String::make(expected_name));
                cycle.edges.push(PackageDependencyCycleEdge {
                    .package    = active_path_[active_path_.len() - usize(1)].clone(),
                    .dependency = String::make(expected_name),
                    .kind       = incoming,
                });
                return Err(PackageError::Cycle(rstd::move(cycle)));
            }
            return Ok(String::make(expected_name));
        }

        auto selected = take_package(source, expected_name);
        if (selected.is_err()) {
            return Err(rstd::move(selected).unwrap_err());
        }
        auto loaded = rstd::move(selected).unwrap();
        if (loaded.package.name.as_str() != expected_name) {
            return package_resolution_failure<String>(
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
        active_path_.push(loaded.package.name.clone());
        active_kinds_.push(rstd::move(incoming));

        auto fetch_requests = Vec<lito::source::PackageSourceFetchRequest>::with_capacity(
            loaded.package.dependencies.len() + loaded.package.dev_dependencies.len() +
            loaded.package.runtime_dependencies.len());
        const auto append_fetch_requests =
            [&](const Vec<lito::manifest::DeclaredDependency>& declarations) -> void {
            for (const auto& dependency : declarations) {
                auto declaring_root = loaded.package.root.clone();
                if (dependency.declaration_root.is_some()) {
                    declaring_root = dependency.declaration_root->clone();
                }
                fetch_requests.push(lito::source::PackageSourceFetchRequest {
                    .source         = clone_dependency_source(dependency.source),
                    .declaring_root = rstd::move(declaring_root),
                });
            }
        };
        append_fetch_requests(loaded.package.dependencies);
        append_fetch_requests(loaded.package.dev_dependencies);
        for (const auto& dependency : loaded.package.runtime_dependencies) {
            auto declaring_root = loaded.package.root.clone();
            if (dependency.declaration_root.is_some()) {
                declaring_root = dependency.declaration_root->clone();
            }
            fetch_requests.push(lito::source::PackageSourceFetchRequest {
                .source         = clone_dependency_source(dependency.source),
                .declaring_root = rstd::move(declaring_root),
            });
        }
        auto       fetched_sources = rstd_try(acquire_frontier(rstd::move(fetch_requests)));
        auto       source_offset   = usize {};
        const auto resolve_dependencies =
            [&](const Vec<lito::manifest::DeclaredDependency>& declarations,
                PackageDependencyKind kind) -> PackageResult<Vec<ResolvedDependency>> {
            auto dependencies = Vec<ResolvedDependency>::with_capacity(declarations.len());
            for (const auto& dependency : declarations) {
                auto dependency_name =
                    resolve(fetched_sources[source_offset++], dependency.name.as_str(), kind);
                if (dependency_name.is_err()) {
                    return Err(rstd::move(dependency_name).unwrap_err());
                }
                dependencies.push(ResolvedDependency {
                    .name       = rstd::move(dependency_name).unwrap(),
                    .visibility = dependency.visibility,
                    .features = dependency.features.clone(),
                    .default_features = dependency.default_features,
                });
            }
            rstd::slice_::sort_unstable_by(
                dependencies.as_mut_slice().as_mut_ref(),
                [](const ResolvedDependency& left, const ResolvedDependency& right) {
                    return left.name < right.name;
                });
            return Ok(rstd::move(dependencies));
        };
        auto dependencies = rstd_try(
            resolve_dependencies(loaded.package.dependencies, PackageDependencyKind::Normal));
        auto dev_dependencies = rstd_try(resolve_dependencies(loaded.package.dev_dependencies,
                                                              PackageDependencyKind::Development));
        auto runtime_dependencies = Vec<ResolvedRuntimeDependency>::with_capacity(
            loaded.package.runtime_dependencies.len());
        for (const auto& dependency : loaded.package.runtime_dependencies) {
            auto dependency_name = resolve(fetched_sources[source_offset++],
                                           dependency.name.as_str(),
                                           PackageDependencyKind::Runtime);
            if (dependency_name.is_err()) {
                return Err(rstd::move(dependency_name).unwrap_err());
            }
            runtime_dependencies.push(ResolvedRuntimeDependency {
                .name = rstd::move(dependency_name).unwrap(),
            });
        }
        rstd::slice_::sort_unstable_by(
            runtime_dependencies.as_mut_slice().as_mut_ref(),
            [](const ResolvedRuntimeDependency& left, const ResolvedRuntimeDependency& right) {
                return left.name < right.name;
            });

        active_.remove(loaded.package.name.as_str());
        active_path_.pop();
        active_kinds_.pop();
        packages_.push(ResolvedPackage {
            .source_identity      = rstd::move(loaded.source_identity),
            .source               = rstd::move(loaded.source),
            .source_manifest      = rstd::move(loaded.manifest),
            .manifest             = rstd::move(loaded.package),
            .dependencies         = rstd::move(dependencies),
            .dev_dependencies     = rstd::move(dev_dependencies),
            .runtime_dependencies = rstd::move(runtime_dependencies),
            .features             = {},
        });
        return Ok(String::make(expected_name));
    }

    auto finish(String                         name,
                Vec<ResolvedProjectRoot>       roots,
                PathBuf                        manifest_path,
                bool                           root_is_workspace,
                lito::manifest::ProjectProfile profile) -> ResolvedPackageGraph {
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

export namespace lito::package
{

auto resolve_package_graph_with_environment(ref<rstd::path::Path>                 requested_root,
                                            lito::source::SourceResolutionOptions options,
                                            ToolResolver&                         tool_resolver,
                                            const ResolvedProcessEnvironment&     environment,
                                            usize                                 jobs = usize(1),
                                            lito::source::SourceEventSink         observer = {},
                                            Option<lito::workspace::WorkspaceCatalog> catalog =
                                                None()) -> PackageResult<ResolvedPackageGraph> {
    if (jobs == usize {}) {
        return package_resolution_failure<ResolvedPackageGraph>(
            String::make("source fetch jobs must be greater than zero"_str));
    }
    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return Err(
            PackageError::System(SystemError::Io(String::make("resolve package graph root"_str),
                                                 PathBuf::from(requested_root),
                                                 rstd::move(canonical).unwrap_err())));
    }
    auto root     = rstd::move(canonical).unwrap();
    auto resolver = PackageGraphResolver(
        root.as_path(), rstd::move(options), tool_resolver, environment, jobs, observer);
    auto source = resolver.acquire_root(root.as_path(), rstd::move(catalog));
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto project_sources   = rstd::move(source).unwrap();
    auto root_source       = project_sources.primary;
    auto project_name      = String::make(resolver.source_name(root_source));
    auto manifest_path     = resolver.source_manifest(root_source);
    auto root_is_workspace = resolver.source_is_workspace(root_source);
    auto profile           = resolver.source_profile(root_source);
    auto roots             = Vec<ResolvedProjectRoot>::make();
    auto resolve_roots     = [&](usize source_index, ProjectRootRole role) -> PackageResult<empty> {
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

auto resolve_package_graph(ref<rstd::path::Path>                 requested_root,
                           lito::source::SourceResolutionOptions options = {})
    -> PackageResult<ResolvedPackageGraph> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = ToolResolver(*environment);
    return resolve_package_graph_with_environment(
        requested_root, rstd::move(options), resolver, *environment);
}

} // namespace lito::package
