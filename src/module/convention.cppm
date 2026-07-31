export module tenon.modules:convention;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringMap = rstd::collections::BTreeMap<String, String>;

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, message));
}

auto path_text(ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("source path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto same_path(ref<rstd::path::Path> left,
               ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

auto extension_text(ref<rstd::path::Path> path) -> Option<ref<str>> {
    auto extension = path.extension();
    if (extension.is_none()) return None();
    return (*extension).to_str();
}

auto supported_source(ref<rstd::path::Path> path) -> bool {
    auto extension = extension_text(path);
    if (extension.is_none()) return false;
    return *extension == "cppm"_str || *extension == "cpp"_str || *extension == "cc"_str ||
           *extension == "cxx"_str;
}

auto module_interface(ref<rstd::path::Path> path) -> bool {
    auto extension = extension_text(path);
    return extension.is_some() && *extension == "cppm"_str;
}

auto valid_segment(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (usize index {}; index < value.size(); ++index) {
        const auto byte = value[index];
        const bool alpha = (byte >= u8('a') && byte <= u8('z')) ||
                           (byte >= u8('A') && byte <= u8('Z')) ||
                           byte == u8('_');
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if ((! alpha && ! digit) || (index == usize {} && digit)) return false;
    }
    return true;
}

auto path_segments(ref<rstd::path::Path> path) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto components = path.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_cur_dir()) continue;
        auto text = component->as_os_str().to_str();
        if (text.is_none()) {
            return failure<Vec<String>>(rstd::format(
                "module convention path '{}' contains a non-UTF-8 component", path));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto expected_module(const PackageManifest& manifest,
                     ref<rstd::path::Path> relative) -> Result<String> {
    if (manifest.root_module.is_none()) {
        return failure<String>("module discovery requires package.module"_str);
    }
    const auto& root_module = *manifest.root_module;
    auto segments_result = path_segments(relative);
    if (segments_result.is_err()) return Err(rstd::move(segments_result).unwrap_err());
    auto segments = rstd::move(segments_result).unwrap();
    if (segments.is_empty()) {
        return failure<String>("module convention produced an empty source path"_str);
    }
    const auto& file = segments[segments.len() - usize(1)];
    if (segments.len() == usize(1) && file.as_str() == "main.cppm"_str) {
        return failure<String>(
            "src/main.cppm is not supported by module discovery in manifest version 1"_str);
    }
    if (segments.len() == usize(1) &&
        (file.as_str() == "lib.cppm"_str || file.as_str() == "mod.cppm"_str)) {
        return Ok(root_module.clone());
    }

    auto partition_segments = Vec<String>::make();
    const auto directory_marker = file.as_str() == "mod.cppm"_str;
    const auto directory_count = segments.len() - usize(1);
    for (usize index {}; index < directory_count; ++index) {
        if (! valid_segment(segments[index].as_str())) {
            return failure<String>(rstd::format(
                "module convention path '{}' has invalid module segment '{}'",
                relative,
                segments[index].as_str()));
        }
        partition_segments.push(segments[index].clone());
    }
    if (! directory_marker) {
        auto stem = file.as_str().split_at(file.len() - usize(5)).template get<0>();
        if (! valid_segment(stem)) {
            return failure<String>(rstd::format(
                "module convention path '{}' has invalid module segment '{}'", relative, stem));
        }
        partition_segments.push(String::make(stem));
    }
    if (partition_segments.is_empty()) {
        return failure<String>(rstd::format(
            "module convention path '{}' does not identify a partition", relative));
    }

    auto logical_name = root_module.clone();
    logical_name.push_ascii(u8(':'));
    for (usize index {}; index < partition_segments.len(); ++index) {
        if (index != usize {}) logical_name.push_ascii(u8('.'));
        logical_name.push_str(partition_segments[index].as_str());
    }
    return Ok(rstd::move(logical_name));
}

struct SourceEntry {
    String         key;
    ResolvedSource source;
};

auto append_source(const PackageManifest& manifest,
                   ref<rstd::path::Path> source_root,
                   ref<rstd::path::Path> requested,
                   StringMap& expected_paths,
                   Vec<SourceEntry>& entries) -> Result<empty> {
    auto canonical = rstd::fs::canonicalize(requested);
    if (canonical.is_err()) {
        return failure<empty>(rstd::format(
            "cannot resolve module convention source '{}': {}",
            requested,
            rstd::move(canonical).unwrap_err()));
    }
    auto resolved = rstd::move(canonical).unwrap();
    auto package_relative = resolved.as_path().strip_prefix(manifest.root.as_path());
    auto source_relative = resolved.as_path().strip_prefix(source_root);
    if (package_relative.is_none() || source_relative.is_none() || (*source_relative).is_empty()) {
        return failure<empty>(rstd::format(
            "module convention source '{}' resolves outside package source root", requested));
    }
    auto key_result = path_text(*source_relative);
    if (key_result.is_err()) return Err(rstd::move(key_result).unwrap_err());
    auto key = rstd::move(key_result).unwrap();
    auto expected = Option<String> {};
    if (module_interface(resolved.as_path())) {
        auto logical_result = expected_module(manifest, *source_relative);
        if (logical_result.is_err()) {
            return Err(rstd::move(logical_result).unwrap_err());
        }
        auto logical = rstd::move(logical_result).unwrap();
        auto existing = expected_paths.get(logical.as_str());
        if (existing.is_some()) {
            return failure<empty>(rstd::format(
                "module convention maps both '{}' and '{}' to '{}'",
                (**existing).as_str(),
                key.as_str(),
                logical.as_str()));
        }
        expected_paths.insert(logical.clone(), key.clone());
        expected = Some(rstd::move(logical));
    }
    entries.push(SourceEntry {
        .key = key.clone(),
        .source = ResolvedSource {
            .relative_path = PathBuf::from(*package_relative),
            .canonical_path = rstd::move(resolved),
            .origin = SourceOrigin::Convention,
            .expected_module = rstd::move(expected),
        },
    });
    return Ok(empty {});
}

auto walk_sources(const PackageManifest& manifest,
                  ref<rstd::path::Path> source_root,
                  ref<rstd::path::Path> directory,
                  StringMap& expected_paths,
                  Vec<SourceEntry>& entries) -> Result<empty> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return failure<empty>(rstd::format(
            "cannot enumerate module source directory '{}': {}",
            directory,
            rstd::move(opened).unwrap_err()));
    }
    auto directory_entries = rstd::move(opened).unwrap();
    for (auto next = directory_entries.next(); next.is_some(); next = directory_entries.next()) {
        auto entry_result = rstd::move(next).unwrap();
        if (entry_result.is_err()) {
            return failure<empty>(rstd::format(
                "cannot enumerate module source directory '{}': {}",
                directory,
                rstd::move(entry_result).unwrap_err()));
        }
        auto entry = rstd::move(entry_result).unwrap();
        auto type = entry.file_type();
        if (type.is_err()) {
            return failure<empty>(rstd::format(
                "cannot inspect module source entry '{}': {}",
                entry.path().as_path(),
                rstd::move(type).unwrap_err()));
        }
        auto path = entry.path();
        if (type->is_dir()) {
            auto nested = walk_sources(
                manifest, source_root, path.as_path(), expected_paths, entries);
            if (nested.is_err()) return nested;
        } else if (type->is_file() && supported_source(path.as_path())) {
            auto appended = append_source(
                manifest, source_root, path.as_path(), expected_paths, entries);
            if (appended.is_err()) return appended;
        }
    }
    return Ok(empty {});
}

} // namespace tenon

export namespace tenon
{

auto discover_module_sources(const PackageManifest& manifest) -> Result<ResolvedSourceSet> {
    if (manifest.root_module.is_none()) {
        return failure<ResolvedSourceSet>("module discovery requires package.module"_str);
    }
    const auto& root_module = *manifest.root_module;
    auto requested_root = manifest.root.join(PathBuf::from("src"_str).as_path());
    auto canonical_root = rstd::fs::canonicalize(requested_root.as_path());
    if (canonical_root.is_err()) {
        return failure<ResolvedSourceSet>(rstd::format(
            "cannot resolve module source root '{}': {}",
            requested_root.as_path(),
            rstd::move(canonical_root).unwrap_err()));
    }
    auto source_root = rstd::move(canonical_root).unwrap();
    auto metadata = rstd::fs::metadata(source_root.as_path());
    if (metadata.is_err() || ! metadata->is_dir()) {
        return failure<ResolvedSourceSet>(rstd::format(
            "module source root '{}' is not a directory", source_root.as_path()));
    }

    auto expected_paths = StringMap::make();
    auto entries = Vec<SourceEntry>::make();
    auto walked = walk_sources(
        manifest, source_root.as_path(), source_root.as_path(), expected_paths, entries);
    if (walked.is_err()) return Err(rstd::move(walked).unwrap_err());
    if (! expected_paths.contains_key(root_module.as_str())) {
        return failure<ResolvedSourceSet>(rstd::format(
            "module discovery requires exactly one primary interface at 'src/lib.cppm' or 'src/mod.cppm' for '{}'",
            root_module.as_str()));
    }
    rstd::slice_::sort_unstable_by(
        entries.as_mut_slice().as_mut_ref(),
        [](const SourceEntry& left, const SourceEntry& right) { return left.key < right.key; });
    auto sources = Vec<ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) sources.push(rstd::move(entry.source));
    return Ok(ResolvedSourceSet { .sources = rstd::move(sources) });
}

auto validate_module_conventions(const PackageSpec& package,
                                 const Vec<PreparedUnit>& units,
                                 const Vec<ScanResult>& scans) -> Result<empty> {
    if (units.len() != scans.len()) {
        return failure<empty>(
            "module convention received mismatched units and scan results"_str);
    }
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        const auto& specification = package.targets[target];
        if (specification.module_expectations.is_empty()) continue;
        for (const auto& expectation : specification.module_expectations) {
            const ScanResult* actual = nullptr;
            for (const auto& scan : scans) {
                if (scan.unit < units.len() && units[scan.unit].unit.target == target &&
                    same_path(units[scan.unit].unit.source.as_path(),
                              expectation.source.as_path())) {
                    actual = rstd::addressof(scan);
                    break;
                }
            }
            if (actual == nullptr) {
                return failure<empty>(rstd::format(
                    "module convention source '{}' was not scanned", expectation.source.as_path()));
            }
            if (actual->provided.is_none()) {
                return failure<empty>(rstd::format(
                    "module convention expected '{}' to provide interface '{}'",
                    expectation.source.as_path(),
                    expectation.logical_name.as_str()));
            }
            const auto& provided = *actual->provided;
            if (! provided.is_interface ||
                provided.logical_name.as_str() != expectation.logical_name.as_str()) {
                return failure<empty>(rstd::format(
                    "module convention expected '{}' to provide interface '{}', but Clang reported '{}'",
                    expectation.source.as_path(),
                    expectation.logical_name.as_str(),
                    provided.logical_name.as_str()));
            }
        }
        for (const auto& scan : scans) {
            if (scan.unit >= units.len() || units[scan.unit].unit.target != target ||
                scan.provided.is_none() || ! scan.provided->is_interface) {
                continue;
            }
            bool expected = false;
            for (const auto& expectation : specification.module_expectations) {
                if (same_path(units[scan.unit].unit.source.as_path(),
                              expectation.source.as_path())) {
                    expected = true;
                    break;
                }
            }
            if (! expected) {
                return failure<empty>(rstd::format(
                    "module convention implementation source '{}' unexpectedly provides interface '{}'",
                    units[scan.unit].unit.source.as_path(),
                    scan.provided->logical_name.as_str()));
            }
        }
    }
    return Ok(empty {});
}

} // namespace tenon
