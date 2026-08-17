export module lito.cpp:modules.convention;

import rstd;
import lito.core;
import :build.scan;
import :build.unit;
import :modules.error;
import :package.metadata;
import :package.spec;
import :package.target;
import :source.discovery;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::cpp
{

auto runnable_target(lito::package::PackageTargetKind kind) -> bool {
    return kind == lito::package::PackageTargetKind::Binary ||
           kind == lito::package::PackageTargetKind::Test ||
           kind == lito::package::PackageTargetKind::Benchmark;
}

template<typename T>
auto convention_failure(String message) -> ModuleResult<T> {
    return Err(ModuleError::Convention(rstd::move(message)));
}

template<typename T>
auto convention_failure(ref<str> message) -> ModuleResult<T> {
    return Err(ModuleError::Convention(String::make(message)));
}

template<typename T>
auto convention_io_failure(ref<str>               operation,
                           ref<rstd::path::Path>  path,
                           rstd::io::error::Error source) -> ModuleResult<T> {
    return Err(ModuleError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
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

auto relative_module_path(ref<str> logical_name, usize start) -> ModuleResult<String> {
    auto relative  = String::make();
    auto segment   = String::make();
    auto partition = false;
    for (auto index = start; index < logical_name.size(); ++index) {
        const auto value = logical_name[index];
        if (value == u8('.') || value == u8(':')) {
            if (value == u8(':') && partition) {
                return convention_failure<String>(rstd::format(
                    "module '{}' contains multiple partition separators", logical_name));
            }
            if (! valid_segment(segment.as_str())) {
                return convention_failure<String>(
                    rstd::format("module '{}' contains an invalid path segment", logical_name));
            }
            if (! relative.is_empty()) relative.push_ascii('/');
            relative.push_str(segment.as_str());
            segment = String::make();
            if (value == u8(':')) partition = true;
        } else {
            segment.push_ascii(value);
        }
    }
    if (! valid_segment(segment.as_str())) {
        return convention_failure<String>(
            rstd::format("module '{}' contains an invalid path segment", logical_name));
    }
    if (! relative.is_empty()) relative.push_ascii('/');
    relative.push_str(segment.as_str());
    return Ok(rstd::move(relative));
}

auto canonical_source_root(ref<rstd::path::Path> package_source_root) -> ModuleResult<PathBuf> {
    auto requested = PathBuf::from(package_source_root).join(PathBuf::from("src"_str).as_path());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return convention_io_failure<PathBuf>(
            "resolve source root"_str, requested.as_path(), rstd::move(canonical).unwrap_err());
    }
    auto root     = rstd::move(canonical).unwrap();
    auto metadata = rstd::fs::metadata(root.as_path());
    if (metadata.is_err() || ! metadata->is_dir()) {
        return convention_failure<PathBuf>(
            rstd::format("module source root '{}' is not a directory", root.as_path()));
    }
    return Ok(rstd::move(root));
}

auto canonical_candidate(ref<rstd::path::Path> package_source_root,
                         ref<rstd::path::Path> source_root,
                         ref<rstd::path::Path> requested,
                         Option<String>        expected) -> ModuleResult<ResolvedSource> {
    auto canonical = rstd::fs::canonicalize(requested);
    if (canonical.is_err()) {
        return convention_io_failure<ResolvedSource>(
            "resolve convention source"_str, requested, rstd::move(canonical).unwrap_err());
    }
    auto resolved         = rstd::move(canonical).unwrap();
    auto source_relative  = resolved.as_path().strip_prefix(source_root);
    auto package_relative = resolved.as_path().strip_prefix(package_source_root);
    if (source_relative.is_none() || package_relative.is_none() || (*source_relative).is_empty()) {
        return convention_failure<ResolvedSource>(rstd::format(
            "module convention source '{}' resolves outside package source root", requested));
    }
    auto metadata = rstd::fs::metadata(resolved.as_path());
    if (metadata.is_err()) {
        return convention_io_failure<ResolvedSource>(
            "inspect convention source"_str, resolved.as_path(), rstd::move(metadata).unwrap_err());
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

auto file_exists(ref<rstd::path::Path> path) -> ModuleResult<bool> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return convention_io_failure<bool>(
            "inspect convention source"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(false);
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return convention_io_failure<bool>(
            "inspect convention source"_str, path, rstd::move(metadata).unwrap_err());
    }
    return Ok(metadata->is_file());
}

auto root_module_source(const ResolvedTarget& target) -> ModuleResult<ResolvedSource> {
    if (target.source.module.is_none()) {
        return convention_failure<ResolvedSource>("module discovery requires target.module"_str);
    }
    auto source_root_result = canonical_source_root(target.source_root.as_path());
    if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
    auto source_root = rstd::move(source_root_result).unwrap();
    auto relative    = runnable_target(target.id.kind) ? "mod.cppm"_str : "lib.cppm"_str;
    auto requested   = source_root.join(PathBuf::from(relative).as_path());
    return canonical_candidate(target.source_root.as_path(),
                               source_root.as_path(),
                               requested.as_path(),
                               Some(target.source.module->clone()));
}

auto namespace_relative_path(ref<str> root_module, ref<str> logical_name) -> ModuleResult<String> {
    auto namespace_size = usize {};
    while (namespace_size < root_module.size() && root_module[namespace_size] != u8('.') &&
           root_module[namespace_size] != u8(':')) {
        ++namespace_size;
    }
    auto same_namespace = logical_name.size() >= namespace_size;
    for (auto index = usize {}; same_namespace && index < namespace_size; ++index) {
        same_namespace = logical_name[index] == root_module[index];
    }
    if (namespace_size == usize {} || logical_name.size() <= namespace_size || ! same_namespace ||
        (logical_name[namespace_size] != u8('.') && logical_name[namespace_size] != u8(':'))) {
        return convention_failure<String>(
            rstd::format("module '{}' is outside namespace '{}'", logical_name, root_module));
    }
    return relative_module_path(logical_name, namespace_size + usize(1));
}

auto relative_source_exists(ref<rstd::path::Path> source_root, ref<str> relative)
    -> ModuleResult<bool> {
    auto direct_relative = String::make(relative);
    direct_relative.push_str(".cppm"_str);
    auto direct     = PathBuf::from(source_root).join(PathBuf::from(direct_relative).as_path());
    auto has_direct = file_exists(direct.as_path());
    if (has_direct.is_err() || *has_direct) return has_direct;

    auto module_relative = String::make(relative);
    module_relative.push_str("/mod.cppm"_str);
    return file_exists(
        PathBuf::from(source_root).join(PathBuf::from(module_relative).as_path()).as_path());
}

auto relative_module_source(const ResolvedTarget& target,
                            ref<rstd::path::Path> source_root,
                            ref<str>              logical_name,
                            ref<str> relative) -> ModuleResult<Option<ResolvedSource>> {
    auto direct_relative = String::make(relative);
    direct_relative.push_str(".cppm"_str);
    auto direct     = PathBuf::from(source_root).join(PathBuf::from(direct_relative).as_path());
    auto has_direct = file_exists(direct.as_path());
    if (has_direct.is_err()) return Err(rstd::move(has_direct).unwrap_err());
    if (*has_direct) {
        auto resolved = canonical_candidate(target.source_root.as_path(),
                                            source_root,
                                            direct.as_path(),
                                            Some(String::make(logical_name)));
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        return Ok(Some(rstd::move(resolved).unwrap()));
    }

    auto module_relative = String::make(relative);
    module_relative.push_str("/mod.cppm"_str);
    auto requested  = PathBuf::from(source_root).join(PathBuf::from(module_relative).as_path());
    auto has_module = file_exists(requested.as_path());
    if (has_module.is_err()) return Err(rstd::move(has_module).unwrap_err());
    if (! *has_module) return Ok(None());
    auto resolved = canonical_candidate(target.source_root.as_path(),
                                        source_root,
                                        requested.as_path(),
                                        Some(String::make(logical_name)));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    return Ok(Some(rstd::move(resolved).unwrap()));
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto module_name_belongs(ref<str> root_module, ref<str> logical_name) -> bool {
    if (logical_name == root_module) return true;
    if (! logical_name.starts_with(root_module)) return false;
    if (logical_name.size() <= root_module.size()) return false;
    const auto boundary = logical_name[root_module.size()];
    return boundary == u8('.') || boundary == u8(':');
}

auto module_relative_path(ref<str> root_module, ref<str> logical_name) -> ModuleResult<String> {
    if (module_name_belongs(root_module, logical_name) && logical_name != root_module) {
        return relative_module_path(logical_name, root_module.size() + usize(1));
    }
    return namespace_relative_path(root_module, logical_name);
}

auto module_entry_source(const ResolvedTarget& target) -> ModuleResult<ResolvedSource> {
    if (runnable_target(target.id.kind)) {
        auto source_root_result = canonical_source_root(target.source_root.as_path());
        if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
        auto source_root = rstd::move(source_root_result).unwrap();
        auto requested   = source_root.join(PathBuf::from("main.cppm"_str).as_path());
        return canonical_candidate(
            target.source_root.as_path(), source_root.as_path(), requested.as_path(), None());
    }
    return root_module_source(target);
}

auto module_source_exists(const ResolvedTarget& target, ref<str> logical_name)
    -> ModuleResult<bool> {
    if (target.source.module.is_none()) return Ok(false);
    auto source_root_result = canonical_source_root(target.source_root.as_path());
    if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
    auto source_root = rstd::move(source_root_result).unwrap();
    if (logical_name == target.source.module->as_str()) {
        auto relative = runnable_target(target.id.kind) ? "mod.cppm"_str : "lib.cppm"_str;
        return file_exists(source_root.join(PathBuf::from(relative).as_path()).as_path());
    }
    auto relative = module_relative_path(target.source.module->as_str(), logical_name);
    if (relative.is_err()) return Ok(false);
    auto exists = relative_source_exists(source_root.as_path(), relative->as_str());
    if (exists.is_err() || *exists ||
        ! module_name_belongs(target.source.module->as_str(), logical_name)) {
        return exists;
    }

    auto fallback = namespace_relative_path(target.source.module->as_str(), logical_name);
    if (fallback.is_err() || fallback->as_str() == relative->as_str()) return exists;
    return relative_source_exists(source_root.as_path(), fallback->as_str());
}

auto module_source(const ResolvedTarget& target, ref<str> logical_name)
    -> ModuleResult<ResolvedSource> {
    if (target.source.module.is_none()) {
        return convention_failure<ResolvedSource>(
            rstd::format("target '{}::{}' does not declare a module",
                         target.id.package.as_str(),
                         target.id.name.as_str()));
    }
    if (logical_name == target.source.module->as_str()) return root_module_source(target);

    auto source_root_result = canonical_source_root(target.source_root.as_path());
    if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
    auto source_root = rstd::move(source_root_result).unwrap();
    auto relative    = module_relative_path(target.source.module->as_str(), logical_name);
    if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
    auto source =
        relative_module_source(target, source_root.as_path(), logical_name, relative->as_str());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    if (source->is_some()) return Ok(rstd::move(**source));

    if (module_name_belongs(target.source.module->as_str(), logical_name)) {
        auto fallback = namespace_relative_path(target.source.module->as_str(), logical_name);
        if (fallback.is_err()) return Err(rstd::move(fallback).unwrap_err());
        if (fallback->as_str() != relative->as_str()) {
            source = relative_module_source(
                target, source_root.as_path(), logical_name, fallback->as_str());
            if (source.is_err()) return Err(rstd::move(source).unwrap_err());
            if (source->is_some()) return Ok(rstd::move(**source));
        }
    }
    return convention_failure<ResolvedSource>(
        rstd::format("module '{}' has no convention source in target '{}::{}'",
                     logical_name,
                     target.id.package.as_str(),
                     target.id.name.as_str()));
}

auto module_companion_source(const ResolvedTarget& target, const ResolvedSource& source)
    -> ModuleResult<Option<ResolvedSource>> {
    auto relative = source.relative_path.as_path().to_str();
    if (relative.is_none()) {
        return convention_failure<Option<ResolvedSource>>(
            rstd::format("module source '{}' is not valid UTF-8", source.relative_path.as_path()));
    }
    if (! relative->ends_with(".cppm"_str)) return Ok(None());

    auto companion = String::make(*relative);
    companion.truncate(companion.len() - usize(1));
    auto requested = target.source_root.join(PathBuf::from(companion.as_str()).as_path());
    auto exists    = file_exists(requested.as_path());
    if (exists.is_err()) return Err(rstd::move(exists).unwrap_err());
    if (! *exists) return Ok(None());

    auto source_root_result = canonical_source_root(target.source_root.as_path());
    if (source_root_result.is_err()) return Err(rstd::move(source_root_result).unwrap_err());
    auto source_root = rstd::move(source_root_result).unwrap();
    auto resolved    = canonical_candidate(
        target.source_root.as_path(), source_root.as_path(), requested.as_path(), None());
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    return Ok(Some(rstd::move(resolved).unwrap()));
}

auto validate_module_conventions(const PackageSpec&       package,
                                 const Vec<PreparedUnit>& units,
                                 const Vec<ScanResult>&   scans) -> ModuleResult<empty> {
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

} // namespace lito::cpp
