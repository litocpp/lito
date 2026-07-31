export module tenon.package:resolver;

import rstd;
import tenon.model;
import tenon.manifest;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringMap   = rstd::collections::BTreeMap<String, String>;
using StringSet   = rstd::collections::BTreeMap<String, empty>;
using ManifestMap = rstd::collections::BTreeMap<String, tenon::PackageManifest>;

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto path_text(ref<rstd::path::Path> path, ref<str> context)
    -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("{} '{}' is not valid UTF-8", context, path));
    }
    return Ok(String::make(*text));
}

auto path_components(ref<rstd::path::Path> path) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto components = path.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_root_dir() || component->is_cur_dir()) continue;
        if (component->is_parent_dir()) {
            return failure<Vec<String>>(rstd::format(
                "canonical path '{}' contains a parent component", path));
        }
        auto text = component->as_os_str().to_str();
        if (text.is_none()) {
            return failure<Vec<String>>(rstd::format(
                "canonical path '{}' contains a non-UTF-8 component", path));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto relative_directory(ref<rstd::path::Path> root,
                        ref<rstd::path::Path> target) -> Result<PathBuf> {
    auto root_components = path_components(root);
    auto target_components = path_components(target);
    if (root_components.is_err()) return Err(rstd::move(root_components).unwrap_err());
    if (target_components.is_err()) {
        return Err(rstd::move(target_components).unwrap_err());
    }
    auto roots = rstd::move(root_components).unwrap();
    auto targets = rstd::move(target_components).unwrap();
    usize common {};
    while (common < roots.len() && common < targets.len() && roots[common] == targets[common]) {
        ++common;
    }
    auto relative = PathBuf::make();
    for (auto index = common; index < roots.len(); ++index) {
        relative.push(PathBuf::from(".."_str).as_path());
    }
    for (auto index = common; index < targets.len(); ++index) {
        relative.push(PathBuf::from(targets[index].as_str()).as_path());
    }
    return Ok(relative.is_empty() ? PathBuf::from("."_str) : rstd::move(relative));
}

auto package_id(const PackageManifest& manifest, ref<rstd::path::Path> source)
    -> Result<String> {
    if (manifest.version.value.is_none()) {
        return failure<String>(rstd::format(
            "package '{}' has an unresolved workspace version", manifest.name.as_str()));
    }
    return Ok(rstd::format("{} {} (path+{})",
                                 manifest.name.as_str(),
                                 manifest.version.value->as_str(),
                                 source));
}

class Resolver {
    PathBuf                 root_directory_;
    Vec<ResolvedPackage>    packages_;
    StringMap               resolved_ { StringMap::make() };
    StringSet               active_ { StringSet::make() };
    StringMap               package_names_ { StringMap::make() };
    ManifestMap             preloaded_ { ManifestMap::make() };

public:
    explicit Resolver(ref<rstd::path::Path> root_directory,
                      ManifestMap preloaded)
        : root_directory_(PathBuf::from(root_directory)),
          preloaded_(rstd::move(preloaded)) {}

    auto resolve(ref<rstd::path::Path> requested) -> Result<String> {
        auto canonical = rstd::fs::canonicalize(requested);
        if (canonical.is_err()) {
            return failure<String>(rstd::format(
                "cannot resolve package directory '{}': {}",
                requested,
                rstd::move(canonical).unwrap_err()));
        }
        auto package_directory = rstd::move(canonical).unwrap();
        auto key = path_text(package_directory.as_path(), "package directory"_str);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        auto path_key = rstd::move(key).unwrap();
        auto existing = resolved_.get(path_key.as_str());
        if (existing.is_some()) return Ok((**existing).clone());
        if (active_.contains_key(path_key.as_str())) {
            return failure<String>(rstd::format(
                "path dependency cycle reaches '{}'", package_directory.as_path()));
        }

        auto manifest = PackageManifest {};
        auto preloaded = preloaded_.remove(path_key.as_str());
        if (preloaded.is_some()) {
            manifest = rstd::move(preloaded).unwrap();
        } else {
            auto loaded = load_package_manifest(package_directory.as_path());
            if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
            manifest = rstd::move(loaded).unwrap();
        }
        auto source_directory = relative_directory(
            root_directory_.as_path(), manifest.root.as_path());
        if (source_directory.is_err()) {
            return Err(rstd::move(source_directory).unwrap_err());
        }
        auto source = rstd::move(source_directory).unwrap();
        auto package_identity = package_id(manifest, source.as_path());
        if (package_identity.is_err()) {
            return Err(rstd::move(package_identity).unwrap_err());
        }
        auto id = rstd::move(package_identity).unwrap();

        auto same_name = package_names_.get(manifest.name.as_str());
        if (same_name.is_some() && **same_name != path_key.as_str()) {
            return failure<String>(rstd::format(
                "package name '{}' resolves to both '{}' and '{}'",
                manifest.name.as_str(),
                (**same_name).as_str(),
                path_key.as_str()));
        }
        if (same_name.is_none()) {
            package_names_.insert(manifest.name.clone(), path_key.clone());
        }
        active_.insert(path_key.clone(), empty {});

        auto dependencies = Vec<ResolvedDependency>::make();
        for (const auto& dependency : manifest.dependencies) {
            auto dependency_directory = manifest.root.join(dependency.directory.as_path());
            auto dependency_id = resolve(dependency_directory.as_path());
            if (dependency_id.is_err()) {
                return Err(rstd::move(dependency_id).unwrap_err());
            }
            dependencies.push(ResolvedDependency {
                .alias = dependency.alias.clone(),
                .package_id = rstd::move(dependency_id).unwrap(),
                .visibility = dependency.visibility,
            });
        }
        rstd::slice_::sort_unstable_by(
            dependencies.as_mut_slice().as_mut_ref(),
            [](const ResolvedDependency& left, const ResolvedDependency& right) {
                return left.package_id < right.package_id;
            });

        active_.remove(path_key.as_str());
        resolved_.insert(rstd::move(path_key), id.clone());
        packages_.push(ResolvedPackage {
            .id = id.clone(),
            .source_directory = rstd::move(source),
            .manifest = rstd::move(manifest),
            .dependencies = rstd::move(dependencies),
        });
        return Ok(rstd::move(id));
    }

    auto finish(Vec<String> root_ids, PathBuf manifest_path) -> ResolvedPackageGraph {
        rstd::slice_::sort_unstable_by(
            packages_.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
                return left.id < right.id;
            });
        rstd::slice_::sort_unstable(root_ids.as_mut_slice().as_mut_ref());
        return ResolvedPackageGraph {
            .root_ids = rstd::move(root_ids),
            .root_directory = rstd::move(root_directory_),
            .manifest_path = rstd::move(manifest_path),
            .packages = rstd::move(packages_),
        };
    }
};

} // namespace tenon

export namespace tenon
{

auto resolve_loaded_package_roots(ref<rstd::path::Path> root_directory,
                                  ref<rstd::path::Path> manifest_path,
                                  Vec<PackageManifest> root_manifests)
    -> Result<ResolvedPackageGraph> {
    auto canonical_root = rstd::fs::canonicalize(root_directory);
    if (canonical_root.is_err()) {
        return failure<ResolvedPackageGraph>(rstd::format(
            "cannot resolve graph root directory '{}': {}",
            root_directory,
            rstd::move(canonical_root).unwrap_err()));
    }
    auto canonical_manifest = rstd::fs::canonicalize(manifest_path);
    if (canonical_manifest.is_err()) {
        return failure<ResolvedPackageGraph>(rstd::format(
            "cannot resolve graph manifest '{}': {}",
            manifest_path,
            rstd::move(canonical_manifest).unwrap_err()));
    }
    auto preloaded = ManifestMap::make();
    auto package_directories = Vec<PathBuf>::with_capacity(root_manifests.len());
    for (auto& manifest : root_manifests) {
        auto key = path_text(manifest.root.as_path(), "package directory"_str);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        if (preloaded.contains_key(key->as_str())) {
            return failure<ResolvedPackageGraph>(rstd::format(
                "package directory '{}' is listed more than once", manifest.root.as_path()));
        }
        package_directories.push(manifest.root.clone());
        preloaded.insert(rstd::move(key).unwrap(), rstd::move(manifest));
    }

    auto resolver = Resolver(canonical_root->as_path(), rstd::move(preloaded));
    auto root_ids = Vec<String>::with_capacity(package_directories.len());
    for (const auto& directory : package_directories) {
        auto root_id = resolver.resolve(directory.as_path());
        if (root_id.is_err()) return Err(rstd::move(root_id).unwrap_err());
        root_ids.push(rstd::move(root_id).unwrap());
    }
    return Ok(resolver.finish(
        rstd::move(root_ids), rstd::move(canonical_manifest).unwrap()));
}

auto resolve_package_roots(ref<rstd::path::Path> root_directory,
                           ref<rstd::path::Path> manifest_path,
                           const Vec<PathBuf>& package_directories)
    -> Result<ResolvedPackageGraph> {
    auto manifests = Vec<PackageManifest>::with_capacity(package_directories.len());
    for (const auto& directory : package_directories) {
        auto loaded = load_package_manifest(directory.as_path());
        if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
        manifests.push(rstd::move(loaded).unwrap());
    }
    return resolve_loaded_package_roots(
        root_directory, manifest_path, rstd::move(manifests));
}

auto resolve_package_graph(ref<rstd::path::Path> requested_root)
    -> Result<ResolvedPackageGraph> {
    auto location = locate_manifest(requested_root);
    if (location.is_err()) return Err(rstd::move(location).unwrap_err());
    auto root = rstd::move(location).unwrap();
    auto roots = Vec<PathBuf>::make();
    roots.push(root.directory.clone());
    return resolve_package_roots(
        root.directory.as_path(), root.manifest.as_path(), roots);
}

} // namespace tenon
