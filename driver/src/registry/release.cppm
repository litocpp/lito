module;
#include <rstd/macro.hpp>

export module lito.driver:registry.release;

import rstd;
import lito.core;
import :config.registry;
import :registry.index;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

using RegistryReleaseLoadResult = Result<VerifiedRegistryRelease, RegistryIndexError>;

struct VerifiedRegistryReleaseRecord {
    VerifiedRegistryRelease release;
    PathBuf                 path;
};

class RegistryReleaseClient {
    PathBuf                  cache_root_;
    RegistryId               registry_;
    RegistryEndpointTemplate endpoint_;
    Ed25519PublicKey         trusted_key_;
    RegistryNetworkPolicy    network_ { RegistryNetworkPolicy::Online };
    RegistryHttpTransport    transport_;
    const Vec<PathBuf>*      source_bundles_ {};

public:
    RegistryReleaseClient(PathBuf                                  cache_root,
                          const lito::config::NamedRegistryConfig& config,
                          RegistryNetworkPolicy                    network,
                          RegistryHttpTransport                    transport,
                          const Vec<PathBuf>*                      source_bundles = nullptr)
        : cache_root_(rstd::move(cache_root)),
          registry_(config.identity.clone()),
          endpoint_(config.effective_endpoints()->release.clone()),
          trusted_key_(config.trusted_public_key.clone()),
          network_(network),
          transport_(transport),
          source_bundles_(source_bundles) {}

    auto load(const RegistryPackageId& package,
              const ReleaseDigest&     release,
              const SemanticVersion*   version = nullptr) -> RegistryReleaseLoadResult;
    auto load_record(const RegistryPackageId& package, const ReleaseDigest& release)
        -> Result<VerifiedRegistryReleaseRecord, RegistryIndexError>;
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;

inline constexpr auto MAX_RELEASE_BYTES = usize(4 * 1024 * 1024);

template<typename T>
auto release_failure(RegistryIndexErrorKind kind, const RegistryPackageId& package, String message)
    -> Result<T, RegistryIndexError> {
    return Err(RegistryIndexError {
        .kind    = kind,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto release_failure(RegistryIndexErrorKind   kind,
                     const RegistryPackageId& package,
                     ref<str>                 message) -> Result<T, RegistryIndexError> {
    return release_failure<T>(kind, package, String::make(message));
}

auto release_path(ref<rstd::path::Path> root, const ReleaseDigest& release) -> PathBuf {
    return PathBuf::from(root)
        .join(PathBuf::from("registry"_str).as_path())
        .join(PathBuf::from("releases"_str).as_path())
        .join(PathBuf::from("sha256"_str).as_path())
        .join(PathBuf::from(rstd::format("{}.json", release.digest().to_hex())).as_path());
}

auto release_lock(ref<rstd::path::Path> record) -> PathBuf {
    return PathBuf::from(rstd::format("{}.lock", record));
}

auto read_cached_release(ref<rstd::path::Path>    record,
                         const RegistryPackageId& package,
                         const ReleaseDigest&     release,
                         const Ed25519PublicKey&  trusted_key)
    -> Result<Option<VerifiedRegistryRelease>, RegistryIndexError> {
    auto body = rstd::fs::read_to_string(record);
    if (body.is_err()) {
        auto error = rstd::move(body).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(None());
        }
        return release_failure<Option<VerifiedRegistryRelease>>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot read Registry release cache '{}': {}", record, error));
    }
    if (body->len() > MAX_RELEASE_BYTES) {
        return release_failure<Option<VerifiedRegistryRelease>>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("Registry release cache '{}' exceeds 4 MiB", record));
    }
    auto verified =
        parse_verified_registry_release(body->as_str().as_bytes(), package, release, trusted_key);
    if (verified.is_err()) {
        return release_failure<Option<VerifiedRegistryRelease>>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("Registry release cache '{}' does not verify: {}",
                         record,
                         rstd::move(verified).unwrap_err()));
    }
    return Ok(Some(rstd::move(verified).unwrap()));
}

auto acquire_release_lock(ref<rstd::path::Path> record, const RegistryPackageId& package)
    -> Result<rstd::fs::FileLock, RegistryIndexError> {
    auto parent  = record.parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) {
        return release_failure<rstd::fs::FileLock>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot create Registry release cache '{}': {}",
                         parent,
                         rstd::move(created).unwrap_err()));
    }
    auto path = release_lock(record);
    auto opened =
        rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(path.as_path());
    if (opened.is_err()) {
        return release_failure<rstd::fs::FileLock>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot open Registry release cache lock '{}': {}",
                         path.as_path(),
                         rstd::move(opened).unwrap_err()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return release_failure<rstd::fs::FileLock>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot lock Registry release cache '{}': {}",
                         path.as_path(),
                         rstd::move(locked).unwrap_err()));
    }
    return Ok(rstd::move(locked).unwrap());
}

auto store_release(ref<rstd::path::Path>    record,
                   const RegistryPackageId& package,
                   const ReleaseDigest&     release,
                   const Ed25519PublicKey&  trusted_key,
                   ref<str> body) -> Result<VerifiedRegistryRelease, RegistryIndexError> {
    auto lock = rstd_try(acquire_release_lock(record, package));
    (void)lock;
    auto concurrent = read_cached_release(record, package, release, trusted_key);
    if (concurrent.is_ok() && concurrent->is_some()) {
        return Ok(rstd::move(concurrent).unwrap().unwrap());
    }
    auto written = rstd::fs::write_atomic(record, body.as_bytes());
    if (written.is_err()) {
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot write Registry release cache '{}': {}",
                         record,
                         rstd::move(written).unwrap_err()));
    }
    auto verified = parse_verified_registry_release(body.as_bytes(), package, release, trusted_key);
    if (verified.is_err()) {
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::Integrity,
            package,
            rstd::format("stored Registry release no longer verifies: {}",
                         rstd::move(verified).unwrap_err()));
    }
    return Ok(rstd::move(verified).unwrap());
}

auto release_http_failure(const RegistryHttpResponse& response, const RegistryPackageId& package)
    -> RegistryReleaseLoadResult {
    auto kind = RegistryIndexErrorKind::Network;
    if (response.status == u16(404)) kind = RegistryIndexErrorKind::NotFound;
    if (response.status == u16(410)) kind = RegistryIndexErrorKind::Gone;
    if (response.status == u16(451)) kind = RegistryIndexErrorKind::LegalUnavailable;
    return release_failure<VerifiedRegistryRelease>(
        kind, package, rstd::format("Registry release request returned HTTP {}", response.status));
}

} // namespace

auto lito::registry::RegistryReleaseClient::load(const RegistryPackageId& package,
                                                 const ReleaseDigest&     release,
                                                 const SemanticVersion*   version)
    -> RegistryReleaseLoadResult {
    if (! (package.registry == registry_)) {
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::ContextMismatch,
            package,
            "Registry release client cannot serve another registry identity"_str);
    }
    auto record = release_path(cache_root_.as_path(), release);
    auto cached = read_cached_release(record.as_path(), package, release, trusted_key_);
    if (cached.is_ok() && cached->is_some()) {
        return Ok(rstd::move(cached).unwrap().unwrap());
    }
    if (version != nullptr && source_bundles_ != nullptr) {
        for (const auto& root : *source_bundles_) {
            auto bundled = lito::source::SourceBundleLayout(root.clone())
                               .registry_release(package, *version, release);
            auto loaded  = read_cached_release(bundled.as_path(), package, release, trusted_key_);
            if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
            if (loaded->is_some()) return Ok(rstd::move(loaded).unwrap().unwrap());
        }
    }
    if (network_ == RegistryNetworkPolicy::Offline) {
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::OfflineCacheMiss,
            package,
            rstd::format("offline Registry resolve has no verified release '{}'", release.text()));
    }
    if (transport_.get == nullptr) {
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::Network,
            package,
            "online Registry resolve has no HTTP transport"_str);
    }
    auto response = transport_.get(transport_.context,
                                   RegistryHttpRequest {
                                       .package = package.clone(),
                                       .url = endpoint_.render(release.digest().to_hex().as_str()),
                                   });
    if (response.is_err()) return Err(rstd::move(response).unwrap_err());
    auto received = rstd::move(response).unwrap();
    if (received.status != u16(200)) return release_http_failure(received, package);
    if (received.body.len() > MAX_RELEASE_BYTES) {
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::Schema, package, "Registry release response exceeds 4 MiB"_str);
    }
    auto verified = parse_verified_registry_release(
        received.body.as_str().as_bytes(), package, release, trusted_key_);
    if (verified.is_err()) {
        return release_failure<VerifiedRegistryRelease>(
            RegistryIndexErrorKind::Schema,
            package,
            rstd::format("Registry release response does not verify: {}",
                         rstd::move(verified).unwrap_err()));
    }
    return store_release(record.as_path(), package, release, trusted_key_, received.body.as_str());
}

auto lito::registry::RegistryReleaseClient::load_record(const RegistryPackageId& package,
                                                        const ReleaseDigest&     release)
    -> Result<VerifiedRegistryReleaseRecord, RegistryIndexError> {
    auto verified = rstd_try(load(package, release));
    return Ok(VerifiedRegistryReleaseRecord {
        .release = rstd::move(verified),
        .path    = release_path(cache_root_.as_path(), release),
    });
}
