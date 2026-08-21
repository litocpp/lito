module;
#include <rstd/macro.hpp>

export module lito.driver:install.catalog;

import rstd;
import rstd.json;
import lito.core;
import :install.error;
import :install.destination;
import :install.catalog.model;
import :install.identity;
import :install.package;
import :install.path;
import :install.source;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

inline constexpr auto INSTALL_PACKAGE_INFO_SCHEMA = u64(2);

template<typename T>
auto catalog_failure(String message) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(InstallStoreCause::Message(rstd::move(message))));
}

template<typename T>
auto catalog_failure(ref<str> message) -> InstallStoreResult<T> {
    return catalog_failure<T>(String::make(message));
}

template<typename T>
auto catalog_io_failure(ref<str>               operation,
                        ref<rstd::path::Path>  path,
                        rstd::io::error::Error error) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(
        InstallStoreCause::Io(String::make(operation), PathBuf::from(path), rstd::move(error))));
}

auto catalog_json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto known_fields(const Json& value, ref<str> context, initializer_list<ref<str>> names)
    -> InstallStoreResult<empty> {
    return Ok(rstd_try(
        lito::parse::json::reject_unknown(value, lito::parse::NodePath::root(context), names)));
}

auto required_member(const Json& value, ref<str> key, ref<str> context)
    -> InstallStoreResult<ref<Json>> {
    return Ok(rstd_try(
        lito::parse::json::required_member(value, key, lito::parse::NodePath::root(context))));
}

auto required_string(const Json& value, ref<str> key, ref<str> context)
    -> InstallStoreResult<String> {
    return Ok(rstd_try(lito::parse::json::required_non_empty_string(
        value, key, lito::parse::NodePath::root(context))));
}

auto relative_link_target_is_valid(ref<rstd::path::Path> path) -> bool {
    if (path.is_empty() || path.is_absolute() || path.has_root()) return false;
    auto components = path.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal() && ! component->is_parent_dir()) return false;
    }
    return true;
}

auto production_json(const InstallOwnedProduction& production) -> Json {
    auto value = JsonMap::make();
    if (production.kind == InstallOwnedProductionKind::Copy) {
        value.insert(String::make("kind"_str), catalog_json_string("copy"_str));
        return Json::Object(rstd::move(value));
    }
    if (production.kind == InstallOwnedProductionKind::Link) {
        value.insert(String::make("kind"_str), catalog_json_string("link"_str));
        return Json::Object(rstd::move(value));
    }
    value.insert(String::make("kind"_str), catalog_json_string("lito-link"_str));
    value.insert(String::make("variant"_str), catalog_json_string("install"_str));
    value.insert(String::make("variant-identity"_str),
                 catalog_json_string(production.variant_identity.as_str()));
    value.insert(String::make("link-identity"_str),
                 catalog_json_string(production.link_identity.as_str()));
    auto runtime_search = JsonArray::make();
    if (production.runtime_search.is_some()) {
        runtime_search.reserve(production.runtime_search->paths.len());
        for (const auto& path : production.runtime_search->paths) {
            auto item = JsonMap::make();
            item.insert(String::make("anchor"_str), catalog_json_string("origin"_str));
            item.insert(String::make("path"_str),
                        Json::String(path.path.as_path().to_string_lossy()));
            runtime_search.push(Json::Object(rstd::move(item)));
        }
    }
    value.insert(String::make("runtime-search"_str), Json::Array(rstd::move(runtime_search)));
    return Json::Object(rstd::move(value));
}

auto transforms_json(const Vec<lito::artifact::StripMode>& transforms) -> Json {
    auto values = JsonArray::with_capacity(transforms.len());
    for (auto mode : transforms) {
        auto value = JsonMap::make();
        value.insert(String::make("kind"_str), catalog_json_string("strip"_str));
        value.insert(String::make("mode"_str),
                     catalog_json_string(mode == lito::artifact::StripMode::DebugInfo
                                             ? "debuginfo"_str
                                             : "symbols"_str));
        values.push(Json::Object(rstd::move(value)));
    }
    return Json::Array(rstd::move(values));
}

auto package_info_json(const InstallPackageInfo& info) -> InstallStoreResult<Json> {
    auto package = JsonMap::make();
    package.insert(String::make("id"_str), catalog_json_string(info.identity.id.as_str()));
    package.insert(String::make("name"_str), catalog_json_string(info.identity.name.as_str()));
    package.insert(String::make("version"_str), catalog_json_string(info.version.as_str()));
    package.insert(String::make("source"_str),
                   rstd_try(serialize_install_source_provenance(info.provenance)));
    package.insert(String::make("profile"_str), catalog_json_string(info.profile.as_str()));
    package.insert(String::make("target"_str), catalog_json_string(info.target.as_str()));

    auto entries = JsonArray::with_capacity(info.entries.len());
    for (const auto& entry : info.entries) {
        auto item = JsonMap::make();
        item.insert(String::make("logical"_str),
                    Json::String(entry.logical_destination.as_path().to_string_lossy()));
        item.insert(String::make("physical"_str),
                    Json::String(entry.physical_destination.as_path().to_string_lossy()));
        item.insert(String::make("kind"_str),
                    catalog_json_string(
                        entry.kind == InstallOwnedEntryKind::File ? "file"_str : "soft-link"_str));
        item.insert(String::make("origin"_str), catalog_json_string(entry.origin.as_str()));
        item.insert(String::make("production"_str), production_json(entry.production));
        item.insert(String::make("transforms"_str), transforms_json(entry.transforms));
        if (entry.link_target.is_some()) {
            item.insert(String::make("link-target"_str),
                        Json::String(entry.link_target->as_path().to_string_lossy()));
        }
        entries.push(Json::Object(rstd::move(item)));
    }

    auto dependencies = JsonArray::with_capacity(info.runtime_dependencies.len());
    for (const auto& dependency : info.runtime_dependencies) {
        auto item = JsonMap::make();
        item.insert(String::make("package-id"_str),
                    catalog_json_string(dependency.package_id.as_str()));
        item.insert(String::make("name"_str), catalog_json_string(dependency.name.as_str()));
        item.insert(String::make("source"_str),
                    catalog_json_string(dependency.source_identity.as_str()));
        dependencies.push(Json::Object(rstd::move(item)));
    }

    auto root = JsonMap::make();
    root.insert(String::make("schema"_str),
                Json::Number(rstd::json::Number::from_u64(INSTALL_PACKAGE_INFO_SCHEMA)));
    root.insert(String::make("package"_str), Json::Object(rstd::move(package)));
    root.insert(String::make("layout"_str),
                catalog_json_string(info.layout == InstallManagedPackageLayout::DirectBin
                                        ? "direct-bin"_str
                                        : "isolated-prefix"_str));
    root.insert(String::make("entries"_str), Json::Array(rstd::move(entries)));
    root.insert(String::make("runtime-dependencies"_str), Json::Array(rstd::move(dependencies)));
    return Ok(Json::Object(rstd::move(root)));
}

auto parse_package_info(const Json& value, ref<rstd::path::Path> path)
    -> InstallStoreResult<InstallPackageInfo> {
    rstd_try(known_fields(
        value,
        "install package info"_str,
        { "schema"_str, "package"_str, "layout"_str, "entries"_str, "runtime-dependencies"_str }));
    auto schema = rstd_try(required_member(value, "schema"_str, "install package info"_str));
    auto number = schema->as_u64();
    if (number.is_none() || *number != INSTALL_PACKAGE_INFO_SCHEMA) {
        return catalog_failure<InstallPackageInfo>(
            rstd::format("install package info '{}' uses an unsupported schema", path));
    }

    auto package_value =
        rstd_try(required_member(value, "package"_str, "install package info"_str));
    rstd_try(known_fields(
        *package_value,
        "install package info.package"_str,
        { "id"_str, "name"_str, "version"_str, "source"_str, "profile"_str, "target"_str }));
    auto id =
        rstd_try(required_string(*package_value, "id"_str, "install package info.package"_str));
    auto name =
        rstd_try(required_string(*package_value, "name"_str, "install package info.package"_str));
    auto source =
        rstd_try(required_member(*package_value, "source"_str, "install package info.package"_str));
    auto provenance      = rstd_try(parse_install_source_provenance(*source));
    auto source_identity = rstd_try(install_source_identity(provenance));
    auto expected_id     = rstd_try(install_package_id(name.as_str(), source_identity.as_str()));
    if (id != expected_id.as_str()) {
        return catalog_failure<InstallPackageInfo>(
            rstd::format("install package info '{}' has package id '{}', expected '{}'",
                         path,
                         id.as_str(),
                         expected_id.as_str()));
    }
    auto filename = path.file_name();
    auto text     = filename.is_some() ? filename->to_str() : None();
    auto stem     = text.is_some() ? text->strip_suffix(".info"_str) : None();
    if (stem.is_none() || *stem != id.as_str()) {
        return catalog_failure<InstallPackageInfo>(
            rstd::format("install package info filename '{}' does not match package id '{}'",
                         path,
                         id.as_str()));
    }

    auto layout_text = rstd_try(required_string(value, "layout"_str, "install package info"_str));
    auto layout      = InstallManagedPackageLayout::DirectBin;
    if (layout_text == "isolated-prefix"_str) {
        layout = InstallManagedPackageLayout::IsolatedPrefix;
    } else if (layout_text != "direct-bin"_str) {
        return catalog_failure<InstallPackageInfo>(rstd::format(
            "install package info '{}' has unknown layout '{}'", path, layout_text.as_str()));
    }

    auto entries_value =
        rstd_try(required_member(value, "entries"_str, "install package info"_str));
    auto entries_array = entries_value->as_array();
    if (entries_array.is_none() || (**entries_array).is_empty()) {
        return catalog_failure<InstallPackageInfo>(
            "install package info.entries must be a non-empty array"_str);
    }
    auto entries = Vec<InstallOwnedEntry>::with_capacity((**entries_array).len());
    for (const auto& item : **entries_array) {
        rstd_try(known_fields(item,
                              "installed entry"_str,
                              { "logical"_str,
                                "physical"_str,
                                "kind"_str,
                                "origin"_str,
                                "link-target"_str,
                                "production"_str,
                                "transforms"_str }));
        auto logical =
            PathBuf::from(rstd_try(required_string(item, "logical"_str, "installed entry"_str)));
        auto physical =
            PathBuf::from(rstd_try(required_string(item, "physical"_str, "installed entry"_str)));
        if (! install_relative_destination_is_valid(logical.as_path()) ||
            ! install_relative_destination_is_valid(physical.as_path())) {
            return catalog_failure<InstallPackageInfo>(
                rstd::format("install package info '{}' contains an unsafe entry path", path));
        }
        auto kind_text = rstd_try(required_string(item, "kind"_str, "installed entry"_str));
        auto kind      = InstallOwnedEntryKind::File;
        auto link      = Option<PathBuf> {};
        if (kind_text == "soft-link"_str) {
            kind = InstallOwnedEntryKind::SoftLink;
            link = Some(PathBuf::from(
                rstd_try(required_string(item, "link-target"_str, "installed entry"_str))));
            if (! relative_link_target_is_valid(link->as_path())) {
                return catalog_failure<InstallPackageInfo>(
                    rstd::format("install package info '{}' contains an unsafe link target", path));
            }
        } else if (kind_text != "file"_str) {
            return catalog_failure<InstallPackageInfo>(rstd::format(
                "install package info '{}' has unknown entry kind '{}'", path, kind_text.as_str()));
        } else if (item.get("link-target"_str).is_some()) {
            return catalog_failure<InstallPackageInfo>(
                "regular installed entry may not contain link-target"_str);
        }
        auto production_value =
            rstd_try(required_member(item, "production"_str, "installed entry"_str));
        rstd_try(known_fields(*production_value,
                              "installed entry.production"_str,
                              { "kind"_str,
                                "variant"_str,
                                "variant-identity"_str,
                                "link-identity"_str,
                                "runtime-search"_str }));
        auto production_kind = rstd_try(
            required_string(*production_value, "kind"_str, "installed entry.production"_str));
        auto production = InstallOwnedProduction {};
        if (production_kind == "link"_str) {
            production.kind = InstallOwnedProductionKind::Link;
            if ((**production_value->as_object()).len() != usize(1)) {
                return catalog_failure<InstallPackageInfo>(
                    "link production may only contain kind"_str);
            }
        } else if (production_kind == "lito-link"_str) {
            production.kind = InstallOwnedProductionKind::LitoLink;
            auto variant    = rstd_try(required_string(
                *production_value, "variant"_str, "installed entry.production"_str));
            if (variant != "install"_str) {
                return catalog_failure<InstallPackageInfo>(
                    "lito-link production variant must be 'install'"_str);
            }
            production.variant_identity = rstd_try(required_string(
                *production_value, "variant-identity"_str, "installed entry.production"_str));
            production.link_identity    = rstd_try(required_string(
                *production_value, "link-identity"_str, "installed entry.production"_str));
            auto runtime_value          = rstd_try(required_member(
                *production_value, "runtime-search"_str, "installed entry.production"_str));
            auto runtime_array          = runtime_value->as_array();
            if (runtime_array.is_none() || (**runtime_array).is_empty()) {
                return catalog_failure<InstallPackageInfo>(
                    "lito-link production runtime-search must be a non-empty array"_str);
            }
            auto paths = Vec<lito::artifact::OriginRelativeRuntimePath>::make();
            for (const auto& runtime : **runtime_array) {
                rstd_try(known_fields(runtime,
                                      "installed entry.production.runtime-search"_str,
                                      { "anchor"_str, "path"_str }));
                auto anchor = rstd_try(required_string(
                    runtime, "anchor"_str, "installed entry.production.runtime-search"_str));
                if (anchor != "origin"_str) {
                    return catalog_failure<InstallPackageInfo>(
                        "runtime search anchor must be 'origin'"_str);
                }
                auto parsed_path = lito::artifact::make_origin_relative_runtime_path(
                    PathBuf::from(rstd_try(required_string(
                        runtime, "path"_str, "installed entry.production.runtime-search"_str))));
                if (parsed_path.is_err()) {
                    return catalog_failure<InstallPackageInfo>(
                        rstd::format("installed runtime search path is invalid: {}",
                                     rstd::move(parsed_path).unwrap_err()));
                }
                paths.push(rstd::move(parsed_path).unwrap());
            }
            auto runpath = lito::artifact::make_elf_runpath(rstd::move(paths));
            if (runpath.is_err()) {
                return catalog_failure<InstallPackageInfo>(
                    rstd::format("installed runtime search policy is invalid: {}",
                                 rstd::move(runpath).unwrap_err()));
            }
            production.runtime_search = Some(rstd::move(runpath).unwrap());
        } else if (production_kind == "copy"_str) {
            if ((**production_value->as_object()).len() != usize(1)) {
                return catalog_failure<InstallPackageInfo>(
                    "copy production may only contain kind"_str);
            }
        } else {
            return catalog_failure<InstallPackageInfo>(rstd::format(
                "installed entry has unknown production '{}'", production_kind.as_str()));
        }

        auto transforms_value =
            rstd_try(required_member(item, "transforms"_str, "installed entry"_str));
        auto transforms_array = transforms_value->as_array();
        if (transforms_array.is_none()) {
            return catalog_failure<InstallPackageInfo>(
                "installed entry.transforms must be an array"_str);
        }
        auto transforms = Vec<lito::artifact::StripMode>::make();
        for (const auto& transform : **transforms_array) {
            rstd_try(known_fields(
                transform, "installed entry.transform"_str, { "kind"_str, "mode"_str }));
            auto transform_kind =
                rstd_try(required_string(transform, "kind"_str, "installed entry.transform"_str));
            if (transform_kind != "strip"_str) {
                return catalog_failure<InstallPackageInfo>(rstd::format(
                    "installed entry has unknown transform '{}'", transform_kind.as_str()));
            }
            auto mode =
                rstd_try(required_string(transform, "mode"_str, "installed entry.transform"_str));
            if (mode == "debuginfo"_str) {
                transforms.push(lito::artifact::StripMode::DebugInfo);
            } else if (mode == "symbols"_str) {
                transforms.push(lito::artifact::StripMode::Symbols);
            } else {
                return catalog_failure<InstallPackageInfo>(
                    rstd::format("installed strip transform has unknown mode '{}'", mode.as_str()));
            }
        }
        entries.push(InstallOwnedEntry {
            .logical_destination  = rstd::move(logical),
            .physical_destination = rstd::move(physical),
            .kind                 = kind,
            .origin      = rstd_try(required_string(item, "origin"_str, "installed entry"_str)),
            .link_target = rstd::move(link),
            .production  = rstd::move(production),
            .transforms  = rstd::move(transforms),
        });
    }

    auto dependencies_value =
        rstd_try(required_member(value, "runtime-dependencies"_str, "install package info"_str));
    auto dependencies_array = dependencies_value->as_array();
    if (dependencies_array.is_none()) {
        return catalog_failure<InstallPackageInfo>(
            "install package info.runtime-dependencies must be an array"_str);
    }
    auto dependencies =
        Vec<InstallStoredRuntimeDependency>::with_capacity((**dependencies_array).len());
    for (const auto& item : **dependencies_array) {
        rstd_try(known_fields(item,
                              "installed runtime dependency"_str,
                              { "package-id"_str, "name"_str, "source"_str }));
        auto dependency = InstallStoredRuntimeDependency {
            .package_id = rstd_try(
                required_string(item, "package-id"_str, "installed runtime dependency"_str)),
            .name = rstd_try(required_string(item, "name"_str, "installed runtime dependency"_str)),
            .source_identity =
                rstd_try(required_string(item, "source"_str, "installed runtime dependency"_str)),
        };
        auto expected = rstd_try(
            install_package_id(dependency.name.as_str(), dependency.source_identity.as_str()));
        if (dependency.package_id != expected.as_str()) {
            return catalog_failure<InstallPackageInfo>(
                rstd::format("installed runtime dependency '{}' has package id '{}', expected '{}'",
                             dependency.name.as_str(),
                             dependency.package_id.as_str(),
                             expected.as_str()));
        }
        dependencies.push(rstd::move(dependency));
    }

    return Ok(InstallPackageInfo {
        .identity =
            InstallPackageIdentity {
                .id              = rstd::move(id),
                .name            = rstd::move(name),
                .source_identity = rstd::move(source_identity),
            },
        .version = rstd_try(
            required_string(*package_value, "version"_str, "install package info.package"_str)),
        .provenance = rstd::move(provenance),
        .profile    = rstd_try(
            required_string(*package_value, "profile"_str, "install package info.package"_str)),
        .target = rstd_try(
            required_string(*package_value, "target"_str, "install package info.package"_str)),
        .layout               = layout,
        .entries              = rstd::move(entries),
        .runtime_dependencies = rstd::move(dependencies),
    });
}

auto validate_catalog(const InstallLayout& layout, const InstallCatalog& catalog)
    -> InstallStoreResult<empty> {
    auto identities   = rstd::collections::BTreeMap<String, String>::make();
    auto destinations = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& package : catalog.packages) {
        if (identities.contains_key(package.identity.id.as_str())) {
            return catalog_failure<empty>(rstd::format("installed package id '{}' is repeated",
                                                       package.identity.id.as_str()));
        }
        identities.insert(package.identity.id.clone(), package.identity.name.clone());
        for (const auto& entry : package.entries) {
            if (entry.kind == InstallOwnedEntryKind::SoftLink &&
                (entry.production.kind != InstallOwnedProductionKind::Link ||
                 ! entry.transforms.is_empty())) {
                return catalog_failure<empty>(
                    rstd::format("installed symbolic link '{}' has invalid production metadata",
                                 entry.physical_destination.as_path()));
            }
            if (entry.kind == InstallOwnedEntryKind::File &&
                entry.production.kind == InstallOwnedProductionKind::Link) {
                return catalog_failure<empty>(
                    rstd::format("installed file '{}' has link production metadata",
                                 entry.physical_destination.as_path()));
            }
            if (entry.production.kind == InstallOwnedProductionKind::LitoLink &&
                entry.production.runtime_search.is_none()) {
                return catalog_failure<empty>(
                    rstd::format("installed file '{}' has incomplete Lito link metadata",
                                 entry.physical_destination.as_path()));
            }
            for (auto transform : entry.transforms) {
                if (transform == lito::artifact::StripMode::None) {
                    return catalog_failure<empty>(
                        rstd::format("installed file '{}' has an empty strip transform",
                                     entry.physical_destination.as_path()));
                }
            }
            auto key   = entry.physical_destination.as_path().to_string_lossy();
            auto owner = destinations.get(key.as_str());
            if (owner.is_some()) {
                return catalog_failure<empty>(
                    rstd::format("installed destination '{}' is owned by both '{}' and '{}'",
                                 entry.physical_destination.as_path(),
                                 **owner,
                                 package.identity.name.as_str()));
            }
            destinations.insert(rstd::move(key), package.identity.name.clone());

            if (package.layout == InstallManagedPackageLayout::DirectBin) {
                if (entry.kind != InstallOwnedEntryKind::File ||
                    entry.physical_destination.as_path() != entry.logical_destination.as_path() ||
                    ! install_path_is_under_bin(entry.logical_destination.as_path())) {
                    return catalog_failure<empty>(
                        rstd::format("direct-bin package '{}' has an invalid owned entry",
                                     package.identity.name.as_str()));
                }
                continue;
            }
            if (entry.kind == InstallOwnedEntryKind::File) {
                auto expected = PathBuf::from("packages"_str);
                expected.push(PathBuf::from(package.identity.id.as_str()).as_path());
                expected.push(entry.logical_destination.as_path());
                if (expected.as_path() != entry.physical_destination.as_path()) {
                    return catalog_failure<empty>(
                        rstd::format("isolated package '{}' has an invalid private entry",
                                     package.identity.name.as_str()));
                }
                continue;
            }
            if (! install_path_is_under_bin(entry.logical_destination.as_path()) ||
                entry.physical_destination.as_path() != entry.logical_destination.as_path() ||
                entry.link_target.is_none()) {
                return catalog_failure<empty>(
                    rstd::format("isolated package '{}' has an invalid public link",
                                 package.identity.name.as_str()));
            }
            const InstallOwnedEntry* private_entry = nullptr;
            for (const auto& candidate : package.entries) {
                if (candidate.kind == InstallOwnedEntryKind::File &&
                    candidate.logical_destination.as_path() ==
                        entry.logical_destination.as_path()) {
                    private_entry = rstd::addressof(candidate);
                    break;
                }
            }
            if (private_entry == nullptr) {
                return catalog_failure<empty>(
                    rstd::format("public link '{}' has no private executable entry",
                                 entry.physical_destination.as_path()));
            }
            auto public_path = layout.root.path.join(entry.physical_destination.as_path());
            auto parent      = public_path.as_path().parent();
            auto target_path = layout.root.path.join(private_entry->physical_destination.as_path());
            auto expected    = parent.is_some()
                                   ? rstd::path::lexically_relative(*parent, target_path.as_path())
                                   : Option<PathBuf> {};
            if (expected.is_none() || expected->as_path() != entry.link_target->as_path()) {
                return catalog_failure<empty>(
                    rstd::format("public link '{}' has an invalid relative target",
                                 entry.physical_destination.as_path()));
            }
        }
    }

    for (const auto& package : catalog.packages) {
        auto dependency_ids = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& dependency : package.runtime_dependencies) {
            if (dependency_ids.contains_key(dependency.package_id.as_str())) {
                return catalog_failure<empty>(
                    rstd::format("installed package '{}' repeats runtime dependency '{}'",
                                 package.identity.name.as_str(),
                                 dependency.name.as_str()));
            }
            dependency_ids.insert(dependency.package_id.clone(), empty {});
            const InstallPackageInfo* target = nullptr;
            for (const auto& candidate : catalog.packages) {
                if (candidate.identity.id == dependency.package_id.as_str()) {
                    target = rstd::addressof(candidate);
                    break;
                }
            }
            if (target == nullptr) {
                return catalog_failure<empty>(
                    rstd::format("installed package '{}' requires missing runtime package '{}'",
                                 package.identity.name.as_str(),
                                 dependency.name.as_str()));
            }
            if (target->identity.name != dependency.name.as_str() ||
                target->identity.source_identity != dependency.source_identity.as_str()) {
                return catalog_failure<empty>(rstd::format(
                    "installed package '{}' runtime dependency '{}' has an identity mismatch",
                    package.identity.name.as_str(),
                    dependency.name.as_str()));
            }
        }
    }
    return Ok(empty {});
}

} // namespace lito

export namespace lito
{

auto serialize_install_package_info(const InstallPackageInfo& info) -> InstallStoreResult<String> {
    auto json = rstd_try(package_info_json(info));
    auto text = rstd::json::to_string(
        json, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return Ok(rstd::move(text));
}

auto load_managed_install_catalog(const InstallLayout& layout)
    -> InstallStoreResult<InstallCatalog> {
    auto opened = rstd::fs::read_dir(layout.packages_directory.as_path());
    if (opened.is_err()) {
        return catalog_io_failure<InstallCatalog>("read install packages"_str,
                                                  layout.packages_directory.as_path(),
                                                  rstd::move(opened).unwrap_err());
    }
    auto paths   = Vec<PathBuf>::make();
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return catalog_io_failure<InstallCatalog>("read install package entry"_str,
                                                      layout.packages_directory.as_path(),
                                                      rstd::move(item).unwrap_err());
        }
        auto path      = item->path();
        auto extension = path.as_path().extension();
        if (extension.is_none() || extension->to_str() != Some("info"_str)) continue;
        paths.push(rstd::move(path));
    }
    rstd::slice_::sort_unstable_by(
        paths.as_mut_slice().as_mut_ref(), [](const PathBuf& left, const PathBuf& right) {
            return left.as_path().to_string_lossy() < right.as_path().to_string_lossy();
        });
    auto catalog = InstallCatalog {};
    for (const auto& path : paths) {
        auto metadata = rstd::fs::symlink_metadata(path.as_path());
        if (metadata.is_err()) {
            return catalog_io_failure<InstallCatalog>("inspect install package info"_str,
                                                      path.as_path(),
                                                      rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_file() || metadata->is_symlink()) {
            return catalog_failure<InstallCatalog>(rstd::format(
                "install package info '{}' is not a regular non-symlink file", path.as_path()));
        }
        auto contents = rstd::fs::read_to_string(path.as_path());
        if (contents.is_err()) {
            return catalog_io_failure<InstallCatalog>(
                "read install package info"_str, path.as_path(), rstd::move(contents).unwrap_err());
        }
        auto json = rstd::json::from_str(contents->as_str());
        if (json.is_err()) {
            return Err(InstallStoreError::Cause(
                InstallStoreCause::Json(path.clone(), rstd::move(json).unwrap_err())));
        }
        catalog.packages.push(rstd_try(parse_package_info(*json, path.as_path())));
    }
    rstd_try(validate_catalog(layout, catalog));
    return Ok(rstd::move(catalog));
}

auto managed_catalog_entry_owner(const InstallCatalog& catalog,
                                 ref<rstd::path::Path> physical_destination) -> Option<usize> {
    for (usize package {}; package < catalog.packages.len(); ++package) {
        for (const auto& entry : catalog.packages[package].entries) {
            if (entry.physical_destination.as_path() == physical_destination) {
                return Some(package);
            }
        }
    }
    return None();
}

auto validate_managed_install_catalog(const InstallLayout& layout, const InstallCatalog& catalog)
    -> InstallStoreResult<empty> {
    return validate_catalog(layout, catalog);
}

} // namespace lito
