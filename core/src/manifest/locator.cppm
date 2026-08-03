export module tenon.manifest:locator;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

template<typename T>
auto locator_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

} // namespace tenon

export namespace tenon
{

auto locate_manifest(ref<rstd::path::Path> requested_directory) -> Result<ManifestLocation>;

auto try_locate_manifest(ref<rstd::path::Path> requested_directory)
    -> Result<Option<ManifestLocation>> {
    auto canonical_directory = rstd::fs::canonicalize(requested_directory);
    if (canonical_directory.is_err()) {
        return locator_failure<Option<ManifestLocation>>(
            rstd::format("cannot resolve manifest directory '{}': {}",
                         requested_directory,
                         rstd::move(canonical_directory).unwrap_err()));
    }
    auto directory = rstd::move(canonical_directory).unwrap();
    auto metadata  = rstd::fs::metadata(directory.as_path());
    if (metadata.is_err()) {
        return locator_failure<Option<ManifestLocation>>(
            rstd::format("cannot inspect manifest directory '{}': {}",
                         directory.as_path(),
                         rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return locator_failure<Option<ManifestLocation>>(
            rstd::format("manifest root '{}' is not a directory", directory.as_path()));
    }

    auto manifest = directory.join(PathBuf::from("tenon.toml"_str).as_path());
    auto exists   = rstd::fs::exists(manifest.as_path());
    if (exists.is_err()) {
        return locator_failure<Option<ManifestLocation>>(
            rstd::format("cannot inspect manifest '{}': {}",
                         manifest.as_path(),
                         rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(Option<ManifestLocation> {});

    auto located = locate_manifest(directory.as_path());
    if (located.is_err()) return Err(rstd::move(located).unwrap_err());
    return Ok(Some(rstd::move(located).unwrap()));
}

auto locate_manifest(ref<rstd::path::Path> requested_directory) -> Result<ManifestLocation> {
    auto canonical_directory = rstd::fs::canonicalize(requested_directory);
    if (canonical_directory.is_err()) {
        return locator_failure<ManifestLocation>(
            rstd::format("cannot resolve package directory '{}': {}",
                         requested_directory,
                         rstd::move(canonical_directory).unwrap_err()));
    }
    auto directory = rstd::move(canonical_directory).unwrap();
    auto metadata  = rstd::fs::metadata(directory.as_path());
    if (metadata.is_err()) {
        return locator_failure<ManifestLocation>(
            rstd::format("cannot inspect package directory '{}': {}",
                         directory.as_path(),
                         rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return locator_failure<ManifestLocation>(
            rstd::format("manifest root '{}' is not a directory", directory.as_path()));
    }

    auto manifest           = directory.join(PathBuf::from("tenon.toml"_str).as_path());
    auto canonical_manifest = rstd::fs::canonicalize(manifest.as_path());
    if (canonical_manifest.is_err()) {
        return locator_failure<ManifestLocation>(
            rstd::format("cannot resolve manifest '{}': {}",
                         manifest.as_path(),
                         rstd::move(canonical_manifest).unwrap_err()));
    }
    auto resolved_manifest = rstd::move(canonical_manifest).unwrap();
    auto manifest_metadata = rstd::fs::metadata(resolved_manifest.as_path());
    if (manifest_metadata.is_err()) {
        return locator_failure<ManifestLocation>(
            rstd::format("cannot inspect manifest '{}': {}",
                         resolved_manifest.as_path(),
                         rstd::move(manifest_metadata).unwrap_err()));
    }
    if (! manifest_metadata->is_file()) {
        return locator_failure<ManifestLocation>(
            rstd::format("manifest '{}' is not a regular file", resolved_manifest.as_path()));
    }
    return Ok(ManifestLocation {
        .directory = rstd::move(directory),
        .manifest  = rstd::move(resolved_manifest),
    });
}

} // namespace tenon
