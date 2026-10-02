module;
#include <rstd/macro.hpp>

module lito.core:manifest.examples;

import rstd;
import :manifest.target;
import :manifest.error;
import :manifest.locator;
import :manifest.source_convention;
import :manifest.convention;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::manifest;

struct ExampleEntry {
    String  name;
    PathBuf path;
};

auto example_extension(ref<rstd::path::Path> path) -> bool {
    auto extension = path.extension();
    return runnable_manifest_source(path) ||
           (extension.is_some() && extension->to_str() == Some("cppm"_str));
}

auto example_manifest(ref<rstd::path::Path> path) -> ManifestSchemaResult<bool> {
    auto located = try_locate_manifest(path);
    if (located.is_err())
        return Err(rstd::into<ManifestSchemaError>(rstd::move(located).unwrap_err()));
    return Ok(located->is_some());
}

auto example_entries(ref<rstd::path::Path>                 root,
                     ref<rstd::path::Path>                 source_root,
                     Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<Vec<ExampleEntry>> {
    auto entries = Vec<ExampleEntry>::make();
    auto add     = [&](ref<rstd::path::Path> relative) -> ManifestSchemaResult<empty> {
        if (! example_extension(relative)) return Ok(empty {});
        auto stem   = relative.file_stem();
        auto parent = relative.parent();
        if (stem.is_none() || parent.is_none()) return Ok(empty {});
        auto parent_text = parent->to_str();
        auto name        = stem->to_str();
        if (parent_text != Some("examples"_str)) {
            auto grandparent = parent->parent();
            if (grandparent.is_none() || grandparent->to_str() != Some("examples"_str) ||
                name != Some("main"_str))
                return Ok(empty {});
            name = parent->file_name().unwrap().to_str();
        }
        if (name.is_none())
            return Err(ManifestSchemaError::Domain("example name must be UTF-8"_Str));
        entries.push(ExampleEntry { .name = String::make(*name), .path = PathBuf::from(relative) });
        return Ok(empty {});
    };
    if (embedded.is_some()) {
        for (const auto& entry : (**embedded).entries()) {
            if (entry.kind() != lito::source::SourceEntryKind::File) continue;
            if (! entry.path().as_str().starts_with("examples/"_str)) continue;
            auto path = PathBuf::from(entry.path().as_str());
            if (! example_extension(path.as_path())) continue;
            auto parent = path.as_path().parent();
            auto nested = false;
            for (const auto& other : (**embedded).entries()) {
                if (other.kind() != lito::source::SourceEntryKind::File) continue;
                auto manifest = PathBuf::from(other.path().as_str());
                auto filename = manifest.as_path().file_name()->to_str();
                if (filename.is_none() || ! is_manifest_filename(*filename)) continue;
                auto owner = manifest.as_path().parent();
                if (owner.is_some() && ! owner->is_empty() && parent.is_some() &&
                    parent->starts_with(*owner))
                    nested = true;
            }
            if (! nested) rstd_try(add(path.as_path()));
        }
    } else {
        auto directory = PathBuf::from(root).join(PathBuf::from("examples"_str).as_path());
        auto exists    = rstd::fs::exists(directory.as_path());
        if (exists.is_err())
            return Err(ManifestSchemaError::Io(
                "examples"_Str, "inspect"_Str, directory.clone(), rstd::move(exists).unwrap_err()));
        if (! *exists || rstd_try(example_manifest(directory.as_path())))
            return Ok(rstd::move(entries));
        auto opened = rstd::fs::read_dir(directory.as_path());
        if (opened.is_err())
            return Err(ManifestSchemaError::Io("examples"_Str,
                                               "enumerate"_Str,
                                               directory.clone(),
                                               rstd::move(opened).unwrap_err()));
        for (auto item : rstd::move(opened).unwrap()) {
            if (item.is_err())
                return Err(ManifestSchemaError::Io("examples"_Str,
                                                   "enumerate"_Str,
                                                   directory.clone(),
                                                   rstd::move(item).unwrap_err()));
            auto entry = rstd::move(item).unwrap();
            auto type  = entry.file_type();
            if (type.is_err())
                return Err(ManifestSchemaError::Io(
                    "examples"_Str, "inspect"_Str, entry.path(), rstd::move(type).unwrap_err()));
            auto path = entry.path();
            if (type->is_file()) {
                auto relative = path.as_path().strip_prefix(root);
                if (relative.is_some()) rstd_try(add(*relative));
            } else if (type->is_dir() && ! rstd_try(example_manifest(path.as_path()))) {
                auto children = rstd::fs::read_dir(path.as_path());
                if (children.is_err())
                    return Err(ManifestSchemaError::Io("examples"_Str,
                                                       "enumerate"_Str,
                                                       path.clone(),
                                                       rstd::move(children).unwrap_err()));
                for (auto child : rstd::move(children).unwrap()) {
                    if (child.is_err())
                        return Err(ManifestSchemaError::Io("examples"_Str,
                                                           "enumerate"_Str,
                                                           path.clone(),
                                                           rstd::move(child).unwrap_err()));
                    auto child_path = child->path();
                    auto child_type = child->file_type();
                    if (child_type.is_err())
                        return Err(ManifestSchemaError::Io("examples"_Str,
                                                           "inspect"_Str,
                                                           child_path.clone(),
                                                           rstd::move(child_type).unwrap_err()));
                    auto relative = child_path.as_path().strip_prefix(root);
                    if (child_type->is_file() && relative.is_some()) rstd_try(add(*relative));
                }
            }
        }
        for (auto& entry : entries) {
            auto absolute = PathBuf::from(root).join(entry.path.as_path());
            auto relative = absolute.as_path().strip_prefix(source_root);
            if (relative.is_none())
                return Err(
                    ManifestSchemaError::Domain("example is outside package source root"_Str));
            entry.path = PathBuf::from(*relative);
        }
    }
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const ExampleEntry& a, const ExampleEntry& b) {
                                       return a.name < b.name;
                                   });
    return Ok(rstd::move(entries));
}

auto resolve_examples(ref<rstd::path::Path>                 root,
                      ref<rstd::path::Path>                 source_root,
                      Vec<PackageTargetManifest>&           targets,
                      bool                                  automatic,
                      Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<empty> {
    auto needs_entries = automatic;
    for (const auto& target : targets) {
        if (target.is_Example() &&
            target.as_Example().source.discovery == SourceDiscoveryMode::Module &&
            target.as_Example().source.entry.is_none())
            needs_entries = true;
    }
    auto entries = needs_entries ? rstd_try(example_entries(root, source_root, embedded))
                                 : Vec<ExampleEntry>::make();
    for (usize index {}; index < entries.len();) {
        auto end = index + usize(1);
        while (end < entries.len() && entries[end].name == entries[index].name) ++end;
        PackageTargetManifest* explicit_target = nullptr;
        for (auto& target : targets) {
            if (target.is_Example() && target.as_Example().name == entries[index].name)
                explicit_target = rstd::addressof(target);
        }
        if (explicit_target == nullptr && ! automatic) {
            index = end;
            continue;
        }
        auto overridden =
            explicit_target != nullptr &&
            (explicit_target->as_Example().source.entry.is_some() ||
             explicit_target->as_Example().source.discovery == SourceDiscoveryMode::Explicit);
        if (! overridden) {
            if (end != index + usize(1))
                return Err(ManifestSchemaError::Domain(
                    rstd::format("example '{}' has ambiguous entry paths", entries[index].name)));
            if (explicit_target != nullptr) {
                explicit_target->as_Example().source.entry = Some(entries[index].path.clone());
            } else {
                targets.push(
                    PackageTargetManifest::Example(entries[index].name.clone(),
                                                   TargetSourceManifest {
                                                       .entry = Some(entries[index].path.clone()),
                                                       .discovery = SourceDiscoveryMode::Module,
                                                   },
                                                   true,
                                                   Vec<RuntimeResourceManifest>::make()));
            }
        }
        index = end;
    }
    for (auto& target : targets) {
        if (! target.is_Example()) continue;
        auto& example = target.as_Example();
        if (! package_name_is_valid(example.name.as_str()))
            return Err(ManifestSchemaError::Domain(
                "example name must contain only ASCII letters, digits, '-' or '_'"_Str));
        auto& source = example.source;
        if (source.discovery == SourceDiscoveryMode::Explicit) continue;
        if (source.entry.is_none())
            return Err(ManifestSchemaError::Domain(
                rstd::format("example '{}' has no entry; specify path or sources", example.name)));
        if (runnable_manifest_source(source.entry->as_path())) {
            if (source.module_root.is_some())
                return Err(
                    ManifestSchemaError::Domain("example.source-root requires a .cppm entry"_Str));
            source.discovery = SourceDiscoveryMode::Explicit;
            source.declared_sources.push(source.entry->clone());
        } else if (! example_extension(source.entry->as_path())) {
            return Err(ManifestSchemaError::Domain(
                "example.path must name a .cppm, .cpp, .cc or .cxx entry"_Str));
        } else if (source.module_root.is_none()) {
            auto parent = source.entry->as_path().parent();
            source.module_root =
                Some(parent.is_some() && ! parent->is_empty() ? PathBuf::from(*parent)
                                                              : PathBuf::from("."_str));
        }
    }
    return Ok(empty {});
}
