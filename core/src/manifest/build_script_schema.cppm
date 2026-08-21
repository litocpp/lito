module;
#include <rstd/macro.hpp>

module lito.core:manifest.build_script_schema;

import rstd;
import rstd.toml;
import :manifest.build_script;
import :manifest.error;
import :manifest.primitives;
import :manifest.key_schema;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using Toml    = rstd::toml::Value;
using namespace lito::manifest;

auto source_tree_file(const lito::source::SourceTree& tree, ref<str> path) -> bool {
    for (const auto& entry : tree.entries()) {
        if (entry.path().as_str() == path && entry.kind() == lito::source::SourceEntryKind::File)
            return true;
    }
    return false;
}

auto validate_script_entry(ref<rstd::path::Path>                 root,
                           Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<empty> {
    if (embedded.is_some()) {
        if (! source_tree_file(**embedded, "lib.lua"_str)) {
            return manifest_schema_failure<empty>(
                "script package source must contain the regular file 'lib.lua'"_str);
        }
        return Ok(empty {});
    }
    auto entry    = PathBuf::from(root).join(PathBuf::from("lib.lua"_str).as_path());
    auto metadata = rstd::fs::symlink_metadata(entry.as_path());
    if (metadata.is_err()) {
        return manifest_io_failure<empty>("manifest.script"_str,
                                          "inspect script package entry"_str,
                                          entry.as_path(),
                                          rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return manifest_schema_failure<empty>(rstd::format(
            "script package entry '{}' must be a regular non-symlink file", entry.as_path()));
    }
    return Ok(empty {});
}

auto parse_script_package(Option<ref<Toml>>                     value,
                          ref<rstd::path::Path>                 root,
                          Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<Option<ScriptPackageManifest>> {
    if (value.is_none()) return Ok(Option<ScriptPackageManifest> {});
    auto script = rstd_try(table_value(**value, "manifest.script"_str));
    rstd_try(reject_unknown(*script, "manifest.script"_str, script_key));
    auto raw =
        rstd_try(string_array(member(**value, "supports"_str), "manifest.script.supports"_str));
    if (raw.is_empty()) {
        return manifest_schema_failure<Option<ScriptPackageManifest>>(
            "manifest.script.supports must not be empty"_str);
    }
    auto supports = Vec<ScriptHostKind>::with_capacity(raw.len());
    for (const auto& value : raw) {
        auto kind = ScriptHostKind::Build;
        if (value == "build"_str) {
            kind = ScriptHostKind::Build;
        } else if (value == "install"_str) {
            kind = ScriptHostKind::Install;
        } else {
            return manifest_schema_failure<Option<ScriptPackageManifest>>(rstd::format(
                "manifest.script.supports contains unknown host kind '{}'", value.as_str()));
        }
        for (auto existing : supports) {
            if (existing == kind) {
                return manifest_schema_failure<Option<ScriptPackageManifest>>(rstd::format(
                    "manifest.script.supports repeats host kind '{}'", value.as_str()));
            }
        }
        supports.push(rstd::move(kind));
    }
    rstd_try(validate_script_entry(root, embedded));
    return Ok(Some(ScriptPackageManifest { .supports = rstd::move(supports) }));
}
