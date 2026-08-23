module;
#include <rstd/macro.hpp>

module lito.core:manifest.target_schema;

import rstd;
import rstd.serde;
import rstd.toml;
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
import :manifest.key_schema;
import :manifest.convention;
import :manifest.wire;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;
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
        return manifest_schema_failure<PathBuf>(rstd::format("{} must not be empty", context));
    auto path = PathBuf::from(rstd::move(text));
    if (! path.as_path().is_relative()) {
        return manifest_schema_failure<PathBuf>(
            rstd::format("{} must be a relative path", context));
    }
    return Ok(rstd::move(path));
}

auto resolve_package_source_root(const Toml& package, ref<rstd::path::Path> root)
    -> ManifestSchemaResult<PathBuf> {
    auto declared = optional_string(package, "source-root"_str, "package"_str);
    if (declared.is_err()) return Err(rstd::move(declared).unwrap_err());
    if (declared->is_none()) return Ok(PathBuf::from(root));

    auto relative =
        relative_path(rstd::move(declared).unwrap().unwrap(), "package.source-root"_str);
    if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
    auto requested = PathBuf::from(root).join(relative->as_path());
    auto source_root =
        canonical_existing(requested.as_path(), "cannot resolve package.source-root"_str);
    if (source_root.is_err()) return Err(rstd::move(source_root).unwrap_err());
    auto metadata = rstd::fs::metadata(source_root->as_path());
    if (metadata.is_err()) {
        return manifest_io_failure<PathBuf>("package.source-root"_str,
                                            "inspect"_str,
                                            source_root->as_path(),
                                            rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return manifest_schema_failure<PathBuf>(
            rstd::format("package.source-root '{}' is not a directory", source_root->as_path()));
    }
    if (root.strip_prefix(source_root->as_path()).is_none()) {
        return manifest_schema_failure<PathBuf>(
            rstd::format("package.source-root '{}' must contain package directory '{}'",
                         source_root->as_path(),
                         root));
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
            return manifest_schema_failure<PathBuf>(
                rstd::format("{} must stay within the CMake install prefix", context));
        }
    }
    if (! found) {
        return manifest_schema_failure<PathBuf>(rstd::format("{} must not be empty", context));
    }
    return path;
}

auto declared_paths(Option<ref<Toml>> value, ref<str> context, bool required)
    -> ManifestSchemaResult<Vec<PathBuf>> {
    if (required && value.is_none()) {
        return manifest_schema_failure<Vec<PathBuf>>(rstd::format("{} is required", context));
    }
    auto strings = string_array(value, context);
    if (strings.is_err()) return Err(rstd::move(strings).unwrap_err());
    auto result = Vec<PathBuf>::make();
    auto items  = rstd::move(strings).unwrap();
    for (auto& item : items) {
        auto path = relative_path(rstd::move(item), context);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        result.push(rstd::move(path).unwrap());
    }
    if (required && result.is_empty()) {
        return manifest_schema_failure<Vec<PathBuf>>(rstd::format("{} must not be empty", context));
    }
    return Ok(rstd::move(result));
}

auto predicate_values(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto single = (**value).as_str();
    if (single.is_some()) {
        if (single->is_empty()) {
            return manifest_schema_failure<Vec<String>>(
                rstd::format("{} must not be empty", context));
        }
        result.push(String::make(*single));
        return Ok(rstd::move(result));
    }
    auto values = string_array(value, context);
    if (values.is_err()) return Err(rstd::move(values).unwrap_err());
    result = rstd::move(values).unwrap();
    if (result.is_empty()) {
        return manifest_schema_failure<Vec<String>>(rstd::format("{} must not be empty", context));
    }
    for (const auto& item : result) {
        if (item.is_empty()) {
            return manifest_schema_failure<Vec<String>>(
                rstd::format("{} item must not be empty", context));
        }
    }
    return Ok(rstd::move(result));
}

auto parse_target_predicate(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<TargetPredicate> {
    if (value.is_none()) return Ok(TargetPredicate {});
    auto table = table_value(**value, context);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_unknown(**table, context, target_predicate_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    auto families = predicate_values(member(**value, "family"_str),
                                     rstd::format("{}.family", context).as_str());
    auto operating_systems =
        predicate_values(member(**value, "os"_str), rstd::format("{}.os", context).as_str());
    auto excluded_families = predicate_values(member(**value, "not-family"_str),
                                              rstd::format("{}.not-family", context).as_str());
    auto excluded_operating_systems = predicate_values(member(**value, "not-os"_str),
                                                       rstd::format("{}.not-os", context).as_str());
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
            return manifest_schema_failure<TargetPredicate>(
                rstd::format("{}.family contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *excluded_families) {
        if (! valid_family(item.as_str())) {
            return manifest_schema_failure<TargetPredicate>(rstd::format(
                "{}.not-family contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *operating_systems) {
        if (! valid_os(item.as_str())) {
            return manifest_schema_failure<TargetPredicate>(
                rstd::format("{}.os contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *excluded_operating_systems) {
        if (! valid_os(item.as_str())) {
            return manifest_schema_failure<TargetPredicate>(
                rstd::format("{}.not-os contains unsupported value '{}'", context, item.as_str()));
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
    for (const auto& path : paths) {
        if (path.as_path() == candidate) return true;
    }
    return false;
}

auto append_attachment_source(TestAttachmentManifest& attachment, PathBuf source, DataPath path)
    -> ManifestSchemaResult<empty> {
    if (path_repeated(attachment.sources, source.as_path())) {
        return manifest_data_failure<empty>(rstd::move(path), "source is repeated"_str);
    }
    attachment.sources.push(rstd::move(source));
    return Ok(empty {});
}

auto parse_source_group_names(Option<ref<Toml>> value, ref<str> context, bool required)
    -> ManifestSchemaResult<Vec<String>> {
    auto names = rstd_try(string_array(value, context));
    if (required && names.is_empty()) {
        return manifest_schema_failure<Vec<String>>(rstd::format("{} must not be empty", context));
    }
    auto seen = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : names) {
        if (! package_name_is_valid(name.as_str())) {
            return manifest_schema_failure<Vec<String>>(
                rstd::format("{} contains invalid source group name '{}'", context, name.as_str()));
        }
        if (seen.contains_key(name.as_str())) {
            return manifest_schema_failure<Vec<String>>(
                rstd::format("{} repeats source group '{}'", context, name.as_str()));
        }
        seen.insert(name.clone(), empty {});
    }
    return Ok(rstd::move(names));
}

auto validate_source_group_names(Vec<String> names, const DataPath& path, bool required)
    -> ManifestSchemaResult<Vec<String>> {
    if (required && names.is_empty()) {
        return manifest_data_failure<Vec<String>>(path.clone(), "must not be empty"_str);
    }
    auto seen = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < names.len(); ++index) {
        const auto& name = names[index];
        auto        item = path.with_index(index);
        if (! package_name_is_valid(name.as_str())) {
            return manifest_data_failure<Vec<String>>(rstd::move(item),
                                                      "invalid source group name"_str);
        }
        if (seen.contains_key(name.as_str())) {
            return manifest_data_failure<Vec<String>>(rstd::move(item),
                                                      "source group is repeated"_str);
        }
        seen.insert(name.clone(), empty {});
    }
    return Ok(rstd::move(names));
}

auto parse_target_source_conditions(Option<ref<Toml>> value, const DataPath& owner_path)
    -> ManifestSchemaResult<Vec<ConditionalTargetSources>> {
    auto result = Vec<ConditionalTargetSources>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path    = owner_path.with_field("when"_str);
    auto entries = rstd_try(decode_manifest_value<Vec<lito::manifest::wire::TargetSourceCondition>>(
        **value, path.clone()));
    for (usize index {}; index < entries.len(); ++index) {
        auto& entry     = entries[index];
        auto  item      = path.with_index(index);
        auto  source    = rstd::move(entry.condition);
        auto  condition = lito::condition::parse(source.as_str());
        if (condition.is_err()) {
            auto message = rstd::format("invalid condition: {}", condition.unwrap_err());
            return manifest_data_failure<Vec<ConditionalTargetSources>>(
                item.with_field("condition"_str), message.as_str());
        }
        auto groups = rstd_try(validate_source_group_names(
            rstd::move(entry.source_groups), item.with_field("source-groups"_str), true));
        result.push(ConditionalTargetSources {
            .source        = rstd::move(source),
            .condition     = rstd::move(condition).unwrap(),
            .source_groups = rstd::move(groups),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_test_attachments(Option<ref<Toml>> value, const DataPath& owner_path)
    -> ManifestSchemaResult<Vec<TestAttachmentManifest>> {
    auto result = Vec<TestAttachmentManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path    = owner_path.with_field("attach"_str);
    auto entries = rstd_try(
        decode_manifest_value<Vec<lito::manifest::wire::TestAttachment>>(**value, path.clone()));
    for (usize index {}; index < entries.len(); ++index) {
        auto  item  = path.with_index(index);
        auto& entry = entries[index];
        if (! package_name_is_valid(entry.package.as_str())) {
            return manifest_data_failure<Vec<TestAttachmentManifest>>(
                item.with_field("package"_str), "must name a valid package"_str);
        }
        if (entry.sources.is_empty()) {
            return manifest_data_failure<Vec<TestAttachmentManifest>>(
                item.with_field("sources"_str), "must not be empty"_str);
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
                return manifest_data_failure<Vec<TestAttachmentManifest>>(
                    item.with_field("sources"_str).with_index(source_index),
                    "must be a relative path"_str);
            }
            auto appended =
                append_attachment_source(attachment,
                                         rstd::move(source),
                                         item.with_field("sources"_str).with_index(source_index));
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    if (result.is_empty()) {
        return manifest_data_failure<Vec<TestAttachmentManifest>>(rstd::move(path),
                                                                  "must not be empty"_str);
    }
    return Ok(rstd::move(result));
}

auto parse_target_source(const Toml&     value,
                         ref<str>        context,
                         const DataPath& path,
                         bool            module_required,
                         PackageLanguage language) -> ManifestSchemaResult<TargetSourceManifest> {
    auto module = rstd_try(optional_string(value, "module"_str, context));
    if (module.is_some() && ! valid_module_name(module->as_str())) {
        return manifest_schema_failure<TargetSourceManifest>(
            rstd::format("{}.module must be a valid module name", context));
    }
    auto source_value = member(value, "sources"_str);
    auto group_value  = member(value, "source-groups"_str);
    auto sources =
        rstd_try(declared_paths(source_value, rstd::format("{}.sources", context).as_str(), false));
    auto groups     = rstd_try(parse_source_group_names(
        group_value, rstd::format("{}.source-groups", context).as_str(), false));
    auto conditions = rstd_try(parse_target_source_conditions(member(value, "when"_str), path));
    const auto module_discovery = source_value.is_none() && group_value.is_none();
    if (language == PackageLanguage::C && module.is_some()) {
        return manifest_schema_failure<TargetSourceManifest>(
            rstd::format("{}.module is not supported by a C package", context));
    }
    if (language == PackageLanguage::C && module_discovery) {
        return manifest_schema_failure<TargetSourceManifest>(
            rstd::format("{} must declare sources or source-groups for a C package", context));
    }
    if (source_value.is_some() && sources.is_empty()) {
        return manifest_schema_failure<TargetSourceManifest>(
            rstd::format("{}.sources must not be empty", context));
    }
    if (module_required && module.is_none()) {
        return manifest_schema_failure<TargetSourceManifest>(
            rstd::format("{}.module is required", context));
    }
    return Ok(TargetSourceManifest {
        .module    = rstd::move(module),
        .discovery = module_discovery ? SourceDiscoveryMode::Module : SourceDiscoveryMode::Explicit,
        .declared_sources = rstd::move(sources),
        .source_groups    = rstd::move(groups),
        .conditions       = rstd::move(conditions),
    });
}

auto parse_source_groups(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Vec<SourceGroupManifest>> {
    auto result = Vec<SourceGroupManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path = DataPath().with_field("source-groups"_str);
    auto groups =
        rstd_try(decode_manifest_value<lito::manifest::wire::SourceGroups>(**value, path.clone()));
    for (auto name_ref : groups.keys()) {
        const auto& name = *name_ref;
        auto        item = path.with_map_key(name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return manifest_data_failure<Vec<SourceGroupManifest>>(rstd::move(item),
                                                                   "invalid source group name"_str);
        }
        auto specification = groups.get_mut(name.as_str()).unwrap_unchecked();
        auto external      = rstd::move(specification->external_source);
        if (external.is_some() && ! package_name_is_valid(external->as_str())) {
            return manifest_data_failure<Vec<SourceGroupManifest>>(
                item.with_field("external-source"_str), "must name a package external source"_str);
        }
        auto root       = SourceGroupRoot::Package;
        auto root_value = rstd::move(specification->root);
        if (root_value.is_some()) {
            if (root_value->as_str() == "generated"_str) {
                root = SourceGroupRoot::Generated;
            } else if (root_value->as_str() != "package"_str) {
                return manifest_data_failure<Vec<SourceGroupManifest>>(
                    item.with_field("root"_str), "must be 'package' or 'generated'"_str);
            }
        }
        if (root == SourceGroupRoot::Generated && external.is_some()) {
            return manifest_data_failure<Vec<SourceGroupManifest>>(
                item.with_field("external-source"_str),
                "cannot be combined with root 'generated'"_str);
        }
        if (specification->sources.is_empty()) {
            return manifest_data_failure<Vec<SourceGroupManifest>>(item.with_field("sources"_str),
                                                                   "must not be empty"_str);
        }
        auto sources = Vec<PathBuf>::with_capacity(specification->sources.len());
        for (usize index {}; index < specification->sources.len(); ++index) {
            auto source = PathBuf::from(rstd::move(specification->sources[index]));
            if (! source.as_path().is_safe_relative()) {
                return manifest_data_failure<Vec<SourceGroupManifest>>(
                    item.with_field("sources"_str).with_index(index),
                    "must be a safe relative path"_str);
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

auto parse_library_target(Option<ref<Toml>> value, PackageLanguage language)
    -> ManifestSchemaResult<Option<PackageTargetManifest>> {
    if (value.is_none()) return Ok(Option<PackageTargetManifest> {});
    auto table = rstd_try(table_value(**value, "manifest.lib"_str));
    rstd_try(reject_unknown(*table, "manifest.lib"_str, library_key));
    auto name = rstd_try(required_string(**value, "name"_str, "manifest.lib"_str));
    if (! package_name_is_valid(name.as_str())) {
        return manifest_schema_failure<Option<PackageTargetManifest>>(
            "manifest.lib.name must be a valid target name"_str);
    }
    auto kind           = rstd_try(optional_string(**value, "kind"_str, "manifest.lib"_str));
    auto archive        = rstd_try(optional_string(**value, "archive"_str, "manifest.lib"_str));
    auto artifact       = rstd_try(optional_string(**value, "artifact"_str, "manifest.lib"_str));
    auto linker_options = rstd_try(
        string_array(member(**value, "linker-options"_str), "manifest.lib.linker-options"_str));
    if (archive.is_some() == artifact.is_some()) {
        return manifest_schema_failure<Option<PackageTargetManifest>>(
            "manifest.lib must contain exactly one of 'archive' or 'artifact'"_str);
    }
    auto output = LibraryOutput::Static(String::make());
    if (kind.is_none() || kind->as_str() == "static"_str) {
        if (archive.is_none()) {
            return manifest_schema_failure<Option<PackageTargetManifest>>(
                "manifest.lib static output requires 'archive'"_str);
        }
        output = LibraryOutput::Static(rstd::move(archive).unwrap());
    } else if (kind->as_str() == "shared"_str) {
        if (artifact.is_none()) {
            return manifest_schema_failure<Option<PackageTargetManifest>>(
                "manifest.lib shared output requires 'artifact'"_str);
        }
        output = LibraryOutput::Shared(rstd::move(artifact).unwrap());
    } else {
        return manifest_schema_failure<Option<PackageTargetManifest>>(
            "manifest.lib.kind must be 'static' or 'shared'"_str);
    }
    auto artifact_name = output.is_Static() ? output.as_Static().artifact.as_str()
                                            : output.as_Shared().artifact.as_str();
    if (! valid_artifact_name(artifact_name)) {
        return manifest_schema_failure<Option<PackageTargetManifest>>(
            "manifest.lib output must be a safe artifact basename"_str);
    }
    if (output.is_Static() && ! linker_options.is_empty()) {
        return manifest_schema_failure<Option<PackageTargetManifest>>(
            "manifest.lib.linker-options requires kind 'shared'"_str);
    }
    auto source = rstd_try(parse_target_source(**value,
                                               "manifest.lib"_str,
                                               DataPath().with_field("lib"_str),
                                               language == PackageLanguage::Cpp,
                                               language));
    return Ok(Some(PackageTargetManifest::Library(
        rstd::move(name), rstd::move(output), rstd::move(source), rstd::move(linker_options))));
}

auto parse_runtime_resources(Option<ref<Toml>> value, const DataPath& owner_path)
    -> ManifestSchemaResult<Vec<RuntimeResourceManifest>> {
    auto result = Vec<RuntimeResourceManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path    = owner_path.with_field("resources"_str);
    auto entries = rstd_try(
        decode_manifest_value<Vec<lito::manifest::wire::RuntimeResource>>(**value, path.clone()));
    if (entries.is_empty()) {
        return manifest_data_failure<Vec<RuntimeResourceManifest>>(rstd::move(path),
                                                                   "must not be empty"_str);
    }
    for (usize index {}; index < entries.len(); ++index) {
        auto  item_path = path.with_index(index);
        auto& item      = entries[index];
        auto& name      = item.name;
        if (! package_name_is_valid(name.as_str())) {
            return manifest_data_failure<Vec<RuntimeResourceManifest>>(
                item_path.with_field("name"_str), "must be a valid resource name"_str);
        }
        for (const auto& existing : result) {
            if (existing.name == name.as_str()) {
                return manifest_data_failure<Vec<RuntimeResourceManifest>>(
                    item_path.with_field("name"_str), "resource name is repeated"_str);
            }
        }
        if (item.root.as_str() != "generated"_str) {
            return manifest_data_failure<Vec<RuntimeResourceManifest>>(
                item_path.with_field("root"_str), "must be 'generated'"_str);
        }
        auto relative = PathBuf::from(rstd::move(item.path));
        if (relative.is_empty() || ! relative.as_path().is_safe_relative()) {
            return manifest_data_failure<Vec<RuntimeResourceManifest>>(
                item_path.with_field("path"_str), "must be a safe non-empty relative path"_str);
        }
        result.push(RuntimeResourceManifest {
            .name = rstd::move(name),
            .path = rstd::move(relative),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_runnable_targets(Option<ref<Toml>>                value,
                            lito::package::PackageTargetKind kind,
                            ref<str>                         key,
                            PackageLanguage                  language)
    -> ManifestSchemaResult<Vec<PackageTargetManifest>> {
    auto result = Vec<PackageTargetManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return manifest_schema_failure<Vec<PackageTargetManifest>>(
            rstd::format("manifest.{} must be an array of tables", key));
    }
    if ((**entries).is_empty()) {
        return manifest_schema_failure<Vec<PackageTargetManifest>>(
            rstd::format("manifest.{} must not be empty", key));
    }
    for (usize index {}; index < (**entries).len(); ++index) {
        const auto  context = rstd::format("manifest.{}[{}]", key, index);
        const auto& item    = (**entries)[index];
        auto        path    = DataPath().with_field(key).with_index(index);
        auto        table   = rstd_try(table_value(item, context.as_str()));
        auto        allowed =
            kind == lito::package::PackageTargetKind::Test
                ? test_key
                : (kind == lito::package::PackageTargetKind::Binary ? binary_key : runnable_key);
        rstd_try(reject_unknown(*table, context.as_str(), allowed));
        auto name = rstd_try(required_string(item, "name"_str, context.as_str()));
        if (! package_name_is_valid(name.as_str())) {
            return manifest_schema_failure<Vec<PackageTargetManifest>>(
                rstd::format("{}.name must be a valid target name", context.as_str()));
        }
        for (const auto& existing : result) {
            if (package_target_name(existing) == name.as_str()) {
                return manifest_schema_failure<Vec<PackageTargetManifest>>(
                    rstd::format("manifest.{} repeats target name '{}'", key, name.as_str()));
            }
        }
        auto source = rstd_try(parse_target_source(item, context.as_str(), path, false, language));
        auto link_stdlib          = true;
        auto declared_link_stdlib = member(item, "link-stdlib"_str);
        if (declared_link_stdlib.is_some()) {
            auto parsed = (**declared_link_stdlib).as_bool();
            if (parsed.is_none()) {
                return manifest_schema_failure<Vec<PackageTargetManifest>>(
                    rstd::format("{}.link-stdlib must be a bool", context.as_str()));
            }
            link_stdlib = *parsed;
        }
        if (kind == lito::package::PackageTargetKind::Binary) {
            auto resources = rstd_try(parse_runtime_resources(member(item, "resources"_str), path));
            result.push(PackageTargetManifest::Binary(
                rstd::move(name), rstd::move(source), link_stdlib, rstd::move(resources)));
        } else if (kind == lito::package::PackageTargetKind::Benchmark) {
            result.push(PackageTargetManifest::Benchmark(
                rstd::move(name), rstd::move(source), link_stdlib));
        } else {
            auto attachments = rstd_try(parse_test_attachments(member(item, "attach"_str), path));
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

auto resolve_package_include_directory(PathBuf path, ref<rstd::path::Path> root, ref<str> context)
    -> ManifestSchemaResult<PathBuf> {
    auto requested = PathBuf::from(root).join(path.as_path());
    auto canonical =
        canonical_existing(requested.as_path(), "cannot resolve include directory"_str);
    if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
    auto resolved = rstd::move(canonical).unwrap();
    if (resolved.as_path().strip_prefix(root).is_none()) {
        return manifest_schema_failure<PathBuf>(
            rstd::format("{} entry '{}' is outside package root", context, path.as_path()));
    }
    auto metadata = rstd::fs::metadata(resolved.as_path());
    if (metadata.is_err()) {
        return manifest_io_failure<PathBuf>(context,
                                            "inspect include directory"_str,
                                            resolved.as_path(),
                                            rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return manifest_schema_failure<PathBuf>(
            rstd::format("{} entry '{}' is not a directory", context, path.as_path()));
    }
    return Ok(rstd::move(resolved));
}

auto resolve_include_directories(Option<ref<Toml>>     value,
                                 ref<rstd::path::Path> root,
                                 ref<str>              context,
                                 bool                  allow_generated)
    -> ManifestSchemaResult<ResolvedIncludeDirectories> {
    auto result = ResolvedIncludeDirectories {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return manifest_schema_failure<ResolvedIncludeDirectories>(
            rstd::format("{} must be an array", context));
    }

    for (usize index {}; index < (**entries).len(); ++index) {
        const auto  item_context = rstd::format("{}[{}]", context, index);
        const auto& item         = (**entries)[index];
        auto        text         = item.as_str();
        if (text.is_some()) {
            auto relative = relative_path(String::make(*text), item_context.as_str());
            if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
            auto resolved = resolve_package_include_directory(
                rstd::move(relative).unwrap(), root, item_context.as_str());
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            result.physical.push(rstd::move(resolved).unwrap());
            continue;
        }

        auto table = table_value(item, item_context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, item_context.as_str(), include_directory_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto declared_path   = required_string(item, "path"_str, item_context.as_str());
        auto declared_root   = optional_string(item, "root"_str, item_context.as_str());
        auto external_source = optional_string(item, "external-source"_str, item_context.as_str());
        if (declared_path.is_err()) return Err(rstd::move(declared_path).unwrap_err());
        if (declared_root.is_err()) return Err(rstd::move(declared_root).unwrap_err());
        if (external_source.is_err()) return Err(rstd::move(external_source).unwrap_err());
        auto relative = relative_path(rstd::move(declared_path).unwrap(),
                                      rstd::format("{}.path", item_context.as_str()).as_str());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto root_value     = rstd::move(declared_root).unwrap();
        auto external_value = rstd::move(external_source).unwrap();
        if (external_value.is_some()) {
            if (root_value.is_some()) {
                return manifest_schema_failure<ResolvedIncludeDirectories>(rstd::format(
                    "{} cannot combine root and external-source", item_context.as_str()));
            }
            if (! package_name_is_valid(external_value->as_str())) {
                return manifest_schema_failure<ResolvedIncludeDirectories>(
                    rstd::format("{}.external-source must name a package external source",
                                 item_context.as_str()));
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
                rstd::move(relative).unwrap(), root, item_context.as_str());
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            result.physical.push(rstd::move(resolved).unwrap());
            continue;
        }
        if (root_kind != "generated"_str) {
            return manifest_schema_failure<ResolvedIncludeDirectories>(
                rstd::format("{}.root must be package or generated", item_context.as_str()));
        }
        if (! allow_generated) {
            return manifest_schema_failure<ResolvedIncludeDirectories>(rstd::format(
                "{} does not support generated public include directories", item_context.as_str()));
        }
        result.deferred.push(lito::dependency::IncludeDirectoryRequirement {
            .root = lito::dependency::IncludeDirectoryRoot::Generated,
            .path = rstd::move(relative).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_compile_tests(Option<ref<Toml>> value) -> ManifestSchemaResult<Vec<CompileTestCase>> {
    auto result = Vec<CompileTestCase>::make();
    auto path   = DataPath().with_field("compile-test"_str).with_field("cases"_str);
    if (value.is_none()) {
        return manifest_data_failure<Vec<CompileTestCase>>(rstd::move(path), "is required"_str);
    }
    auto cases = rstd_try(
        decode_manifest_value<Vec<lito::manifest::wire::CompileTestCase>>(**value, path.clone()));
    auto names   = rstd::collections::BTreeMap<String, empty>::make();
    auto sources = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < cases.len(); ++index) {
        auto  item = path.with_index(index);
        auto& wire = cases[index];
        if (wire.name.is_empty()) {
            return manifest_data_failure<Vec<CompileTestCase>>(item.with_field("name"_str),
                                                               "must not be empty"_str);
        }
        if (names.contains_key(wire.name.as_str())) {
            return manifest_data_failure<Vec<CompileTestCase>>(item.with_field("name"_str),
                                                               "case name is repeated"_str);
        }
        auto relative = PathBuf::from(rstd::move(wire.source));
        if (! relative.as_path().is_relative()) {
            return manifest_data_failure<Vec<CompileTestCase>>(item.with_field("source"_str),
                                                               "must be a relative path"_str);
        }
        auto source_text = relative.as_path().to_str();
        if (source_text.is_none()) {
            return manifest_data_failure<Vec<CompileTestCase>>(item.with_field("source"_str),
                                                               "must be valid UTF-8"_str);
        }
        if (sources.contains_key(*source_text)) {
            return manifest_data_failure<Vec<CompileTestCase>>(
                item.with_field("source"_str), "source is used by more than one case"_str);
        }
        auto expected = CompileTestOutcome::Failure;
        if (wire.outcome.as_str() == "success"_str) {
            expected = CompileTestOutcome::Success;
        } else if (wire.outcome.as_str() != "failure"_str) {
            return manifest_data_failure<Vec<CompileTestCase>>(
                item.with_field("outcome"_str), "must be 'success' or 'failure'"_str);
        }
        if (expected == CompileTestOutcome::Success &&
            (! wire.diagnostic_contains.is_empty() || ! wire.diagnostic_contains_any.is_empty())) {
            return manifest_data_failure<Vec<CompileTestCase>>(
                rstd::move(item), "successful outcome cannot require diagnostics"_str);
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
        return manifest_data_failure<Vec<CompileTestCase>>(rstd::move(path),
                                                           "must not be empty"_str);
    }
    return Ok(rstd::move(result));
}

auto parse_usage(Option<ref<Toml>>     value,
                 ref<rstd::path::Path> root,
                 ref<str>              context = "manifest.usage"_str)
    -> ManifestSchemaResult<lito::dependency::DeclaredUsageRequirements> {
    if (value.is_none()) return Ok(lito::dependency::DeclaredUsageRequirements {});
    auto table = table_value(**value, context);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_unknown(**table, context, usage_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());

    auto public_includes =
        resolve_include_directories(member(**value, "public-include-directories"_str),
                                    root,
                                    rstd::format("{}.public-include-directories", context).as_str(),
                                    false);
    auto private_includes = resolve_include_directories(
        member(**value, "private-include-directories"_str),
        root,
        rstd::format("{}.private-include-directories", context).as_str(),
        true);
    auto public_definitions = string_array(member(**value, "public-definitions"_str),
                                           rstd::format("{}.public-definitions", context).as_str());
    auto private_definitions =
        string_array(member(**value, "private-definitions"_str),
                     rstd::format("{}.private-definitions", context).as_str());
    auto options =
        string_array(member(**value, "options"_str), rstd::format("{}.options", context).as_str());
    auto linker_options   = string_array(member(**value, "linker-options"_str),
                                         rstd::format("{}.linker-options", context).as_str());
    auto system_libraries = string_array(member(**value, "system-libraries"_str),
                                         rstd::format("{}.system-libraries", context).as_str());
    auto threads          = false;
    auto declared_threads = member(**value, "threads"_str);
    if (declared_threads.is_some()) {
        auto parsed = (**declared_threads).as_bool();
        if (parsed.is_none()) {
            return manifest_schema_failure<lito::dependency::DeclaredUsageRequirements>(
                rstd::format("{}.threads must be a boolean", context));
        }
        threads = *parsed;
    }
    if (public_includes.is_err()) return Err(rstd::move(public_includes).unwrap_err());
    if (private_includes.is_err()) return Err(rstd::move(private_includes).unwrap_err());
    if (public_definitions.is_err()) return Err(rstd::move(public_definitions).unwrap_err());
    if (private_definitions.is_err()) return Err(rstd::move(private_definitions).unwrap_err());
    if (options.is_err()) return Err(rstd::move(options).unwrap_err());
    if (linker_options.is_err()) return Err(rstd::move(linker_options).unwrap_err());
    if (system_libraries.is_err()) return Err(rstd::move(system_libraries).unwrap_err());
    auto public_include_values     = rstd::move(public_includes).unwrap();
    auto private_include_values    = rstd::move(private_includes).unwrap();
    auto public_definition_values  = rstd::move(public_definitions).unwrap();
    auto private_definition_values = rstd::move(private_definitions).unwrap();
    auto option_values             = rstd::move(options).unwrap();
    auto linker_option_values      = rstd::move(linker_options).unwrap();
    auto system_library_values     = rstd::move(system_libraries).unwrap();
    return Ok(lito::dependency::DeclaredUsageRequirements {
        .public_include_directories             = rstd::move(public_include_values.physical),
        .private_include_directories            = rstd::move(private_include_values.physical),
        .public_definitions                     = rstd::move(public_definition_values),
        .private_definitions                    = rstd::move(private_definition_values),
        .options                                = rstd::move(option_values),
        .linker_options                         = rstd::move(linker_option_values),
        .threads                                = threads,
        .system_libraries                       = rstd::move(system_library_values),
        .private_include_directory_requirements = rstd::move(private_include_values.deferred),
        .public_include_directory_requirements  = rstd::move(public_include_values.deferred),
    });
}

auto parse_conditional_configurations(Option<ref<Toml>> value, ref<rstd::path::Path> root)
    -> ManifestSchemaResult<Vec<ConditionalConfiguration>> {
    auto result = Vec<ConditionalConfiguration>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return manifest_schema_failure<Vec<ConditionalConfiguration>>(
            "manifest.when must be an array"_str);
    }
    for (usize index {}; index < (**entries).len(); ++index) {
        const auto  context = rstd::format("manifest.when[{}]", index);
        const auto& entry   = (**entries)[index];
        auto        table   = table_value(entry, context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, context.as_str(), when_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto source = required_string(entry, "condition"_str, context.as_str());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        auto condition = lito::condition::parse(source->as_str());
        if (condition.is_err()) {
            return manifest_schema_failure<Vec<ConditionalConfiguration>>(
                rstd::format("{}", rstd::move(condition).unwrap_err()));
        }
        auto usage_value = member(entry, "usage"_str);
        if (usage_value.is_none()) {
            return manifest_schema_failure<Vec<ConditionalConfiguration>>(
                rstd::format("{} is missing 'usage'", context.as_str()));
        }
        auto usage =
            parse_usage(usage_value, root, rstd::format("{}.usage", context.as_str()).as_str());
        if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
        auto usage_table = table_value(**usage_value, rstd::format("{}.usage", context).as_str());
        if (usage_table.is_err()) return Err(rstd::move(usage_table).unwrap_err());
        result.push(ConditionalConfiguration {
            .source    = rstd::move(source).unwrap(),
            .condition = rstd::move(condition).unwrap(),
            .usage =
                ConditionalUsage {
                    .values           = rstd::move(usage).unwrap(),
                    .declares_threads = member(**usage_value, "threads"_str).is_some(),
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

auto parse_features(Option<ref<Toml>> value) -> ManifestSchemaResult<Vec<FeatureDeclaration>> {
    auto result = Vec<FeatureDeclaration>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path = DataPath().with_field("features"_str);
    auto features =
        rstd_try(decode_manifest_value<lito::manifest::wire::Features>(**value, path.clone()));
    auto macros = rstd::collections::BTreeMap<String, String>::make();
    for (auto name_ref : features.keys()) {
        const auto& name = *name_ref;
        auto        item = path.with_map_key(name.as_str());
        if (! feature_name_is_valid(name.as_str())) {
            return manifest_data_failure<Vec<FeatureDeclaration>>(rstd::move(item),
                                                                  "invalid feature name"_str);
        }
        auto specification = features.get(name.as_str()).unwrap_unchecked();
        auto macro         = normalized_feature_macro(name.as_str());
        if (! macro_name_is_valid(macro.as_str())) {
            return manifest_data_failure<Vec<FeatureDeclaration>>(
                rstd::move(item), "macro is not a C/C++ identifier"_str);
        }
        auto existing = macros.get(macro.as_str());
        if (existing.is_some()) {
            return manifest_data_failure<Vec<FeatureDeclaration>>(
                rstd::move(item), "normalized macro is repeated"_str);
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
