module;
#include <rstd/macro.hpp>

export module lito.manifest:target_schema;

import rstd;
import rstd.toml;
import lito.error;
import lito.cpp;
import lito.manifest.contract;
import lito.package.identity;
import lito.dependency.contract;
import lito.platform;
import lito.build.profile;
import :primitives;
import :key_schema;
import :convention;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;

namespace lito
{

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
    if (text.is_empty()) return failure<PathBuf>(rstd::format("{} must not be empty", context));
    auto path = PathBuf::from(rstd::move(text));
    if (! path.as_path().is_relative()) {
        return failure<PathBuf>(rstd::format("{} must be a relative path", context));
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
        return failure<PathBuf>(
            rstd::format("package.source-root '{}' is not a directory", source_root->as_path()));
    }
    if (root.strip_prefix(source_root->as_path()).is_none()) {
        return failure<PathBuf>(
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
    auto component  = components.next();
    if (component.is_none()) {
        return failure<PathBuf>(rstd::format("{} must not be empty", context));
    }
    while (component.is_some()) {
        if (! component->is_normal()) {
            return failure<PathBuf>(
                rstd::format("{} must stay within the CMake install prefix", context));
        }
        component = components.next();
    }
    return path;
}

auto declared_paths(Option<ref<Toml>> value, ref<str> context, bool required)
    -> ManifestSchemaResult<Vec<PathBuf>> {
    if (required && value.is_none()) {
        return failure<Vec<PathBuf>>(rstd::format("{} is required", context));
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
        return failure<Vec<PathBuf>>(rstd::format("{} must not be empty", context));
    }
    return Ok(rstd::move(result));
}

auto predicate_values(Option<ref<Toml>> value, ref<str> context) -> ManifestSchemaResult<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto single = (**value).as_str();
    if (single.is_some()) {
        if (single->is_empty()) {
            return failure<Vec<String>>(rstd::format("{} must not be empty", context));
        }
        result.push(String::make(*single));
        return Ok(rstd::move(result));
    }
    auto values = string_array(value, context);
    if (values.is_err()) return Err(rstd::move(values).unwrap_err());
    result = rstd::move(values).unwrap();
    if (result.is_empty()) {
        return failure<Vec<String>>(rstd::format("{} must not be empty", context));
    }
    for (const auto& item : result) {
        if (item.is_empty()) {
            return failure<Vec<String>>(rstd::format("{} item must not be empty", context));
        }
    }
    return Ok(rstd::move(result));
}

auto parse_target_predicate(Option<ref<Toml>> value, ref<str> context) -> ManifestSchemaResult<TargetPredicate> {
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
            return failure<TargetPredicate>(
                rstd::format("{}.family contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *excluded_families) {
        if (! valid_family(item.as_str())) {
            return failure<TargetPredicate>(rstd::format(
                "{}.not-family contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *operating_systems) {
        if (! valid_os(item.as_str())) {
            return failure<TargetPredicate>(
                rstd::format("{}.os contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *excluded_operating_systems) {
        if (! valid_os(item.as_str())) {
            return failure<TargetPredicate>(
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

auto parse_source_groups(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<ConditionalSourceGroup>> {
    auto result = Vec<ConditionalSourceGroup>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto groups = (**value).as_array();
    if (groups.is_none()) {
        return failure<Vec<ConditionalSourceGroup>>(rstd::format("{} must be an array", context));
    }
    for (usize index {}; index < (**groups).len(); ++index) {
        const auto item_context = rstd::format("{}[{}]", context, index);
        auto       table        = table_value((**groups)[index], item_context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, item_context.as_str(), source_group_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto sources = declared_paths(member((**groups)[index], "sources"_str),
                                      rstd::format("{}.sources", item_context.as_str()).as_str(),
                                      true);
        auto predicate =
            parse_target_predicate(member((**groups)[index], "target"_str),
                                   rstd::format("{}.target", item_context.as_str()).as_str());
        if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());
        if (predicate.is_err()) return Err(rstd::move(predicate).unwrap_err());
        result.push(ConditionalSourceGroup {
            .predicate = rstd::move(predicate).unwrap(),
            .sources   = rstd::move(sources).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto path_repeated(const Vec<PathBuf>& paths, ref<rstd::path::Path> candidate) -> bool {
    for (const auto& path : paths) {
        if (path.as_path() == candidate) return true;
    }
    return false;
}

auto append_attachment_source(TestAttachmentManifest& attachment, PathBuf source, ref<str> context)
    -> ManifestSchemaResult<empty> {
    if (path_repeated(attachment.sources, source.as_path())) {
        return failure<empty>(rstd::format("{} repeats source '{}'", context, source.as_path()));
    }
    attachment.sources.push(rstd::move(source));
    return Ok(empty {});
}

auto append_attachment_group(TestAttachmentManifest& attachment,
                             ConditionalSourceGroup  group,
                             ref<str>                context) -> ManifestSchemaResult<empty> {
    for (usize index {}; index < group.sources.len(); ++index) {
        for (usize prior {}; prior < index; ++prior) {
            if (group.sources[prior].as_path() == group.sources[index].as_path()) {
                return failure<empty>(rstd::format(
                    "{} repeats source '{}'", context, group.sources[index].as_path()));
            }
        }
    }
    attachment.conditional_source_groups.push(rstd::move(group));
    return Ok(empty {});
}

auto parse_test_attachments(Option<ref<Toml>> value, ref<str> owner_context)
    -> ManifestSchemaResult<Vec<TestAttachmentManifest>> {
    auto result = Vec<TestAttachmentManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return failure<Vec<TestAttachmentManifest>>(
            rstd::format("{}.attach must be an array", owner_context));
    }
    for (usize index {}; index < (**entries).len(); ++index) {
        const auto context = rstd::format("{}.attach[{}]", owner_context, index);
        auto       table   = table_value((**entries)[index], context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, context.as_str(), test_attachment_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto package = required_string((**entries)[index], "package"_str, context.as_str());
        auto sources = declared_paths(member((**entries)[index], "sources"_str),
                                      rstd::format("{}.sources", context.as_str()).as_str(),
                                      false);
        auto groups =
            parse_source_groups(member((**entries)[index], "source-groups"_str),
                                rstd::format("{}.source-groups", context.as_str()).as_str());
        if (package.is_err()) return Err(rstd::move(package).unwrap_err());
        if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());
        if (groups.is_err()) return Err(rstd::move(groups).unwrap_err());
        if (! package_name_is_valid(package->as_str())) {
            return failure<Vec<TestAttachmentManifest>>(
                rstd::format("{}.package must name a valid package", context.as_str()));
        }
        if (sources->is_empty() && groups->is_empty()) {
            return failure<Vec<TestAttachmentManifest>>(
                rstd::format("{} must contain sources or source-groups", context.as_str()));
        }

        auto position = Option<usize> {};
        for (usize candidate {}; candidate < result.len(); ++candidate) {
            if (result[candidate].package == package->as_str()) {
                position = Some(candidate);
                break;
            }
        }
        if (position.is_none()) {
            result.push(TestAttachmentManifest { .package = rstd::move(package).unwrap() });
            position = Some(result.len() - usize(1));
        }
        auto& attachment = result[*position];
        for (auto& source : *sources) {
            auto appended =
                append_attachment_source(attachment, rstd::move(source), context.as_str());
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
        for (auto& group : *groups) {
            auto appended =
                append_attachment_group(attachment, rstd::move(group), context.as_str());
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    if (result.is_empty()) {
        return failure<Vec<TestAttachmentManifest>>(
            rstd::format("{}.attach must not be empty", owner_context));
    }
    return Ok(rstd::move(result));
}

auto parse_target_source(const Toml& value, ref<str> context, bool module_required)
    -> ManifestSchemaResult<TargetSourceManifest> {
    auto module = rstd_try(optional_string(value, "module"_str, context));
    if (module.is_some() && ! valid_module_name(module->as_str())) {
        return failure<TargetSourceManifest>(
            rstd::format("{}.module must be a valid module name", context));
    }
    auto source_value = member(value, "sources"_str);
    auto group_value  = member(value, "source-groups"_str);
    auto groups       = rstd_try(parse_source_groups(
        group_value, rstd::format("{}.source-groups", context).as_str()));
    auto sources      = rstd_try(declared_paths(
        source_value, rstd::format("{}.sources", context).as_str(), false));
    const auto module_discovery = source_value.is_none() && group_value.is_none();
    if (! module_discovery && sources.is_empty() && groups.is_empty()) {
        return failure<TargetSourceManifest>(
            rstd::format("{} must contain sources or source-groups", context));
    }
    if (module_required && module.is_none()) {
        return failure<TargetSourceManifest>(rstd::format("{}.module is required", context));
    }
    return Ok(TargetSourceManifest {
        .module = rstd::move(module),
        .discovery = module_discovery ? SourceDiscoveryMode::Module : SourceDiscoveryMode::Explicit,
        .declared_sources          = rstd::move(sources),
        .conditional_source_groups = rstd::move(groups),
    });
}

auto parse_library_target(Option<ref<Toml>> value) -> ManifestSchemaResult<Option<PackageTargetManifest>> {
    if (value.is_none()) return Ok(Option<PackageTargetManifest> {});
    auto table = rstd_try(table_value(**value, "manifest.lib"_str));
    rstd_try(reject_unknown(*table, "manifest.lib"_str, library_key));
    auto name    = rstd_try(required_string(**value, "name"_str, "manifest.lib"_str));
    auto archive = rstd_try(required_string(**value, "archive"_str, "manifest.lib"_str));
    if (! package_name_is_valid(name.as_str())) {
        return failure<Option<PackageTargetManifest>>(
            "manifest.lib.name must be a valid target name"_str);
    }
    if (! valid_artifact_name(archive.as_str())) {
        return failure<Option<PackageTargetManifest>>(
            "manifest.lib.archive must be a safe artifact basename"_str);
    }
    auto source = rstd_try(parse_target_source(**value, "manifest.lib"_str, true));
    return Ok(Some(
        PackageTargetManifest::Library(rstd::move(name), rstd::move(archive), rstd::move(source))));
}

auto parse_runnable_targets(Option<ref<Toml>> value, PackageTargetKind kind, ref<str> key)
    -> ManifestSchemaResult<Vec<PackageTargetManifest>> {
    auto result = Vec<PackageTargetManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return failure<Vec<PackageTargetManifest>>(
            rstd::format("manifest.{} must be an array of tables", key));
    }
    if ((**entries).is_empty()) {
        return failure<Vec<PackageTargetManifest>>(
            rstd::format("manifest.{} must not be empty", key));
    }
    for (usize index {}; index < (**entries).len(); ++index) {
        const auto  context = rstd::format("manifest.{}[{}]", key, index);
        const auto& item    = (**entries)[index];
        auto        table   = rstd_try(table_value(item, context.as_str()));
        rstd_try(reject_unknown(
            *table, context.as_str(), kind == PackageTargetKind::Test ? test_key : runnable_key));
        auto name = rstd_try(required_string(item, "name"_str, context.as_str()));
        if (! package_name_is_valid(name.as_str())) {
            return failure<Vec<PackageTargetManifest>>(
                rstd::format("{}.name must be a valid target name", context.as_str()));
        }
        for (const auto& existing : result) {
            if (package_target_name(existing) == name.as_str()) {
                return failure<Vec<PackageTargetManifest>>(
                    rstd::format("manifest.{} repeats target name '{}'", key, name.as_str()));
            }
        }
        auto source               = rstd_try(parse_target_source(item, context.as_str(), false));
        auto link_stdlib          = true;
        auto declared_link_stdlib = member(item, "link-stdlib"_str);
        if (declared_link_stdlib.is_some()) {
            auto parsed = (**declared_link_stdlib).as_bool();
            if (parsed.is_none()) {
                return failure<Vec<PackageTargetManifest>>(
                    rstd::format("{}.link-stdlib must be a bool", context.as_str()));
            }
            link_stdlib = *parsed;
        }
        if (kind == PackageTargetKind::Binary) {
            result.push(
                PackageTargetManifest::Binary(rstd::move(name), rstd::move(source), link_stdlib));
        } else if (kind == PackageTargetKind::Benchmark) {
            result.push(PackageTargetManifest::Benchmark(
                rstd::move(name), rstd::move(source), link_stdlib));
        } else {
            auto attachments =
                rstd_try(parse_test_attachments(member(item, "attach"_str), context.as_str()));
            result.push(PackageTargetManifest::Test(
                rstd::move(name), rstd::move(source), link_stdlib, rstd::move(attachments)));
        }
    }
    return Ok(rstd::move(result));
}

struct ResolvedIncludeDirectories {
    Vec<PathBuf>                     physical;
    Vec<IncludeDirectoryRequirement> deferred;
};

auto resolve_package_include_directory(PathBuf path, ref<rstd::path::Path> root, ref<str> context)
    -> ManifestSchemaResult<PathBuf> {
    auto requested = PathBuf::from(root).join(path.as_path());
    auto canonical =
        canonical_existing(requested.as_path(), "cannot resolve include directory"_str);
    if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
    auto resolved = rstd::move(canonical).unwrap();
    if (resolved.as_path().strip_prefix(root).is_none()) {
        return failure<PathBuf>(
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
        return failure<PathBuf>(
            rstd::format("{} entry '{}' is not a directory", context, path.as_path()));
    }
    return Ok(rstd::move(resolved));
}

auto resolve_include_directories(Option<ref<Toml>>     value,
                                 ref<rstd::path::Path> root,
                                 ref<str>              context,
                                 bool allow_generated) -> ManifestSchemaResult<ResolvedIncludeDirectories> {
    auto result = ResolvedIncludeDirectories {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return failure<ResolvedIncludeDirectories>(rstd::format("{} must be an array", context));
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
        auto declared_path = required_string(item, "path"_str, item_context.as_str());
        auto declared_root = optional_string(item, "root"_str, item_context.as_str());
        if (declared_path.is_err()) return Err(rstd::move(declared_path).unwrap_err());
        if (declared_root.is_err()) return Err(rstd::move(declared_root).unwrap_err());
        auto relative = relative_path(rstd::move(declared_path).unwrap(),
                                      rstd::format("{}.path", item_context.as_str()).as_str());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto root_value = rstd::move(declared_root).unwrap();
        auto root_kind  = root_value.is_some() ? root_value->as_str() : "package"_str;
        if (root_kind == "package"_str) {
            auto resolved = resolve_package_include_directory(
                rstd::move(relative).unwrap(), root, item_context.as_str());
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            result.physical.push(rstd::move(resolved).unwrap());
            continue;
        }
        if (root_kind != "generated"_str) {
            return failure<ResolvedIncludeDirectories>(
                rstd::format("{}.root must be package or generated", item_context.as_str()));
        }
        if (! allow_generated) {
            return failure<ResolvedIncludeDirectories>(rstd::format(
                "{} does not support generated public include directories", item_context.as_str()));
        }
        result.deferred.push(IncludeDirectoryRequirement {
            .root = IncludeDirectoryRoot::Generated,
            .path = rstd::move(relative).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_compile_tests(Option<ref<Toml>> value) -> ManifestSchemaResult<Vec<CompileTestCase>> {
    auto result = Vec<CompileTestCase>::make();
    if (value.is_none()) {
        return failure<Vec<CompileTestCase>>("compile-test.cases is required"_str);
    }
    auto cases = (**value).as_array();
    if (cases.is_none()) {
        return failure<Vec<CompileTestCase>>("compile-test.cases must be an array"_str);
    }
    auto names   = rstd::collections::BTreeMap<String, empty>::make();
    auto sources = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < (**cases).len(); ++index) {
        const auto  context = rstd::format("compile-test.cases[{}]", index);
        const auto& value   = (**cases)[index];
        auto        table   = table_value(value, context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, context.as_str(), compile_test_case_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto name    = required_string(value, "name"_str, context.as_str());
        auto source  = required_string(value, "source"_str, context.as_str());
        auto outcome = required_string(value, "outcome"_str, context.as_str());
        auto options = string_array(member(value, "options"_str),
                                    rstd::format("{}.options", context.as_str()).as_str());
        auto contains =
            string_array(member(value, "diagnostic-contains"_str),
                         rstd::format("{}.diagnostic-contains", context.as_str()).as_str());
        auto contains_any =
            string_array(member(value, "diagnostic-contains-any"_str),
                         rstd::format("{}.diagnostic-contains-any", context.as_str()).as_str());
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (outcome.is_err()) return Err(rstd::move(outcome).unwrap_err());
        if (options.is_err()) return Err(rstd::move(options).unwrap_err());
        if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
        if (contains_any.is_err()) return Err(rstd::move(contains_any).unwrap_err());
        if (name->is_empty()) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("{}.name must not be empty", context.as_str()));
        }
        if (names.contains_key(name->as_str())) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("compile-test repeats case name '{}'", name->as_str()));
        }
        auto relative = relative_path(rstd::move(source).unwrap(),
                                      rstd::format("{}.source", context.as_str()).as_str());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto source_text = relative->as_path().to_str();
        if (source_text.is_none()) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("{}.source is not valid UTF-8", context.as_str()));
        }
        if (sources.contains_key(*source_text)) {
            return failure<Vec<CompileTestCase>>(rstd::format(
                "compile-test source '{}' is used by more than one case", relative->as_path()));
        }
        auto expected = CompileTestOutcome::Failure;
        if (outcome->as_str() == "success"_str) {
            expected = CompileTestOutcome::Success;
        } else if (outcome->as_str() != "failure"_str) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("{}.outcome must be success or failure", context.as_str()));
        }
        auto option_values       = rstd::move(options).unwrap();
        auto contains_values     = rstd::move(contains).unwrap();
        auto contains_any_values = rstd::move(contains_any).unwrap();
        if (expected == CompileTestOutcome::Success &&
            (! contains_values.is_empty() || ! contains_any_values.is_empty())) {
            return failure<Vec<CompileTestCase>>(rstd::format(
                "{} cannot require diagnostics for a successful outcome", context.as_str()));
        }
        names.insert(name->clone(), empty {});
        sources.insert(String::make(*source_text), empty {});
        result.push(CompileTestCase {
            .name                    = rstd::move(name).unwrap(),
            .source                  = rstd::move(relative).unwrap(),
            .outcome                 = expected,
            .options                 = rstd::move(option_values),
            .diagnostic_contains     = rstd::move(contains_values),
            .diagnostic_contains_any = rstd::move(contains_any_values),
        });
    }
    if (result.is_empty()) {
        return failure<Vec<CompileTestCase>>("compile-test.cases must not be empty"_str);
    }
    return Ok(rstd::move(result));
}

auto validate_definitions(const Vec<String>& definitions, ref<str> context) -> ManifestSchemaResult<empty> {
    for (const auto& definition : definitions) {
        if (is_profile_owned_definition(definition.as_str())) {
            return failure<empty>(rstd::format(
                "{} definition '{}' overrides a Lito-owned setting", context, definition.as_str()));
        }
    }
    return Ok(empty {});
}

auto validate_linker_options(const Vec<String>& options, ref<str> context) -> ManifestSchemaResult<empty> {
    for (const auto& option : options) {
        if (option.as_str().starts_with("-stdlib="_str) || option.as_str() == "-nostdlib++"_str ||
            is_profile_owned_linker_option(option.as_str())) {
            return failure<empty>(rstd::format(
                "{} option '{}' overrides a Lito-owned setting", context, option.as_str()));
        }
    }
    return Ok(empty {});
}

auto parse_usage(Option<ref<Toml>> value, ref<rstd::path::Path> root) -> ManifestSchemaResult<UsageRequirements> {
    if (value.is_none()) return Ok(UsageRequirements {});
    auto table = table_value(**value, "manifest.usage"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_unknown(**table, "manifest.usage"_str, usage_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());

    auto public_includes =
        resolve_include_directories(member(**value, "public-include-directories"_str),
                                    root,
                                    "usage.public-include-directories"_str,
                                    false);
    auto private_includes =
        resolve_include_directories(member(**value, "private-include-directories"_str),
                                    root,
                                    "usage.private-include-directories"_str,
                                    true);
    auto public_definitions =
        string_array(member(**value, "public-definitions"_str), "usage.public-definitions"_str);
    auto private_definitions =
        string_array(member(**value, "private-definitions"_str), "usage.private-definitions"_str);
    auto public_options =
        string_array(member(**value, "public-options"_str), "usage.public-options"_str);
    auto private_options =
        string_array(member(**value, "private-options"_str), "usage.private-options"_str);
    auto private_linker_options = string_array(member(**value, "private-linker-options"_str),
                                               "usage.private-linker-options"_str);
    if (public_includes.is_err()) return Err(rstd::move(public_includes).unwrap_err());
    if (private_includes.is_err()) return Err(rstd::move(private_includes).unwrap_err());
    if (public_definitions.is_err()) return Err(rstd::move(public_definitions).unwrap_err());
    if (private_definitions.is_err()) return Err(rstd::move(private_definitions).unwrap_err());
    if (public_options.is_err()) return Err(rstd::move(public_options).unwrap_err());
    if (private_options.is_err()) return Err(rstd::move(private_options).unwrap_err());
    if (private_linker_options.is_err()) {
        return Err(rstd::move(private_linker_options).unwrap_err());
    }
    auto public_include_values        = rstd::move(public_includes).unwrap();
    auto private_include_values       = rstd::move(private_includes).unwrap();
    auto public_definition_values     = rstd::move(public_definitions).unwrap();
    auto private_definition_values    = rstd::move(private_definitions).unwrap();
    auto public_option_values         = rstd::move(public_options).unwrap();
    auto private_option_values        = rstd::move(private_options).unwrap();
    auto private_linker_option_values = rstd::move(private_linker_options).unwrap();
    auto public_definitions_valid =
        validate_definitions(public_definition_values, "usage.public-definitions"_str);
    auto private_definitions_valid =
        validate_definitions(private_definition_values, "usage.private-definitions"_str);
    auto private_linker_valid =
        validate_linker_options(private_linker_option_values, "usage.private-linker-options"_str);
    if (public_definitions_valid.is_err()) {
        return Err(rstd::move(public_definitions_valid).unwrap_err());
    }
    if (private_definitions_valid.is_err()) {
        return Err(rstd::move(private_definitions_valid).unwrap_err());
    }
    if (private_linker_valid.is_err()) {
        return Err(rstd::move(private_linker_valid).unwrap_err());
    }
    return Ok(UsageRequirements {
        .public_include_directories             = rstd::move(public_include_values.physical),
        .private_include_directories            = rstd::move(private_include_values.physical),
        .public_definitions                     = rstd::move(public_definition_values),
        .private_definitions                    = rstd::move(private_definition_values),
        .public_options                         = rstd::move(public_option_values),
        .private_options                        = rstd::move(private_option_values),
        .private_linker_options                 = rstd::move(private_linker_option_values),
        .private_include_directory_requirements = rstd::move(private_include_values.deferred),
    });
}

} // namespace lito
