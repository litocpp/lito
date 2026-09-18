module;
#include <rstd/macro.hpp>

module lito.core:manifest.target_schema;

import rstd;
import rstd.serde;
import :manifest.target;
import :manifest.conditional;
import :condition;
import :manifest.error;
import :package.identity;
import :dependency.usage;
import lito.system;
import :manifest.profile;
import :manifest.language;
import :manifest.primitives;
import :manifest.convention;
import :manifest.wire;
import :manifest.wire.target;
import :manifest.wire.common;
import :source.tree;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito::manifest;
using DataPath = rstd::serde::DataPath;

auto valid_artifact_name(ref<str> value) -> bool {
    if (value.size() == usize {} || value == "."_str || value == ".."_str) return false;
    for (usize index {}; index < value.size(); ++index) {
        const auto byte     = value[index];
        const bool accepted = (byte >= u8('a') && byte <= u8('z')) ||
                              (byte >= u8('A') && byte <= u8('Z')) ||
                              (byte >= u8('0') && byte <= u8('9')) || byte == u8('_') ||
                              byte == u8('-') || byte == u8('.');
        if (! accepted) return false;
    }
    return true;
}

auto relative_path(String text, ref<str> context) -> ManifestSchemaResult<PathBuf> {
    if (text.is_empty())
        return Err(ManifestSchemaError::Domain(rstd::format("{} must not be empty", context)));
    auto path = PathBuf::from(rstd::move(text));
    if (! path.as_path().is_relative()) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{} must be a relative path", context)));
    }
    return Ok(rstd::move(path));
}

auto resolve_package_source_root(Option<String> declared, ref<rstd::path::Path> root)
    -> ManifestSchemaResult<PathBuf> {
    if (declared.is_none()) return Ok(PathBuf::from(root));

    auto relative = relative_path(rstd::move(declared).unwrap(), "package.source-root"_str);
    if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
    auto requested = PathBuf::from(root).join(relative->as_path());
    auto source_root =
        canonical_existing(requested.as_path(), "cannot resolve package.source-root"_str);
    if (source_root.is_err()) return Err(rstd::move(source_root).unwrap_err());
    auto metadata = rstd::fs::metadata(source_root->as_path());
    if (metadata.is_err()) {
        return Err(ManifestSchemaError::Io("package.source-root"_Str,
                                           "inspect"_Str,
                                           PathBuf::from(source_root->as_path()),
                                           rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("package.source-root '{}' is not a directory", source_root->as_path())));
    }
    if (root.strip_prefix(source_root->as_path()).is_none()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("package.source-root '{}' must contain package directory '{}'",
                         source_root->as_path(),
                         root)));
    }
    return source_root;
}

auto install_relative_path(String text, ref<str> context) -> ManifestSchemaResult<PathBuf> {
    auto path = relative_path(rstd::move(text), context);
    if (path.is_err()) return path;
    auto components = path->as_path().components();
    auto found      = false;
    for (auto component : components) {
        found = true;
        if (! component.is_normal()) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{} must stay within the CMake install prefix", context)));
        }
    }
    if (! found) {
        return Err(ManifestSchemaError::Domain(rstd::format("{} must not be empty", context)));
    }
    return path;
}

auto declared_paths(Option<Vec<String>> value, ref<str> context, bool required)
    -> ManifestSchemaResult<Vec<PathBuf>> {
    if (required && value.is_none()) {
        return Err(ManifestSchemaError::Domain(rstd::format("{} is required", context)));
    }

    auto items  = rstd::move(value).unwrap_or(Vec<String>::make());
    auto result = rstd_try(rstd::move(items)
                               .into_iter()
                               .map([context](String item) {
                                   return relative_path(rstd::move(item), context);
                               })
                               .collect<ManifestSchemaResult<Vec<PathBuf>>>());
    if (required && result.is_empty()) {
        return Err(ManifestSchemaError::Domain(rstd::format("{} must not be empty", context)));
    }
    return Ok(rstd::move(result));
}

auto predicate_values(Option<wire::TextList> value, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    if (value.is_none()) return Ok(Vec<String>::make());
    auto result = rstd::move(value->values);
    if (result.is_empty()) {
        return Err(ManifestSchemaError::Domain(rstd::format("{} must not be empty", context)));
    }
    if (result.iter().any([](auto item) {
            return item->is_empty();
        })) {
        return Err(ManifestSchemaError::Domain(rstd::format("{} item must not be empty", context)));
    }
    return Ok(rstd::move(result));
}

auto parse_target_predicate(Option<wire::Predicate> value, ref<str> context)
    -> ManifestSchemaResult<TargetPredicate> {
    if (value.is_none()) return Ok(TargetPredicate {});
    auto decoded = rstd::move(value).unwrap();
    auto families =
        predicate_values(rstd::move(decoded.family), rstd::format("{}.family", context).as_str());
    auto operating_systems =
        predicate_values(rstd::move(decoded.os), rstd::format("{}.os", context).as_str());
    auto excluded_families = predicate_values(rstd::move(decoded.not_family),
                                              rstd::format("{}.not-family", context).as_str());
    auto excluded_operating_systems =
        predicate_values(rstd::move(decoded.not_os), rstd::format("{}.not-os", context).as_str());
    if (families.is_err()) return Err(rstd::move(families).unwrap_err());
    if (operating_systems.is_err()) {
        return Err(rstd::move(operating_systems).unwrap_err());
    }
    if (excluded_families.is_err()) {
        return Err(rstd::move(excluded_families).unwrap_err());
    }
    if (excluded_operating_systems.is_err()) {
        return Err(rstd::move(excluded_operating_systems).unwrap_err());
    }
    const auto valid_family = [](ref<str> item) {
        return item == "unix"_str || item == "windows"_str || item == "unknown"_str;
    };
    const auto valid_os = [](ref<str> item) {
        return item == "linux"_str || item == "windows"_str || item == "macos"_str ||
               item == "android"_str || item == "freebsd"_str || item == "netbsd"_str ||
               item == "openbsd"_str || item == "unknown"_str;
    };
    for (const auto& item : *families) {
        if (! valid_family(item.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.family contains unsupported value '{}'", context, item.as_str())));
        }
    }
    for (const auto& item : *excluded_families) {
        if (! valid_family(item.as_str())) {
            return Err(ManifestSchemaError::Domain(rstd::format(
                "{}.not-family contains unsupported value '{}'", context, item.as_str())));
        }
    }
    for (const auto& item : *operating_systems) {
        if (! valid_os(item.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.os contains unsupported value '{}'", context, item.as_str())));
        }
    }
    for (const auto& item : *excluded_operating_systems) {
        if (! valid_os(item.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.not-os contains unsupported value '{}'", context, item.as_str())));
        }
    }
    return Ok(TargetPredicate {
        .families                   = rstd::move(families).unwrap(),
        .operating_systems          = rstd::move(operating_systems).unwrap(),
        .excluded_families          = rstd::move(excluded_families).unwrap(),
        .excluded_operating_systems = rstd::move(excluded_operating_systems).unwrap(),
    });
}

auto path_repeated(const Vec<PathBuf>& paths, ref<rstd::path::Path> candidate) -> bool {
    return paths.iter().any([candidate](auto path) {
        return path->as_path() == candidate;
    });
}

auto append_attachment_source(TestAttachmentManifest& attachment, PathBuf source, DataPath path)
    -> ManifestSchemaResult<empty> {
    if (path_repeated(attachment.sources, source.as_path())) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "source is repeated"_str)));
    }
    attachment.sources.push(rstd::move(source));
    return Ok(empty {});
}

auto validate_source_group_names(Vec<String> names, const DataPath& path, bool required)
    -> ManifestSchemaResult<Vec<String>> {
    if (required && names.is_empty()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(path.clone(), "must not be empty"_str)));
    }
    auto seen = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < names.len(); ++index) {
        const auto& name = names[index];
        auto        item = path.with_index(index);
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                rstd::move(item), "invalid source group name"_str)));
        }
        if (seen.contains_key(name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                rstd::move(item), "source group is repeated"_str)));
        }
        seen.insert(name.clone(), empty {});
    }
    return Ok(rstd::move(names));
}

auto parse_target_source_conditions(Vec<lito::manifest::wire::TargetSourceCondition> entries,
                                    const DataPath&                                  owner_path)
    -> ManifestSchemaResult<Vec<ConditionalTargetSources>> {
    auto path = owner_path.with_field("when"_str);
    return rstd::move(entries)
        .into_iter()
        .enumerate()
        .map([&path](auto indexed) -> ManifestSchemaResult<ConditionalTargetSources> {
            auto& entry     = indexed.template get<1>();
            auto  item      = path.with_index(indexed.template get<0>());
            auto  source    = rstd::move(entry.condition);
            auto  condition = lito::condition::parse(source.as_str());
            if (condition.is_err()) {
                auto message = rstd::format("invalid condition: {}", condition.unwrap_err());
                return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                    item.with_field("condition"_str), message.as_str())));
            }
            auto groups = rstd_try(validate_source_group_names(
                rstd::move(entry.source_groups), item.with_field("source-groups"_str), true));
            return Ok(ConditionalTargetSources {
                .source        = rstd::move(source),
                .condition     = rstd::move(condition).unwrap(),
                .source_groups = rstd::move(groups),
            });
        })
        .collect<ManifestSchemaResult<Vec<ConditionalTargetSources>>>();
}

auto parse_test_attachments(Option<Vec<lito::manifest::wire::TestAttachment>> value,
                            const DataPath&                                   owner_path)
    -> ManifestSchemaResult<Vec<TestAttachmentManifest>> {
    auto result = Vec<TestAttachmentManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path    = owner_path.with_field("attach"_str);
    auto entries = rstd::move(value).unwrap();
    for (usize index {}; index < entries.len(); ++index) {
        auto  item  = path.with_index(index);
        auto& entry = entries[index];
        if (! package_name_is_valid(entry.package.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("package"_str), "must name a valid package"_str)));
        }
        if (entry.sources.is_empty()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("sources"_str), "must not be empty"_str)));
        }

        auto position = Option<usize> {};
        for (usize candidate {}; candidate < result.len(); ++candidate) {
            if (result[candidate].package == entry.package.as_str()) {
                position = Some(candidate);
                break;
            }
        }
        if (position.is_none()) {
            result.push(TestAttachmentManifest { .package = rstd::move(entry.package) });
            position = Some(result.len() - usize(1));
        }
        auto& attachment = result[*position];
        for (usize source_index {}; source_index < entry.sources.len(); ++source_index) {
            auto source = PathBuf::from(rstd::move(entry.sources[source_index]));
            if (! source.as_path().is_relative()) {
                return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                    item.with_field("sources"_str).with_index(source_index),
                    "must be a relative path"_str)));
            }
            auto appended =
                append_attachment_source(attachment,
                                         rstd::move(source),
                                         item.with_field("sources"_str).with_index(source_index));
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    if (result.is_empty()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "must not be empty"_str)));
    }
    return Ok(rstd::move(result));
}

auto parse_target_source(lito::manifest::wire::TargetSource value,
                         ref<str>                           context,
                         const DataPath&                    path,
                         bool                               module_required,
                         PackageLanguage language) -> ManifestSchemaResult<TargetSourceManifest> {
    auto module = rstd::move(value.module);
    if (module.is_some() && ! valid_module_name(module->as_str())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.module must be a valid module name", context)));
    }
    const auto has_sources      = value.sources.is_some();
    const auto module_discovery = ! has_sources && value.source_groups.is_none();
    auto       sources          = Vec<PathBuf>::make();
    if (has_sources) {
        for (auto& source : *value.sources) {
            sources.push(rstd_try(
                relative_path(rstd::move(source), rstd::format("{}.sources", context).as_str())));
        }
    }
    auto groups = rstd_try(
        validate_source_group_names(rstd::move(value.source_groups).unwrap_or(Vec<String>::make()),
                                    path.with_field("source-groups"_str),
                                    false));
    auto conditions = rstd_try(parse_target_source_conditions(rstd::move(value.when), path));
    if (language == PackageLanguage::C && module.is_some()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.module is not supported by a C package", context)));
    }
    if (language == PackageLanguage::C && module_discovery) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} must declare sources or source-groups for a C package", context)));
    }
    if (has_sources && sources.is_empty()) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{}.sources must not be empty", context)));
    }
    if (module_required && module.is_none()) {
        return Err(ManifestSchemaError::Domain(rstd::format("{}.module is required", context)));
    }
    return Ok(TargetSourceManifest {
        .module    = rstd::move(module),
        .discovery = module_discovery ? SourceDiscoveryMode::Module : SourceDiscoveryMode::Explicit,
        .declared_sources = rstd::move(sources),
        .source_groups    = rstd::move(groups),
        .conditions       = rstd::move(conditions),
    });
}

auto parse_source_groups(Option<wire::SourceGroups> value)
    -> ManifestSchemaResult<Vec<SourceGroupManifest>> {
    auto result = Vec<SourceGroupManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path   = DataPath().with_field("source-groups"_str);
    auto groups = rstd::move(value).unwrap();
    for (auto name_ref : groups.keys()) {
        const auto& name = *name_ref;
        auto        item = path.with_map_key(name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                rstd::move(item), "invalid source group name"_str)));
        }
        auto specification = groups.get_mut(name.as_str()).unwrap_unchecked();
        auto external      = rstd::move(specification->external_source);
        if (external.is_some() && ! package_name_is_valid(external->as_str())) {
            return Err(ManifestSchemaError::Data(
                rstd::serde::Error::invalid_value(item.with_field("external-source"_str),
                                                  "must name a package external source"_str)));
        }
        auto root       = SourceGroupRoot::Package;
        auto root_value = rstd::move(specification->root);
        if (root_value.is_some()) {
            if (root_value->as_str() == "generated"_str) {
                root = SourceGroupRoot::Generated;
            } else if (root_value->as_str() != "package"_str) {
                return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                    item.with_field("root"_str), "must be 'package' or 'generated'"_str)));
            }
        }
        if (root == SourceGroupRoot::Generated && external.is_some()) {
            return Err(ManifestSchemaError::Data(
                rstd::serde::Error::invalid_value(item.with_field("external-source"_str),
                                                  "cannot be combined with root 'generated'"_str)));
        }
        if (specification->sources.is_empty()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("sources"_str), "must not be empty"_str)));
        }
        auto sources = Vec<PathBuf>::with_capacity(specification->sources.len());
        for (usize index {}; index < specification->sources.len(); ++index) {
            auto source = PathBuf::from(rstd::move(specification->sources[index]));
            if (! source.as_path().is_safe_relative()) {
                return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                    item.with_field("sources"_str).with_index(index),
                    "must be a safe relative path"_str)));
            }
            sources.push(rstd::move(source));
        }
        result.push(SourceGroupManifest {
            .name            = name.clone(),
            .root            = root,
            .external_source = rstd::move(external),
            .sources         = rstd::move(sources),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_library_target(Option<wire::LibraryTarget> value, PackageLanguage language)
    -> ManifestSchemaResult<Option<PackageTargetManifest>> {
    if (value.is_none()) return Ok(Option<PackageTargetManifest> {});
    auto wire = rstd::move(value).unwrap();
    auto name = rstd::move(wire.name);
    if (! package_name_is_valid(name.as_str())) {
        return Err(
            ManifestSchemaError::Domain("manifest.lib.name must be a valid target name"_Str));
    }
    auto kind           = rstd::move(wire.kind);
    auto archive        = rstd::move(wire.archive);
    auto artifact       = rstd::move(wire.artifact);
    auto linker_options = rstd::move(wire.linker_options);
    if (archive.is_some() == artifact.is_some()) {
        return Err(ManifestSchemaError::Domain(
            "manifest.lib must contain exactly one of 'archive' or 'artifact'"_Str));
    }
    auto output = LibraryOutput::Static(String::make());
    if (kind.is_none() || kind->as_str() == "static"_str) {
        if (archive.is_none()) {
            return Err(
                ManifestSchemaError::Domain("manifest.lib static output requires 'archive'"_Str));
        }
        output = LibraryOutput::Static(rstd::move(archive).unwrap());
    } else if (kind->as_str() == "shared"_str) {
        if (artifact.is_none()) {
            return Err(
                ManifestSchemaError::Domain("manifest.lib shared output requires 'artifact'"_Str));
        }
        output = LibraryOutput::Shared(rstd::move(artifact).unwrap());
    } else {
        return Err(
            ManifestSchemaError::Domain("manifest.lib.kind must be 'static' or 'shared'"_Str));
    }
    auto artifact_name = output.is_Static() ? output.as_Static().artifact.as_str()
                                            : output.as_Shared().artifact.as_str();
    if (! valid_artifact_name(artifact_name)) {
        return Err(ManifestSchemaError::Domain(
            "manifest.lib output must be a safe artifact basename"_Str));
    }
    if (output.is_Static() && ! linker_options.is_empty()) {
        return Err(
            ManifestSchemaError::Domain("manifest.lib.linker-options requires kind 'shared'"_Str));
    }
    auto source = rstd_try(parse_target_source(rstd::move(wire),
                                               "manifest.lib"_str,
                                               DataPath().with_field("lib"_str),
                                               language == PackageLanguage::Cpp,
                                               language));
    return Ok(Some(PackageTargetManifest::Library(
        rstd::move(name), rstd::move(output), rstd::move(source), rstd::move(linker_options))));
}

auto parse_plugin_target(Option<wire::ModuleTarget> value,
                         ref<str>                   package_name,
                         PackageLanguage            language)
    -> ManifestSchemaResult<Option<PackageTargetManifest>> {
    if (value.is_none()) return Ok(Option<PackageTargetManifest> {});
    if (language != PackageLanguage::Cpp) {
        return Err(ManifestSchemaError::Domain("manifest.plugin requires a C++ package"_Str));
    }
    auto wire   = rstd::move(value).unwrap();
    auto source = rstd_try(parse_target_source(rstd::move(wire),
                                               "manifest.plugin"_str,
                                               DataPath().with_field("plugin"_str),
                                               true,
                                               language));
    return Ok(Some(PackageTargetManifest::Plugin(String::make(package_name), rstd::move(source))));
}

auto parse_pmacro_target(Option<wire::ModuleTarget> value,
                         ref<str>                   package_name,
                         PackageLanguage            language)
    -> ManifestSchemaResult<Option<PackageTargetManifest>> {
    if (value.is_none()) return Ok(Option<PackageTargetManifest> {});
    if (language != PackageLanguage::Cpp) {
        return Err(ManifestSchemaError::Domain("manifest.pmacro requires a C++ package"_Str));
    }
    auto wire   = rstd::move(value).unwrap();
    auto source = rstd_try(parse_target_source(rstd::move(wire),
                                               "manifest.pmacro"_str,
                                               DataPath().with_field("pmacro"_str),
                                               true,
                                               language));
    return Ok(
        Some(PackageTargetManifest::ProcMacro(String::make(package_name), rstd::move(source))));
}

auto parse_runtime_resources(Option<Vec<lito::manifest::wire::RuntimeResource>> value,
                             const DataPath&                                    owner_path)
    -> ManifestSchemaResult<Vec<RuntimeResourceManifest>> {
    auto result = Vec<RuntimeResourceManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path    = owner_path.with_field("resources"_str);
    auto entries = rstd::move(value).unwrap();
    if (entries.is_empty()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "must not be empty"_str)));
    }
    for (usize index {}; index < entries.len(); ++index) {
        auto  item_path = path.with_index(index);
        auto& item      = entries[index];
        auto& name      = item.name;
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item_path.with_field("name"_str), "must be a valid resource name"_str)));
        }
        for (const auto& existing : result) {
            if (existing.name == name.as_str()) {
                return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                    item_path.with_field("name"_str), "resource name is repeated"_str)));
            }
        }
        if (item.root.as_str() != "generated"_str) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item_path.with_field("root"_str), "must be 'generated'"_str)));
        }
        auto relative = PathBuf::from(rstd::move(item.path));
        if (relative.is_empty() || ! relative.as_path().is_safe_relative()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item_path.with_field("path"_str), "must be a safe non-empty relative path"_str)));
        }
        result.push(RuntimeResourceManifest {
            .name = rstd::move(name),
            .path = rstd::move(relative),
        });
    }
    return Ok(rstd::move(result));
}

template<typename T>
auto parse_runnable_targets(Option<Vec<T>>                   value,
                            lito::package::PackageTargetKind kind,
                            ref<str>                         key,
                            PackageLanguage                  language)
    -> ManifestSchemaResult<Vec<PackageTargetManifest>> {
    auto result = Vec<PackageTargetManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = rstd::move(value).unwrap();
    if (entries.is_empty()) {
        return Err(ManifestSchemaError::Domain(rstd::format("manifest.{} must not be empty", key)));
    }
    for (usize index {}; index < entries.len(); ++index) {
        const auto context = rstd::format("manifest.{}[{}]", key, index);
        auto       path    = DataPath().with_field(key).with_index(index);
        auto&      wire    = entries[index];
        auto       name    = rstd::move(wire.name);
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.name must be a valid target name", context.as_str())));
        }
        for (const auto& existing : result) {
            if (package_target_name(existing) == name.as_str()) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("manifest.{} repeats target name '{}'", key, name.as_str())));
            }
        }
        auto source = rstd_try(
            parse_target_source(rstd::move(static_cast<lito::manifest::wire::TargetSource&>(wire)),
                                context.as_str(),
                                path,
                                false,
                                language));
        auto link_stdlib = wire.link_stdlib;
        if (kind == lito::package::PackageTargetKind::Binary) {
            auto host_tool = wire.host_tool;
            auto resources = rstd_try(parse_runtime_resources(rstd::move(wire.resources), path));
            if (host_tool && ! resources.is_empty()) {
                return Err(ManifestSchemaError::Domain(rstd::format(
                    "{}.resources are not allowed for a host-tool binary", context.as_str())));
            }
            result.push(PackageTargetManifest::Binary(rstd::move(name),
                                                      rstd::move(source),
                                                      link_stdlib,
                                                      host_tool,
                                                      rstd::move(resources)));
        } else if (kind == lito::package::PackageTargetKind::Benchmark) {
            result.push(PackageTargetManifest::Benchmark(
                rstd::move(name), rstd::move(source), link_stdlib));
        } else {
            auto attachments = rstd_try(parse_test_attachments(rstd::move(wire.attach), path));
            result.push(PackageTargetManifest::Test(
                rstd::move(name), rstd::move(source), link_stdlib, rstd::move(attachments)));
        }
    }
    return Ok(rstd::move(result));
}

struct ResolvedIncludeDirectories {
    Vec<PathBuf>                                       physical;
    Vec<lito::dependency::IncludeDirectoryRequirement> deferred;
};

auto source_tree_directory(const lito::source::SourceTree& tree, ref<rstd::path::Path> path)
    -> bool {
    auto text = path.to_str();
    if (text.is_none()) return false;
    for (const auto& entry : tree.entries()) {
        if (entry.path().as_str() == *text &&
            entry.kind() == lito::source::SourceEntryKind::Directory) {
            return true;
        }
    }
    return false;
}

auto resolve_package_include_directory(PathBuf                               path,
                                       ref<rstd::path::Path>                 root,
                                       ref<str>                              context,
                                       Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<PathBuf> {
    if (embedded.is_some()) {
        if (! source_tree_directory(**embedded, path.as_path())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{} entry '{}' is not a directory", context, path.as_path())));
        }
        return Ok(PathBuf::from(root).join(path.as_path()));
    }
    auto requested = PathBuf::from(root).join(path.as_path());
    auto canonical =
        canonical_existing(requested.as_path(), "cannot resolve include directory"_str);
    if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
    auto resolved = rstd::move(canonical).unwrap();
    if (resolved.as_path().strip_prefix(root).is_none()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} entry '{}' is outside package root", context, path.as_path())));
    }
    auto metadata = rstd::fs::metadata(resolved.as_path());
    if (metadata.is_err()) {
        return Err(ManifestSchemaError::Io((context).into(),
                                           "inspect include directory"_Str,
                                           PathBuf::from(resolved.as_path()),
                                           rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} entry '{}' is not a directory", context, path.as_path())));
    }
    return Ok(rstd::move(resolved));
}

auto resolve_include_directories(Option<Vec<wire::IncludeDirectory>>   value,
                                 ref<rstd::path::Path>                 root,
                                 ref<str>                              context,
                                 bool                                  allow_generated,
                                 Option<ref<lito::source::SourceTree>> embedded)
    -> ManifestSchemaResult<ResolvedIncludeDirectories> {
    auto result = ResolvedIncludeDirectories {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = rstd::move(value).unwrap();
    for (usize index {}; index < entries.len(); ++index) {
        const auto item_context = rstd::format("{}[{}]", context, index);
        auto&      item         = entries[index];
        auto relative = relative_path(rstd::move(item.path),
                                      rstd::format("{}.path", item_context.as_str()).as_str());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto root_value     = rstd::move(item.root);
        auto external_value = rstd::move(item.external_source);
        if (external_value.is_some()) {
            if (root_value.is_some()) {
                return Err(ManifestSchemaError::Domain(rstd::format(
                    "{} cannot combine root and external-source", item_context.as_str())));
            }
            if (! package_name_is_valid(external_value->as_str())) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("{}.external-source must name a package external source",
                                 item_context.as_str())));
            }
            result.deferred.push(lito::dependency::IncludeDirectoryRequirement {
                .root            = lito::dependency::IncludeDirectoryRoot::ExternalSource,
                .path            = rstd::move(relative).unwrap(),
                .external_source = rstd::move(external_value),
            });
            continue;
        }
        auto root_kind = root_value.is_some() ? root_value->as_str() : "package"_str;
        if (root_kind == "package"_str) {
            auto resolved = resolve_package_include_directory(
                rstd::move(relative).unwrap(), root, item_context.as_str(), embedded);
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            result.physical.push(rstd::move(resolved).unwrap());
            continue;
        }
        if (root_kind != "generated"_str) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.root must be package or generated", item_context.as_str())));
        }
        if (! allow_generated) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{} does not support generated public include directories",
                             item_context.as_str())));
        }
        result.deferred.push(lito::dependency::IncludeDirectoryRequirement {
            .root = lito::dependency::IncludeDirectoryRoot::Generated,
            .path = rstd::move(relative).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_compile_tests(Vec<wire::CompileTestCase> cases)
    -> ManifestSchemaResult<Vec<CompileTestCase>> {
    auto result  = Vec<CompileTestCase>::make();
    auto path    = DataPath().with_field("compile-test"_str).with_field("cases"_str);
    auto names   = rstd::collections::BTreeMap<String, empty>::make();
    auto sources = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < cases.len(); ++index) {
        auto  item = path.with_index(index);
        auto& wire = cases[index];
        if (wire.name.is_empty()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("name"_str), "must not be empty"_str)));
        }
        if (names.contains_key(wire.name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("name"_str), "case name is repeated"_str)));
        }
        auto relative = PathBuf::from(rstd::move(wire.source));
        if (! relative.as_path().is_relative()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("source"_str), "must be a relative path"_str)));
        }
        auto source_text = relative.as_path().to_str();
        if (source_text.is_none()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("source"_str), "must be valid UTF-8"_str)));
        }
        if (sources.contains_key(*source_text)) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("source"_str), "source is used by more than one case"_str)));
        }
        auto expected = CompileTestOutcome::Failure;
        if (wire.outcome.as_str() == "success"_str) {
            expected = CompileTestOutcome::Success;
        } else if (wire.outcome.as_str() != "failure"_str) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("outcome"_str), "must be 'success' or 'failure'"_str)));
        }
        if (expected == CompileTestOutcome::Success &&
            (! wire.diagnostic_contains.is_empty() || ! wire.diagnostic_contains_any.is_empty())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                rstd::move(item), "successful outcome cannot require diagnostics"_str)));
        }
        names.insert(wire.name.clone(), empty {});
        sources.insert(String::make(*source_text), empty {});
        result.push(CompileTestCase {
            .name                    = rstd::move(wire.name),
            .source                  = rstd::move(relative),
            .outcome                 = expected,
            .options                 = rstd::move(wire.options),
            .diagnostic_contains     = rstd::move(wire.diagnostic_contains),
            .diagnostic_contains_any = rstd::move(wire.diagnostic_contains_any),
        });
    }
    if (result.is_empty()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "must not be empty"_str)));
    }
    return Ok(rstd::move(result));
}

auto parse_usage(wire::Usage                           value,
                 ref<rstd::path::Path>                 root,
                 ref<str>                              context  = "manifest.usage"_str,
                 Option<ref<lito::source::SourceTree>> embedded = None())
    -> ManifestSchemaResult<lito::dependency::DeclaredUsageRequirements> {
    auto public_include_values = rstd_try(
        resolve_include_directories(rstd::move(value.public_includes),
                                    root,
                                    rstd::format("{}.public-include-directories", context).as_str(),
                                    false,
                                    embedded));
    auto       private_include_values = rstd_try(resolve_include_directories(
        rstd::move(value.private_includes),
        root,
        rstd::format("{}.private-include-directories", context).as_str(),
        true,
        embedded));
    const auto frameworks_present     = value.frameworks.is_some();
    auto       raw_framework_values   = rstd::move(value.frameworks).unwrap_or(Vec<String>::make());
    if (frameworks_present && raw_framework_values.is_empty()) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{}.frameworks must not be empty", context)));
    }
    auto framework_values = Vec<lito::dependency::DeclaredFrameworkRequirement>::with_capacity(
        raw_framework_values.len());
    for (auto& framework : raw_framework_values) {
        framework_values.push(lito::dependency::DeclaredFrameworkRequirement {
            .name   = rstd::move(framework),
            .source = rstd::format("{}.frameworks", context),
        });
    }
    return Ok(lito::dependency::DeclaredUsageRequirements {
        .public_include_directories  = rstd::move(public_include_values.physical),
        .private_include_directories = rstd::move(private_include_values.physical),
        .public_definitions  = rstd::move(value.public_definitions).unwrap_or(Vec<String>::make()),
        .private_definitions = rstd::move(value.private_definitions).unwrap_or(Vec<String>::make()),
        .options             = rstd::move(value.options).unwrap_or(Vec<String>::make()),
        .linker_options      = rstd::move(value.linker_options).unwrap_or(Vec<String>::make()),
        .threads             = value.threads.unwrap_or(false),
        .system_libraries    = rstd::move(value.system_libraries).unwrap_or(Vec<String>::make()),
        .frameworks          = rstd::move(framework_values),
        .private_include_directory_requirements = rstd::move(private_include_values.deferred),
        .public_include_directory_requirements  = rstd::move(public_include_values.deferred),
    });
}

auto parse_usage(Option<wire::Usage>                   value,
                 ref<rstd::path::Path>                 root,
                 ref<str>                              context  = "manifest.usage"_str,
                 Option<ref<lito::source::SourceTree>> embedded = None())
    -> ManifestSchemaResult<lito::dependency::DeclaredUsageRequirements> {
    if (value.is_none()) return Ok(lito::dependency::DeclaredUsageRequirements {});
    auto decoded = rstd::move(value).unwrap();
    return parse_usage(rstd::move(decoded), root, context, embedded);
}

auto parse_conditional_configurations(Option<Vec<wire::Condition>> value,
                                      ref<rstd::path::Path>        root)
    -> ManifestSchemaResult<Vec<ConditionalConfiguration>> {
    auto result = Vec<ConditionalConfiguration>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = rstd::move(value).unwrap();
    for (usize index {}; index < entries.len(); ++index) {
        const auto context   = rstd::format("manifest.when[{}]", index);
        auto&      entry     = entries[index];
        auto       condition = lito::condition::parse(entry.condition.as_str());
        if (condition.is_err()) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}", rstd::move(condition).unwrap_err())));
        }
        const auto declares_threads = entry.usage.threads.is_some();
        auto       usage            = rstd_try(parse_usage(
            rstd::move(entry.usage), root, rstd::format("{}.usage", context.as_str()).as_str()));
        result.push(ConditionalConfiguration {
            .source    = rstd::move(entry.condition),
            .condition = rstd::move(condition).unwrap(),
            .usage =
                ConditionalUsage {
                    .values           = rstd::move(usage),
                    .declares_threads = declares_threads,
                },
        });
    }
    return Ok(rstd::move(result));
}

auto feature_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    const auto bytes = value.as_bytes();
    const auto first = bytes[usize {}];
    if (! ((first >= u8('a') && first <= u8('z')) || (first >= u8('A') && first <= u8('Z')) ||
           first == u8('_'))) {
        return false;
    }
    for (usize index { 1 }; index < bytes.len(); ++index) {
        const auto byte = bytes[index];
        if ((byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
            (byte >= u8('0') && byte <= u8('9')) || byte == u8('-') || byte == u8('_')) {
            continue;
        }
        return false;
    }
    return true;
}

auto macro_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    const auto bytes = value.as_bytes();
    const auto first = bytes[usize {}];
    if (! ((first >= u8('a') && first <= u8('z')) || (first >= u8('A') && first <= u8('Z')) ||
           first == u8('_'))) {
        return false;
    }
    for (usize index { 1 }; index < bytes.len(); ++index) {
        const auto byte = bytes[index];
        if ((byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
            (byte >= u8('0') && byte <= u8('9')) || byte == u8('_')) {
            continue;
        }
        return false;
    }
    return true;
}

auto parse_features(Option<wire::Features> value) -> ManifestSchemaResult<Vec<FeatureDeclaration>> {
    auto result = Vec<FeatureDeclaration>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path     = DataPath().with_field("features"_str);
    auto features = rstd::move(value).unwrap();
    auto macros   = rstd::collections::BTreeMap<String, String>::make();
    for (auto name_ref : features.keys()) {
        const auto& name = *name_ref;
        auto        item = path.with_map_key(name.as_str());
        if (! feature_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Data(
                rstd::serde::Error::invalid_value(rstd::move(item), "invalid feature name"_str)));
        }
        auto specification = features.get(name.as_str()).unwrap_unchecked();
        auto macro         = normalized_feature_macro(name.as_str());
        if (! macro_name_is_valid(macro.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                rstd::move(item), "macro is not a C/C++ identifier"_str)));
        }
        auto existing = macros.get(macro.as_str());
        if (existing.is_some()) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                rstd::move(item), "normalized macro is repeated"_str)));
        }
        macros.insert(macro.clone(), name.clone());
        result.push(FeatureDeclaration {
            .name            = name.clone(),
            .macro_name      = rstd::move(macro),
            .default_enabled = specification->default_enabled,
        });
    }
    return Ok(rstd::move(result));
}
