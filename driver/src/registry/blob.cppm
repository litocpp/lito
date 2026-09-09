module;
#include <rstd/macro.hpp>

export module lito.driver:registry.blob;

import rstd;
import lito.core;
import lito.pack;
import lito.system;
import :registry.index;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

struct RegistryBlobDownloadRequest {
    RegistryPackageId package;
    String            url;
    PathBuf           destination;
};

struct RegistryBlobTransport {
    void* context {};
    RegistryArtifactResult<empty> (*download)(void*,
                                              const RegistryBlobDownloadRequest&) noexcept {};
};

class RegistryBlobCache {
    PathBuf                          cache_root_;
    RegistryDownloadEndpointTemplate endpoint_;
    RegistryNetworkPolicy            network_ { RegistryNetworkPolicy::Online };
    RegistryBlobTransport            transport_;
    const Vec<PathBuf>*              source_bundles_ {};

public:
    RegistryBlobCache(PathBuf                          cache_root,
                      RegistryDownloadEndpointTemplate endpoint,
                      RegistryNetworkPolicy            network,
                      RegistryBlobTransport            transport,
                      const Vec<PathBuf>*              source_bundles = nullptr)
        : cache_root_(rstd::move(cache_root)),
          endpoint_(rstd::move(endpoint)),
          network_(network),
          transport_(transport),
          source_bundles_(source_bundles) {}

    auto acquire(const RegistryReleasePin& pin) -> RegistryArtifactResult<VerifiedRegistryBlob>;
    auto publish(const RegistryReleasePin& pin, slice<u8> contents)
        -> RegistryArtifactResult<VerifiedRegistryBlob>;
};

class CurlRegistryBlobTransport {
    PathBuf                                         executable_;
    const lito::system::ResolvedProcessEnvironment* environment_ {};

    static auto download_callback(void*, const RegistryBlobDownloadRequest&) noexcept
        -> RegistryArtifactResult<empty>;

public:
    CurlRegistryBlobTransport(PathBuf                                         executable,
                              const lito::system::ResolvedProcessEnvironment& environment)
        : executable_(rstd::move(executable)), environment_(rstd::addressof(environment)) {}

    auto download(const RegistryBlobDownloadRequest& request) -> RegistryArtifactResult<empty>;
    auto transport() noexcept -> RegistryBlobTransport {
        return RegistryBlobTransport { .context = this, .download = download_callback };
    }
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;

template<typename T>
auto artifact_failure(RegistryArtifactErrorKind kind,
                      const RegistryPackageId&  package,
                      String                    message) -> RegistryArtifactResult<T> {
    return Err(RegistryArtifactError {
        .kind    = kind,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto artifact_failure(RegistryArtifactErrorKind kind,
                      const RegistryPackageId&  package,
                      ref<str>                  message) -> RegistryArtifactResult<T> {
    return artifact_failure<T>(kind, package, String::make(message));
}

struct BlobLayout {
    PathBuf source;
    PathBuf lock;
};

auto blob_layout(ref<rstd::path::Path> root, const RegistryReleasePin& pin) -> BlobLayout {
    auto cache = RegistryCacheLayout(PathBuf::from(root));
    return BlobLayout {
        .source = cache.archive(pin),
        .lock   = cache.release_lock(pin),
    };
}

auto ordinary_file(ref<rstd::path::Path> path, const RegistryPackageId& package)
    -> RegistryArtifactResult<Option<rstd::fs::Metadata>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_ok()) {
        if (! metadata->is_file() || metadata->is_symlink()) {
            return artifact_failure<Option<rstd::fs::Metadata>>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("Registry blob path '{}' is not an ordinary file", path));
        }
        return Ok(Some(rstd::move(metadata).unwrap()));
    }
    auto error = rstd::move(metadata).unwrap_err();
    if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return Ok(Option<rstd::fs::Metadata> {});
    }
    return artifact_failure<Option<rstd::fs::Metadata>>(
        RegistryArtifactErrorKind::Io,
        package,
        rstd::format("cannot inspect Registry blob path '{}': {}", path, error));
}

auto reserve_staging(const BlobLayout& layout, const RegistryPackageId& package)
    -> RegistryArtifactResult<PathBuf> {
    auto parent  = layout.source.as_path().parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) {
        return artifact_failure<PathBuf>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot create Registry source cache directory '{}': {}",
                         parent,
                         rstd::move(created).unwrap_err()));
    }
    auto time = rstd::time::SystemTime::now().as_unix_time();
    for (usize attempt {}; attempt < usize(64); ++attempt) {
        auto staging = PathBuf::from(parent).join(
            PathBuf::from(rstd::format(".registry-source.tmp.{}.{}.{}.{}",
                                       rstd::process::id(),
                                       time.seconds,
                                       time.nanoseconds,
                                       attempt))
                .as_path());
        auto file = rstd::fs::File::create_new(staging.as_path());
        if (file.is_ok()) return Ok(rstd::move(staging));
        auto error = rstd::move(file).unwrap_err();
        if (error.kind() !=
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::AlreadyExists }) {
            return artifact_failure<PathBuf>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot reserve Registry blob staging file '{}': {}",
                             staging.as_path(),
                             error));
        }
    }
    return artifact_failure<PathBuf>(
        RegistryArtifactErrorKind::Io, package, "cannot reserve Registry blob staging file"_str);
}

auto acquire_lock(const BlobLayout& layout, const RegistryPackageId& package)
    -> RegistryArtifactResult<rstd::fs::FileLock> {
    auto parent  = layout.lock.as_path().parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) {
        return artifact_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot create Registry cache lock directory '{}': {}",
                         parent,
                         rstd::move(created).unwrap_err()));
    }
    auto opened = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
        layout.lock.as_path());
    if (opened.is_err()) {
        return artifact_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot open Registry blob lock '{}': {}",
                         layout.lock.as_path(),
                         rstd::move(opened).unwrap_err()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return artifact_failure<rstd::fs::FileLock>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot acquire Registry blob lock '{}': {}",
                         layout.lock.as_path(),
                         rstd::move(locked).unwrap_err()));
    }
    return Ok(rstd::move(locked).unwrap());
}

struct CachedBlobProbe {
    Option<VerifiedRegistryBlob> verified;
    bool                         corrupt {};
};

auto probe_blob(const BlobLayout&        layout,
                const RegistryPackageId& package,
                const PackageChecksum&   checksum) -> RegistryArtifactResult<CachedBlobProbe> {
    auto metadata = rstd_try(ordinary_file(layout.source.as_path(), package));
    if (metadata.is_none()) return Ok(CachedBlobProbe {});
    if (metadata->len() == u64 {} || metadata->len() > MAX_REGISTRY_PACKAGE_ARCHIVE_BYTES) {
        return Ok(CachedBlobProbe { .corrupt = true });
    }
    auto verified = rstd_try(registry_blob_from_file(layout.source.clone(), package));
    if (verified.checksum == checksum) {
        return Ok(CachedBlobProbe { .verified = Some(rstd::move(verified)) });
    }
    auto expected_prefix = checksum.text();
    expected_prefix.truncate(REGISTRY_CHECKSUM_PREFIX_LENGTH);
    auto actual = verified.checksum.text();
    if (actual.as_str().starts_with(expected_prefix.as_str())) {
        return artifact_failure<CachedBlobProbe>(
            RegistryArtifactErrorKind::Digest,
            package,
            rstd::format("Registry cache filename '{}' has a short checksum collision: expected "
                         "'{}', found '{}'",
                         layout.source.as_path(),
                         checksum.text(),
                         actual.as_str()));
    }
    return Ok(CachedBlobProbe { .corrupt = true });
}

auto set_read_only(ref<rstd::path::Path> path, const RegistryPackageId& package)
    -> RegistryArtifactResult<empty> {
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return artifact_failure<empty>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot inspect Registry blob staging file '{}': {}",
                         path,
                         rstd::move(metadata).unwrap_err()));
    }
    auto permissions = metadata->permissions();
    permissions.set_readonly(true);
    auto changed = rstd::fs::set_permissions(path, permissions);
    if (changed.is_err()) {
        return artifact_failure<empty>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot make Registry blob staging file '{}' read-only: {}",
                         path,
                         rstd::move(changed).unwrap_err()));
    }
    return Ok(empty {});
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path, const RegistryPackageId& package)
    -> RegistryArtifactResult<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return artifact_failure<empty>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry transport path '{}' is not UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto discard(ref<rstd::path::Path> path) -> void {
    (void)rstd::fs::remove_file(path);
}

auto publish_staging(const BlobLayout&           layout,
                     const RegistryReleasePin&   pin,
                     PathBuf                     staging,
                     const VerifiedRegistryBlob& verified)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    const auto& package = pin.release.package;
    auto        lock    = rstd_try(acquire_lock(layout, package));
    (void)lock;
    auto current = rstd_try(probe_blob(layout, package, pin.checksum));
    if (current.verified.is_some()) {
        discard(staging.as_path());
        return Ok(rstd::move(current.verified).unwrap());
    }
    if (current.corrupt) {
        auto removed = rstd::fs::remove_file(layout.source.as_path());
        if (removed.is_err()) {
            discard(staging.as_path());
            return artifact_failure<VerifiedRegistryBlob>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot replace corrupt Registry blob '{}': {}",
                             layout.source.as_path(),
                             rstd::move(removed).unwrap_err()));
        }
    }
    auto read_only = set_read_only(staging.as_path(), package);
    if (read_only.is_err()) {
        discard(staging.as_path());
        return Err(rstd::move(read_only).unwrap_err());
    }
    auto renamed = rstd::fs::rename(staging.as_path(), layout.source.as_path());
    if (renamed.is_err()) {
        discard(staging.as_path());
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot publish Registry blob cache entry '{}': {}",
                         layout.source.as_path(),
                         rstd::move(renamed).unwrap_err()));
    }
    return Ok(VerifiedRegistryBlob {
        .checksum = pin.checksum.clone(),
        .path     = layout.source.clone(),
        .size     = verified.size,
    });
}

} // namespace

auto lito::registry::RegistryBlobCache::acquire(const RegistryReleasePin& pin)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    const auto& package = pin.release.package;
    auto        layout  = blob_layout(cache_root_.as_path(), pin);
    auto        cached  = rstd_try(probe_blob(layout, package, pin.checksum));
    if (cached.verified.is_some()) return Ok(rstd::move(cached.verified).unwrap());
    if (source_bundles_ != nullptr) {
        for (const auto& root : *source_bundles_) {
            auto bundled = lito::source::SourceBundleLayout(root.clone()).registry_package(pin);
            auto source  = rstd_try(ordinary_file(bundled.as_path(), package));
            if (source.is_none()) continue;
            auto staging = rstd_try(reserve_staging(layout, package));
            auto copied  = rstd::fs::copy(bundled.as_path(), staging.as_path());
            if (copied.is_err()) {
                discard(staging.as_path());
                return artifact_failure<VerifiedRegistryBlob>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot import Registry source bundle blob '{}' into '{}': {}",
                                 bundled.as_path(),
                                 staging.as_path(),
                                 rstd::move(copied).unwrap_err()));
            }
            auto verified = verify_registry_blob_file(staging.clone(), package, pin.checksum);
            if (verified.is_err()) {
                discard(staging.as_path());
                return Err(rstd::move(verified).unwrap_err());
            }
            return publish_staging(layout, pin, rstd::move(staging), *verified);
        }
    }
    if (network_ == RegistryNetworkPolicy::Offline) {
        if (cached.corrupt) {
            return artifact_failure<VerifiedRegistryBlob>(
                RegistryArtifactErrorKind::Digest,
                package,
                rstd::format("offline Registry package archive '{}' is corrupt",
                             layout.source.as_path()));
        }
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::OfflineCacheMiss,
            package,
            rstd::format("offline Registry resolve has no verified package archive '{}'",
                         registry_archive_filename(pin).as_str()));
    }
    if (transport_.download == nullptr) {
        return artifact_failure<VerifiedRegistryBlob>(RegistryArtifactErrorKind::Network,
                                                      package,
                                                      "Registry blob transport is unavailable"_str);
    }

    auto staging = rstd_try(reserve_staging(layout, package));
    auto requested =
        transport_.download(transport_.context,
                            RegistryBlobDownloadRequest {
                                .package     = package.clone(),
                                .url         = endpoint_.render(package.name, pin.release.version),
                                .destination = staging.clone(),
                            });
    if (requested.is_err()) {
        discard(staging.as_path());
        return Err(rstd::move(requested).unwrap_err());
    }
    auto verified = verify_registry_blob_file(staging.clone(), package, pin.checksum);
    if (verified.is_err()) {
        discard(staging.as_path());
        return Err(rstd::move(verified).unwrap_err());
    }

    return publish_staging(layout, pin, rstd::move(staging), *verified);
}

auto lito::registry::RegistryBlobCache::publish(const RegistryReleasePin& pin, slice<u8> contents)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    const auto& package = pin.release.package;
    if (contents.is_empty() || as_cast<u64>(contents.len()) > MAX_REGISTRY_PACKAGE_ARCHIVE_BYTES) {
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::Size,
            package,
            rstd::format("embedded Registry package archive size {} is outside the supported range",
                         contents.len()));
    }
    auto layout = blob_layout(cache_root_.as_path(), pin);
    auto cached = rstd_try(probe_blob(layout, package, pin.checksum));
    if (cached.verified.is_some()) return Ok(rstd::move(cached.verified).unwrap());
    auto staging = rstd_try(reserve_staging(layout, package));
    auto written = rstd::fs::write(staging.as_path(), contents);
    if (written.is_err()) {
        discard(staging.as_path());
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot write embedded Registry package staging file '{}': {}",
                         staging.as_path(),
                         rstd::move(written).unwrap_err()));
    }
    auto verified = verify_registry_blob_file(staging.clone(), package, pin.checksum);
    if (verified.is_err()) {
        discard(staging.as_path());
        return Err(rstd::move(verified).unwrap_err());
    }
    return publish_staging(layout, pin, rstd::move(staging), *verified);
}

auto lito::registry::CurlRegistryBlobTransport::download_callback(
    void*                              context,
    const RegistryBlobDownloadRequest& request) noexcept -> RegistryArtifactResult<empty> {
    return static_cast<CurlRegistryBlobTransport*>(context)->download(request);
}

auto lito::registry::CurlRegistryBlobTransport::download(const RegistryBlobDownloadRequest& request)
    -> RegistryArtifactResult<empty> {
    if (environment_ == nullptr) {
        return artifact_failure<empty>(RegistryArtifactErrorKind::Network,
                                       request.package,
                                       "curl Registry blob transport has no environment"_str);
    }
    auto arguments = Vec<String>::make();
    rstd_try(push_path(arguments, executable_.as_path(), request.package));
    arguments.push(String::make("--fail"_str));
    arguments.push(String::make("--silent"_str));
    arguments.push(String::make("--show-error"_str));
    arguments.push(String::make("--location"_str));
    arguments.push(String::make("--globoff"_str));
    arguments.push(String::make("--proto"_str));
    arguments.push(String::make("=https"_str));
    arguments.push(String::make("--proto-redir"_str));
    arguments.push(String::make("=https"_str));
    arguments.push(String::make("--connect-timeout"_str));
    arguments.push(String::make("30"_str));
    arguments.push(String::make("--output"_str));
    rstd_try(push_path(arguments, request.destination.as_path(), request.package));
    arguments.push(String::make("--"_str));
    arguments.push(request.url.clone());
    auto executed = lito::system::run_command(arguments, *environment_);
    if (executed.is_err()) {
        return artifact_failure<empty>(RegistryArtifactErrorKind::Network,
                                       request.package,
                                       rstd::format("cannot execute Registry blob download: {}",
                                                    rstd::move(executed).unwrap_err()));
    }
    auto output = rstd::move(executed).unwrap();
    if (output.exit_code != i32 {}) {
        return artifact_failure<empty>(
            RegistryArtifactErrorKind::Network,
            request.package,
            rstd::format("Registry blob download failed with curl exit code {}: {}",
                         output.exit_code,
                         output.standard_error.as_str().trim_ascii()));
    }
    return Ok(empty {});
}
