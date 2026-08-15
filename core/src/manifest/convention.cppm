module;
#include <rstd/macro.hpp>

module lito.core:manifest.convention;

import rstd;
import :manifest.target;
import :manifest.error;
import :package.identity;
import :manifest.source_convention;
import :manifest.locator;
import :manifest.primitives;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

namespace lito
{

auto valid_module_name(ref<str> value) -> bool {
    if (value.size() == usize {}) return false;
    bool segment_start = true;
    for (usize index {}; index < value.size(); ++index) {
        const auto byte = value[index];
        if (byte == u8('.')) {
            if (segment_start) return false;
            segment_start = true;
            continue;
        }
        const bool alpha = (byte >= u8('a') && byte <= u8('z')) ||
                           (byte >= u8('A') && byte <= u8('Z')) || byte == u8('_');
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if ((! alpha && ! digit) || (segment_start && digit)) return false;
        segment_start = false;
    }
    return ! segment_start;
}

auto package_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value) {
        const bool alpha =
            (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z'));
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if (! alpha && ! digit && byte != u8('-') && byte != u8('_')) return false;
    }
    return true;
}

auto discover_install_script(ref<rstd::path::Path> package_root)
    -> ManifestSchemaResult<Option<PathBuf>> {
    auto requested = PathBuf::from(package_root).join(PathBuf::from("install.lua"_str).as_path());
    auto exists    = rstd::fs::exists(requested.as_path());
    if (exists.is_err()) {
        return manifest_io_failure<Option<PathBuf>>("install script"_str,
                                                    "inspect file"_str,
                                                    requested.as_path(),
                                                    rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(None());
    auto metadata = rstd::fs::symlink_metadata(requested.as_path());
    if (metadata.is_err()) {
        return manifest_io_failure<Option<PathBuf>>("install script"_str,
                                                    "inspect file"_str,
                                                    requested.as_path(),
                                                    rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return manifest_schema_failure<Option<PathBuf>>(rstd::format(
            "install script '{}' must be a regular file and not a symlink", requested.as_path()));
    }
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return manifest_io_failure<Option<PathBuf>>("install script"_str,
                                                    "resolve file"_str,
                                                    requested.as_path(),
                                                    rstd::move(canonical).unwrap_err());
    }
    return Ok(Some(rstd::move(canonical).unwrap()));
}

struct ConventionalSource {
    String  key;
    PathBuf path;
};

struct ConventionalBenchmark {
    String       name;
    Vec<PathBuf> sources;
};

auto conventional_source(ref<rstd::path::Path> source_root, ref<rstd::path::Path> path)
    -> ManifestSchemaResult<ConventionalSource> {
    auto relative = path.strip_prefix(source_root);
    if (relative.is_none() || relative->is_empty()) {
        return manifest_schema_failure<ConventionalSource>(
            rstd::format("conventional benchmark source '{}' is outside package source root '{}'",
                         path,
                         source_root));
    }
    auto text = relative->to_str();
    if (text.is_none()) {
        return manifest_schema_failure<ConventionalSource>(
            rstd::format("conventional benchmark source '{}' is not valid UTF-8", path));
    }
    return Ok(ConventionalSource {
        .key  = String::make(*text),
        .path = PathBuf::from(*relative),
    });
}

auto collect_conventional_sources(ref<rstd::path::Path>    source_root,
                                  ref<rstd::path::Path>    directory,
                                  Vec<ConventionalSource>& sources) -> ManifestSchemaResult<empty> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return manifest_io_failure<empty>("conventional benchmark"_str,
                                          "enumerate directory"_str,
                                          directory,
                                          rstd::move(opened).unwrap_err());
    }
    auto stream = rstd::move(opened).unwrap();
    for (auto next = stream.next(); next.is_some(); next = stream.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return manifest_io_failure<empty>("conventional benchmark"_str,
                                              "enumerate directory"_str,
                                              directory,
                                              rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            return manifest_io_failure<empty>("conventional benchmark"_str,
                                              "inspect entry"_str,
                                              entry.path().as_path(),
                                              rstd::move(type).unwrap_err());
        }
        auto path = entry.path();
        if (type->is_dir()) {
            auto nested_manifest = try_locate_manifest(path.as_path());
            if (nested_manifest.is_err()) {
                return Err(
                    rstd::into<ManifestSchemaError>(rstd::move(nested_manifest).unwrap_err()));
            }
            if (nested_manifest->is_some()) continue;
            rstd_try(collect_conventional_sources(source_root, path.as_path(), sources));
            continue;
        }
        if (! type->is_file() || ! supported_manifest_source(path.as_path())) continue;
        sources.push(rstd_try(conventional_source(source_root, path.as_path())));
    }
    return Ok(empty {});
}

auto path_name(ref<rstd::path::Path> path, ref<str> context) -> ManifestSchemaResult<String> {
    auto name = path.file_name();
    if (name.is_none())
        return manifest_schema_failure<String>(rstd::format("{} '{}' has no name", context, path));
    auto text = name->to_str();
    if (text.is_none()) {
        return manifest_schema_failure<String>(
            rstd::format("{} '{}' is not valid UTF-8", context, path));
    }
    return Ok(String::make(*text));
}

auto file_stem(ref<rstd::path::Path> path) -> ManifestSchemaResult<String> {
    auto name      = rstd_try(path_name(path, "benchmark source"_str));
    auto extension = path.extension();
    if (extension.is_none()) {
        return manifest_schema_failure<String>(
            rstd::format("benchmark source '{}' has no extension", path));
    }
    auto extension_text = extension->to_str();
    if (extension_text.is_none() || name.len() <= extension_text->len() + usize(1)) {
        return manifest_schema_failure<String>(
            rstd::format("benchmark source '{}' has no target name", path));
    }
    name.truncate(name.len() - extension_text->len() - usize(1));
    return Ok(rstd::move(name));
}

auto explicit_benchmark_name(const Vec<PackageTargetManifest>& targets, ref<str> name) -> bool {
    for (const auto& target : targets) {
        if (target.is_Benchmark() && target.as_Benchmark().name.as_str() == name) return true;
    }
    return false;
}

auto discover_conventional_benchmarks(ref<rstd::path::Path>             package_root,
                                      ref<rstd::path::Path>             source_root,
                                      const Vec<PackageTargetManifest>& explicit_targets)
    -> ManifestSchemaResult<Vec<PackageTargetManifest>> {
    auto result    = Vec<PackageTargetManifest>::make();
    auto directory = PathBuf::from(package_root).join(PathBuf::from("benches"_str).as_path());
    auto exists    = rstd::fs::exists(directory.as_path());
    if (exists.is_err()) {
        return manifest_io_failure<Vec<PackageTargetManifest>>("conventional benchmark"_str,
                                                               "inspect directory"_str,
                                                               directory.as_path(),
                                                               rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(rstd::move(result));

    auto metadata = rstd::fs::metadata(directory.as_path());
    if (metadata.is_err()) {
        return manifest_io_failure<Vec<PackageTargetManifest>>("conventional benchmark"_str,
                                                               "inspect directory"_str,
                                                               directory.as_path(),
                                                               rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return manifest_schema_failure<Vec<PackageTargetManifest>>(
            rstd::format("benchmark path '{}' is not a directory", directory.as_path()));
    }
    auto nested_manifest = try_locate_manifest(directory.as_path());
    if (nested_manifest.is_err()) {
        return Err(rstd::into<ManifestSchemaError>(rstd::move(nested_manifest).unwrap_err()));
    }
    if (nested_manifest->is_some()) return Ok(rstd::move(result));

    auto candidates = Vec<ConventionalBenchmark>::make();
    auto opened     = rstd::fs::read_dir(directory.as_path());
    if (opened.is_err()) {
        return manifest_io_failure<Vec<PackageTargetManifest>>("conventional benchmark"_str,
                                                               "enumerate directory"_str,
                                                               directory.as_path(),
                                                               rstd::move(opened).unwrap_err());
    }
    auto stream = rstd::move(opened).unwrap();
    for (auto next = stream.next(); next.is_some(); next = stream.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return manifest_io_failure<Vec<PackageTargetManifest>>("conventional benchmark"_str,
                                                                   "enumerate directory"_str,
                                                                   directory.as_path(),
                                                                   rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            return manifest_io_failure<Vec<PackageTargetManifest>>("conventional benchmark"_str,
                                                                   "inspect entry"_str,
                                                                   entry.path().as_path(),
                                                                   rstd::move(type).unwrap_err());
        }
        auto path = entry.path();
        if (type->is_file()) {
            if (! runnable_manifest_source(path.as_path())) continue;
            auto name = rstd_try(file_stem(path.as_path()));
            if (! package_name_is_valid(name.as_str())) {
                return manifest_schema_failure<Vec<PackageTargetManifest>>(rstd::format(
                    "conventional benchmark source '{}' infers invalid target name '{}'",
                    path.as_path(),
                    name.as_str()));
            }
            auto source  = rstd_try(conventional_source(source_root, path.as_path()));
            auto sources = Vec<PathBuf>::make();
            sources.push(rstd::move(source.path));
            candidates.push(ConventionalBenchmark {
                .name    = rstd::move(name),
                .sources = rstd::move(sources),
            });
            continue;
        }
        if (! type->is_dir()) continue;
        auto child_manifest = try_locate_manifest(path.as_path());
        if (child_manifest.is_err()) {
            return Err(rstd::into<ManifestSchemaError>(rstd::move(child_manifest).unwrap_err()));
        }
        if (child_manifest->is_some()) continue;

        auto name = rstd_try(path_name(path.as_path(), "benchmark directory"_str));
        if (! package_name_is_valid(name.as_str())) {
            return manifest_schema_failure<Vec<PackageTargetManifest>>(rstd::format(
                "conventional benchmark directory '{}' infers invalid target name '{}'",
                path.as_path(),
                name.as_str()));
        }
        auto child = rstd::fs::read_dir(path.as_path());
        if (child.is_err()) {
            return manifest_io_failure<Vec<PackageTargetManifest>>("conventional benchmark"_str,
                                                                   "enumerate directory"_str,
                                                                   path.as_path(),
                                                                   rstd::move(child).unwrap_err());
        }
        auto child_stream = rstd::move(child).unwrap();
        auto main_sources = usize {};
        for (auto child_next = child_stream.next(); child_next.is_some();
             child_next      = child_stream.next()) {
            auto child_item = rstd::move(child_next).unwrap();
            if (child_item.is_err()) {
                return manifest_io_failure<Vec<PackageTargetManifest>>(
                    "conventional benchmark"_str,
                    "enumerate directory"_str,
                    path.as_path(),
                    rstd::move(child_item).unwrap_err());
            }
            auto child_entry = rstd::move(child_item).unwrap();
            auto child_type  = child_entry.file_type();
            if (child_type.is_err()) {
                return manifest_io_failure<Vec<PackageTargetManifest>>(
                    "conventional benchmark"_str,
                    "inspect entry"_str,
                    child_entry.path().as_path(),
                    rstd::move(child_type).unwrap_err());
            }
            if (! child_type->is_file() ||
                ! runnable_manifest_source(child_entry.path().as_path())) {
                continue;
            }
            auto main_name = rstd_try(file_stem(child_entry.path().as_path()));
            if (main_name.as_str() == "main"_str) ++main_sources;
        }
        if (main_sources == usize {}) continue;
        if (main_sources != usize(1)) {
            return manifest_schema_failure<Vec<PackageTargetManifest>>(rstd::format(
                "conventional benchmark directory '{}' contains more than one main source",
                path.as_path()));
        }
        auto sources = Vec<ConventionalSource>::make();
        rstd_try(collect_conventional_sources(source_root, path.as_path(), sources));
        rstd::slice_::sort_unstable_by(
            sources.as_mut_slice().as_mut_ref(),
            [](const ConventionalSource& left, const ConventionalSource& right) {
                return left.key < right.key;
            });
        auto paths = Vec<PathBuf>::with_capacity(sources.len());
        for (auto& source : sources) paths.push(rstd::move(source.path));
        candidates.push(ConventionalBenchmark {
            .name    = rstd::move(name),
            .sources = rstd::move(paths),
        });
    }

    rstd::slice_::sort_unstable_by(
        candidates.as_mut_slice().as_mut_ref(),
        [](const ConventionalBenchmark& left, const ConventionalBenchmark& right) {
            return left.name < right.name;
        });
    for (usize index {}; index < candidates.len(); ++index) {
        if (index != usize {} && candidates[index - usize(1)].name == candidates[index].name) {
            return manifest_schema_failure<Vec<PackageTargetManifest>>(rstd::format(
                "conventional benches repeat target name '{}'", candidates[index].name.as_str()));
        }
        if (explicit_benchmark_name(explicit_targets, candidates[index].name.as_str())) continue;
        result.push(PackageTargetManifest::Benchmark(
            rstd::move(candidates[index].name),
            TargetSourceManifest {
                .discovery        = SourceDiscoveryMode::Explicit,
                .declared_sources = rstd::move(candidates[index].sources),
            },
            true));
    }
    return Ok(rstd::move(result));
}

} // namespace lito
