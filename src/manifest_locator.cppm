export module tenon.manifest_locator;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::manifest_locator_detail
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

} // namespace tenon::manifest_locator_detail

export namespace tenon
{

auto locate_manifest(ref<rstd::path::Path> requested_directory)
    -> Result<ManifestLocation> {
    using namespace manifest_locator_detail;

    auto canonical_directory = rstd::fs::canonicalize(requested_directory);
    if (canonical_directory.is_err()) {
        return failure<ManifestLocation>(rstd::format(
            "cannot resolve package directory '{}': {}",
            requested_directory,
            rstd::move(canonical_directory).unwrap_err()));
    }
    auto directory = rstd::move(canonical_directory).unwrap();
    auto metadata = rstd::fs::metadata(directory.as_path());
    if (metadata.is_err()) {
        return failure<ManifestLocation>(rstd::format(
            "cannot inspect package directory '{}': {}",
            directory.as_path(),
            rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return failure<ManifestLocation>(
            rstd::format("manifest root '{}' is not a directory", directory.as_path()));
    }

    auto manifest = directory.join(PathBuf::from("tenon.toml"_str).as_path());
    auto canonical_manifest = rstd::fs::canonicalize(manifest.as_path());
    if (canonical_manifest.is_err()) {
        return failure<ManifestLocation>(rstd::format(
            "cannot resolve manifest '{}': {}",
            manifest.as_path(),
            rstd::move(canonical_manifest).unwrap_err()));
    }
    auto resolved_manifest = rstd::move(canonical_manifest).unwrap();
    auto manifest_metadata = rstd::fs::metadata(resolved_manifest.as_path());
    if (manifest_metadata.is_err()) {
        return failure<ManifestLocation>(rstd::format(
            "cannot inspect manifest '{}': {}",
            resolved_manifest.as_path(),
            rstd::move(manifest_metadata).unwrap_err()));
    }
    if (! manifest_metadata->is_file()) {
        return failure<ManifestLocation>(
            rstd::format("manifest '{}' is not a regular file", resolved_manifest.as_path()));
    }
    return Ok(ManifestLocation {
        .directory = rstd::move(directory),
        .manifest = rstd::move(resolved_manifest),
    });
}

} // namespace tenon
