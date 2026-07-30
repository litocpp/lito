export module tenon.lock_store;

import rstd;
import rstd.json;
import tenon.model;

using namespace rstd::literals;

namespace tenon::lock_store_detail
{

using Json = rstd::json::Value;
using Map = rstd::json::Map;
using Array = rstd::json::Array;
using StringSet = rstd::collections::BTreeMap<String, rstd::empty>;

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Lock, rstd::move(message)));
}

template<typename T>
auto failure(rstd::ref<rstd::str> message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Lock, message));
}

auto lock_path(const ResolvedPackageGraph& graph) -> PathBuf {
    return graph.root_directory.join(PathBuf::from("tenon.lock"_str).as_path());
}

auto path_string(rstd::ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("lock source path '{}' is not valid UTF-8", path));
    }
    return rstd::Ok(String::make(*text));
}

auto string_json(rstd::ref<rstd::str> value) -> Json {
    return Json::String(String::make(value));
}

auto graph_json(const ResolvedPackageGraph& graph) -> Result<Json> {
    auto packages = Array::make();
    for (const auto& package : graph.packages) {
        auto dependencies = Array::make();
        for (const auto& dependency : package.dependencies) {
            dependencies.push(string_json(dependency.package_id.as_str()));
        }
        auto source_path = path_string(package.source_directory.as_path());
        if (source_path.is_err()) return rstd::Err(rstd::move(source_path).unwrap_err());

        auto source = Map::make();
        source.insert(String::make("kind"_str), string_json("path"_str));
        source.insert(String::make("path"_str), string_json(source_path->as_str()));

        auto item = Map::make();
        item.insert(String::make("dependencies"_str), Json::Array(rstd::move(dependencies)));
        item.insert(String::make("id"_str), string_json(package.id.as_str()));
        item.insert(String::make("name"_str), string_json(package.manifest.name.as_str()));
        item.insert(String::make("source"_str), Json::Object(rstd::move(source)));
        item.insert(String::make("version"_str),
                    string_json(package.manifest.version.value->as_str()));
        packages.push(Json::Object(rstd::move(item)));
    }

    auto roots = Array::make();
    for (const auto& id : graph.root_ids) roots.push(string_json(id.as_str()));
    auto root = Map::make();
    root.insert(String::make("packages"_str), Json::Array(rstd::move(packages)));
    root.insert(String::make("roots"_str), Json::Array(rstd::move(roots)));
    root.insert(String::make("version"_str),
                Json::Number(rstd::json::Number::from_u64(rstd::u64(2))));
    return rstd::Ok(Json::Object(rstd::move(root)));
}

using KeyPredicate = bool (*)(rstd::ref<rstd::str>);

auto reject_unknown(const Json& value,
                    rstd::ref<rstd::str> context,
                    KeyPredicate allowed) -> Result<rstd::empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return failure<rstd::empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return failure<rstd::empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return rstd::Ok(rstd::empty {});
}

auto root_key_v1(rstd::ref<rstd::str> key) -> bool {
    return key == "packages"_str || key == "root"_str || key == "version"_str;
}

auto root_key_v2(rstd::ref<rstd::str> key) -> bool {
    return key == "packages"_str || key == "roots"_str || key == "version"_str;
}

auto package_key(rstd::ref<rstd::str> key) -> bool {
    return key == "dependencies"_str || key == "id"_str || key == "name"_str ||
           key == "source"_str || key == "version"_str;
}

auto source_key_v1(rstd::ref<rstd::str> key) -> bool {
    return key == "kind"_str || key == "manifest"_str || key == "path"_str;
}

auto source_key_v2(rstd::ref<rstd::str> key) -> bool {
    return key == "kind"_str || key == "path"_str;
}

auto required_member(const Json& value,
                     rstd::ref<rstd::str> key,
                     rstd::ref<rstd::str> context) -> Result<rstd::ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return failure<rstd::ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return rstd::Ok(*member);
}

auto required_string(const Json& value,
                     rstd::ref<rstd::str> key,
                     rstd::ref<rstd::str> context) -> Result<rstd::ref<rstd::str>> {
    auto member = required_member(value, key, context);
    if (member.is_err()) return rstd::Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) {
        return failure<rstd::ref<rstd::str>>(
            rstd::format("{}.{} must be a string", context, key));
    }
    return rstd::Ok(*text);
}

auto validate_lock_v1(const Json& document) -> Result<rstd::empty> {
    auto known = reject_unknown(document, "lock root"_str, root_key_v1);
    if (known.is_err()) return known;
    auto version = required_member(document, "version"_str, "lock root"_str);
    auto root = required_string(document, "root"_str, "lock root"_str);
    auto packages_value = required_member(document, "packages"_str, "lock root"_str);
    if (version.is_err()) return rstd::Err(rstd::move(version).unwrap_err());
    if (root.is_err()) return rstd::Err(rstd::move(root).unwrap_err());
    if (packages_value.is_err()) return rstd::Err(rstd::move(packages_value).unwrap_err());
    auto version_number = (**version).as_u64();
    if (version_number.is_none() || *version_number != rstd::u64(1)) {
        return failure<rstd::empty>("lock.version must be integer 1"_str);
    }
    auto packages = (**packages_value).as_array();
    if (packages.is_none()) {
        return failure<rstd::empty>("lock.packages must be an array"_str);
    }

    auto ids = StringSet::make();
    for (const auto& package : **packages) {
        auto package_known = reject_unknown(package, "lock package"_str, package_key);
        if (package_known.is_err()) return package_known;
        auto id = required_string(package, "id"_str, "lock package"_str);
        auto name = required_string(package, "name"_str, "lock package"_str);
        auto package_version = required_string(package, "version"_str, "lock package"_str);
        auto source = required_member(package, "source"_str, "lock package"_str);
        auto dependencies =
            required_member(package, "dependencies"_str, "lock package"_str);
        if (id.is_err()) return rstd::Err(rstd::move(id).unwrap_err());
        if (name.is_err()) return rstd::Err(rstd::move(name).unwrap_err());
        if (package_version.is_err()) {
            return rstd::Err(rstd::move(package_version).unwrap_err());
        }
        if (source.is_err()) return rstd::Err(rstd::move(source).unwrap_err());
        if (dependencies.is_err()) return rstd::Err(rstd::move(dependencies).unwrap_err());
        if (ids.contains_key(*id)) {
            return failure<rstd::empty>(rstd::format("lock repeats package id '{}'", *id));
        }
        ids.insert(String::make(*id), rstd::empty {});

        auto source_known = reject_unknown(**source, "lock package source"_str, source_key_v1);
        if (source_known.is_err()) return source_known;
        auto kind = required_string(**source, "kind"_str, "lock package source"_str);
        auto manifest = required_string(**source, "manifest"_str, "lock package source"_str);
        auto path = required_string(**source, "path"_str, "lock package source"_str);
        if (kind.is_err()) return rstd::Err(rstd::move(kind).unwrap_err());
        if (manifest.is_err()) return rstd::Err(rstd::move(manifest).unwrap_err());
        if (path.is_err()) return rstd::Err(rstd::move(path).unwrap_err());
        if (*kind != "path"_str) {
            return failure<rstd::empty>("lock source kind must be path"_str);
        }

        auto dependency_array = (**dependencies).as_array();
        if (dependency_array.is_none()) {
            return failure<rstd::empty>("lock package dependencies must be an array"_str);
        }
        for (const auto& dependency : **dependency_array) {
            if (dependency.as_str().is_none()) {
                return failure<rstd::empty>("lock dependency id must be a string"_str);
            }
        }
    }
    if (! ids.contains_key(*root)) {
        return failure<rstd::empty>("lock.root does not identify a package"_str);
    }
    for (const auto& package : **packages) {
        auto dependencies = (**package.get("dependencies"_str)).as_array();
        for (const auto& dependency : **dependencies) {
            if (! ids.contains_key(*dependency.as_str())) {
                return failure<rstd::empty>(rstd::format(
                    "lock dependency '{}' does not identify a package", *dependency.as_str()));
            }
        }
    }
    return rstd::Ok(rstd::empty {});
}

auto validate_lock_v2(const Json& document) -> Result<rstd::empty> {
    auto known = reject_unknown(document, "lock root"_str, root_key_v2);
    if (known.is_err()) return known;
    auto version = required_member(document, "version"_str, "lock root"_str);
    auto roots_value = required_member(document, "roots"_str, "lock root"_str);
    auto packages_value = required_member(document, "packages"_str, "lock root"_str);
    if (version.is_err()) return rstd::Err(rstd::move(version).unwrap_err());
    if (roots_value.is_err()) return rstd::Err(rstd::move(roots_value).unwrap_err());
    if (packages_value.is_err()) return rstd::Err(rstd::move(packages_value).unwrap_err());
    auto version_number = (**version).as_u64();
    if (version_number.is_none() || *version_number != rstd::u64(2)) {
        return failure<rstd::empty>("lock.version must be integer 2"_str);
    }
    auto roots = (**roots_value).as_array();
    auto packages = (**packages_value).as_array();
    if (roots.is_none() || (**roots).is_empty()) {
        return failure<rstd::empty>("lock.roots must be a non-empty array"_str);
    }
    if (packages.is_none()) {
        return failure<rstd::empty>("lock.packages must be an array"_str);
    }

    auto ids = StringSet::make();
    for (const auto& package : **packages) {
        auto package_known = reject_unknown(package, "lock package"_str, package_key);
        if (package_known.is_err()) return package_known;
        auto id = required_string(package, "id"_str, "lock package"_str);
        auto name = required_string(package, "name"_str, "lock package"_str);
        auto package_version = required_string(package, "version"_str, "lock package"_str);
        auto source = required_member(package, "source"_str, "lock package"_str);
        auto dependencies = required_member(package, "dependencies"_str, "lock package"_str);
        if (id.is_err()) return rstd::Err(rstd::move(id).unwrap_err());
        if (name.is_err()) return rstd::Err(rstd::move(name).unwrap_err());
        if (package_version.is_err()) {
            return rstd::Err(rstd::move(package_version).unwrap_err());
        }
        if (source.is_err()) return rstd::Err(rstd::move(source).unwrap_err());
        if (dependencies.is_err()) return rstd::Err(rstd::move(dependencies).unwrap_err());
        if (ids.contains_key(*id)) {
            return failure<rstd::empty>(rstd::format("lock repeats package id '{}'", *id));
        }
        ids.insert(String::make(*id), rstd::empty {});

        auto source_known = reject_unknown(**source, "lock package source"_str, source_key_v2);
        if (source_known.is_err()) return source_known;
        auto kind = required_string(**source, "kind"_str, "lock package source"_str);
        auto path = required_string(**source, "path"_str, "lock package source"_str);
        if (kind.is_err()) return rstd::Err(rstd::move(kind).unwrap_err());
        if (path.is_err()) return rstd::Err(rstd::move(path).unwrap_err());
        if (*kind != "path"_str) {
            return failure<rstd::empty>("lock source kind must be path"_str);
        }

        auto dependency_array = (**dependencies).as_array();
        if (dependency_array.is_none()) {
            return failure<rstd::empty>("lock package dependencies must be an array"_str);
        }
        for (const auto& dependency : **dependency_array) {
            if (dependency.as_str().is_none()) {
                return failure<rstd::empty>("lock dependency id must be a string"_str);
            }
        }
    }
    auto root_ids = StringSet::make();
    for (const auto& root : **roots) {
        auto id = root.as_str();
        if (id.is_none()) return failure<rstd::empty>("lock root id must be a string"_str);
        if (! ids.contains_key(*id)) {
            return failure<rstd::empty>(rstd::format(
                "lock root '{}' does not identify a package", *id));
        }
        if (root_ids.contains_key(*id)) {
            return failure<rstd::empty>(rstd::format("lock repeats root id '{}'", *id));
        }
        root_ids.insert(String::make(*id), rstd::empty {});
    }
    for (const auto& package : **packages) {
        auto dependencies = (**package.get("dependencies"_str)).as_array();
        for (const auto& dependency : **dependencies) {
            if (! ids.contains_key(*dependency.as_str())) {
                return failure<rstd::empty>(rstd::format(
                    "lock dependency '{}' does not identify a package", *dependency.as_str()));
            }
        }
    }
    return rstd::Ok(rstd::empty {});
}

auto load_existing(rstd::ref<rstd::path::Path> path) -> Result<rstd::Option<Json>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return failure<rstd::Option<Json>>(rstd::format(
            "cannot inspect lock '{}': {}", path, rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return rstd::Ok(rstd::Option<Json> {});
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return failure<rstd::Option<Json>>(rstd::format(
            "cannot read lock '{}': {}", path, rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return failure<rstd::Option<Json>>(rstd::format(
            "cannot parse lock '{}': {}", path, rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto version = required_member(document, "version"_str, "lock root"_str);
    if (version.is_err()) return rstd::Err(rstd::move(version).unwrap_err());
    auto number = (**version).as_u64();
    auto valid = number.is_some() && *number == rstd::u64(1)
                     ? validate_lock_v1(document)
                     : validate_lock_v2(document);
    if (valid.is_err()) return rstd::Err(rstd::move(valid).unwrap_err());
    return rstd::Ok(rstd::Some(rstd::move(document)));
}

auto cleanup_temp(rstd::ref<rstd::path::Path> path) noexcept -> void {
    auto removed = rstd::fs::remove_file(path);
    (void)removed;
}

auto write_lock(const ResolvedPackageGraph& graph,
                rstd::ref<rstd::path::Path> destination,
                const Json& desired) -> Result<rstd::empty> {
    auto text = rstd::json::to_string(
        desired, rstd::json::FormatOptions { .pretty = true, .indent = rstd::usize(2) });
    text.push_ascii(rstd::u8('\n'));

    auto temp_file = rstd::Option<rstd::fs::File> {};
    auto temp_path = PathBuf::make();
    for (rstd::usize index {}; index < rstd::usize(64); ++index) {
        auto name = rstd::format("tenon.lock.tmp.{}", index);
        auto candidate = graph.root_directory.join(PathBuf::from(name.as_str()).as_path());
        auto created = rstd::fs::File::create_new(candidate.as_path());
        if (created.is_ok()) {
            temp_path = rstd::move(candidate);
            temp_file = rstd::Some(rstd::move(created).unwrap());
            break;
        }
        auto cause = rstd::move(created).unwrap_err();
        if (cause.kind() != rstd::io::error::ErrorKind {
                                rstd::io::error::ErrorKind::AlreadyExists }) {
            return failure<rstd::empty>(rstd::format(
                "cannot create lock temp '{}': {}", candidate.as_path(), cause));
        }
    }
    if (temp_file.is_none()) {
        return failure<rstd::empty>("cannot allocate a sibling lock temp file"_str);
    }
    auto bytes = text.as_str().as_bytes();
    auto written = rstd::io::write_all(*temp_file, bytes);
    if (written.is_err()) {
        cleanup_temp(temp_path.as_path());
        return failure<rstd::empty>(rstd::format(
            "cannot write lock temp '{}': {}", temp_path.as_path(), rstd::move(written).unwrap_err()));
    }
    auto flushed = (*temp_file).flush();
    if (flushed.is_err()) {
        cleanup_temp(temp_path.as_path());
        return failure<rstd::empty>(rstd::format(
            "cannot flush lock temp '{}': {}", temp_path.as_path(), rstd::move(flushed).unwrap_err()));
    }
    auto synced = (*temp_file).sync_all();
    if (synced.is_err()) {
        cleanup_temp(temp_path.as_path());
        return failure<rstd::empty>(rstd::format(
            "cannot sync lock temp '{}': {}", temp_path.as_path(), rstd::move(synced).unwrap_err()));
    }
    temp_file = rstd::None();
    auto renamed = rstd::fs::rename(temp_path.as_path(), destination);
    if (renamed.is_err()) {
        cleanup_temp(temp_path.as_path());
        return failure<rstd::empty>(rstd::format(
            "cannot replace lock '{}': {}", destination, rstd::move(renamed).unwrap_err()));
    }
    return rstd::Ok(rstd::empty {});
}

} // namespace tenon::lock_store_detail

export namespace tenon
{

auto sync_lock(const ResolvedPackageGraph& graph, bool locked) -> Result<LockStatus> {
    using namespace lock_store_detail;

    auto desired_result = graph_json(graph);
    if (desired_result.is_err()) return rstd::Err(rstd::move(desired_result).unwrap_err());
    auto desired = rstd::move(desired_result).unwrap();
    auto destination = lock_path(graph);
    if (locked) {
        auto existing_result = load_existing(destination.as_path());
        if (existing_result.is_err()) {
            return rstd::Err(rstd::move(existing_result).unwrap_err());
        }
        auto existing = rstd::move(existing_result).unwrap();
        if (existing.is_some() && *existing == desired) {
            return rstd::Ok(LockStatus::Unchanged);
        }
        return failure<LockStatus>(existing.is_none()
                                       ? String::make(
                                             "--locked requires an existing tenon.lock"_str)
                                       : String::make(
                                             "--locked forbids updating stale tenon.lock"_str));
    }

    auto existing_result = load_existing(destination.as_path());
    if (existing_result.is_err()) return rstd::Err(rstd::move(existing_result).unwrap_err());
    auto existing = rstd::move(existing_result).unwrap();
    if (existing.is_some() && *existing == desired) {
        return rstd::Ok(LockStatus::Unchanged);
    }
    auto written = write_lock(graph, destination.as_path(), desired);
    if (written.is_err()) return rstd::Err(rstd::move(written).unwrap_err());
    return rstd::Ok(LockStatus::Updated);
}

} // namespace tenon
