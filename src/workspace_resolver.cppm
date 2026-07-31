export module tenon.workspace_resolver;

import rstd;
import tenon.model;
import tenon.manifest;
import tenon.package;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringMap = rstd::collections::BTreeMap<String, String>;
using IndexMap  = rstd::collections::BTreeMap<String, usize>;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, message));
}

auto path_text(ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("workspace path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto member_directory(const WorkspaceManifest& workspace,
                      ref<rstd::path::Path> declared,
                      ref<str> context) -> Result<PathBuf> {
    auto requested = workspace.root.join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return failure<PathBuf>(rstd::format(
            "cannot resolve {} directory '{}': {}",
            context,
            declared,
            rstd::move(canonical).unwrap_err()));
    }
    auto directory = rstd::move(canonical).unwrap();
    if (directory.as_path().strip_prefix(workspace.root.as_path()).is_none()) {
        return failure<PathBuf>(rstd::format(
            "{} directory '{}' is outside workspace root", context, declared));
    }
    return Ok(rstd::move(directory));
}

auto copy_strings(const Vec<String>& values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(values.len());
    for (const auto& value : values) result.push(value.clone());
    return result;
}

auto resolve_member_version(PackageManifest& manifest,
                            const WorkspaceManifest& workspace) -> Result<empty> {
    if (manifest.version.source == PackageVersionSource::Explicit) {
        return Ok(empty {});
    }
    if (workspace.package.version.is_none()) {
        return failure<empty>(rstd::format(
            "workspace member '{}' inherits package.version but workspace.package.version is not set",
            manifest.name.as_str()));
    }
    manifest.version.value = Some(workspace.package.version->clone());
    return Ok(empty {});
}

auto selected_closure(const ResolvedPackageGraph& graph,
                      const Vec<String>& selected_roots) -> Result<Vec<String>> {
    auto indices = IndexMap::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].id.clone(), index);
    }

    auto pending = copy_strings(selected_roots);
    auto selected = StringSet::make();
    while (! pending.is_empty()) {
        auto current = rstd::move(pending.pop()).unwrap();
        if (selected.contains_key(current.as_str())) continue;
        auto index = indices.get(current.as_str());
        if (index.is_none()) {
            return failure<Vec<String>>(rstd::format(
                "selected package id '{}' is missing from resolved graph", current.as_str()));
        }
        selected.insert(current.clone(), empty {});
        for (const auto& dependency : graph.packages[**index].dependencies) {
            pending.push(dependency.package_id.clone());
        }
    }

    auto result = Vec<String>::make();
    for (const auto& package : graph.packages) {
        if (selected.contains_key(package.id.as_str())) result.push(package.id.clone());
    }
    return Ok(rstd::move(result));
}

auto package_build(PackageManifest manifest,
                   const BuildRequest& request) -> Result<ResolvedBuild> {
    if (request.workspace || ! request.packages.is_empty()) {
        return failure<ResolvedBuild>(
            "--workspace and --package require a workspace directory"_str);
    }
    if (manifest.version.source == PackageVersionSource::Workspace) {
        return failure<ResolvedBuild>(rstd::format(
            "package '{}' inherits package.version and must be built from its workspace root",
            manifest.name.as_str()));
    }
    auto root = manifest.root.clone();
    auto manifest_path = manifest.manifest_path.clone();
    auto manifests = Vec<PackageManifest>::make();
    manifests.push(rstd::move(manifest));
    auto resolved = resolve_loaded_package_roots(
        root.as_path(), manifest_path.as_path(), rstd::move(manifests));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto graph = rstd::move(resolved).unwrap();
    auto selected_roots = copy_strings(graph.root_ids);
    auto selected_packages = Vec<String>::with_capacity(graph.packages.len());
    for (const auto& package : graph.packages) selected_packages.push(package.id.clone());
    return Ok(ResolvedBuild {
        .graph = rstd::move(graph),
        .selected_root_ids = rstd::move(selected_roots),
        .selected_package_ids = rstd::move(selected_packages),
    });
}

auto workspace_build(WorkspaceManifest workspace,
                     const BuildRequest& request) -> Result<ResolvedBuild> {
    if (request.workspace && ! request.packages.is_empty()) {
        return failure<ResolvedBuild>("--workspace cannot be combined with --package"_str);
    }

    auto member_keys = StringSet::make();
    auto member_directories = Vec<PathBuf>::with_capacity(workspace.members.len());
    auto manifests = Vec<PackageManifest>::with_capacity(workspace.members.len());
    for (const auto& declared : workspace.members) {
        auto canonical = member_directory(workspace, declared.as_path(), "workspace member"_str);
        if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
        auto directory = rstd::move(canonical).unwrap();
        auto key = path_text(directory.as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        if (member_keys.contains_key(key->as_str())) {
            return failure<ResolvedBuild>(rstd::format(
                "workspace member directory '{}' is listed more than once", declared.as_path()));
        }
        auto document = load_manifest_document(directory.as_path());
        if (document.is_err()) return Err(rstd::move(document).unwrap_err());
        auto loaded = rstd::move(document).unwrap();
        if (loaded.kind != ManifestKind::Package || loaded.package.is_none()) {
            return failure<ResolvedBuild>(rstd::format(
                "workspace member '{}' must contain a package manifest", declared.as_path()));
        }
        auto manifest = rstd::move(loaded.package).unwrap();
        auto version = resolve_member_version(manifest, workspace);
        if (version.is_err()) return Err(rstd::move(version).unwrap_err());
        member_keys.insert(rstd::move(key).unwrap(), empty {});
        member_directories.push(rstd::move(directory));
        manifests.push(rstd::move(manifest));
    }

    auto resolved = resolve_loaded_package_roots(
        workspace.root.as_path(), workspace.manifest_path.as_path(), rstd::move(manifests));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto graph = rstd::move(resolved).unwrap();

    auto member_ids = StringMap::make();
    auto member_names = StringMap::make();
    auto root_ids = StringSet::make();
    for (const auto& id : graph.root_ids) root_ids.insert(id.clone(), empty {});
    for (const auto& package : graph.packages) {
        if (! root_ids.contains_key(package.id.as_str())) continue;
        auto key = path_text(package.manifest.root.as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        member_ids.insert(rstd::move(key).unwrap(), package.id.clone());
        member_names.insert(package.manifest.name.clone(), package.id.clone());
    }
    if (member_ids.len() != member_directories.len()) {
        return failure<ResolvedBuild>("resolved workspace members are incomplete"_str);
    }

    auto default_ids = Vec<String>::make();
    auto default_keys = StringSet::make();
    for (const auto& declared : workspace.default_members) {
        auto canonical = member_directory(
            workspace, declared.as_path(), "workspace default member"_str);
        if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
        auto key = path_text(canonical->as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        if (default_keys.contains_key(key->as_str())) {
            return failure<ResolvedBuild>(rstd::format(
                "workspace default member directory '{}' is listed more than once",
                declared.as_path()));
        }
        auto id = member_ids.get(key->as_str());
        if (id.is_none()) {
            return failure<ResolvedBuild>(rstd::format(
                "workspace default member '{}' is not listed in workspace.members",
                declared.as_path()));
        }
        default_keys.insert(key->clone(), empty {});
        default_ids.push((**id).clone());
    }

    auto selected_roots = Vec<String>::make();
    if (request.workspace || (request.packages.is_empty() && default_ids.is_empty())) {
        selected_roots = copy_strings(graph.root_ids);
    } else if (! request.packages.is_empty()) {
        auto selected_names = StringSet::make();
        for (const auto& name : request.packages) {
            if (selected_names.contains_key(name.as_str())) {
                return failure<ResolvedBuild>(rstd::format(
                    "workspace package '{}' was selected more than once", name.as_str()));
            }
            auto id = member_names.get(name.as_str());
            if (id.is_none()) {
                return failure<ResolvedBuild>(rstd::format(
                    "workspace has no member package named '{}'", name.as_str()));
            }
            selected_names.insert(name.clone(), empty {});
            selected_roots.push((**id).clone());
        }
    } else {
        selected_roots = rstd::move(default_ids);
    }
    rstd::slice_::sort_unstable(selected_roots.as_mut_slice().as_mut_ref());

    auto selected_packages = selected_closure(graph, selected_roots);
    if (selected_packages.is_err()) {
        return Err(rstd::move(selected_packages).unwrap_err());
    }
    return Ok(ResolvedBuild {
        .graph = rstd::move(graph),
        .selected_root_ids = rstd::move(selected_roots),
        .selected_package_ids = rstd::move(selected_packages).unwrap(),
    });
}

} // namespace tenon

export namespace tenon
{

auto resolve_build_root(const BuildRequest& request) -> Result<ResolvedBuild> {
    auto document = load_manifest_document(request.root.as_path());
    if (document.is_err()) return Err(rstd::move(document).unwrap_err());
    auto loaded = rstd::move(document).unwrap();
    if (loaded.kind == ManifestKind::Package && loaded.package.is_some()) {
        return package_build(rstd::move(loaded.package).unwrap(), request);
    }
    if (loaded.kind == ManifestKind::Workspace && loaded.workspace.is_some()) {
        return workspace_build(rstd::move(loaded.workspace).unwrap(), request);
    }
    return failure<ResolvedBuild>("manifest document has no package or workspace"_str);
}

} // namespace tenon
