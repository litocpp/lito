export module tenon.workspace:catalog;

import rstd;
import tenon.model;
import tenon.manifest;
import :member;

using namespace rstd::prelude;
using namespace rstd::literals;
using PackageMap = rstd::collections::BTreeMap<String, tenon::PackageManifest>;
using StringSet  = rstd::collections::BTreeMap<String, empty>;

namespace tenon
{

template<typename T>
auto catalog_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto path_text(ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return catalog_failure<String>(
            rstd::format("workspace path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto workspace_contains(const WorkspaceManifest& workspace, ref<rstd::path::Path> package_root)
    -> Result<bool> {
    for (const auto& declared : workspace.members) {
        auto member =
            workspace_member_directory(workspace, declared.as_path(), "workspace member"_str);
        if (member.is_err()) return Err(rstd::move(member).unwrap_err());
        if (same_path(member->as_path(), package_root)) return Ok(true);
    }
    return Ok(false);
}

} // namespace tenon

export namespace tenon
{

class WorkspaceCatalog {
    PathBuf     root_;
    PathBuf     manifest_path_;
    Vec<String> names_;
    PackageMap  packages_ { PackageMap::make() };
    StringSet   member_roots_ { StringSet::make() };
    bool        workspace_ { false };

public:
    WorkspaceCatalog() = default;

    static auto single(PackageManifest manifest) -> WorkspaceCatalog {
        auto catalog           = WorkspaceCatalog {};
        catalog.root_          = manifest.root.clone();
        catalog.manifest_path_ = manifest.manifest_path.clone();
        catalog.names_.push(manifest.name.clone());
        auto root_text = manifest.root.as_path().to_str();
        if (root_text.is_some()) {
            catalog.member_roots_.insert(String::make(*root_text), empty {});
        }
        catalog.packages_.insert(manifest.name.clone(), rstd::move(manifest));
        return catalog;
    }

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }

    auto manifest_path() const noexcept -> ref<rstd::path::Path> {
        return manifest_path_.as_path();
    }

    auto names() const noexcept -> const Vec<String>& { return names_; }

    auto is_workspace() const noexcept -> bool { return workspace_; }

    auto take_package(ref<str> name) -> Option<PackageManifest> { return packages_.remove(name); }

    auto contains_package_root(ref<rstd::path::Path> root) const -> bool {
        auto text = root.to_str();
        return text.is_some() && member_roots_.contains_key(*text);
    }

    friend auto load_workspace_catalog(WorkspaceManifest       workspace,
                                       Option<PackageManifest> preloaded)
        -> Result<WorkspaceCatalog>;
};

auto load_workspace_catalog(WorkspaceManifest workspace, Option<PackageManifest> preloaded = None())
    -> Result<WorkspaceCatalog> {
    auto catalog           = WorkspaceCatalog {};
    catalog.workspace_     = true;
    catalog.root_          = workspace.root.clone();
    catalog.manifest_path_ = workspace.manifest_path.clone();
    auto directories       = StringSet::make();
    for (const auto& declared : workspace.members) {
        auto member =
            workspace_member_directory(workspace, declared.as_path(), "workspace member"_str);
        if (member.is_err()) return Err(rstd::move(member).unwrap_err());
        auto directory = rstd::move(member).unwrap();
        auto key       = path_text(directory.as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        if (directories.contains_key(key->as_str())) {
            return catalog_failure<WorkspaceCatalog>(rstd::format(
                "workspace member directory '{}' is listed more than once", declared.as_path()));
        }

        auto manifest = PackageManifest {};
        if (preloaded.is_some() && same_path(preloaded->root.as_path(), directory.as_path())) {
            manifest = rstd::move(preloaded).unwrap();
        } else {
            auto document = load_manifest_document(directory.as_path());
            if (document.is_err()) return Err(rstd::move(document).unwrap_err());
            auto loaded = rstd::move(document).unwrap();
            if (loaded.kind != ManifestKind::Package || loaded.package.is_none()) {
                return catalog_failure<WorkspaceCatalog>(rstd::format(
                    "workspace member '{}' must contain a package manifest", declared.as_path()));
            }
            manifest = rstd::move(loaded.package).unwrap();
        }

        auto version = resolve_workspace_member_version(manifest, workspace);
        if (version.is_err()) return Err(rstd::move(version).unwrap_err());
        if (catalog.packages_.contains_key(manifest.name.as_str())) {
            return catalog_failure<WorkspaceCatalog>(rstd::format(
                "workspace contains more than one package named '{}'", manifest.name.as_str()));
        }
        directories.insert(rstd::move(key).unwrap(), empty {});
        auto member_key = path_text(manifest.root.as_path());
        if (member_key.is_err()) return Err(rstd::move(member_key).unwrap_err());
        catalog.member_roots_.insert(rstd::move(member_key).unwrap(), empty {});
        catalog.names_.push(manifest.name.clone());
        catalog.packages_.insert(manifest.name.clone(), rstd::move(manifest));
    }
    auto defaults = StringSet::make();
    for (const auto& declared : workspace.default_members) {
        auto member = workspace_member_directory(
            workspace, declared.as_path(), "workspace default member"_str);
        if (member.is_err()) return Err(rstd::move(member).unwrap_err());
        auto key = path_text(member->as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        if (defaults.contains_key(key->as_str())) {
            return catalog_failure<WorkspaceCatalog>(
                rstd::format("workspace default member directory '{}' is listed more than once",
                             declared.as_path()));
        }
        if (! catalog.member_roots_.contains_key(key->as_str())) {
            return catalog_failure<WorkspaceCatalog>(
                rstd::format("workspace default member '{}' is not listed in workspace.members",
                             declared.as_path()));
        }
        defaults.insert(rstd::move(key).unwrap(), empty {});
    }
    rstd::slice_::sort_unstable(catalog.names_.as_mut_slice().as_mut_ref());
    return Ok(rstd::move(catalog));
}

auto try_containing_workspace(const PackageManifest& manifest)
    -> Result<Option<WorkspaceManifest>> {
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
        auto contains  = workspace_contains(workspace, manifest.root.as_path());
        if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
        if (! *contains) continue;
        return Ok(Some(rstd::move(workspace)));
    }
    return Ok(None());
}

} // namespace tenon
