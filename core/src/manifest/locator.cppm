export module lito.core:manifest.locator;

import rstd;
import :manifest.error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using namespace lito::manifest;

inline constexpr ref<str> MANIFEST_NAMES[] = { "lito.toml"_str, "tenon.toml"_str };

auto locator_io(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> ManifestLocatorError {
    return ManifestLocatorError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source));
}

auto manifest_directory(ref<rstd::path::Path> requested_directory)
    -> ManifestLocatorResult<PathBuf> {
    auto canonical_directory = rstd::fs::canonicalize(requested_directory);
    if (canonical_directory.is_err()) {
        return Err(locator_io("resolve manifest directory"_str,
                              requested_directory,
                              rstd::move(canonical_directory).unwrap_err()));
    }
    auto directory = rstd::move(canonical_directory).unwrap();
    auto metadata  = rstd::fs::metadata(directory.as_path());
    if (metadata.is_err()) {
        return Err(locator_io("inspect manifest directory"_str,
                              directory.as_path(),
                              rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return Err(ManifestLocatorError::NotDirectory(rstd::move(directory)));
    }
    return Ok(rstd::move(directory));
}

auto try_manifest_path(ref<rstd::path::Path> directory) -> ManifestLocatorResult<Option<PathBuf>> {
    for (auto name : MANIFEST_NAMES) {
        auto candidate = PathBuf::from(directory).join(PathBuf::from(name).as_path());
        auto exists    = rstd::fs::exists(candidate.as_path());
        if (exists.is_err()) {
            return Err(locator_io(
                "inspect manifest"_str, candidate.as_path(), rstd::move(exists).unwrap_err()));
        }
        if (! *exists) continue;

        auto canonical = rstd::fs::canonicalize(candidate.as_path());
        if (canonical.is_err()) {
            return Err(locator_io(
                "resolve manifest"_str, candidate.as_path(), rstd::move(canonical).unwrap_err()));
        }
        auto manifest = rstd::move(canonical).unwrap();
        auto metadata = rstd::fs::metadata(manifest.as_path());
        if (metadata.is_err()) {
            return Err(locator_io(
                "inspect manifest"_str, manifest.as_path(), rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_file()) {
            return Err(ManifestLocatorError::NotRegularFile(rstd::move(manifest)));
        }
        return Ok(Some(rstd::move(manifest)));
    }
    return Ok(None());
}

export namespace lito::manifest
{

struct ManifestLocation {
    PathBuf directory;
    PathBuf manifest;
};

auto locate_manifest(ref<rstd::path::Path> requested_directory)
    -> ManifestLocatorResult<ManifestLocation>;

auto try_locate_manifest(ref<rstd::path::Path> requested_directory)
    -> ManifestLocatorResult<Option<ManifestLocation>> {
    auto exists = rstd::fs::exists(requested_directory);
    if (exists.is_err()) {
        return Err(locator_io("inspect manifest directory"_str,
                              requested_directory,
                              rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(None());
    auto directory = manifest_directory(requested_directory);
    if (directory.is_err()) return Err(rstd::move(directory).unwrap_err());
    auto manifest = try_manifest_path(directory->as_path());
    if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());
    if (manifest->is_none()) return Ok(None());
    return Ok(Some(ManifestLocation {
        .directory = rstd::move(directory).unwrap(),
        .manifest  = rstd::move(manifest).unwrap().unwrap(),
    }));
}

auto locate_manifest(ref<rstd::path::Path> requested_directory)
    -> ManifestLocatorResult<ManifestLocation> {
    auto located = try_locate_manifest(requested_directory);
    if (located.is_err()) return Err(rstd::move(located).unwrap_err());
    auto manifest = rstd::move(located).unwrap();
    if (manifest.is_none()) {
        return Err(ManifestLocatorError::NotFound(PathBuf::from(requested_directory)));
    }
    return Ok(rstd::move(manifest).unwrap());
}

} // namespace lito::manifest
