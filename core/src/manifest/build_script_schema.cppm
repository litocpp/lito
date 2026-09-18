module;
#include <rstd/macro.hpp>

module lito.core:manifest.build_script_schema;

import rstd;
import rstd.serde;
import :manifest.build_script;
import :manifest.error;
import :manifest.primitives;
import :manifest.wire;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using namespace lito::manifest;

auto source_tree_file(const lito::source::SourceTree& tree, ref<str> path) -> bool {
    return rstd::iter::from_slice(tree.entries()).any([&](auto entry) {
        return entry->path().as_str() == path &&
               entry->kind() == lito::source::SourceEntryKind::File;
    });
}

auto validate_script_entry(ref<rstd::path::Path>                 root,
                           Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<empty> {
    if (embedded.is_some()) {
        if (! source_tree_file(**embedded, "lib.lua"_str)) {
            return Err(ManifestSchemaError::Domain(
                "script package source must contain the regular file 'lib.lua'"_Str));
        }
        return Ok(empty {});
    }
    auto entry    = PathBuf::from(root).join(PathBuf::from("lib.lua"_str).as_path());
    auto metadata = rstd::fs::symlink_metadata(entry.as_path());
    if (metadata.is_err()) {
        return Err(ManifestSchemaError::Io("manifest.script"_Str,
                                           "inspect script package entry"_Str,
                                           PathBuf::from(entry.as_path()),
                                           rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return Err(ManifestSchemaError::Domain(rstd::format(
            "script package entry '{}' must be a regular non-symlink file", entry.as_path())));
    }
    return Ok(empty {});
}

auto parse_script_package(Option<wire::Script>                  value,
                          ref<rstd::path::Path>                 root,
                          Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<Option<ScriptPackageManifest>> {
    if (value.is_none()) return Ok(Option<ScriptPackageManifest> {});
    auto path = rstd::serde::DataPath().with_field("script"_str);
    auto wire = rstd::move(value).unwrap();
    if (wire.supports.is_empty()) {
        return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
            path.with_field("supports"_str), "must not be empty"_str)));
    }
    auto supports = Vec<ScriptHostKind>::with_capacity(wire.supports.len());
    for (usize index {}; index < wire.supports.len(); ++index) {
        const auto& value = wire.supports[index];
        auto        kind  = ScriptHostKind::Build;
        if (value == "build"_str) {
            kind = ScriptHostKind::Build;
        } else if (value == "install"_str) {
            kind = ScriptHostKind::Install;
        } else {
            return Err(ManifestSchemaError::Data(
                rstd::serde::Error::invalid_value(path.with_field("supports"_str).with_index(index),
                                                  "unknown script host kind"_str)));
        }
        for (auto existing : supports) {
            if (existing == kind) {
                return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                    path.with_field("supports"_str).with_index(index),
                    "script host kind is repeated"_str)));
            }
        }
        supports.push(rstd::move(kind));
    }
    rstd_try(validate_script_entry(root, embedded));
    return Ok(Some(ScriptPackageManifest { .supports = rstd::move(supports) }));
}
