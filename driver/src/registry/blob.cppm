module;
#include <rstd/macro.hpp>

export module lito.driver:registry.blob;

import rstd;
import licrypto;
import rstd.json;
import lito.core;
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

struct VerifiedRegistryBlob {
    PackageChecksum checksum;
    PathBuf         path;
    u64             size {};
};

class RegistryBlobCache {
    PathBuf                  cache_root_;
    RegistryEndpointTemplate endpoint_;
    RegistryNetworkPolicy    network_ { RegistryNetworkPolicy::Online };
    RegistryBlobTransport    transport_;
    const Vec<PathBuf>*      source_bundles_ {};

public:
    RegistryBlobCache(PathBuf                  cache_root,
                      RegistryEndpointTemplate endpoint,
                      RegistryNetworkPolicy    network,
                      RegistryBlobTransport    transport,
                      const Vec<PathBuf>*      source_bundles = nullptr)
        : cache_root_(rstd::move(cache_root)),
          endpoint_(rstd::move(endpoint)),
          network_(network),
          transport_(transport),
          source_bundles_(source_bundles) {}

    auto acquire(const RegistryPackageId& package, const PackageChecksum& checksum)
        -> RegistryArtifactResult<VerifiedRegistryBlob>;
};

auto verify_registry_blob_file(PathBuf                  path,
                               const RegistryPackageId& package,
                               const PackageChecksum&   checksum)
    -> RegistryArtifactResult<VerifiedRegistryBlob>;
auto registry_package_archive_from_file(ref<rstd::path::Path>    path,
                                        const RegistryPackageId& package)
    -> RegistryArtifactResult<RegistryPackageArchive>;

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
    PathBuf bucket;
    PathBuf source;
    PathBuf receipt;
    PathBuf lock;
};

inline constexpr auto MAX_PACKAGE_ARCHIVE_BYTES = u64(512) * u64(1024) * u64(1024);

auto blob_layout(ref<rstd::path::Path> root, const PackageChecksum& checksum) -> BlobLayout {
    auto bucket = PathBuf::from(root)
                      .join(PathBuf::from("files"_str).as_path())
                      .join(PathBuf::from("sha256"_str).as_path())
                      .join(PathBuf::from(checksum.text().as_str()).as_path());
    return BlobLayout {
        .bucket  = bucket.clone(),
        .source  = bucket.join(PathBuf::from("source"_str).as_path()),
        .receipt = bucket.join(PathBuf::from("receipt.json"_str).as_path()),
        .lock    = bucket.join(PathBuf::from("lock"_str).as_path()),
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

auto digest_matches(ref<rstd::path::Path>    path,
                    const PackageChecksum&   checksum,
                    const RegistryPackageId& package) -> RegistryArtifactResult<bool> {
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        return artifact_failure<bool>(RegistryArtifactErrorKind::Io,
                                      package,
                                      rstd::format("cannot open Registry blob '{}': {}",
                                                   path,
                                                   rstd::move(opened).unwrap_err()));
    }
    auto file   = rstd::move(opened).unwrap();
    auto state  = licrypto::Sha256::make();
    auto buffer = array<u8, 65536> {};
    while (true) {
        auto read = file.read(buffer.as_mut_slice());
        if (read.is_err()) {
            return artifact_failure<bool>(RegistryArtifactErrorKind::Io,
                                          package,
                                          rstd::format("cannot read Registry blob '{}': {}",
                                                       path,
                                                       rstd::move(read).unwrap_err()));
        }
        if (*read == usize {}) break;
        state.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    return Ok(rstd::move(state).finalize_digest() == checksum.digest());
}

auto verify_blob_bytes(ref<rstd::path::Path>    path,
                       const RegistryPackageId& package,
                       const PackageChecksum&   checksum)
    -> RegistryArtifactResult<Option<VerifiedRegistryBlob>> {
    auto metadata = rstd_try(ordinary_file(path, package));
    if (metadata.is_none() || metadata->len() == u64 {} ||
        metadata->len() > MAX_PACKAGE_ARCHIVE_BYTES) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    if (! rstd_try(digest_matches(path, checksum, package))) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    return Ok(Some(VerifiedRegistryBlob {
        .checksum = checksum.clone(),
        .path     = PathBuf::from(path),
        .size     = metadata->len(),
    }));
}

auto reserve_staging(const BlobLayout& layout, const RegistryPackageId& package)
    -> RegistryArtifactResult<PathBuf> {
    auto created = rstd::fs::create_dir_all(layout.bucket.as_path());
    if (created.is_err()) {
        return artifact_failure<PathBuf>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot create Registry blob cache bucket '{}': {}",
                         layout.bucket.as_path(),
                         rstd::move(created).unwrap_err()));
    }
    auto time = rstd::time::SystemTime::now().as_unix_time();
    for (usize attempt {}; attempt < usize(64); ++attempt) {
        auto staging = layout.bucket.join(PathBuf::from(rstd::format("source.tmp.{}.{}.{}.{}",
                                                                     rstd::process::id(),
                                                                     time.seconds,
                                                                     time.nanoseconds,
                                                                     attempt))
                                              .as_path());
        auto file    = rstd::fs::File::create_new(staging.as_path());
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

auto write_receipt(const BlobLayout&        layout,
                   const RegistryPackageId& package,
                   const PackageChecksum&   checksum,
                   u64                      size) -> RegistryArtifactResult<empty> {
    auto object = rstd::json::Map::make();
    object.insert(String::make("schema"_str),
                  rstd::json::Value::String(String::make("lito.registry.blob-cache.v1"_str)));
    object.insert(String::make("checksum"_str), rstd::json::Value::String(checksum.text()));
    object.insert(String::make("size"_str), rstd::json::Value::String(rstd::format("{}", size)));
    object.insert(String::make("format"_str),
                  rstd::json::Value::String(String::make(RegistryArchiveFormat::TAR_ZSTD_V1)));
    auto text = rstd::json::to_string(rstd::json::Value::Object(rstd::move(object)));
    text.push_ascii(u8('\n'));
    auto written = rstd::fs::write_atomic(layout.receipt.as_path(), text.as_str().as_bytes());
    if (written.is_err()) {
        return artifact_failure<empty>(RegistryArtifactErrorKind::Io,
                                       package,
                                       rstd::format("cannot write Registry blob receipt '{}': {}",
                                                    layout.receipt.as_path(),
                                                    rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto completed_blob(const BlobLayout&        layout,
                    const RegistryPackageId& package,
                    const PackageChecksum&   checksum)
    -> RegistryArtifactResult<Option<VerifiedRegistryBlob>> {
    auto metadata = rstd_try(ordinary_file(layout.source.as_path(), package));
    if (metadata.is_none() || metadata->len() == u64 {} ||
        metadata->len() > MAX_PACKAGE_ARCHIVE_BYTES) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    auto receipt = rstd::fs::read_to_string(layout.receipt.as_path());
    if (receipt.is_err()) return Ok(Option<VerifiedRegistryBlob> {});
    auto parsed = rstd::json::from_str(receipt->as_str(),
                                       rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) return Ok(Option<VerifiedRegistryBlob> {});
    auto object = parsed->as_object();
    if (object.is_none() || (**object).len() != usize(4)) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    auto schema   = parsed->get("schema"_str);
    auto recorded = parsed->get("checksum"_str);
    auto size     = parsed->get("size"_str);
    auto format   = parsed->get("format"_str);
    if (schema.is_none() || recorded.is_none() || size.is_none() || format.is_none()) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    auto schema_text   = (**schema).as_str();
    auto checksum_text = (**recorded).as_str();
    auto size_text     = (**size).as_str();
    auto format_text   = (**format).as_str();
    if (schema_text.is_none() || checksum_text.is_none() || size_text.is_none() ||
        format_text.is_none() || *schema_text != "lito.registry.blob-cache.v1"_str ||
        *checksum_text != checksum.text().as_str() ||
        *format_text != RegistryArchiveFormat::TAR_ZSTD_V1) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    auto parsed_size = lito::parse::parse_canonical_u64_decimal(*size_text);
    if (parsed_size.is_err() || *parsed_size != metadata->len()) {
        return Ok(Option<VerifiedRegistryBlob> {});
    }
    return Ok(Some(VerifiedRegistryBlob {
        .checksum = checksum.clone(),
        .path     = layout.source.clone(),
        .size     = metadata->len(),
    }));
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

} // namespace

auto lito::registry::RegistryBlobCache::acquire(const RegistryPackageId& package,
                                                const PackageChecksum&   checksum)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    auto layout = blob_layout(cache_root_.as_path(), checksum);
    auto cached = rstd_try(completed_blob(layout, package, checksum));
    if (cached.is_some()) return Ok(rstd::move(cached).unwrap());
    if (source_bundles_ != nullptr) {
        for (const auto& root : *source_bundles_) {
            auto bundled =
                lito::source::SourceBundleLayout(root.clone()).registry_package(checksum);
            auto exists = rstd::fs::exists(bundled.as_path());
            if (exists.is_err()) {
                return artifact_failure<VerifiedRegistryBlob>(
                    RegistryArtifactErrorKind::Io,
                    package,
                    rstd::format("cannot inspect Registry source bundle blob '{}': {}",
                                 bundled.as_path(),
                                 rstd::move(exists).unwrap_err()));
            }
            if (*exists) return verify_registry_blob_file(rstd::move(bundled), package, checksum);
        }
    }
    if (network_ == RegistryNetworkPolicy::Offline) {
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::OfflineCacheMiss,
            package,
            rstd::format("offline Registry resolve has no verified package archive '{}'",
                         checksum.text()));
    }
    if (transport_.download == nullptr) {
        return artifact_failure<VerifiedRegistryBlob>(RegistryArtifactErrorKind::Network,
                                                      package,
                                                      "Registry blob transport is unavailable"_str);
    }

    auto staging   = rstd_try(reserve_staging(layout, package));
    auto requested = transport_.download(transport_.context,
                                         RegistryBlobDownloadRequest {
                                             .package = package.clone(),
                                             .url     = endpoint_.render(checksum.text().as_str()),
                                             .destination = staging.clone(),
                                         });
    if (requested.is_err()) {
        discard(staging.as_path());
        return Err(rstd::move(requested).unwrap_err());
    }
    auto verified = verify_blob_bytes(staging.as_path(), package, checksum);
    if (verified.is_err()) {
        discard(staging.as_path());
        return Err(rstd::move(verified).unwrap_err());
    }
    if (verified->is_none()) {
        auto metadata = ordinary_file(staging.as_path(), package);
        auto kind     = RegistryArtifactErrorKind::Digest;
        auto message =
            rstd::format("Registry package archive does not match checksum '{}'", checksum.text());
        if (metadata.is_ok() && metadata->is_some() &&
            ((**metadata).len() == u64 {} || (**metadata).len() > MAX_PACKAGE_ARCHIVE_BYTES)) {
            kind = RegistryArtifactErrorKind::Size;
            message =
                rstd::format("Registry package archive size {} is outside the supported range",
                             (**metadata).len());
        }
        discard(staging.as_path());
        return artifact_failure<VerifiedRegistryBlob>(kind, package, rstd::move(message));
    }

    auto lock       = rstd_try(acquire_lock(layout, package));
    auto concurrent = rstd_try(completed_blob(layout, package, checksum));
    if (concurrent.is_some()) {
        discard(staging.as_path());
        return Ok(rstd::move(concurrent).unwrap());
    }
    auto existing = rstd_try(ordinary_file(layout.source.as_path(), package));
    if (existing.is_some()) {
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
    auto size = verified->as_ref().unwrap().size;
    rstd_try(write_receipt(layout, package, checksum, size));
    return Ok(VerifiedRegistryBlob {
        .checksum = checksum.clone(),
        .path     = layout.source.clone(),
        .size     = size,
    });
}

auto lito::registry::verify_registry_blob_file(PathBuf                  path,
                                               const RegistryPackageId& package,
                                               const PackageChecksum&   checksum)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    auto verified = rstd_try(verify_blob_bytes(path.as_path(), package, checksum));
    if (verified.is_some()) return Ok(rstd::move(verified).unwrap());
    auto metadata = rstd_try(ordinary_file(path.as_path(), package));
    if (metadata.is_none()) {
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry blob '{}' does not exist", path.as_path()));
    }
    if (metadata->len() == u64 {} || metadata->len() > MAX_PACKAGE_ARCHIVE_BYTES) {
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::Size,
            package,
            rstd::format("Registry package archive size {} is outside the supported range",
                         metadata->len()));
    }
    return artifact_failure<VerifiedRegistryBlob>(
        RegistryArtifactErrorKind::Digest,
        package,
        rstd::format("Registry package archive does not match checksum '{}'", checksum.text()));
}

auto lito::registry::registry_package_archive_from_file(ref<rstd::path::Path>    path,
                                                        const RegistryPackageId& package)
    -> RegistryArtifactResult<RegistryPackageArchive> {
    auto metadata = rstd_try(ordinary_file(path, package));
    if (metadata.is_none()) {
        return artifact_failure<RegistryPackageArchive>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry blob '{}' does not exist", path));
    }
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        return artifact_failure<RegistryPackageArchive>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format(
                "cannot open Registry blob '{}': {}", path, rstd::move(opened).unwrap_err()));
    }
    auto file   = rstd::move(opened).unwrap();
    auto state  = licrypto::Sha256::make();
    auto buffer = array<u8, 65536> {};
    while (true) {
        auto read = file.read(buffer.as_mut_slice());
        if (read.is_err()) {
            return artifact_failure<RegistryPackageArchive>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format(
                    "cannot read Registry blob '{}': {}", path, rstd::move(read).unwrap_err()));
        }
        if (*read == usize {}) break;
        state.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    return Ok(RegistryPackageArchive {
        .checksum = PackageChecksum(rstd::move(state).finalize_digest()),
        .size     = RegistryBlobSize(metadata->len()),
        .format   = RegistryArchiveFormat::parse(RegistryArchiveFormat::TAR_ZSTD_V1).unwrap(),
    });
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
