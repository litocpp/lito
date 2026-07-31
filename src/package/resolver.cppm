export module tenon.package:resolver;

import rstd;
import tenon.model;
import :source;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringMap = rstd::collections::BTreeMap<String, String>;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

auto package_id(const PackageManifest& manifest, ref<str> source_id) -> Result<String> {
    if (manifest.version.value.is_none()) {
        return failure<String>(rstd::format("package '{}' has an unresolved workspace version",
                                            manifest.name.as_str()));
    }
    return Ok(rstd::format(
        "{} {} ({})", manifest.name.as_str(), manifest.version.value->as_str(), source_id));
}

auto package_key(ref<str> source_id, ref<str> name) -> String {
    return rstd::format("{}\n{}", source_id, name);
}

class Resolver {
    PathBuf              root_directory_;
    PackageSourceManager sources_;
    Vec<ResolvedPackage> packages_;
    StringMap            resolved_ { StringMap::make() };
    StringSet            active_ { StringSet::make() };
    StringMap            package_sources_ { StringMap::make() };

public:
    explicit Resolver(ref<rstd::path::Path> root_directory, PackageResolutionOptions options)
        : root_directory_(PathBuf::from(root_directory)),
          sources_(root_directory, rstd::move(options)) {}

    auto acquire_root(ref<rstd::path::Path> root) -> Result<usize> {
        return sources_.acquire_root(root);
    }

    auto package_names(usize source) const -> Vec<String> { return sources_.package_names(source); }

    auto source_manifest(usize source) const -> PathBuf { return sources_.source_manifest(source); }

    auto source_is_workspace(usize source) const noexcept -> bool {
        return sources_.source_is_workspace(source);
    }

    auto resolve(usize source, ref<str> expected_name) -> Result<String> {
        auto source_id = String::make(sources_.source_id(source));
        auto key       = package_key(source_id.as_str(), expected_name);
        auto existing  = resolved_.get(key.as_str());
        if (existing.is_some()) return Ok((**existing).clone());
        if (active_.contains_key(key.as_str())) {
            return failure<String>(
                rstd::format("dependency cycle reaches package '{}' from source '{}'",
                             expected_name,
                             source_id.as_str()));
        }

        auto selected = sources_.take_package(source, expected_name);
        if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
        auto loaded = rstd::move(selected).unwrap();
        if (loaded.package.name.as_str() != expected_name) {
            return failure<String>(
                rstd::format("dependency '{}' resolves to package '{}' from source '{}'",
                             expected_name,
                             loaded.package.name.as_str(),
                             source_id.as_str()));
        }
        auto identity = package_id(loaded.package, source_id.as_str());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        auto id = rstd::move(identity).unwrap();

        auto same_name = package_sources_.get(loaded.package.name.as_str());
        if (same_name.is_some() && **same_name != source_id) {
            return failure<String>(
                rstd::format("package name '{}' resolves from both '{}' and '{}'",
                             loaded.package.name.as_str(),
                             (**same_name).as_str(),
                             source_id.as_str()));
        }
        if (same_name.is_none()) {
            package_sources_.insert(loaded.package.name.clone(), source_id.clone());
        }
        active_.insert(key.clone(), empty {});

        auto dependencies = Vec<ResolvedDependency>::make();
        for (const auto& dependency : loaded.package.dependencies) {
            auto dependency_source =
                sources_.acquire(dependency.source, loaded.package.root.as_path());
            if (dependency_source.is_err()) {
                return Err(rstd::move(dependency_source).unwrap_err());
            }
            auto dependency_id = resolve(*dependency_source, dependency.name.as_str());
            if (dependency_id.is_err()) {
                return Err(rstd::move(dependency_id).unwrap_err());
            }
            dependencies.push(ResolvedDependency {
                .name       = dependency.name.clone(),
                .package_id = rstd::move(dependency_id).unwrap(),
                .visibility = dependency.visibility,
            });
        }
        rstd::slice_::sort_unstable_by(
            dependencies.as_mut_slice().as_mut_ref(),
            [](const ResolvedDependency& left, const ResolvedDependency& right) {
                return left.package_id < right.package_id;
            });

        active_.remove(key.as_str());
        resolved_.insert(rstd::move(key), id.clone());
        packages_.push(ResolvedPackage {
            .id              = id.clone(),
            .source_id       = rstd::move(loaded.source_id),
            .source_manifest = rstd::move(loaded.manifest),
            .manifest        = rstd::move(loaded.package),
            .dependencies    = rstd::move(dependencies),
        });
        return Ok(rstd::move(id));
    }

    auto finish(Vec<String> root_ids, PathBuf manifest_path, bool root_is_workspace)
        -> ResolvedPackageGraph {
        rstd::slice_::sort_unstable_by(
            packages_.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
                return left.id < right.id;
            });
        rstd::slice_::sort_unstable(root_ids.as_mut_slice().as_mut_ref());
        return ResolvedPackageGraph {
            .root_ids          = rstd::move(root_ids),
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
    auto manifest_path     = resolver.source_manifest(root_source);
    auto root_is_workspace = resolver.source_is_workspace(root_source);
    auto names             = resolver.package_names(root_source);
    auto root_ids          = Vec<String>::with_capacity(names.len());
    for (const auto& name : names) {
        auto id = resolver.resolve(root_source, name.as_str());
        if (id.is_err()) return Err(rstd::move(id).unwrap_err());
        root_ids.push(rstd::move(id).unwrap());
    }
    return Ok(resolver.finish(rstd::move(root_ids), rstd::move(manifest_path), root_is_workspace));
}

} // namespace tenon
