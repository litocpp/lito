module;
#include <rstd/macro.hpp>

export module lito.driver:registry.source;

import rstd;
import lito.core;
import lito.pack;
import :registry.blob;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

struct MaterializedRegistrySource {
    lito::source::ResolvedPackageSource source;
    lito::workspace::WorkspaceCatalog   catalog;
};

class RegistrySourceResolver {
    PathBuf           cache_root_;
    RegistryBlobCache blobs_;

public:
    RegistrySourceResolver(PathBuf                          cache_root,
                           RegistryDownloadEndpointTemplate download_endpoint,
                           RegistryNetworkPolicy            network,
                           RegistryBlobTransport            transport,
                           const Vec<PathBuf>*              source_bundles = nullptr)
        : cache_root_(cache_root.clone()),
          blobs_(rstd::move(cache_root),
                 rstd::move(download_endpoint),
                 network,
                 transport,
                 source_bundles) {}

    auto materialize(const RegistryReleasePin& pin)
        -> RegistryArtifactResult<MaterializedRegistrySource>;
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;

template<typename T>
auto source_failure(RegistryArtifactErrorKind kind,
                    const RegistryPackageId&  package,
                    String                    message) -> RegistryArtifactResult<T> {
    return Err(RegistryArtifactError {
        .kind    = kind,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto source_failure(RegistryArtifactErrorKind kind,
                    const RegistryPackageId&  package,
                    ref<str>                  message) -> RegistryArtifactResult<T> {
    return source_failure<T>(kind, package, String::make(message));
}

struct SourceLayout {
    PathBuf tree;
    PathBuf marker;
    PathBuf lock;
};

auto source_layout(ref<rstd::path::Path> root, const RegistryReleasePin& pin) -> SourceLayout {
    auto cache = RegistryCacheLayout(PathBuf::from(root));
    return SourceLayout {
        .tree   = cache.tree(pin),
        .marker = cache.tree_marker(pin),
        .lock   = cache.release_lock(pin),
    };
}

auto acquire_source_lock(const SourceLayout& layout, const RegistryPackageId& package)
    -> RegistryArtifactResult<rstd::fs::FileLock> {
    auto tree_parent = layout.tree.as_path().parent().unwrap();
    auto created     = rstd::fs::create_dir_all(tree_parent);
    if (created.is_err()) {
        return source_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot create Registry tree cache directory '{}': {}",
                         tree_parent,
                         rstd::move(created).unwrap_err()));
    }
    auto lock_parent = layout.lock.as_path().parent().unwrap();
    created          = rstd::fs::create_dir_all(lock_parent);
    if (created.is_err()) {
        return source_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot create Registry cache lock directory '{}': {}",
                         lock_parent,
                         rstd::move(created).unwrap_err()));
    }
    auto file = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
        layout.lock.as_path());
    if (file.is_err()) {
        return source_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot open Registry source cache lock '{}': {}",
                         layout.lock.as_path(),
                         rstd::move(file).unwrap_err()));
    }
    auto lock =
        rstd::fs::FileLock::acquire(rstd::move(file).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (lock.is_err()) {
        return source_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot lock Registry source cache '{}': {}",
                         layout.lock.as_path(),
                         rstd::move(lock).unwrap_err()));
    }
    return Ok(rstd::move(lock).unwrap());
}

auto reusable_tree(const SourceLayout&      layout,
                   const RegistryPackageId& package,
                   const PackageChecksum&   checksum) -> RegistryArtifactResult<bool> {
    auto marker_metadata = rstd::fs::symlink_metadata(layout.marker.as_path());
    if (marker_metadata.is_err()) {
        auto error = rstd::move(marker_metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(false);
        }
        return source_failure<bool>(RegistryArtifactErrorKind::Io,
                                    package,
                                    rstd::format("cannot inspect Registry tree marker '{}': {}",
                                                 layout.marker.as_path(),
                                                 error));
    }
    if (! marker_metadata->is_file() || marker_metadata->is_symlink()) {
        return source_failure<bool>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry tree marker '{}' is not an ordinary file",
                         layout.marker.as_path()));
    }
    auto text = rstd::fs::read_to_string(layout.marker.as_path());
    if (text.is_err()) {
        return source_failure<bool>(RegistryArtifactErrorKind::Io,
                                    package,
                                    rstd::format("cannot read Registry tree marker '{}': {}",
                                                 layout.marker.as_path(),
                                                 rstd::move(text).unwrap_err()));
    }
    auto expected = checksum.text();
    expected.push_ascii('\n');
    if (*text != expected.as_str()) return Ok(false);

    auto tree = rstd::fs::symlink_metadata(layout.tree.as_path());
    if (tree.is_err()) {
        auto error = rstd::move(tree).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(false);
        }
        return source_failure<bool>(RegistryArtifactErrorKind::Io,
                                    package,
                                    rstd::format("cannot inspect Registry source tree '{}': {}",
                                                 layout.tree.as_path(),
                                                 error));
    }
    if (! tree->is_dir() || tree->is_symlink()) {
        return source_failure<bool>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry source tree '{}' is not an ordinary directory",
                         layout.tree.as_path()));
    }
    return Ok(true);
}

auto set_read_only(const lito::source::SourceTree& tree,
                   ref<rstd::path::Path>           root,
                   const RegistryPackageId&        package) -> RegistryArtifactResult<empty> {
    for (const auto& entry : tree.entries()) {
        if (entry.kind() != lito::source::SourceEntryKind::File) continue;
        auto path     = PathBuf::from(root).join(entry.path().as_path());
        auto metadata = rstd::fs::metadata(path.as_path());
        if (metadata.is_err()) {
            return source_failure<empty>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot inspect materialized Registry source '{}': {}",
                             path.as_path(),
                             rstd::move(metadata).unwrap_err()));
        }
        auto permissions = metadata->permissions();
        permissions.set_readonly(true);
        auto changed = rstd::fs::set_permissions(path.as_path(), permissions);
        if (changed.is_err()) {
            return source_failure<empty>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot make Registry source file '{}' read-only: {}",
                             path.as_path(),
                             rstd::move(changed).unwrap_err()));
        }
    }
    return Ok(empty {});
}

} // namespace

auto lito::registry::RegistrySourceResolver::materialize(const RegistryReleasePin& pin)
    -> RegistryArtifactResult<MaterializedRegistrySource> {
    const auto& package   = pin.release.package;
    auto        layout    = source_layout(cache_root_.as_path(), pin);
    auto        blob      = rstd_try(blobs_.acquire(pin));
    auto        inspected = PackageArchiveInspector::inspect_at_root(
        blob, package, pin.release.version, layout.tree.as_path());
    if (inspected.is_err()) return Err(rstd::move(inspected).unwrap_err());
    auto lock = rstd_try(acquire_source_lock(layout, package));
    (void)lock;
    auto reusable = rstd_try(reusable_tree(layout, package, pin.checksum));
    if (! reusable) {
        auto marker = rstd::fs::symlink_metadata(layout.marker.as_path());
        if (marker.is_ok()) {
            if (! marker->is_file() || marker->is_symlink()) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("Registry tree marker '{}' is not an ordinary file",
                                 layout.marker.as_path()));
            }
            auto removed = rstd::fs::remove_file(layout.marker.as_path());
            if (removed.is_err()) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot remove incomplete Registry tree marker '{}': {}",
                                 layout.marker.as_path(),
                                 rstd::move(removed).unwrap_err()));
            }
        } else {
            auto error = rstd::move(marker).unwrap_err();
            if (error.kind() !=
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot inspect Registry tree marker '{}': {}",
                                 layout.marker.as_path(),
                                 error));
            }
        }

        auto existing = rstd::fs::symlink_metadata(layout.tree.as_path());
        if (existing.is_ok()) {
            if (! existing->is_dir() || existing->is_symlink()) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("Registry source tree '{}' is not an ordinary directory",
                                 layout.tree.as_path()));
            }
            auto removed = rstd::fs::remove_dir_all(layout.tree.as_path());
            if (removed.is_err()) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot remove incomplete Registry source cache '{}': {}",
                                 layout.tree.as_path(),
                                 rstd::move(removed).unwrap_err()));
            }
        } else {
            auto error = rstd::move(existing).unwrap_err();
            if (error.kind() !=
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot inspect Registry source tree '{}': {}",
                                 layout.tree.as_path(),
                                 error));
            }
        }
        auto now     = rstd::time::SystemTime::now().as_unix_time();
        auto staging = PathBuf::from(layout.tree.as_path().parent().unwrap())
                           .join(PathBuf::from(rstd::format(".registry-tree.tmp.{}.{}.{}",
                                                            rstd::process::id(),
                                                            now.seconds,
                                                            now.nanoseconds))
                                     .as_path());
        auto materialized =
            lito::source::materialize_source_tree(inspected->tree, staging.as_path());
        if (materialized.is_err()) {
            return source_failure<MaterializedRegistrySource>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot materialize Registry source tree: {}",
                             rstd::move(materialized).unwrap_err()));
        }
        auto read_only = set_read_only(inspected->tree, staging.as_path(), package);
        if (read_only.is_err()) {
            (void)rstd::fs::remove_dir_all(staging.as_path());
            return Err(rstd::move(read_only).unwrap_err());
        }
        auto committed = rstd::fs::rename(staging.as_path(), layout.tree.as_path());
        if (committed.is_err()) {
            (void)rstd::fs::remove_dir_all(staging.as_path());
            return source_failure<MaterializedRegistrySource>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot commit Registry source cache '{}': {}",
                             layout.tree.as_path(),
                             rstd::move(committed).unwrap_err()));
        }
        auto marker_text = pin.checksum.text();
        marker_text.push_ascii('\n');
        auto written =
            rstd::fs::write_atomic(layout.marker.as_path(), marker_text.as_str().as_bytes());
        if (written.is_err()) {
            return source_failure<MaterializedRegistrySource>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot write Registry tree marker '{}': {}",
                             layout.marker.as_path(),
                             rstd::move(written).unwrap_err()));
        }
    }
    auto catalog = lito::workspace::WorkspaceCatalog::single(
        rstd::move(inspected).unwrap().candidate.manifest);
    if (catalog.is_err()) {
        return source_failure<MaterializedRegistrySource>(
            RegistryArtifactErrorKind::Manifest,
            package,
            rstd::format("cannot construct Registry package catalog: {}",
                         rstd::move(catalog).unwrap_err()));
    }
    return Ok(MaterializedRegistrySource {
        .source =
            lito::source::ResolvedPackageSource {
                .identity       = lito::source::registry_source_identity(pin),
                .kind           = lito::source::PackageSourceKind::Registry,
                .root_directory = layout.tree.clone(),
                .registry       = Some(pin.clone()),
            },
        .catalog = rstd::move(catalog).unwrap(),
    });
}
