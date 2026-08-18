module;
#include <rstd/macro.hpp>

export module lito.core:workspace.catalog;

import rstd;
import :workspace.error;
import :package.identity;
import :package.graph;
import :manifest.profile;
import :manifest;
import :workspace.member;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using PackageMap = rstd::collections::BTreeMap<String, lito::manifest::PackageManifest>;
using StringSet  = rstd::collections::BTreeMap<String, empty>;
using namespace lito;
using namespace lito::workspace;

template<typename T>
auto catalog_failure(String message) -> WorkspaceResult<T> {
    return Err(WorkspaceError::Message(rstd::move(message)));
}

auto path_text(ref<rstd::path::Path> path) -> WorkspaceResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return catalog_failure<String>(
            rstd::format("workspace path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto workspace_contains(const lito::manifest::WorkspaceManifest& workspace,
                        ref<rstd::path::Path> package_root) -> WorkspaceResult<bool> {
    for (const auto& declared : workspace.members) {
        auto member =
            workspace_member_directory(workspace, declared.as_path(), "workspace member"_str);
        if (member.is_err()) return Err(rstd::move(member).unwrap_err());
        if (same_path(member->as_path(), package_root)) return Ok(true);
    }
    return Ok(false);
}

auto test_only_package(const lito::manifest::PackageManifest& package) -> bool {
    if (package.targets.is_empty()) return ! package.compile_tests.is_empty();
    for (const auto& target : package.targets) {
        if (lito::manifest::package_target_kind(target) != lito::package::PackageTargetKind::Test)
            return false;
    }
    return true;
}

auto associated_package_matches(const lito::manifest::PackageManifest& package,
                                lito::package::ProjectRootRole         role) -> bool {
    if (role == lito::package::ProjectRootRole::AssociatedTest) return test_only_package(package);
    return false;
}

auto associated_declarations(lito::package::ProjectRootRole role) noexcept -> ref<str> {
    if (role == lito::package::ProjectRootRole::AssociatedTest)
        return "[[test]] or [compile-test]"_str;
    return "associated targets"_str;
}

export namespace lito::workspace
{

class WorkspaceCatalog {
    String                                    name_;
    PathBuf                                   root_;
    PathBuf                                   manifest_path_;
    lito::manifest::ProjectProfile            profile_;
    Vec<String>                               names_;
    Vec<String>                               default_names_;
    PackageMap                                packages_ { PackageMap::make() };
    StringSet                                 member_roots_ { StringSet::make() };
    Option<lito::manifest::WorkspaceManifest> workspace_manifest_;
    bool                                      workspace_ { false };
    bool                                      profile_declared_ { false };

public:
    WorkspaceCatalog() = default;

    static auto single(lito::manifest::PackageManifest manifest)
        -> WorkspaceResult<WorkspaceCatalog> {
        if (manifest.license.source == lito::manifest::PackageLicenseSource::Workspace &&
            manifest.license.value.is_none()) {
            return catalog_failure<WorkspaceCatalog>(
                rstd::format("package '{}' inherits package.license but is not a member of a "
                             "containing workspace",
                             manifest.name.as_str()));
        }
        if (! manifest.workspace_dependencies.is_empty() ||
            ! manifest.workspace_dev_dependencies.is_empty() ||
            ! manifest.workspace_runtime_dependencies.is_empty() ||
            ! manifest.workspace_pkg_config_external_dependencies.is_empty() ||
            ! manifest.workspace_cmake_external_dependencies.is_empty()) {
            return catalog_failure<WorkspaceCatalog>(rstd::format(
                "package '{}' inherits workspace dependencies but is not a member of a "
                "containing workspace",
                manifest.name.as_str()));
        }
        auto catalog           = WorkspaceCatalog {};
        catalog.name_          = manifest.name.clone();
        catalog.root_          = manifest.root.clone();
        catalog.manifest_path_ = manifest.manifest_path.clone();
        if (manifest.profile.is_some()) {
            catalog.profile_          = manifest.profile->clone();
            catalog.profile_declared_ = true;
        }
        catalog.names_.push(manifest.name.clone());
        auto root_text = manifest.root.as_path().to_str();
        if (root_text.is_some()) {
            catalog.member_roots_.insert(String::make(*root_text), empty {});
        }
        auto package_name = manifest.name.clone();
        catalog.packages_.insert(rstd::move(package_name), rstd::move(manifest));
        return Ok(rstd::move(catalog));
    }

    static auto associated_package(lito::manifest::PackageManifest manifest,
                                   const WorkspaceCatalog&         primary)
        -> WorkspaceResult<WorkspaceCatalog>;

    auto name() const noexcept -> ref<str> { return name_.as_str(); }

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }

    auto manifest_path() const noexcept -> ref<rstd::path::Path> {
        return manifest_path_.as_path();
    }

    auto names() const noexcept -> const Vec<String>& { return names_; }

    auto default_names() const noexcept -> const Vec<String>& { return default_names_; }

    auto is_workspace() const noexcept -> bool { return workspace_; }

    auto profile() const -> lito::manifest::ProjectProfile { return profile_.clone(); }

    auto take_package(ref<str> name) -> Option<lito::manifest::PackageManifest> {
        return packages_.remove(name);
    }

    auto contains_package_root(ref<rstd::path::Path> root) const -> bool {
        auto text = root.to_str();
        return text.is_some() && member_roots_.contains_key(*text);
    }

    friend auto load_workspace_catalog(lito::manifest::WorkspaceManifest       workspace,
                                       Option<lito::manifest::PackageManifest> preloaded)
        -> WorkspaceResult<WorkspaceCatalog>;
    friend auto validate_associated_catalog(const WorkspaceCatalog&        primary,
                                            const WorkspaceCatalog&        associated,
                                            lito::package::ProjectRootRole role)
        -> WorkspaceResult<empty>;
};

auto load_workspace_catalog(lito::manifest::WorkspaceManifest       workspace,
                            Option<lito::manifest::PackageManifest> preloaded = None())
    -> WorkspaceResult<WorkspaceCatalog> {
    auto catalog           = WorkspaceCatalog {};
    catalog.workspace_     = true;
    catalog.name_          = workspace.name.clone();
    catalog.root_          = workspace.root.clone();
    catalog.manifest_path_ = workspace.manifest_path.clone();
    if (workspace.profile.is_some()) {
        catalog.profile_          = workspace.profile->clone();
        catalog.profile_declared_ = true;
    }
    auto directories  = StringSet::make();
    auto member_names = rstd::collections::BTreeMap<String, String>::make();
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

        auto manifest = lito::manifest::PackageManifest {};
        if (preloaded.is_some() && same_path(preloaded->root.as_path(), directory.as_path())) {
            manifest = rstd::move(preloaded).unwrap();
        } else {
            auto document = lito::manifest::load_manifest_document(directory.as_path());
            if (document.is_err()) {
                return Err(rstd::into<WorkspaceError>(rstd::move(document).unwrap_err()));
            }
            auto loaded = rstd::move(document).unwrap();
            if (loaded.kind != lito::manifest::ManifestKind::Package || loaded.package.is_none()) {
                return catalog_failure<WorkspaceCatalog>(rstd::format(
                    "workspace member '{}' must contain a package manifest", declared.as_path()));
            }
            manifest = rstd::move(loaded.package).unwrap();
        }

        if (manifest.profile.is_some()) {
            return catalog_failure<WorkspaceCatalog>(rstd::format(
                "workspace member '{}' declares [profile]; project profile must be declared at "
                "the workspace root",
                manifest.name.as_str()));
        }

        rstd_try(resolve_workspace_member(manifest, workspace));
        if (catalog.packages_.contains_key(manifest.name.as_str())) {
            return catalog_failure<WorkspaceCatalog>(rstd::format(
                "workspace contains more than one package named '{}'", manifest.name.as_str()));
        }
        directories.insert(rstd::move(key).unwrap(), empty {});
        auto member_key = path_text(manifest.root.as_path());
        if (member_key.is_err()) return Err(rstd::move(member_key).unwrap_err());
        member_names.insert(member_key->clone(), manifest.name.clone());
        catalog.member_roots_.insert(rstd::move(member_key).unwrap(), empty {});
        catalog.names_.push(manifest.name.clone());
        auto package_name = manifest.name.clone();
        catalog.packages_.insert(rstd::move(package_name), rstd::move(manifest));
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
        auto name = member_names.get(key->as_str());
        if (name.is_none()) {
            return catalog_failure<WorkspaceCatalog>(rstd::format(
                "workspace default member '{}' has no package identity", declared.as_path()));
        }
        catalog.default_names_.push((**name).clone());
        defaults.insert(rstd::move(key).unwrap(), empty {});
    }
    rstd::slice_::sort_unstable(catalog.names_.as_mut_slice().as_mut_ref());
    rstd::slice_::sort_unstable(catalog.default_names_.as_mut_slice().as_mut_ref());
    catalog.workspace_manifest_ = Some(rstd::move(workspace));
    return Ok(rstd::move(catalog));
}

auto WorkspaceCatalog::associated_package(lito::manifest::PackageManifest manifest,
                                          const WorkspaceCatalog&         primary)
    -> WorkspaceResult<WorkspaceCatalog> {
    if (primary.workspace_manifest_.is_some()) {
        rstd_try(resolve_workspace_member(manifest, *primary.workspace_manifest_));
    }
    return WorkspaceCatalog::single(rstd::move(manifest));
}

auto validate_associated_catalog(const WorkspaceCatalog&        primary,
                                 const WorkspaceCatalog&        associated,
                                 lito::package::ProjectRootRole role) -> WorkspaceResult<empty> {
    if (role != lito::package::ProjectRootRole::AssociatedTest) {
        return catalog_failure<empty>(String::make("invalid associated catalog role"_str));
    }
    const auto kind = role;
    if (associated.profile_declared_) {
        return catalog_failure<empty>(rstd::format(
            "associated {} manifest '{}' declares [profile]; project profile belongs to '{}'",
            kind,
            associated.manifest_path_.as_path(),
            primary.manifest_path_.as_path()));
    }

    if (! primary.workspace_ && primary.names_.len() == usize(1)) {
        const auto root = primary.packages_.get(primary.names_[usize {}].as_str());
        if (root.is_some() && associated_package_matches(**root, role)) {
            return catalog_failure<empty>(
                rstd::format("primary package '{}' is already a {} artifact and cannot attach '{}'",
                             (**root).name.as_str(),
                             kind,
                             associated.manifest_path_.as_path()));
        }
    }

    for (const auto& name : associated.names_) {
        const auto package = associated.packages_.get(name.as_str());
        if (package.is_none()) {
            return catalog_failure<empty>(
                rstd::format("associated {} catalog is missing package '{}'", kind, name.as_str()));
        }
        const auto& manifest = **package;
        if (! associated_package_matches(manifest, role)) {
            return catalog_failure<empty>(
                rstd::format("associated {} package '{}' at '{}' may only declare {}",
                             kind,
                             manifest.name.as_str(),
                             manifest.manifest_path.as_path(),
                             associated_declarations(role)));
        }
        if (! associated.workspace_ &&
            manifest.version.source == lito::manifest::PackageVersionSource::Workspace &&
            manifest.version.value.is_none()) {
            return catalog_failure<empty>(rstd::format(
                "associated {} package '{}' at '{}' cannot inherit a workspace version",
                kind,
                manifest.name.as_str(),
                manifest.manifest_path.as_path()));
        }
        if (primary.packages_.contains_key(name.as_str())) {
            return catalog_failure<empty>(rstd::format(
                "associated {} package '{}' at '{}' conflicts with primary project manifest '{}'",
                kind,
                name.as_str(),
                manifest.manifest_path.as_path(),
                primary.manifest_path_.as_path()));
        }
        if (primary.contains_package_root(manifest.root.as_path())) {
            return catalog_failure<empty>(rstd::format(
                "associated {} package '{}' at '{}' is already owned by primary project "
                "manifest '{}'",
                kind,
                name.as_str(),
                manifest.manifest_path.as_path(),
                primary.manifest_path_.as_path()));
        }
    }
    return Ok(empty {});
}

auto try_containing_workspace(const lito::manifest::PackageManifest& manifest)
    -> WorkspaceResult<Option<lito::manifest::WorkspaceManifest>> {
    auto directory = manifest.root.clone();
    while (directory.pop()) {
        auto located = lito::manifest::try_locate_manifest(directory.as_path());
        if (located.is_err()) {
            return Err(WorkspaceError::Manifest(
                lito::manifest::ManifestError::Locate(rstd::move(located).unwrap_err())));
        }
        if (located->is_none()) continue;

        auto document = lito::manifest::load_manifest_document(directory.as_path());
        if (document.is_err()) {
            return Err(rstd::into<WorkspaceError>(rstd::move(document).unwrap_err()));
        }
        auto loaded = rstd::move(document).unwrap();
        if (loaded.kind != lito::manifest::ManifestKind::Workspace || loaded.workspace.is_none())
            continue;
        auto workspace = rstd::move(loaded.workspace).unwrap();
        auto contains  = workspace_contains(workspace, manifest.root.as_path());
        if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
        if (! *contains) continue;
        return Ok(Some(rstd::move(workspace)));
    }
    return Ok(None());
}

struct ResolvedProjectEntry {
    PathBuf          root;
    WorkspaceCatalog catalog;
};

auto resolve_project_entry(ref<rstd::path::Path> requested_root)
    -> WorkspaceResult<ResolvedProjectEntry> {
    auto document = rstd_try(lito::manifest::load_manifest_document(requested_root));
    if (document.kind == lito::manifest::ManifestKind::Workspace && document.workspace.is_some()) {
        auto catalog = rstd_try(load_workspace_catalog(rstd::move(document.workspace).unwrap()));
        return Ok(ResolvedProjectEntry {
            .root    = PathBuf::from(catalog.root()),
            .catalog = rstd::move(catalog),
        });
    }
    if (document.kind != lito::manifest::ManifestKind::Package || document.package.is_none()) {
        return catalog_failure<ResolvedProjectEntry>(
            String::make("project manifest has no package or workspace"_str));
    }
    auto package   = rstd::move(document.package).unwrap();
    auto workspace = rstd_try(try_containing_workspace(package));
    auto catalog   = workspace.is_some()
                         ? rstd_try(load_workspace_catalog(rstd::move(workspace).unwrap(),
                                                           Some(rstd::move(package))))
                         : rstd_try(WorkspaceCatalog::single(rstd::move(package)));
    return Ok(ResolvedProjectEntry {
        .root    = PathBuf::from(catalog.root()),
        .catalog = rstd::move(catalog),
    });
}

} // namespace lito::workspace
