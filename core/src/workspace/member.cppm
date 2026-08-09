export module lito.workspace:member;

import rstd;
import lito.model;
import lito.manifest;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto workspace_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto same_path(ref<rstd::path::Path> left, ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

} // namespace lito

export namespace lito
{

auto workspace_member_directory(const WorkspaceManifest& workspace,
                                ref<rstd::path::Path>    declared,
                                ref<str>                 context) -> Result<PathBuf> {
    auto requested = workspace.root.join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return workspace_failure<PathBuf>(rstd::format("cannot resolve {} directory '{}': {}",
                                                       context,
                                                       declared,
                                                       rstd::move(canonical).unwrap_err()));
    }
    auto directory = rstd::move(canonical).unwrap();
    if (directory.as_path().strip_prefix(workspace.root.as_path()).is_none()) {
        return workspace_failure<PathBuf>(
            rstd::format("{} directory '{}' is outside workspace root", context, declared));
    }
    return Ok(rstd::move(directory));
}

auto resolve_workspace_member_version(PackageManifest& manifest, const WorkspaceManifest& workspace)
    -> Result<empty> {
    if (manifest.version.value.is_some()) {
        return Ok(empty {});
    }
    if (workspace.package.version.is_none()) {
        return workspace_failure<empty>(
            rstd::format("workspace member '{}' inherits package.version but "
                         "workspace.package.version is not set",
                         manifest.name.as_str()));
    }
    manifest.version.value = Some(workspace.package.version->clone());
    return Ok(empty {});
}

auto resolve_containing_workspace_version(PackageManifest& manifest) -> Result<empty> {
    if (manifest.version.value.is_some()) {
        return Ok(empty {});
    }

    auto directory = manifest.root.clone();
    while (directory.pop()) {
        auto located = try_locate_manifest(directory.as_path());
        if (located.is_err()) return Err(rstd::move(located).unwrap_err());
        if (located->is_none()) continue;

        auto document = load_manifest_document(directory.as_path());
        if (document.is_err()) return Err(rstd::move(document).unwrap_err());
        auto loaded = rstd::move(document).unwrap();
        if (loaded.kind != ManifestKind::Workspace || loaded.workspace.is_none()) continue;
        auto workspace = rstd::move(loaded.workspace).unwrap();
        for (const auto& declared : workspace.members) {
            auto member =
                workspace_member_directory(workspace, declared.as_path(), "workspace member"_str);
            if (member.is_err()) return Err(rstd::move(member).unwrap_err());
            if (! same_path(member->as_path(), manifest.root.as_path())) continue;
            return resolve_workspace_member_version(manifest, workspace);
        }
    }

    return workspace_failure<empty>(
        rstd::format("package '{}' inherits package.version but no containing "
                     "workspace lists directory '{}'",
                     manifest.name.as_str(),
                     manifest.root.as_path()));
}

} // namespace lito
