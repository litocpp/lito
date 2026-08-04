export module tenon.package:resolver;

import rstd;
import tenon.model;
import :source;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

struct PackageCoordinate {
    String  version;
    String  source_identity;
    PathBuf manifest;
};

using CoordinateMap = rstd::collections::BTreeMap<String, PackageCoordinate>;

auto package_coordinate(const SelectedSourcePackage& selected) -> Result<PackageCoordinate> {
    if (selected.package.version.value.is_none()) {
        return failure<PackageCoordinate>(rstd::format(
            "package '{}' has an unresolved workspace version", selected.package.name.as_str()));
    }
    return Ok(PackageCoordinate {
        .version         = selected.package.version.value->clone(),
        .source_identity = selected.source_identity.clone(),
        .manifest        = selected.manifest.clone(),
    });
}

auto package_conflict(ref<str>                 name,
                      const PackageCoordinate& existing,
                      const PackageCoordinate& candidate) -> Error {
    return Error::make(
        ErrorKind::Dependency,
        rstd::format("package conflict for '{}': version '{}' at '{}' from source '{}' conflicts "
                     "with version '{}' at '{}' from source '{}'",
                     name,
                     existing.version.as_str(),
                     existing.manifest.as_path(),
                     existing.source_identity.as_str(),
                     candidate.version.as_str(),
                     candidate.manifest.as_path(),
                     candidate.source_identity.as_str()));
}

class Resolver {
    PathBuf              root_directory_;
    PackageSourceManager sources_;
    Vec<ResolvedPackage> packages_;
    CoordinateMap        coordinates_ { CoordinateMap::make() };
    StringSet            active_ { StringSet::make() };

public:
    explicit Resolver(ref<rstd::path::Path> root_directory, PackageResolutionOptions options)
        : root_directory_(PathBuf::from(root_directory)),
          sources_(root_directory, rstd::move(options)) {}

    auto acquire_root(ref<rstd::path::Path> root) -> Result<usize> {
        return sources_.acquire_root(root);
    }

    auto package_names(usize source) const -> Vec<String> { return sources_.package_names(source); }

    auto source_name(usize source) const noexcept -> ref<str> {
        return sources_.source_name(source);
    }

    auto source_manifest(usize source) const -> PathBuf { return sources_.source_manifest(source); }

    auto source_is_workspace(usize source) const noexcept -> bool {
        return sources_.source_is_workspace(source);
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

        auto dependencies = Vec<ResolvedDependency>::make();
        for (const auto& dependency : loaded.package.dependencies) {
            auto dependency_source =
                sources_.acquire(dependency.source, loaded.package.root.as_path());
            if (dependency_source.is_err()) {
                return Err(rstd::move(dependency_source).unwrap_err());
            }
            auto dependency_name = resolve(*dependency_source, dependency.name.as_str());
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

        active_.remove(loaded.package.name.as_str());
        packages_.push(ResolvedPackage {
            .source_identity = rstd::move(loaded.source_identity),
            .source_manifest = rstd::move(loaded.manifest),
            .manifest        = rstd::move(loaded.package),
            .dependencies    = rstd::move(dependencies),
        });
        return Ok(String::make(expected_name));
    }

    auto finish(String name, Vec<String> root_names, PathBuf manifest_path, bool root_is_workspace)
        -> ResolvedPackageGraph {
        rstd::slice_::sort_unstable_by(
            packages_.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
                return left.manifest.name < right.manifest.name;
            });
        rstd::slice_::sort_unstable(root_names.as_mut_slice().as_mut_ref());
        return ResolvedPackageGraph {
            .name              = rstd::move(name),
            .root_names        = rstd::move(root_names),
            .root_directory    = rstd::move(root_directory_),
            .manifest_path     = rstd::move(manifest_path),
            .root_is_workspace = root_is_workspace,
            .sources           = sources_.finish(),
            .packages          = rstd::move(packages_),
        };
    }
};

} // namespace tenon

export namespace tenon
{

auto resolve_package_graph(ref<rstd::path::Path>    requested_root,
                           PackageResolutionOptions options = {}) -> Result<ResolvedPackageGraph> {
    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return failure<ResolvedPackageGraph>(
            rstd::format("cannot resolve graph root directory '{}': {}",
                         requested_root,
                         rstd::move(canonical).unwrap_err()));
    }
    auto root     = rstd::move(canonical).unwrap();
    auto resolver = Resolver(root.as_path(), rstd::move(options));
    auto source   = resolver.acquire_root(root.as_path());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto root_source       = *source;
    auto project_name      = String::make(resolver.source_name(root_source));
    auto manifest_path     = resolver.source_manifest(root_source);
    auto root_is_workspace = resolver.source_is_workspace(root_source);
    auto names             = resolver.package_names(root_source);
    auto root_names        = Vec<String>::with_capacity(names.len());
    for (const auto& name : names) {
        auto resolved_name = resolver.resolve(root_source, name.as_str());
        if (resolved_name.is_err()) return Err(rstd::move(resolved_name).unwrap_err());
        root_names.push(rstd::move(resolved_name).unwrap());
    }
    return Ok(resolver.finish(rstd::move(project_name),
                              rstd::move(root_names),
                              rstd::move(manifest_path),
                              root_is_workspace));
}

} // namespace tenon
