module;
#include <rstd/macro.hpp>

export module lito.pack:blob;

import rstd;
import licrypto;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

inline constexpr auto MAX_REGISTRY_PACKAGE_ARCHIVE_BYTES = u64(512) * u64(1024) * u64(1024);

struct VerifiedRegistryBlob {
    PackageChecksum checksum;
    PathBuf         path;
    u64             size {};
};

auto verify_registry_blob_file(PathBuf                  path,
                               const RegistryPackageId& package,
                               const PackageChecksum&   checksum)
    -> RegistryArtifactResult<VerifiedRegistryBlob>;
auto registry_package_archive_from_file(ref<rstd::path::Path>    path,
                                        const RegistryPackageId& package)
    -> RegistryArtifactResult<RegistryPackageArchive>;

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

auto ordinary_file(ref<rstd::path::Path> path, const RegistryPackageId& package)
    -> RegistryArtifactResult<Option<rstd::fs::Metadata>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<rstd::fs::Metadata> {});
        }
        return artifact_failure<Option<rstd::fs::Metadata>>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("cannot inspect Registry blob path '{}': {}", path, error));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return artifact_failure<Option<rstd::fs::Metadata>>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry blob path '{}' is not an ordinary file", path));
    }
    if (metadata->len() == u64 {} || metadata->len() > MAX_REGISTRY_PACKAGE_ARCHIVE_BYTES) {
        return artifact_failure<Option<rstd::fs::Metadata>>(
            RegistryArtifactErrorKind::Size,
            package,
            rstd::format("Registry package archive size {} is outside the supported range",
                         metadata->len()));
    }
    return Ok(Some(rstd::move(metadata).unwrap()));
}

auto checksum(ref<rstd::path::Path> path, const RegistryPackageId& package)
    -> RegistryArtifactResult<PackageChecksum> {
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        return artifact_failure<PackageChecksum>(RegistryArtifactErrorKind::Io,
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
            return artifact_failure<PackageChecksum>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format(
                    "cannot read Registry blob '{}': {}", path, rstd::move(read).unwrap_err()));
        }
        if (*read == usize {}) break;
        state.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    return Ok(PackageChecksum(rstd::move(state).finalize_digest()));
}

} // namespace

auto registry_blob_from_file(PathBuf path, const RegistryPackageId& package)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    auto metadata = rstd_try(ordinary_file(path.as_path(), package));
    if (metadata.is_none()) {
        return artifact_failure<VerifiedRegistryBlob>(
            RegistryArtifactErrorKind::Io,
            package,
            rstd::format("Registry blob '{}' does not exist", path.as_path()));
    }
    return Ok(VerifiedRegistryBlob {
        .checksum = rstd_try(checksum(path.as_path(), package)),
        .path     = rstd::move(path),
        .size     = metadata->len(),
    });
}

auto lito::registry::verify_registry_blob_file(PathBuf                  path,
                                               const RegistryPackageId& package,
                                               const PackageChecksum&   expected)
    -> RegistryArtifactResult<VerifiedRegistryBlob> {
    auto verified = rstd_try(::registry_blob_from_file(rstd::move(path), package));
    if (verified.checksum == expected) return Ok(rstd::move(verified));
    return artifact_failure<VerifiedRegistryBlob>(
        RegistryArtifactErrorKind::Digest,
        package,
        rstd::format("Registry package archive does not match checksum '{}'", expected.text()));
}

auto lito::registry::registry_package_archive_from_file(ref<rstd::path::Path>    path,
                                                        const RegistryPackageId& package)
    -> RegistryArtifactResult<RegistryPackageArchive> {
    auto verified = rstd_try(::registry_blob_from_file(PathBuf::from(path), package));
    return Ok(RegistryPackageArchive {
        .checksum = rstd::move(verified.checksum),
        .size     = RegistryBlobSize(verified.size),
        .format   = RegistryArchiveFormat::parse(RegistryArchiveFormat::TAR_ZSTD_V1).unwrap(),
    });
}
