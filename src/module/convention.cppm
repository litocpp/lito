export module tenon.modules:convention;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

template<typename T>
auto convention_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

template<typename T>
auto convention_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, message));
}

auto same_path(ref<rstd::path::Path> left, ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

auto valid_segment(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (usize index {}; index < value.size(); ++index) {
        const auto byte  = value[index];
        const bool alpha = (byte >= u8('a') && byte <= u8('z')) ||
                           (byte >= u8('A') && byte <= u8('Z')) || byte == u8('_');
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if ((! alpha && ! digit) || (index == usize {} && digit)) return false;
    }
    return true;
}

auto canonical_source_root(const PackageManifest& manifest) -> Result<PathBuf> {
    auto requested = manifest.root.join(PathBuf::from("src"_str).as_path());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return convention_failure<PathBuf>(
            rstd::format("cannot resolve module source root '{}': {}",
                         requested.as_path(),
                         rstd::move(canonical).unwrap_err()));
    }
    auto root     = rstd::move(canonical).unwrap();
    auto metadata = rstd::fs::metadata(root.as_path());
    if (metadata.is_err() || ! metadata->is_dir()) {
        return convention_failure<PathBuf>(
            rstd::format("module source root '{}' is not a directory", root.as_path()));
    }
    return Ok(rstd::move(root));
}

auto canonical_candidate(const PackageManifest& manifest,
                         ref<rstd::path::Path>  source_root,
                         ref<rstd::path::Path>  requested,
                         Option<String>         expected) -> Result<ResolvedSource> {
    auto canonical = rstd::fs::canonicalize(requested);
    if (canonical.is_err()) {
        return convention_failure<ResolvedSource>(
            rstd::format("cannot resolve module convention source '{}': {}",
                         requested,
                         rstd::move(canonical).unwrap_err()));
    }
    auto resolved         = rstd::move(canonical).unwrap();
    auto source_relative  = resolved.as_path().strip_prefix(source_root);
    auto package_relative = resolved.as_path().strip_prefix(manifest.root.as_path());
    if (source_relative.is_none() || package_relative.is_none() || (*source_relative).is_empty()) {
        return convention_failure<ResolvedSource>(rstd::format(
            "module convention source '{}' resolves outside package source root", requested));
    }
    auto metadata = rstd::fs::metadata(resolved.as_path());
    if (metadata.is_err()) {
        return convention_failure<ResolvedSource>(
            rstd::format("cannot inspect module convention source '{}': {}",
                         resolved.as_path(),
                         rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_file()) {
        return convention_failure<ResolvedSource>(
            rstd::format("module convention source '{}' is not a file", resolved.as_path()));
    }
    return Ok(ResolvedSource {
        .relative_path   = PathBuf::from(*package_relative),
        .canonical_path  = rstd::move(resolved),
        .origin          = SourceOrigin::Convention,
        .expected_module = rstd::move(expected),
    });
}

auto path_exists(ref<rstd::path::Path> path) -> Result<bool> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return convention_failure<bool>(
            rstd::format("cannot inspect module convention source '{}': {}",
                         path,
                         rstd::move(exists).unwrap_err()));
    }
    return Ok(*exists);
}

} // namespace tenon

export namespace tenon
{

auto module_name_belongs(ref<str> root_module, ref<str> logical_name) -> bool {
    if (logical_name == root_module) return true;
    if (! logical_name.starts_with(root_module)) return false;
    if (logical_name.size() <= root_module.size()) return false;
    const auto boundary = logical_name[root_module.size()];
    return boundary == u8('.') || boundary == u8(':');
}

auto module_entry_source(const PackageManifest& manifest) -> Result<ResolvedSource> {
    auto source_root_result = canonical_source_root(manifest);
    if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
    auto source_root = rstd::move(source_root_result).unwrap();
    if (manifest.artifact_kind == ArtifactKind::Executable) {
        auto requested = source_root.join(PathBuf::from("main.cppm"_str).as_path());
        return canonical_candidate(manifest, source_root.as_path(), requested.as_path(), None());
    }
    if (manifest.root_module.is_none()) {
        return convention_failure<ResolvedSource>("module discovery requires package.module"_str);
    }
    auto library     = source_root.join(PathBuf::from("lib.cppm"_str).as_path());
    auto module      = source_root.join(PathBuf::from("mod.cppm"_str).as_path());
    auto has_library = path_exists(library.as_path());
    if (has_library.is_err()) return Err(rstd::move(has_library).unwrap_err());
    auto has_module = path_exists(module.as_path());
    if (has_module.is_err()) return Err(rstd::move(has_module).unwrap_err());
    if (*has_library == *has_module) {
        return convention_failure<ResolvedSource>(
            rstd::format("module discovery requires exactly one primary interface "
                         "at 'src/lib.cppm' or 'src/mod.cppm' for '{}'",
                         manifest.root_module->as_str()));
    }
    const auto& requested = *has_library ? library : module;
    return canonical_candidate(
        manifest, source_root.as_path(), requested.as_path(), Some(manifest.root_module->clone()));
}

auto module_source(const PackageManifest& manifest, ref<str> logical_name)
    -> Result<ResolvedSource> {
    if (manifest.root_module.is_none() ||
        ! module_name_belongs(manifest.root_module->as_str(), logical_name)) {
        return convention_failure<ResolvedSource>(rstd::format(
            "module '{}' does not belong to package '{}'", logical_name, manifest.name.as_str()));
    }
    if (logical_name == manifest.root_module->as_str()) return module_entry_source(manifest);

    auto source_root_result = canonical_source_root(manifest);
    if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
    auto source_root = rstd::move(source_root_result).unwrap();
    auto relative    = String::make();
    auto segment     = String::make();
    for (auto index = manifest.root_module->size() + usize(1); index < logical_name.size();
         ++index) {
        const auto value = logical_name[index];
        if (value == u8('.') || value == u8(':')) {
            if (! valid_segment(segment.as_str())) {
                return convention_failure<ResolvedSource>(
                    rstd::format("module '{}' contains an invalid path segment", logical_name));
            }
            if (! relative.is_empty()) relative.push_ascii('/');
            relative.push_str(segment.as_str());
            segment = String::make();
        } else {
            segment.push_ascii(value);
        }
    }
    if (! valid_segment(segment.as_str())) {
        return convention_failure<ResolvedSource>(
            rstd::format("module '{}' contains an invalid path segment", logical_name));
    }
    if (! relative.is_empty()) relative.push_ascii('/');
    relative.push_str(segment.as_str());
    relative.push_str(".cppm"_str);
    auto requested = source_root.join(PathBuf::from(relative.as_str()).as_path());
    return canonical_candidate(
        manifest, source_root.as_path(), requested.as_path(), Some(String::make(logical_name)));
}

auto validate_module_conventions(const PackageSpec&       package,
                                 const Vec<PreparedUnit>& units,
                                 const Vec<ScanResult>&   scans) -> Result<empty> {
    if (units.len() != scans.len()) {
        return convention_failure<empty>(
            "module convention received mismatched units and scan results"_str);
    }
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        const auto& specification = package.targets[target];
        for (const auto& expectation : specification.sources) {
            if (expectation.expected_module.is_none()) continue;
            const ScanResult* actual = nullptr;
            for (const auto& scan : scans) {
                if (scan.unit < units.len() && units[scan.unit].unit.target == target &&
                    same_path(units[scan.unit].unit.source.as_path(), expectation.path.as_path())) {
                    actual = rstd::addressof(scan);
                    break;
                }
            }
            if (actual == nullptr) {
                return convention_failure<empty>(rstd::format(
                    "module convention source '{}' was not scanned", expectation.path.as_path()));
            }
            if (actual->provided.is_none()) {
                return convention_failure<empty>(
                    rstd::format("module convention expected '{}' to provide module '{}'",
                                 expectation.path.as_path(),
                                 expectation.expected_module->as_str()));
            }
            if (actual->provided->logical_name.as_str() != expectation.expected_module->as_str()) {
                return convention_failure<empty>(rstd::format(
                    "module convention expected '{}' to provide module '{}', but Clang "
                    "reported '{}'",
                    expectation.path.as_path(),
                    expectation.expected_module->as_str(),
                    actual->provided->logical_name.as_str()));
            }
        }
    }
    return Ok(empty {});
}

} // namespace tenon
