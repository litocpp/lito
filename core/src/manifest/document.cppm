export module lito.core:manifest.document;

import rstd;
import :manifest.error;
import :manifest.package;
import :manifest.workspace;
import :source.tree;

using namespace rstd::prelude;

export namespace lito::manifest
{

enum class ManifestKind
{
    Package,
    Workspace,
};

struct ManifestDocument {
    ManifestKind              kind { ManifestKind::Package };
    Option<PackageManifest>   package;
    Option<WorkspaceManifest> workspace;
};

auto valid_package_name(ref<str> value) -> bool;

auto load_manifest_document(ref<rstd::path::Path> requested_directory)
    -> ManifestResult<ManifestDocument>;

auto load_package_manifest(ref<rstd::path::Path> requested_directory)
    -> ManifestResult<PackageManifest>;

auto load_package_manifest_from_source_tree(ref<str>                        source_identity,
                                            const lito::source::SourceTree& tree)
    -> ManifestResult<PackageManifest>;
auto load_package_manifest_from_source_tree_at(ref<str>                        source_identity,
                                               ref<rstd::path::Path>           root,
                                               const lito::source::SourceTree& tree)
    -> ManifestResult<PackageManifest>;

} // namespace lito::manifest
