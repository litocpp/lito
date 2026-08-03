export module tenon.lock_store;

import rstd;
import rstd.json;
import tenon.model;
import tenon.manifest;
import tenon.package;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json         = rstd::json::Value;
using Map          = rstd::json::Map;
using Array        = rstd::json::Array;
using StringSet    = rstd::collections::BTreeMap<String, empty>;
using KeyPredicate = bool (*)(ref<str>);

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Lock, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Lock, message));
}

auto lock_path(const ResolvedPackageGraph& graph) -> PathBuf {
    return graph.root_directory.join(PathBuf::from("tenon.lock"_str).as_path());
}

auto path_string(ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("lock source path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto string_json(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto reference_kind(GitReferenceKind kind) -> ref<str> {
    switch (kind) {
    case GitReferenceKind::DefaultBranch: return "default"_str;
    case GitReferenceKind::Branch: return "branch"_str;
    case GitReferenceKind::Tag: return "tag"_str;
    case GitReferenceKind::Rev: return "rev"_str;
    }
    return "default"_str;
}

auto graph_json(const ResolvedPackageGraph& graph) -> Result<Json> {
    auto sources = Array::make();
    for (const auto& source : graph.sources) {
        auto item = Map::make();
        if (source.kind == PackageSourceKind::Path) {
            auto path = path_string(source.path.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            item.insert(String::make("kind"_str), string_json("path"_str));
            item.insert(String::make("path"_str), string_json(path->as_str()));
        } else {
            auto reference = Map::make();
            reference.insert(String::make("kind"_str),
                             string_json(reference_kind(source.reference.kind)));
            reference.insert(String::make("value"_str),
                             string_json(source.reference.value.as_str()));
            item.insert(String::make("commit"_str), string_json(source.commit.as_str()));
            item.insert(String::make("kind"_str), string_json("git"_str));
            item.insert(String::make("reference"_str), Json::Object(rstd::move(reference)));
            item.insert(String::make("url"_str), string_json(source.git.as_str()));
        }
        sources.push(Json::Object(rstd::move(item)));
    }

    auto packages = Array::make();
    for (const auto& package : graph.packages) {
        auto dependencies = Array::make();
        for (const auto& dependency : package.dependencies) {
            dependencies.push(string_json(dependency.name.as_str()));
        }
        auto manifest = path_string(package.source_manifest.as_path());
        if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());

        auto item = Map::make();
        item.insert(String::make("dependencies"_str), Json::Array(rstd::move(dependencies)));
        item.insert(String::make("manifest"_str), string_json(manifest->as_str()));
        item.insert(String::make("name"_str), string_json(package.manifest.name.as_str()));
        item.insert(String::make("source"_str), string_json(package.source_identity.as_str()));
        item.insert(String::make("version"_str),
                    string_json(package.manifest.version.value->as_str()));
        packages.push(Json::Object(rstd::move(item)));
    }

    auto roots = Array::make();
    for (const auto& name : graph.root_names) roots.push(string_json(name.as_str()));
    auto root = Map::make();
    root.insert(String::make("packages"_str), Json::Array(rstd::move(packages)));
    root.insert(String::make("roots"_str), Json::Array(rstd::move(roots)));
    root.insert(String::make("sources"_str), Json::Array(rstd::move(sources)));
    root.insert(String::make("version"_str), Json::Number(rstd::json::Number::from_u64(u64(4))));
    return Ok(Json::Object(rstd::move(root)));
}

auto reject_unknown(const Json& value, ref<str> context, KeyPredicate allowed) -> Result<empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return failure<empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto root_key_v1(ref<str> key) -> bool {
    return key == "packages"_str || key == "root"_str || key == "version"_str;
}

auto root_key_v2(ref<str> key) -> bool {
    return key == "packages"_str || key == "roots"_str || key == "version"_str;
}

auto root_key_v3(ref<str> key) -> bool {
    return key == "packages"_str || key == "roots"_str || key == "sources"_str ||
           key == "version"_str;
}

auto package_key_v2(ref<str> key) -> bool {
    return key == "dependencies"_str || key == "id"_str || key == "name"_str ||
           key == "source"_str || key == "version"_str;
}

auto package_key_v3(ref<str> key) -> bool {
    return package_key_v2(key) || key == "manifest"_str;
}

auto package_key_v4(ref<str> key) -> bool {
    return key == "dependencies"_str || key == "manifest"_str || key == "name"_str ||
           key == "source"_str || key == "version"_str;
}

auto source_key_v1(ref<str> key) -> bool {
    return key == "kind"_str || key == "manifest"_str || key == "path"_str;
}

auto source_key_v2(ref<str> key) -> bool {
    return key == "kind"_str || key == "path"_str;
}

auto path_source_key_v3(ref<str> key) -> bool {
    return key == "id"_str || key == "kind"_str || key == "path"_str;
}

auto git_source_key_v3(ref<str> key) -> bool {
    return key == "commit"_str || key == "id"_str || key == "kind"_str || key == "reference"_str ||
           key == "url"_str;
}

auto path_source_key_v4(ref<str> key) -> bool {
    return key == "kind"_str || key == "path"_str;
}

auto git_source_key_v4(ref<str> key) -> bool {
    return key == "commit"_str || key == "kind"_str || key == "reference"_str || key == "url"_str;
}

auto reference_key_v3(ref<str> key) -> bool {
    return key == "kind"_str || key == "value"_str;
}

auto required_member(const Json& value, ref<str> key, ref<str> context) -> Result<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context) -> Result<ref<str>> {
    auto member = required_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) {
        return failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto validate_lock_v1(const Json& document) -> Result<empty> {
    auto known = reject_unknown(document, "lock root"_str, root_key_v1);
    if (known.is_err()) return known;
    auto version        = required_member(document, "version"_str, "lock root"_str);
    auto root           = required_string(document, "root"_str, "lock root"_str);
    auto packages_value = required_member(document, "packages"_str, "lock root"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (root.is_err()) return Err(rstd::move(root).unwrap_err());
    if (packages_value.is_err()) return Err(rstd::move(packages_value).unwrap_err());
    auto version_number = (**version).as_u64();
    if (version_number.is_none() || *version_number != u64(1)) {
        return failure<empty>("lock.version must be integer 1"_str);
    }
    auto packages = (**packages_value).as_array();
    if (packages.is_none()) {
        return failure<empty>("lock.packages must be an array"_str);
    }

    auto ids = StringSet::make();
    for (const auto& package : **packages) {
        auto package_known = reject_unknown(package, "lock package"_str, package_key_v2);
        if (package_known.is_err()) return package_known;
        auto id              = required_string(package, "id"_str, "lock package"_str);
        auto name            = required_string(package, "name"_str, "lock package"_str);
        auto package_version = required_string(package, "version"_str, "lock package"_str);
        auto source          = required_member(package, "source"_str, "lock package"_str);
        auto dependencies    = required_member(package, "dependencies"_str, "lock package"_str);
        if (id.is_err()) return Err(rstd::move(id).unwrap_err());
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (package_version.is_err()) {
            return Err(rstd::move(package_version).unwrap_err());
        }
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
        if (ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock repeats package id '{}'", *id));
        }
        ids.insert(String::make(*id), empty {});

        auto source_known = reject_unknown(**source, "lock package source"_str, source_key_v1);
        if (source_known.is_err()) return source_known;
        auto kind     = required_string(**source, "kind"_str, "lock package source"_str);
        auto manifest = required_string(**source, "manifest"_str, "lock package source"_str);
        auto path     = required_string(**source, "path"_str, "lock package source"_str);
        if (kind.is_err()) return Err(rstd::move(kind).unwrap_err());
        if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (*kind != "path"_str) {
            return failure<empty>("lock source kind must be path"_str);
        }

        auto dependency_array = (**dependencies).as_array();
        if (dependency_array.is_none()) {
            return failure<empty>("lock package dependencies must be an array"_str);
        }
        for (const auto& dependency : **dependency_array) {
            if (dependency.as_str().is_none()) {
                return failure<empty>("lock dependency id must be a string"_str);
            }
        }
    }
    if (! ids.contains_key(*root)) {
        return failure<empty>("lock.root does not identify a package"_str);
    }
    for (const auto& package : **packages) {
        auto dependencies = (**package.get("dependencies"_str)).as_array();
        for (const auto& dependency : **dependencies) {
            if (! ids.contains_key(*dependency.as_str())) {
                return failure<empty>(rstd::format(
                    "lock dependency '{}' does not identify a package", *dependency.as_str()));
            }
        }
    }
    return Ok(empty {});
}

auto validate_lock_v2(const Json& document) -> Result<empty> {
    auto known = reject_unknown(document, "lock root"_str, root_key_v2);
    if (known.is_err()) return known;
    auto version        = required_member(document, "version"_str, "lock root"_str);
    auto roots_value    = required_member(document, "roots"_str, "lock root"_str);
    auto packages_value = required_member(document, "packages"_str, "lock root"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (roots_value.is_err()) return Err(rstd::move(roots_value).unwrap_err());
    if (packages_value.is_err()) return Err(rstd::move(packages_value).unwrap_err());
    auto version_number = (**version).as_u64();
    if (version_number.is_none() || *version_number != u64(2)) {
        return failure<empty>("lock.version must be integer 2"_str);
    }
    auto roots    = (**roots_value).as_array();
    auto packages = (**packages_value).as_array();
    if (roots.is_none() || (**roots).is_empty()) {
        return failure<empty>("lock.roots must be a non-empty array"_str);
    }
    if (packages.is_none()) {
        return failure<empty>("lock.packages must be an array"_str);
    }

    auto ids = StringSet::make();
    for (const auto& package : **packages) {
        auto package_known = reject_unknown(package, "lock package"_str, package_key_v2);
        if (package_known.is_err()) return package_known;
        auto id              = required_string(package, "id"_str, "lock package"_str);
        auto name            = required_string(package, "name"_str, "lock package"_str);
        auto package_version = required_string(package, "version"_str, "lock package"_str);
        auto source          = required_member(package, "source"_str, "lock package"_str);
        auto dependencies    = required_member(package, "dependencies"_str, "lock package"_str);
        if (id.is_err()) return Err(rstd::move(id).unwrap_err());
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (package_version.is_err()) {
            return Err(rstd::move(package_version).unwrap_err());
        }
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
        if (ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock repeats package id '{}'", *id));
        }
        ids.insert(String::make(*id), empty {});

        auto source_known = reject_unknown(**source, "lock package source"_str, source_key_v2);
        if (source_known.is_err()) return source_known;
        auto kind = required_string(**source, "kind"_str, "lock package source"_str);
        auto path = required_string(**source, "path"_str, "lock package source"_str);
        if (kind.is_err()) return Err(rstd::move(kind).unwrap_err());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (*kind != "path"_str) {
            return failure<empty>("lock source kind must be path"_str);
        }

        auto dependency_array = (**dependencies).as_array();
        if (dependency_array.is_none()) {
            return failure<empty>("lock package dependencies must be an array"_str);
        }
        for (const auto& dependency : **dependency_array) {
            if (dependency.as_str().is_none()) {
                return failure<empty>("lock dependency id must be a string"_str);
            }
        }
    }
    auto root_ids = StringSet::make();
    for (const auto& root : **roots) {
        auto id = root.as_str();
        if (id.is_none()) return failure<empty>("lock root id must be a string"_str);
        if (! ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock root '{}' does not identify a package", *id));
        }
        if (root_ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock repeats root id '{}'", *id));
        }
        root_ids.insert(String::make(*id), empty {});
    }
    for (const auto& package : **packages) {
        auto dependencies = (**package.get("dependencies"_str)).as_array();
        for (const auto& dependency : **dependencies) {
            if (! ids.contains_key(*dependency.as_str())) {
                return failure<empty>(rstd::format(
                    "lock dependency '{}' does not identify a package", *dependency.as_str()));
            }
        }
    }
    return Ok(empty {});
}

auto valid_commit(ref<str> value) -> bool {
    if (value.len() != usize(40)) return false;
    for (auto byte : value) {
        const bool digit = byte >= u8('0') && byte <= u8('9');
        const bool lower = byte >= u8('a') && byte <= u8('f');
        const bool upper = byte >= u8('A') && byte <= u8('F');
        if (! digit && ! lower && ! upper) return false;
    }
    return true;
}

auto valid_source_manifest(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    auto path       = PathBuf::from(value);
    auto components = path.as_path().components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_root_dir() || component->is_parent_dir()) return false;
    }
    return true;
}

auto validate_lock_v3(const Json& document) -> Result<empty> {
    auto known = reject_unknown(document, "lock root"_str, root_key_v3);
    if (known.is_err()) return known;
    auto version        = required_member(document, "version"_str, "lock root"_str);
    auto roots_value    = required_member(document, "roots"_str, "lock root"_str);
    auto sources_value  = required_member(document, "sources"_str, "lock root"_str);
    auto packages_value = required_member(document, "packages"_str, "lock root"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (roots_value.is_err()) return Err(rstd::move(roots_value).unwrap_err());
    if (sources_value.is_err()) return Err(rstd::move(sources_value).unwrap_err());
    if (packages_value.is_err()) return Err(rstd::move(packages_value).unwrap_err());
    auto version_number = (**version).as_u64();
    if (version_number.is_none() || *version_number != u64(3)) {
        return failure<empty>("lock.version must be integer 3"_str);
    }
    auto roots    = (**roots_value).as_array();
    auto sources  = (**sources_value).as_array();
    auto packages = (**packages_value).as_array();
    if (roots.is_none() || (**roots).is_empty()) {
        return failure<empty>("lock.roots must be a non-empty array"_str);
    }
    if (sources.is_none() || (**sources).is_empty()) {
        return failure<empty>("lock.sources must be a non-empty array"_str);
    }
    if (packages.is_none()) {
        return failure<empty>("lock.packages must be an array"_str);
    }

    auto source_ids = StringSet::make();
    for (const auto& source : **sources) {
        auto id   = required_string(source, "id"_str, "lock source"_str);
        auto kind = required_string(source, "kind"_str, "lock source"_str);
        if (id.is_err()) return Err(rstd::move(id).unwrap_err());
        if (kind.is_err()) return Err(rstd::move(kind).unwrap_err());
        if (source_ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock repeats source id '{}'", *id));
        }
        if (*kind == "path"_str) {
            auto source_known = reject_unknown(source, "lock path source"_str, path_source_key_v3);
            if (source_known.is_err()) return source_known;
            auto path = required_string(source, "path"_str, "lock path source"_str);
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (*id != rstd::format("path+{}", *path)) {
                return failure<empty>(
                    rstd::format("lock path source id '{}' does not match path '{}'", *id, *path));
            }
        } else if (*kind == "git"_str) {
            auto source_known = reject_unknown(source, "lock Git source"_str, git_source_key_v3);
            if (source_known.is_err()) return source_known;
            auto url       = required_string(source, "url"_str, "lock Git source"_str);
            auto commit    = required_string(source, "commit"_str, "lock Git source"_str);
            auto reference = required_member(source, "reference"_str, "lock Git source"_str);
            if (url.is_err()) return Err(rstd::move(url).unwrap_err());
            if (commit.is_err()) return Err(rstd::move(commit).unwrap_err());
            if (reference.is_err()) return Err(rstd::move(reference).unwrap_err());
            if (url->is_empty()) {
                return failure<empty>("lock Git source URL must not be empty"_str);
            }
            if (! valid_commit(*commit)) {
                return failure<empty>(
                    "lock Git source commit must be a full hexadecimal object id"_str);
            }
            if (*id != rstd::format("git+{}#{}", *url, *commit)) {
                return failure<empty>(
                    rstd::format("lock Git source id '{}' does not match URL and commit", *id));
            }
            auto reference_known =
                reject_unknown(**reference, "lock Git reference"_str, reference_key_v3);
            if (reference_known.is_err()) return reference_known;
            auto reference_kind =
                required_string(**reference, "kind"_str, "lock Git reference"_str);
            auto reference_value =
                required_string(**reference, "value"_str, "lock Git reference"_str);
            if (reference_kind.is_err()) {
                return Err(rstd::move(reference_kind).unwrap_err());
            }
            if (reference_value.is_err()) {
                return Err(rstd::move(reference_value).unwrap_err());
            }
            const auto default_reference = *reference_kind == "default"_str;
            const auto named_reference   = *reference_kind == "branch"_str ||
                                           *reference_kind == "tag"_str ||
                                           *reference_kind == "rev"_str;
            if (! default_reference && ! named_reference) {
                return failure<empty>(
                    "lock Git reference kind must be default, branch, tag, or rev"_str);
            }
            if (default_reference != reference_value->is_empty()) {
                return failure<empty>(
                    "lock Git reference value must be empty only for default"_str);
            }
        } else {
            return failure<empty>(
                rstd::format("lock source '{}' has unsupported kind '{}'", *id, *kind));
        }
        source_ids.insert(String::make(*id), empty {});
    }

    auto ids   = StringSet::make();
    auto names = StringSet::make();
    for (const auto& package : **packages) {
        auto package_known = reject_unknown(package, "lock package"_str, package_key_v3);
        if (package_known.is_err()) return package_known;
        auto id              = required_string(package, "id"_str, "lock package"_str);
        auto name            = required_string(package, "name"_str, "lock package"_str);
        auto package_version = required_string(package, "version"_str, "lock package"_str);
        auto source          = required_string(package, "source"_str, "lock package"_str);
        auto manifest        = required_string(package, "manifest"_str, "lock package"_str);
        auto dependencies    = required_member(package, "dependencies"_str, "lock package"_str);
        if (id.is_err()) return Err(rstd::move(id).unwrap_err());
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (package_version.is_err()) {
            return Err(rstd::move(package_version).unwrap_err());
        }
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());
        if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
        if (ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock repeats package id '{}'", *id));
        }
        if (names.contains_key(*name)) {
            return failure<empty>(rstd::format("lock repeats package name '{}'", *name));
        }
        if (! source_ids.contains_key(*source)) {
            return failure<empty>(
                rstd::format("lock package '{}' references missing source '{}'", *id, *source));
        }
        if (! valid_source_manifest(*manifest)) {
            return failure<empty>(
                "lock package manifest must be a relative path without parent components"_str);
        }
        ids.insert(String::make(*id), empty {});
        names.insert(String::make(*name), empty {});

        auto dependency_array = (**dependencies).as_array();
        if (dependency_array.is_none()) {
            return failure<empty>("lock package dependencies must be an array"_str);
        }
        for (const auto& dependency : **dependency_array) {
            if (dependency.as_str().is_none()) {
                return failure<empty>("lock dependency id must be a string"_str);
            }
        }
    }
    auto root_ids = StringSet::make();
    for (const auto& root : **roots) {
        auto id = root.as_str();
        if (id.is_none()) return failure<empty>("lock root id must be a string"_str);
        if (! ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock root '{}' does not identify a package", *id));
        }
        if (root_ids.contains_key(*id)) {
            return failure<empty>(rstd::format("lock repeats root id '{}'", *id));
        }
        root_ids.insert(String::make(*id), empty {});
    }
    for (const auto& package : **packages) {
        auto dependencies = (**package.get("dependencies"_str)).as_array();
        for (const auto& dependency : **dependencies) {
            if (! ids.contains_key(*dependency.as_str())) {
                return failure<empty>(rstd::format(
                    "lock dependency '{}' does not identify a package", *dependency.as_str()));
            }
        }
    }
    return Ok(empty {});
}

auto validate_lock_v4(const Json& document) -> Result<empty> {
    auto known = reject_unknown(document, "lock root"_str, root_key_v3);
    if (known.is_err()) return known;
    auto version        = required_member(document, "version"_str, "lock root"_str);
    auto roots_value    = required_member(document, "roots"_str, "lock root"_str);
    auto sources_value  = required_member(document, "sources"_str, "lock root"_str);
    auto packages_value = required_member(document, "packages"_str, "lock root"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (roots_value.is_err()) return Err(rstd::move(roots_value).unwrap_err());
    if (sources_value.is_err()) return Err(rstd::move(sources_value).unwrap_err());
    if (packages_value.is_err()) return Err(rstd::move(packages_value).unwrap_err());
    auto version_number = (**version).as_u64();
    if (version_number.is_none() || *version_number != u64(4)) {
        return failure<empty>("lock.version must be integer 4"_str);
    }
    auto roots    = (**roots_value).as_array();
    auto sources  = (**sources_value).as_array();
    auto packages = (**packages_value).as_array();
    if (roots.is_none() || (**roots).is_empty()) {
        return failure<empty>("lock.roots must be a non-empty array"_str);
    }
    if (sources.is_none() || (**sources).is_empty()) {
        return failure<empty>("lock.sources must be a non-empty array"_str);
    }
    if (packages.is_none()) {
        return failure<empty>("lock.packages must be an array"_str);
    }

    auto source_identities = StringSet::make();
    auto git_requirements  = StringSet::make();
    for (const auto& source : **sources) {
        auto kind = required_string(source, "kind"_str, "lock source"_str);
        if (kind.is_err()) return Err(rstd::move(kind).unwrap_err());
        auto identity = String::make();
        if (*kind == "path"_str) {
            auto source_known = reject_unknown(source, "lock path source"_str, path_source_key_v4);
            if (source_known.is_err()) return source_known;
            auto path = required_string(source, "path"_str, "lock path source"_str);
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (path->is_empty()) {
                return failure<empty>("lock path source path must not be empty"_str);
            }
            auto path_value = PathBuf::from(*path);
            identity        = path_source_identity(path_value.as_path());
        } else if (*kind == "git"_str) {
            auto source_known = reject_unknown(source, "lock Git source"_str, git_source_key_v4);
            if (source_known.is_err()) return source_known;
            auto url       = required_string(source, "url"_str, "lock Git source"_str);
            auto commit    = required_string(source, "commit"_str, "lock Git source"_str);
            auto reference = required_member(source, "reference"_str, "lock Git source"_str);
            if (url.is_err()) return Err(rstd::move(url).unwrap_err());
            if (commit.is_err()) return Err(rstd::move(commit).unwrap_err());
            if (reference.is_err()) return Err(rstd::move(reference).unwrap_err());
            if (url->is_empty()) {
                return failure<empty>("lock Git source URL must not be empty"_str);
            }
            if (! valid_commit(*commit)) {
                return failure<empty>(
                    "lock Git source commit must be a full hexadecimal object id"_str);
            }
            auto reference_known =
                reject_unknown(**reference, "lock Git reference"_str, reference_key_v3);
            if (reference_known.is_err()) return reference_known;
            auto reference_kind =
                required_string(**reference, "kind"_str, "lock Git reference"_str);
            auto reference_value =
                required_string(**reference, "value"_str, "lock Git reference"_str);
            if (reference_kind.is_err()) {
                return Err(rstd::move(reference_kind).unwrap_err());
            }
            if (reference_value.is_err()) {
                return Err(rstd::move(reference_value).unwrap_err());
            }
            const auto default_reference = *reference_kind == "default"_str;
            const auto named_reference   = *reference_kind == "branch"_str ||
                                           *reference_kind == "tag"_str ||
                                           *reference_kind == "rev"_str;
            if (! default_reference && ! named_reference) {
                return failure<empty>(
                    "lock Git reference kind must be default, branch, tag, or rev"_str);
            }
            if (default_reference != reference_value->is_empty()) {
                return failure<empty>(
                    "lock Git reference value must be empty only for default"_str);
            }
            auto requirement = rstd::format("{}\n{}\n{}", *url, *reference_kind, *reference_value);
            if (git_requirements.contains_key(requirement.as_str())) {
                return failure<empty>(
                    rstd::format("lock repeats Git source requirement '{}', kind '{}', value '{}'",
                                 *url,
                                 *reference_kind,
                                 *reference_value));
            }
            git_requirements.insert(rstd::move(requirement), empty {});
            identity = git_source_identity(*url, *commit);
        } else {
            return failure<empty>(rstd::format("lock source has unsupported kind '{}'", *kind));
        }
        if (source_identities.contains_key(identity.as_str())) {
            return failure<empty>(
                rstd::format("lock repeats source identity '{}'", identity.as_str()));
        }
        source_identities.insert(rstd::move(identity), empty {});
    }

    auto names = StringSet::make();
    for (const auto& package : **packages) {
        auto package_known = reject_unknown(package, "lock package"_str, package_key_v4);
        if (package_known.is_err()) return package_known;
        auto name            = required_string(package, "name"_str, "lock package"_str);
        auto package_version = required_string(package, "version"_str, "lock package"_str);
        auto source          = required_string(package, "source"_str, "lock package"_str);
        auto manifest        = required_string(package, "manifest"_str, "lock package"_str);
        auto dependencies    = required_member(package, "dependencies"_str, "lock package"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (package_version.is_err()) {
            return Err(rstd::move(package_version).unwrap_err());
        }
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());
        if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
        if (! valid_package_name(*name)) {
            return failure<empty>(rstd::format("lock package name '{}' is invalid", *name));
        }
        if (names.contains_key(*name)) {
            return failure<empty>(rstd::format("lock repeats package name '{}'", *name));
        }
        if (package_version->is_empty()) {
            return failure<empty>(
                rstd::format("lock package '{}' version must not be empty", *name));
        }
        if (! source_identities.contains_key(*source)) {
            return failure<empty>(
                rstd::format("lock package '{}' references missing source '{}'", *name, *source));
        }
        if (! valid_source_manifest(*manifest)) {
            return failure<empty>(
                "lock package manifest must be a relative path without parent components"_str);
        }
        names.insert(String::make(*name), empty {});

        auto dependency_array = (**dependencies).as_array();
        if (dependency_array.is_none()) {
            return failure<empty>("lock package dependencies must be an array"_str);
        }
        auto dependency_names = StringSet::make();
        for (const auto& dependency : **dependency_array) {
            auto dependency_name = dependency.as_str();
            if (dependency_name.is_none()) {
                return failure<empty>("lock dependency name must be a string"_str);
            }
            if (! valid_package_name(*dependency_name)) {
                return failure<empty>(
                    rstd::format("lock dependency name '{}' is invalid", *dependency_name));
            }
            if (dependency_names.contains_key(*dependency_name)) {
                return failure<empty>(rstd::format(
                    "lock package '{}' repeats dependency '{}'", *name, *dependency_name));
            }
            dependency_names.insert(String::make(*dependency_name), empty {});
        }
    }

    auto root_names = StringSet::make();
    for (const auto& root : **roots) {
        auto name = root.as_str();
        if (name.is_none()) return failure<empty>("lock root name must be a string"_str);
        if (! names.contains_key(*name)) {
            return failure<empty>(
                rstd::format("lock root '{}' does not identify a package", *name));
        }
        if (root_names.contains_key(*name)) {
            return failure<empty>(rstd::format("lock repeats root name '{}'", *name));
        }
        root_names.insert(String::make(*name), empty {});
    }
    for (const auto& package : **packages) {
        auto dependencies = (**package.get("dependencies"_str)).as_array();
        for (const auto& dependency : **dependencies) {
            if (! names.contains_key(*dependency.as_str())) {
                return failure<empty>(rstd::format(
                    "lock dependency '{}' does not identify a package", *dependency.as_str()));
            }
        }
    }
    return Ok(empty {});
}

auto load_existing(ref<rstd::path::Path> path) -> Result<Option<Json>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return failure<Option<Json>>(
            rstd::format("cannot inspect lock '{}': {}", path, rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(Option<Json> {});
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return failure<Option<Json>>(
            rstd::format("cannot read lock '{}': {}", path, rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return failure<Option<Json>>(
            rstd::format("cannot parse lock '{}': {}", path, rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto version  = required_member(document, "version"_str, "lock root"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    auto number = (**version).as_u64();
    if (number.is_some() && *number == u64(1)) {
        auto valid = validate_lock_v1(document);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        return Ok(Some(rstd::move(document)));
    }
    if (number.is_some() && *number == u64(2)) {
        auto valid = validate_lock_v2(document);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        return Ok(Some(rstd::move(document)));
    }
    if (number.is_some() && *number == u64(3)) {
        auto valid = validate_lock_v3(document);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        return Ok(Some(rstd::move(document)));
    }
    if (number.is_none() || *number != u64(4)) {
        return failure<Option<Json>>("lock.version must be integer 1, 2, 3, or 4"_str);
    }
    auto valid = validate_lock_v4(document);
    if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
    return Ok(Some(rstd::move(document)));
}

auto write_lock(ref<rstd::path::Path> destination, const Json& desired) -> Result<empty> {
    auto text = rstd::json::to_string(
        desired, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii(u8('\n'));

    auto written = rstd::fs::write_atomic(destination, text.as_str().as_bytes());
    if (written.is_err()) {
        return failure<empty>(rstd::format("cannot atomically write lock '{}': {}",
                                           destination,
                                           rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

} // namespace tenon

export namespace tenon
{

class LockSession {
    bool                     locked_ { false };
    PathBuf                  destination_;
    Option<Json>             existing_;
    PackageResolutionOptions options_;

public:
    LockSession() = default;

    auto take_resolution_options() -> PackageResolutionOptions { return rstd::move(options_); }

    friend auto load_lock_session(ref<rstd::path::Path> root, bool locked) -> Result<LockSession>;
    friend auto sync_lock(const ResolvedPackageGraph& graph, LockSession session)
        -> Result<LockStatus>;
};

auto load_lock_session(ref<rstd::path::Path> root, bool locked) -> Result<LockSession> {
    auto destination = PathBuf::from(root).join(PathBuf::from("tenon.lock"_str).as_path());
    auto loaded      = load_existing(destination.as_path());
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto existing = rstd::move(loaded).unwrap();
    if (existing.is_none()) {
        if (locked) {
            return failure<LockSession>("--locked requires an existing tenon.lock"_str);
        }
        auto session         = LockSession {};
        session.destination_ = rstd::move(destination);
        return Ok(rstd::move(session));
    }

    auto version_value = (*existing).get("version"_str);
    auto version       = (**version_value).as_u64();
    if (version.is_none() || *version != u64(4)) {
        if (locked) {
            return failure<LockSession>(
                "--locked requires migrating tenon.lock with a non-locked build"_str);
        }
        auto session         = LockSession {};
        session.destination_ = rstd::move(destination);
        session.existing_    = rstd::move(existing);
        return Ok(rstd::move(session));
    }

    auto options = PackageResolutionOptions { .locked = locked };
    auto sources = (**(*existing).get("sources"_str)).as_array();
    for (const auto& source : **sources) {
        auto kind_value = source.get("kind"_str);
        auto kind       = *(**kind_value).as_str();
        if (kind != "git"_str) continue;
        const auto& reference            = **source.get("reference"_str);
        auto        reference_kind_value = reference.get("kind"_str);
        auto        reference_kind       = *(**reference_kind_value).as_str();
        auto        parsed_kind          = GitReferenceKind::DefaultBranch;
        if (reference_kind == "branch"_str) parsed_kind = GitReferenceKind::Branch;
        if (reference_kind == "tag"_str) parsed_kind = GitReferenceKind::Tag;
        if (reference_kind == "rev"_str) parsed_kind = GitReferenceKind::Rev;
        auto url             = source.get("url"_str);
        auto reference_value = reference.get("value"_str);
        auto commit          = source.get("commit"_str);
        options.git_sources.push(LockedGitSource {
            .git = String::make(*(**url).as_str()),
            .reference =
                GitReference {
                    .kind  = parsed_kind,
                    .value = String::make(*(**reference_value).as_str()),
                },
            .commit = String::make(*(**commit).as_str()),
        });
    }
    auto session         = LockSession {};
    session.locked_      = locked;
    session.destination_ = rstd::move(destination);
    session.existing_    = rstd::move(existing);
    session.options_     = rstd::move(options);
    return Ok(rstd::move(session));
}

auto sync_lock(const ResolvedPackageGraph& graph, LockSession session) -> Result<LockStatus> {
    auto desired_result = graph_json(graph);
    if (desired_result.is_err()) return Err(rstd::move(desired_result).unwrap_err());
    auto desired           = rstd::move(desired_result).unwrap();
    auto graph_destination = lock_path(graph);
    if (! (graph_destination.as_path().starts_with(session.destination_.as_path()) &&
           session.destination_.as_path().starts_with(graph_destination.as_path()))) {
        return failure<LockStatus>("lock session root does not match resolved graph root"_str);
    }
    if (session.locked_) {
        if (session.existing_.is_some() && *session.existing_ == desired) {
            return Ok(LockStatus::Unchanged);
        }
        return failure<LockStatus>("--locked forbids updating stale tenon.lock"_str);
    }

    if (session.existing_.is_some() && *session.existing_ == desired) {
        return Ok(LockStatus::Unchanged);
    }
    auto written = write_lock(session.destination_.as_path(), desired);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    return Ok(LockStatus::Updated);
}

} // namespace tenon
