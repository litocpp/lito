export module lito.manifest:locator;

import rstd;
import lito.error;
import lito.manifest.contract;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

inline constexpr ref<str> MANIFEST_NAMES[] = { "lito.toml"_str, "tenon.toml"_str };

template<typename T>
auto locator_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto manifest_directory(ref<rstd::path::Path> requested_directory) -> Result<PathBuf> {
    auto canonical_directory = rstd::fs::canonicalize(requested_directory);
    if (canonical_directory.is_err()) {
        return locator_failure<PathBuf>(rstd::format("cannot resolve manifest directory '{}': {}",
                                                     requested_directory,
                                                     rstd::move(canonical_directory).unwrap_err()));
    }
    auto directory = rstd::move(canonical_directory).unwrap();
    auto metadata  = rstd::fs::metadata(directory.as_path());
    if (metadata.is_err()) {
        return locator_failure<PathBuf>(rstd::format("cannot inspect manifest directory '{}': {}",
                                                     directory.as_path(),
                                                     rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return locator_failure<PathBuf>(
            rstd::format("manifest root '{}' is not a directory", directory.as_path()));
    }
    return Ok(rstd::move(directory));
}

auto try_manifest_path(ref<rstd::path::Path> directory) -> Result<Option<PathBuf>> {
    for (auto name : MANIFEST_NAMES) {
        auto candidate = PathBuf::from(directory).join(PathBuf::from(name).as_path());
        auto exists    = rstd::fs::exists(candidate.as_path());
        if (exists.is_err()) {
            return locator_failure<Option<PathBuf>>(rstd::format("cannot inspect manifest '{}': {}",
                                                                 candidate.as_path(),
                                                                 rstd::move(exists).unwrap_err()));
        }
        if (! *exists) continue;

        auto canonical = rstd::fs::canonicalize(candidate.as_path());
        if (canonical.is_err()) {
            return locator_failure<Option<PathBuf>>(
                rstd::format("cannot resolve manifest '{}': {}",
                             candidate.as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        auto manifest = rstd::move(canonical).unwrap();
        auto metadata = rstd::fs::metadata(manifest.as_path());
        if (metadata.is_err()) {
            return locator_failure<Option<PathBuf>>(
                rstd::format("cannot inspect manifest '{}': {}",
                             manifest.as_path(),
                             rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_file()) {
            return locator_failure<Option<PathBuf>>(
                rstd::format("manifest '{}' is not a regular file", manifest.as_path()));
        }
        return Ok(Some(rstd::move(manifest)));
    }
    return Ok(None());
}

} // namespace lito

export namespace lito
{

auto locate_manifest(ref<rstd::path::Path> requested_directory) -> Result<ManifestLocation>;

auto try_locate_manifest(ref<rstd::path::Path> requested_directory)
    -> Result<Option<ManifestLocation>> {
    auto exists = rstd::fs::exists(requested_directory);
    if (exists.is_err()) {
        return locator_failure<Option<ManifestLocation>>(
            rstd::format("cannot inspect manifest directory '{}': {}",
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

auto locate_manifest(ref<rstd::path::Path> requested_directory) -> Result<ManifestLocation> {
    auto located = try_locate_manifest(requested_directory);
    if (located.is_err()) return Err(rstd::move(located).unwrap_err());
    auto manifest = rstd::move(located).unwrap();
    if (manifest.is_none()) {
        return locator_failure<ManifestLocation>(rstd::format(
            "cannot find lito.toml or legacy tenon.toml in '{}'", requested_directory));
    }
    return Ok(rstd::move(manifest).unwrap());
}

} // namespace lito
