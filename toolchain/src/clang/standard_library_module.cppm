module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:standard_library_module;

import rstd;
import lito.crypto;
import rstd.json;
import lito.core;
import lito.cpp;
import lito.toolchain.common;
import :support;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;

namespace lito
{

auto module_error_context(const cpp::ResolvedStandardLibrary& library)
    -> StandardLibraryModuleErrorContext {
    return StandardLibraryModuleErrorContext {
        .family   = library.family,
        .target   = library.target.clone(),
        .artifact = library.artifact.clone(),
    };
}

template<typename T>
auto module_failure(StandardLibraryModuleError error) -> ToolchainResult<T> {
    return Err(ToolchainError::StandardLibraryModule(rstd::move(error)));
}

template<typename T>
auto manifest_failure(const cpp::ResolvedStandardLibrary& library,
                      ref<rstd::path::Path>               path,
                      String                              message,
                      Option<String>                      entry = None()) -> ToolchainResult<T> {
    return module_failure<T>(StandardLibraryModuleError::Manifest(module_error_context(library),
                                                                  PathBuf::from(path),
                                                                  rstd::move(entry),
                                                                  rstd::move(message)));
}

template<typename T>
auto manifest_failure(const cpp::ResolvedStandardLibrary& library,
                      ref<rstd::path::Path>               path,
                      ref<str>                            message,
                      Option<String>                      entry = None()) -> ToolchainResult<T> {
    return manifest_failure<T>(library, path, String::make(message), rstd::move(entry));
}

template<typename T>
auto manifest_parse_failure(const cpp::ResolvedStandardLibrary& library,
                            ref<rstd::path::Path>               path,
                            lito::parse::Error                  source,
                            Option<String> entry = None()) -> ToolchainResult<T> {
    return module_failure<T>(StandardLibraryModuleError::Parse(
        module_error_context(library), PathBuf::from(path), rstd::move(entry), rstd::move(source)));
}

template<typename T>
auto module_io_failure(const cpp::ResolvedStandardLibrary& library,
                       ref<str>                            operation,
                       ref<rstd::path::Path>               path,
                       rstd::io::error::Error              source) -> ToolchainResult<T> {
    return module_failure<T>(StandardLibraryModuleError::Io(module_error_context(library),
                                                            String::make(operation),
                                                            PathBuf::from(path),
                                                            None(),
                                                            None(),
                                                            rstd::move(source)));
}

template<typename T>
auto module_manifest_io_failure(const cpp::ResolvedStandardLibrary& library,
                                ref<str>                            operation,
                                ref<rstd::path::Path>               path,
                                ref<rstd::path::Path>               manifest,
                                Option<String>                      entry,
                                rstd::io::error::Error              source) -> ToolchainResult<T> {
    return module_failure<T>(StandardLibraryModuleError::Io(module_error_context(library),
                                                            String::make(operation),
                                                            PathBuf::from(path),
                                                            Some(PathBuf::from(manifest)),
                                                            rstd::move(entry),
                                                            rstd::move(source)));
}

auto required_member(const Json&                         value,
                     ref<str>                            key,
                     ref<str>                            entry,
                     const cpp::ResolvedStandardLibrary& library,
                     ref<rstd::path::Path>               manifest) -> ToolchainResult<ref<Json>> {
    auto member =
        lito::parse::json::required_member(value, key, lito::parse::NodePath::root(entry));
    if (member.is_err()) {
        return manifest_parse_failure<ref<Json>>(
            library, manifest, rstd::move(member).unwrap_err(), Some(String::make(entry)));
    }
    return Ok(*member);
}

auto required_string(const Json&                         value,
                     ref<str>                            key,
                     ref<str>                            entry,
                     const cpp::ResolvedStandardLibrary& library,
                     ref<rstd::path::Path>               manifest) -> ToolchainResult<ref<str>> {
    auto member = rstd_try(required_member(value, key, entry, library, manifest));
    auto text   = lito::parse::json::string(*member, lito::parse::NodePath::root(entry).field(key));
    if (text.is_err()) {
        return manifest_parse_failure<ref<str>>(
            library, manifest, rstd::move(text).unwrap_err(), Some(String::make(entry)));
    }
    return Ok(*text);
}

auto resolve_manifest_path(const cpp::ResolvedStandardLibrary& library,
                           ref<rstd::path::Path>               manifest,
                           ref<str>                            value,
                           ref<str>                            entry,
                           ref<str>                            field,
                           bool directory) -> ToolchainResult<PathBuf> {
    auto parent = manifest.parent();
    if (parent.is_none()) {
        return manifest_failure<PathBuf>(
            library, manifest, "manifest has no parent directory"_str, Some(String::make(entry)));
    }
    auto requested = PathBuf::from(*parent).join(PathBuf::from(value).as_path());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return module_manifest_io_failure<PathBuf>(library,
                                                   rstd::format("resolve {}", field).as_str(),
                                                   requested.as_path(),
                                                   manifest,
                                                   Some(String::make(entry)),
                                                   rstd::move(canonical).unwrap_err());
    }
    auto metadata = rstd::fs::metadata(canonical->as_path());
    if (metadata.is_err()) {
        return module_manifest_io_failure<PathBuf>(library,
                                                   rstd::format("inspect {}", field).as_str(),
                                                   canonical->as_path(),
                                                   manifest,
                                                   Some(String::make(entry)),
                                                   rstd::move(metadata).unwrap_err());
    }
    if ((directory && ! metadata->is_dir()) || (! directory && ! metadata->is_file())) {
        return manifest_failure<PathBuf>(library,
                                         manifest,
                                         rstd::format("{} '{}' is not a {}",
                                                      field,
                                                      canonical->as_path(),
                                                      directory ? "directory"_str : "file"_str),
                                         Some(String::make(entry)));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto select_manifest(const cpp::ResolvedStandardLibrary& library) -> ToolchainResult<PathBuf> {
    auto selected = Option<PathBuf> {};
    for (const auto& candidate : library.module_manifest.paths) {
        auto exists = rstd::fs::exists(candidate.as_path());
        if (exists.is_err()) {
            return module_io_failure<PathBuf>(library,
                                              "inspect standard library module manifest"_str,
                                              candidate.as_path(),
                                              rstd::move(exists).unwrap_err());
        }
        if (! *exists) continue;
        auto canonical = rstd::fs::canonicalize(candidate.as_path());
        if (canonical.is_err()) {
            return module_io_failure<PathBuf>(library,
                                              "resolve standard library module manifest"_str,
                                              candidate.as_path(),
                                              rstd::move(canonical).unwrap_err());
        }
        if (selected.is_some() && selected->as_path() != canonical->as_path()) {
            return module_failure<PathBuf>(StandardLibraryModuleError::Ambiguous(
                module_error_context(library), selected->clone(), canonical->clone()));
        }
        selected = Some(rstd::move(canonical).unwrap());
    }
    if (selected.is_none()) {
        return module_failure<PathBuf>(StandardLibraryModuleError::Missing(
            module_error_context(library), library.module_manifest.paths.clone()));
    }
    return Ok(rstd::move(selected).unwrap());
}

} // namespace lito

export namespace lito
{

auto read_standard_library_module_catalog(const cpp::ResolvedStandardLibrary& library)
    -> ToolchainResult<cpp::StandardLibraryModuleCatalog> {
    auto manifest = rstd_try(select_manifest(library));
    auto contents = rstd::fs::read_to_string(manifest.as_path());
    if (contents.is_err()) {
        return module_manifest_io_failure<cpp::StandardLibraryModuleCatalog>(
            library,
            "read standard library module manifest"_str,
            manifest.as_path(),
            manifest.as_path(),
            None(),
            rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return manifest_failure<cpp::StandardLibraryModuleCatalog>(
            library, manifest.as_path(), rstd::format("invalid JSON: {}", parsed.unwrap_err()));
    }
    auto object = parsed->as_object();
    if (object.is_none()) {
        return manifest_failure<cpp::StandardLibraryModuleCatalog>(
            library, manifest.as_path(), "root must be an object"_str);
    }
    auto version_value = rstd_try(
        required_member(*parsed, "version"_str, "manifest"_str, library, manifest.as_path()));
    auto revision_value = rstd_try(
        required_member(*parsed, "revision"_str, "manifest"_str, library, manifest.as_path()));
    auto modules_value = rstd_try(
        required_member(*parsed, "modules"_str, "manifest"_str, library, manifest.as_path()));
    auto version  = version_value->as_u64();
    auto revision = revision_value->as_u64();
    auto modules  = modules_value->as_array();
    if (version != Some(u64(1)) || revision != Some(u64(1))) {
        return manifest_failure<cpp::StandardLibraryModuleCatalog>(
            library,
            manifest.as_path(),
            rstd::format("unsupported version/revision {}/{}; expected 1/1",
                         version.is_some() ? *version : u64 {},
                         revision.is_some() ? *revision : u64 {}));
    }
    if (modules.is_none()) {
        return manifest_failure<cpp::StandardLibraryModuleCatalog>(
            library, manifest.as_path(), "manifest.modules must be an array"_str);
    }

    auto result = cpp::StandardLibraryModuleCatalog {
        .family            = library.family,
        .manifest          = manifest.clone(),
        .manifest_identity = lito::crypto::sha256_hex(contents->as_str()),
        .version           = *version,
        .revision          = *revision,
    };
    for (auto index = usize {}; index < (**modules).len(); ++index) {
        const auto& value   = (**modules)[index];
        auto        context = rstd::format("manifest.modules[{}]", index);
        if (! value.is_object()) {
            return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                library, manifest.as_path(), "must be an object"_str, Some(context.clone()));
        }
        auto is_stdlib_member = rstd_try(required_member(
            value, "is-std-library"_str, context.as_str(), library, manifest.as_path()));
        auto is_stdlib        = is_stdlib_member->as_bool();
        if (is_stdlib.is_none()) {
            return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                library,
                manifest.as_path(),
                "'is-std-library' must be a boolean"_str,
                Some(context.clone()));
        }
        if (! *is_stdlib) continue;
        auto logical_name = rstd_try(required_string(
            value, "logical-name"_str, context.as_str(), library, manifest.as_path()));
        auto source_text  = rstd_try(required_string(
            value, "source-path"_str, context.as_str(), library, manifest.as_path()));
        if (logical_name.is_empty()) {
            return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                library,
                manifest.as_path(),
                "'logical-name' must not be empty"_str,
                Some(context.clone()));
        }
        if (result.get(logical_name).is_some()) {
            return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                library,
                manifest.as_path(),
                rstd::format("duplicate logical module '{}'", logical_name),
                Some(context.clone()));
        }
        auto source          = rstd_try(resolve_manifest_path(library,
                                                              manifest.as_path(),
                                                              source_text,
                                                              context.as_str(),
                                                              "module source"_str,
                                                              false));
        auto source_contents = rstd::fs::read_to_string(source.as_path());
        if (source_contents.is_err()) {
            return module_manifest_io_failure<cpp::StandardLibraryModuleCatalog>(
                library,
                "read standard library module source"_str,
                source.as_path(),
                manifest.as_path(),
                Some(context.clone()),
                rstd::move(source_contents).unwrap_err());
        }
        auto entry = cpp::StandardLibraryModuleEntry {
            .logical_name    = String::make(logical_name),
            .source          = rstd::move(source),
            .source_identity = lito::crypto::sha256_hex(source_contents->as_str()),
        };
        auto local = value.get("local-arguments"_str);
        if (local.is_some()) {
            if (! (**local).is_object()) {
                return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                    library,
                    manifest.as_path(),
                    "'local-arguments' must be an object"_str,
                    Some(context.clone()));
            }
            auto includes = (**local).get("system-include-directories"_str);
            if (includes.is_some()) {
                auto array = (**includes).as_array();
                if (array.is_none()) {
                    return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                        library,
                        manifest.as_path(),
                        "'local-arguments.system-include-directories' must be an array"_str,
                        Some(context.clone()));
                }
                for (const auto& include : **array) {
                    auto text = include.as_str();
                    if (text.is_none()) {
                        return manifest_failure<cpp::StandardLibraryModuleCatalog>(
                            library,
                            manifest.as_path(),
                            "'local-arguments.system-include-directories' entries must be strings"_str,
                            Some(context.clone()));
                    }
                    entry.system_include_directories.push(
                        rstd_try(resolve_manifest_path(library,
                                                       manifest.as_path(),
                                                       *text,
                                                       context.as_str(),
                                                       "module system include directory"_str,
                                                       true)));
                }
            }
        }
        result.modules.push(rstd::move(entry));
    }
    return Ok(rstd::move(result));
}

} // namespace lito
