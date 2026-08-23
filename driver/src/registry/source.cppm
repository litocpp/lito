module;
#include <rstd/macro.hpp>

export module lito.driver:registry.source;

import rstd;
import rstd.json;
import lito.core;
import :registry.archive;
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
    RegistrySourceResolver(PathBuf                  cache_root,
                           RegistryEndpointTemplate blob_endpoint,
                           RegistryNetworkPolicy    network,
                           RegistryBlobTransport    transport)
        : cache_root_(cache_root.clone()),
          blobs_(rstd::move(cache_root), rstd::move(blob_endpoint), network, transport) {}

    auto materialize(const RegistryPackageId& package, const RegistryReleaseProjection& release)
        -> RegistryArtifactResult<MaterializedRegistrySource>;
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

inline constexpr auto SOURCE_RECEIPT_SCHEMA = "lito.registry.source-receipt.v1"_str;

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
    PathBuf bucket;
    PathBuf tree;
    PathBuf receipt;
    PathBuf lock;
};

auto source_layout(ref<rstd::path::Path> root, const SourceDigest& digest) -> SourceLayout {
    auto bucket = PathBuf::from(root)
                      .join(PathBuf::from("registry"_str).as_path())
                      .join(PathBuf::from("sources"_str).as_path())
                      .join(PathBuf::from("sha256"_str).as_path())
                      .join(PathBuf::from(digest.digest().to_hex()).as_path());
    return SourceLayout {
        .bucket  = bucket.clone(),
        .tree    = bucket.join(PathBuf::from("tree"_str).as_path()),
        .receipt = bucket.join(PathBuf::from("receipt.json"_str).as_path()),
        .lock    = bucket.join(PathBuf::from("lock"_str).as_path()),
    };
}

auto acquire_source_lock(const SourceLayout& layout, const RegistryPackageId& package)
    -> RegistryArtifactResult<rstd::fs::FileLock> {
    auto created = rstd::fs::create_dir_all(layout.bucket.as_path());
    if (created.is_err()) {
        return source_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot create Registry source cache directory '{}': {}",
                         layout.bucket.as_path(),
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

auto required_string(const Json& value, ref<str> field) -> Option<ref<str>> {
    auto member = value.get(field);
    if (member.is_none()) return None();
    return (**member).as_str();
}

auto receipt_matches(const SourceLayout&              layout,
                     const RegistryPackageId&         package,
                     const RegistryReleaseProjection& release) -> RegistryArtifactResult<bool> {
    auto text = rstd::fs::read_to_string(layout.receipt.as_path());
    if (text.is_err()) {
        auto error = rstd::move(text).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(false);
        }
        return source_failure<bool>(RegistryArtifactErrorKind::Io,
                                    package,
                                    rstd::format("cannot read Registry source receipt '{}': {}",
                                                 layout.receipt.as_path(),
                                                 error));
    }
    auto parsed = rstd::json::from_str(text->as_str(),
                                       rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) return Ok(false);
    auto object = parsed->as_object();
    if (object.is_none() || (**object).len() != usize(9)) return Ok(false);
    auto schema         = required_string(*parsed, "schema"_str);
    auto registry       = required_string(*parsed, "registry"_str);
    auto name           = required_string(*parsed, "package"_str);
    auto version        = required_string(*parsed, "version"_str);
    auto release_digest = required_string(*parsed, "release"_str);
    auto source         = required_string(*parsed, "source"_str);
    auto manifest       = required_string(*parsed, "manifest"_str);
    auto blob           = required_string(*parsed, "blob"_str);
    auto format         = required_string(*parsed, "format"_str);
    if (schema.is_none() || registry.is_none() || name.is_none() || version.is_none() ||
        release_digest.is_none() || source.is_none() || manifest.is_none() || blob.is_none() ||
        format.is_none() || *schema != SOURCE_RECEIPT_SCHEMA ||
        *registry != package.registry.as_str() || *name != package.name.as_str() ||
        *version != release.version.text() || *release_digest != release.release.text() ||
        *source != release.source.text() || *manifest != release.manifest.text() ||
        *blob != release.blob.digest.text() || *format != release.blob.format.as_str()) {
        return Ok(false);
    }
    auto metadata = rstd::fs::symlink_metadata(layout.tree.as_path());
    return Ok(metadata.is_ok() && metadata->is_dir() && ! metadata->is_symlink());
}

auto receipt_json(const RegistryPackageId& package, const RegistryReleaseProjection& release)
    -> String {
    const auto string_value = [](ref<str> value) -> Json {
        return Json::String(String::make(value));
    };
    auto value = JsonMap::make();
    value.insert(String::make("schema"_str), string_value(SOURCE_RECEIPT_SCHEMA));
    value.insert(String::make("registry"_str), string_value(package.registry.as_str()));
    value.insert(String::make("package"_str), string_value(package.name.as_str()));
    value.insert(String::make("version"_str), string_value(release.version.text()));
    value.insert(String::make("release"_str), string_value(release.release.text().as_str()));
    value.insert(String::make("source"_str), string_value(release.source.text().as_str()));
    value.insert(String::make("manifest"_str), string_value(release.manifest.text().as_str()));
    value.insert(String::make("blob"_str), string_value(release.blob.digest.text().as_str()));
    value.insert(String::make("format"_str), string_value(release.blob.format.as_str()));
    auto text = rstd::json::to_string(Json::Object(rstd::move(value)));
    text.push_ascii('\n');
    return text;
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

auto lito::registry::RegistrySourceResolver::materialize(const RegistryPackageId&         package,
                                                         const RegistryReleaseProjection& release)
    -> RegistryArtifactResult<MaterializedRegistrySource> {
    auto layout = source_layout(cache_root_.as_path(), release.source);
    auto blob   = rstd_try(blobs_.acquire(package, release.blob));
    auto inspected =
        PackageArchiveInspector::inspect_at_root(blob, package, release, layout.tree.as_path());
    if (inspected.is_err()) return Err(rstd::move(inspected).unwrap_err());
    auto lock = rstd_try(acquire_source_lock(layout, package));
    (void)lock;
    auto reusable = rstd_try(receipt_matches(layout, package, release));
    if (! reusable) {
        auto existing = rstd::fs::exists(layout.tree.as_path());
        if (existing.is_err()) {
            return source_failure<MaterializedRegistrySource>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot inspect Registry source cache '{}': {}",
                             layout.tree.as_path(),
                             rstd::move(existing).unwrap_err()));
        }
        if (*existing) {
            auto removed = rstd::fs::remove_dir_all(layout.tree.as_path());
            if (removed.is_err()) {
                return source_failure<MaterializedRegistrySource>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot remove incomplete Registry source cache '{}': {}",
                                 layout.tree.as_path(),
                                 rstd::move(removed).unwrap_err()));
            }
        }
        auto now     = rstd::time::SystemTime::now().as_unix_time();
        auto staging = layout.bucket.join(PathBuf::from(rstd::format("tree-staging-{}-{}-{}",
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
        auto receipt = receipt_json(package, release);
        auto written =
            rstd::fs::write_atomic(layout.receipt.as_path(), receipt.as_str().as_bytes());
        if (written.is_err()) {
            return source_failure<MaterializedRegistrySource>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot write Registry source receipt '{}': {}",
                             layout.receipt.as_path(),
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
                .identity       = lito::source::registry_source_identity(package, release.version),
                .kind           = lito::source::PackageSourceKind::Registry,
                .root_directory = layout.tree.clone(),
                .registry_package = Some(package.clone()),
                .registry_version = Some(release.version.clone()),
                .release_digest   = Some(release.release.clone()),
                .source_digest    = Some(release.source.clone()),
                .manifest_digest  = Some(release.manifest.clone()),
                .blob_digest      = Some(release.blob.digest.clone()),
                .blob_size        = Some(release.blob.size.clone()),
                .archive_format   = Some(release.blob.format.clone()),
            },
        .catalog = rstd::move(catalog).unwrap(),
    });
}
