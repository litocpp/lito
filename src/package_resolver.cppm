export module tenon.package_resolver;

import rstd;
import tenon.model;
import tenon.manifest_schema;

using namespace rstd::literals;

namespace tenon::package_resolver_detail
{

using StringMap = rstd::collections::BTreeMap<String, String>;
using StringSet = rstd::collections::BTreeMap<String, rstd::empty>;

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto failure(rstd::ref<rstd::str> message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Dependency, message));
}

auto path_text(rstd::ref<rstd::path::Path> path, rstd::ref<rstd::str> context)
    -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("{} '{}' is not valid UTF-8", context, path));
    }
    return rstd::Ok(String::make(*text));
}

auto path_components(rstd::ref<rstd::path::Path> path) -> Result<Vec<String>> {
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
    return rstd::Ok(rstd::move(result));
}

auto relative_directory(rstd::ref<rstd::path::Path> root,
                        rstd::ref<rstd::path::Path> target) -> Result<PathBuf> {
    auto root_components = path_components(root);
    auto target_components = path_components(target);
    if (root_components.is_err()) return rstd::Err(rstd::move(root_components).unwrap_err());
    if (target_components.is_err()) {
        return rstd::Err(rstd::move(target_components).unwrap_err());
    }
    auto roots = rstd::move(root_components).unwrap();
    auto targets = rstd::move(target_components).unwrap();
    rstd::usize common {};
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
    return rstd::Ok(relative.is_empty() ? PathBuf::from("."_str) : rstd::move(relative));
}

auto manifest_filename(rstd::ref<rstd::path::Path> path) -> Result<String> {
    auto name = path.file_name();
    if (name.is_none()) {
        return failure<String>(rstd::format("manifest path '{}' has no file name", path));
    }
    auto text = (*name).to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("manifest file name '{}' is not valid UTF-8", path));
    }
    return rstd::Ok(String::make(*text));
}

auto package_id(const PackageManifest& manifest, rstd::ref<rstd::path::Path> source) -> String {
    return rstd::format("{} {} (path+{})",
                        manifest.name.as_str(),
                        manifest.version.as_str(),
                        source);
}

class Resolver {
    PathBuf                 root_directory_;
    Vec<ResolvedPackage>    packages_;
    StringMap               resolved_ { StringMap::make() };
    StringSet               active_ { StringSet::make() };
    StringMap               package_names_ { StringMap::make() };

public:
    explicit Resolver(rstd::ref<rstd::path::Path> root_directory)
        : root_directory_(PathBuf::from(root_directory)) {}

    auto resolve(rstd::ref<rstd::path::Path> requested) -> Result<String> {
        auto canonical = rstd::fs::canonicalize(requested);
        if (canonical.is_err()) {
            return failure<String>(rstd::format(
                "cannot resolve package manifest '{}': {}",
                requested,
                rstd::move(canonical).unwrap_err()));
        }
        auto manifest_path = rstd::move(canonical).unwrap();
        auto key = path_text(manifest_path.as_path(), "manifest path"_str);
        if (key.is_err()) return rstd::Err(rstd::move(key).unwrap_err());
        auto path_key = rstd::move(key).unwrap();
        auto existing = resolved_.get(path_key.as_str());
        if (existing.is_some()) return rstd::Ok((**existing).clone());
        if (active_.contains_key(path_key.as_str())) {
            return failure<String>(rstd::format(
                "path dependency cycle reaches '{}'", manifest_path.as_path()));
        }
        auto extension = manifest_path.as_path().extension();
        if (extension.is_none() || (*extension).to_str().is_none() ||
            *(*extension).to_str() != "toml"_str) {
            return failure<String>(rstd::format(
                "path dependency manifest '{}' must end in .toml", manifest_path.as_path()));
        }

        auto loaded = load_package_manifest(manifest_path.as_path());
        if (loaded.is_err()) return rstd::Err(rstd::move(loaded).unwrap_err());
        auto manifest = rstd::move(loaded).unwrap();
        auto source_directory = relative_directory(
            root_directory_.as_path(), manifest.root.as_path());
        auto source_manifest = manifest_filename(manifest.manifest_path.as_path());
        if (source_directory.is_err()) {
            return rstd::Err(rstd::move(source_directory).unwrap_err());
        }
        if (source_manifest.is_err()) {
            return rstd::Err(rstd::move(source_manifest).unwrap_err());
        }
        auto source = rstd::move(source_directory).unwrap();
        auto id = package_id(manifest, source.as_path());

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
        active_.insert(path_key.clone(), rstd::empty {});

        auto dependencies = Vec<ResolvedDependency>::make();
        for (const auto& dependency : manifest.dependencies) {
            auto dependency_path = manifest.root.join(dependency.manifest.as_path());
            auto dependency_id = resolve(dependency_path.as_path());
            if (dependency_id.is_err()) {
                return rstd::Err(rstd::move(dependency_id).unwrap_err());
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
            .source_manifest = rstd::move(source_manifest).unwrap(),
            .manifest = rstd::move(manifest),
            .dependencies = rstd::move(dependencies),
        });
        return rstd::Ok(rstd::move(id));
    }

    auto finish(String root_id) -> ResolvedPackageGraph {
        rstd::slice_::sort_unstable_by(
            packages_.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
                return left.id < right.id;
            });
        return ResolvedPackageGraph {
            .root_id = rstd::move(root_id),
            .root_directory = rstd::move(root_directory_),
            .packages = rstd::move(packages_),
        };
    }
};

} // namespace tenon::package_resolver_detail

export namespace tenon
{

auto resolve_package_graph(rstd::ref<rstd::path::Path> requested_root)
    -> Result<ResolvedPackageGraph> {
    using namespace package_resolver_detail;

    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return failure<ResolvedPackageGraph>(rstd::format(
            "cannot resolve root manifest '{}': {}",
            requested_root,
            rstd::move(canonical).unwrap_err()));
    }
    auto root_manifest = rstd::move(canonical).unwrap();
    auto parent = root_manifest.as_path().parent();
    if (parent.is_none()) {
        return failure<ResolvedPackageGraph>("root manifest has no parent directory"_str);
    }
    auto resolver = Resolver(*parent);
    auto root_id = resolver.resolve(root_manifest.as_path());
    if (root_id.is_err()) return rstd::Err(rstd::move(root_id).unwrap_err());
    return rstd::Ok(resolver.finish(rstd::move(root_id).unwrap()));
}

} // namespace tenon
