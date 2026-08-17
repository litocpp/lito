module;
#include <rstd/macro.hpp>

module lito.core;

import rstd;
import rstd.json;
import :lock;
import :package.graph;
import :dependency.source;
import :manifest;
import :source.git;
import :source.requirement;
import :source.resolution;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using Json         = rstd::json::Value;
using Map          = rstd::json::Map;
using Array        = rstd::json::Array;
using StringSet    = rstd::collections::BTreeMap<String, empty>;
using KeyPredicate = bool (*)(ref<str>);
using namespace lito::lock;

template<typename T>
auto lock_failure(String message) -> LockResult<T> {
    return Err(LockError::Schema(rstd::move(message)));
}

template<typename T>
auto lock_failure(ref<str> message) -> LockResult<T> {
    return Err(LockError::Schema(String::make(message)));
}

template<typename T>
auto lock_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> LockResult<T> {
    return Err(LockError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto lock_path(ref<rstd::path::Path> root, const LockConfig& config) -> PathBuf {
    if (! config.path.is_empty()) {
        if (config.path.as_path().is_absolute()) return config.path.clone();
        return PathBuf::from(root).join(config.path.as_path());
    }
    return PathBuf::from(root).join(PathBuf::from("lito.lock"_str).as_path());
}

auto path_string(ref<rstd::path::Path> path) -> LockResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return lock_failure<String>(rstd::format("lock source path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto string_json(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto reference_json(const lito::source::GitReference& value) -> Json {
    auto reference = Map::make();
    reference.insert(String::make("kind"_str),
                     string_json(lito::source::git_reference_kind_name(value.kind)));
    reference.insert(String::make("value"_str), string_json(value.value.as_str()));
    return Json::Object(rstd::move(reference));
}

auto package_source_json(const lito::source::ResolvedPackageSource& source) -> LockResult<Json> {
    auto item = Map::make();
    if (source.kind == lito::source::PackageSourceKind::Path) {
        auto path = rstd_try(path_string(source.path.as_path()));
        item.insert(String::make("kind"_str), string_json("path"_str));
        item.insert(String::make("path"_str), string_json(path.as_str()));
    } else {
        item.insert(String::make("commit"_str), string_json(source.commit.as_str()));
        item.insert(String::make("kind"_str), string_json("git"_str));
        item.insert(String::make("reference"_str), reference_json(source.reference));
        item.insert(String::make("url"_str), string_json(source.git.as_str()));
    }
    return Ok(Json::Object(rstd::move(item)));
}

auto external_source_json(const lito::dependency::ResolvedExternalSource& source)
    -> LockResult<Json> {
    auto item = Map::make();
    if (source.is_Path()) {
        auto path = rstd_try(path_string(source.as_Path().path.as_path()));
        item.insert(String::make("kind"_str), string_json("path"_str));
        item.insert(String::make("path"_str), string_json(path.as_str()));
    } else if (source.is_Package()) {
        auto path = rstd_try(path_string(source.as_Package().path.as_path()));
        item.insert(String::make("kind"_str), string_json("package"_str));
        item.insert(String::make("path"_str), string_json(path.as_str()));
    } else if (source.is_Git()) {
        item.insert(String::make("commit"_str), string_json(source.as_Git().commit.as_str()));
        item.insert(String::make("kind"_str), string_json("git"_str));
        item.insert(String::make("reference"_str), reference_json(source.as_Git().reference));
        item.insert(String::make("url"_str), string_json(source.as_Git().url.as_str()));
    } else {
        item.insert(String::make("kind"_str), string_json("archive"_str));
        item.insert(String::make("sha256"_str), string_json(source.as_Archive().sha256.as_str()));
        item.insert(String::make("url"_str), string_json(source.as_Archive().url.as_str()));
    }
    return Ok(Json::Object(rstd::move(item)));
}

auto build_tool_metadata_json(const lito::dependency::ResolvedBuildToolSourceMetadata& metadata)
    -> LockResult<Json> {
    auto executable = rstd_try(path_string(metadata.executable.as_path()));
    auto item       = Map::make();
    item.insert(String::make("executable"_str), string_json(executable.as_str()));
    item.insert(String::make("operating-system"_str),
                string_json(metadata.operating_system.as_str()));
    item.insert(String::make("schema-version"_str),
                Json::Number(rstd::json::Number::from_u64(metadata.schema_version)));
    item.insert(String::make("version"_str), string_json(metadata.version.as_str()));
    return Ok(Json::Object(rstd::move(item)));
}

auto external_order_key(const lito::dependency::ResolvedExternalSourceRecord& external) -> String {
    auto architectures = Vec<String>::with_capacity(external.architectures.len());
    for (const auto& architecture : external.architectures) {
        architectures.push(architecture.name.clone());
    }
    rstd::slice_::sort_unstable(architectures.as_mut_slice().as_mut_ref());
    auto key = rstd::format("{}\n{}\n{}",
                            external.package.as_str(),
                            external.alias.as_str(),
                            external.provider.as_str());
    for (const auto& architecture : architectures) {
        key.push_ascii(u8('\n'));
        key.push_str(architecture.as_str());
    }
    return key;
}

auto graph_json(const lito::package::ResolvedPackageGraph& graph) -> LockResult<Json> {
    auto package_indices = Vec<usize>::with_capacity(graph.packages.len());
    for (usize index {}; index < graph.packages.len(); ++index) package_indices.push(usize(index));
    rstd::slice_::sort_unstable_by(
        package_indices.as_mut_slice().as_mut_ref(), [&graph](usize left, usize right) {
            return graph.packages[left].manifest.name < graph.packages[right].manifest.name;
        });
    auto packages = Array::make();
    for (const auto index : package_indices) {
        const auto& package = graph.packages[index];
        auto        dependency_names =
            Vec<String>::with_capacity(package.dependencies.len() + package.dev_dependencies.len());
        for (const auto& dependency : package.dependencies) {
            dependency_names.push(dependency.name.clone());
        }
        for (const auto& dependency : package.dev_dependencies) {
            dependency_names.push(dependency.name.clone());
        }
        rstd::slice_::sort_unstable(dependency_names.as_mut_slice().as_mut_ref());
        auto dependencies = Array::make();
        for (const auto& dependency : dependency_names) {
            dependencies.push(string_json(dependency.as_str()));
        }
        auto manifest = path_string(package.source_manifest.as_path());
        if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());

        auto item = Map::make();
        item.insert(String::make("dependencies"_str), Json::Array(rstd::move(dependencies)));
        auto runtime_dependency_names =
            Vec<String>::with_capacity(package.runtime_dependencies.len());
        for (const auto& dependency : package.runtime_dependencies) {
            runtime_dependency_names.push(dependency.name.clone());
        }
        rstd::slice_::sort_unstable(runtime_dependency_names.as_mut_slice().as_mut_ref());
        auto runtime_dependencies = Array::make();
        for (const auto& dependency : runtime_dependency_names) {
            runtime_dependencies.push(string_json(dependency.as_str()));
        }
        item.insert(String::make("runtime-dependencies"_str),
                    Json::Array(rstd::move(runtime_dependencies)));
        item.insert(String::make("manifest"_str), string_json(manifest->as_str()));
        item.insert(String::make("name"_str), string_json(package.manifest.name.as_str()));
        auto source = rstd_try(package_source_json(package.source));
        item.insert(String::make("source"_str), rstd::move(source));
        if (package.manifest.version.value.is_some()) {
            item.insert(String::make("version"_str),
                        string_json(package.manifest.version.value->as_str()));
        }
        packages.push(Json::Object(rstd::move(item)));
    }

    auto external_indices = Vec<usize>::with_capacity(graph.externals.len());
    for (usize index {}; index < graph.externals.len(); ++index)
        external_indices.push(usize(index));
    rstd::slice_::sort_unstable_by(external_indices.as_mut_slice().as_mut_ref(),
                                   [&graph](usize left, usize right) {
                                       return external_order_key(graph.externals[left]) <
                                              external_order_key(graph.externals[right]);
                                   });
    auto externals = Array::make();
    for (const auto index : external_indices) {
        const auto& external           = graph.externals[index];
        auto        architecture_names = Vec<String>::with_capacity(external.architectures.len());
        for (const auto& architecture : external.architectures) {
            architecture_names.push(architecture.name.clone());
        }
        rstd::slice_::sort_unstable(architecture_names.as_mut_slice().as_mut_ref());
        auto architectures = Array::make();
        for (const auto& architecture : architecture_names) {
            architectures.push(string_json(architecture.as_str()));
        }
        auto item = Map::make();
        item.insert(String::make("alias"_str), string_json(external.alias.as_str()));
        if (! architectures.is_empty()) {
            item.insert(String::make("architectures"_str), Json::Array(rstd::move(architectures)));
        }
        item.insert(String::make("package"_str), string_json(external.package.as_str()));
        item.insert(String::make("provider"_str), string_json(external.provider.as_str()));
        if (external.build_tool.is_some()) {
            auto metadata = rstd_try(build_tool_metadata_json(*external.build_tool));
            item.insert(String::make("build-tool"_str), rstd::move(metadata));
        }
        auto source = rstd_try(external_source_json(external.source));
        item.insert(String::make("source"_str), rstd::move(source));
        externals.push(Json::Object(rstd::move(item)));
    }

    auto root = Map::make();
    root.insert(String::make("externals"_str), Json::Array(rstd::move(externals)));
    root.insert(String::make("packages"_str), Json::Array(rstd::move(packages)));
    root.insert(String::make("version"_str),
                Json::Number(rstd::json::Number::from_u64(LOCK_FORMAT_VERSION)));
    return Ok(Json::Object(rstd::move(root)));
}

auto reject_unknown(const Json& value, ref<str> context, KeyPredicate allowed)
    -> LockResult<empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return lock_failure<empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return lock_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto lock_package_key(ref<str> key) -> bool {
    return key == "dependencies"_str || key == "manifest"_str || key == "name"_str ||
           key == "runtime-dependencies"_str || key == "source"_str || key == "version"_str;
}

auto path_source_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "path"_str;
}

auto git_source_key(ref<str> key) -> bool {
    return key == "commit"_str || key == "kind"_str || key == "reference"_str || key == "url"_str;
}

auto reference_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "value"_str;
}

auto required_member(const Json& value, ref<str> key, ref<str> context) -> LockResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return lock_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context) -> LockResult<ref<str>> {
    auto member = required_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) {
        return lock_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto valid_source_manifest(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    return PathBuf::from(value).as_path().is_safe_relative();
}

auto root_key(ref<str> key) -> bool {
    return key == "externals"_str || key == "packages"_str || key == "version"_str;
}

auto external_key(ref<str> key) -> bool {
    return key == "alias"_str || key == "architectures"_str || key == "build-tool"_str ||
           key == "package"_str || key == "provider"_str || key == "source"_str;
}

auto build_tool_metadata_key(ref<str> key) -> bool {
    return key == "executable"_str || key == "operating-system"_str ||
           key == "schema-version"_str || key == "version"_str;
}

auto archive_source_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "sha256"_str || key == "url"_str;
}

auto hexadecimal(ref<str> value, usize length) -> bool {
    if (value.len() != length) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'f') ||
               (ascii >= 'A' && ascii <= 'F'))) {
            return false;
        }
    }
    return true;
}

auto lowercase_hexadecimal(ref<str> value, usize length) -> bool {
    if (value.len() != length) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'f'))) return false;
    }
    return true;
}

auto valid_fetch_url(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (ascii < 0x20 || ascii == 0x7f) return false;
    }
    return true;
}

auto valid_lock_source_path(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    return ! PathBuf::from(value).as_path().is_absolute();
}

auto validate_reference(const Json& value, ref<str> context, ref<str> commit) -> LockResult<empty> {
    rstd_try(reject_unknown(value, context, reference_key));
    auto       kind              = rstd_try(required_string(value, "kind"_str, context));
    auto       text              = rstd_try(required_string(value, "value"_str, context));
    const auto default_reference = kind == "default"_str;
    const auto named_reference =
        kind == "branch"_str || kind == "tag"_str || kind == "rev"_str || kind == "commit"_str;
    if (! default_reference && ! named_reference) {
        return lock_failure<empty>(
            rstd::format("{}.kind must be default, branch, tag, rev, or commit", context));
    }
    if (default_reference != text.is_empty()) {
        return lock_failure<empty>(
            rstd::format("{}.value must be empty only for default", context));
    }
    if (kind == "commit"_str && text != commit) {
        return lock_failure<empty>(
            rstd::format("{} commit reference must match the resolved commit", context));
    }
    return Ok(empty {});
}

auto validate_source(const Json& value, ref<str> context, bool external_source)
    -> LockResult<empty> {
    auto kind = rstd_try(required_string(value, "kind"_str, context));
    if (kind == "path"_str) {
        rstd_try(reject_unknown(value, context, path_source_key));
        auto path = rstd_try(required_string(value, "path"_str, context));
        if (! valid_lock_source_path(path)) {
            return lock_failure<empty>(
                rstd::format("{}.path must be a non-empty relative path", context));
        }
        return Ok(empty {});
    }
    if (kind == "package"_str && external_source) {
        rstd_try(reject_unknown(value, context, path_source_key));
        auto path = rstd_try(required_string(value, "path"_str, context));
        if (path.is_empty() || ! PathBuf::from(path).as_path().is_safe_relative()) {
            return lock_failure<empty>(
                rstd::format("{}.path must be a safe non-empty package-relative path", context));
        }
        return Ok(empty {});
    }
    if (kind == "git"_str) {
        rstd_try(reject_unknown(value, context, git_source_key));
        auto url       = rstd_try(required_string(value, "url"_str, context));
        auto commit    = rstd_try(required_string(value, "commit"_str, context));
        auto reference = rstd_try(required_member(value, "reference"_str, context));
        if (! valid_fetch_url(url)) {
            return lock_failure<empty>(rstd::format("{}.url must not be empty", context));
        }
        if (! lito::source::git_commit_is_valid(commit)) {
            return lock_failure<empty>(
                rstd::format("{}.commit must be a full hexadecimal object id", context));
        }
        return validate_reference(*reference, "lock Git reference"_str, commit);
    }
    if (kind == "archive"_str && external_source) {
        rstd_try(reject_unknown(value, context, archive_source_key));
        auto url    = rstd_try(required_string(value, "url"_str, context));
        auto sha256 = rstd_try(required_string(value, "sha256"_str, context));
        if (! valid_fetch_url(url)) {
            return lock_failure<empty>(rstd::format("{}.url must not be empty", context));
        }
        if (! lowercase_hexadecimal(sha256, usize(64))) {
            return lock_failure<empty>(
                rstd::format("{}.sha256 must be 64 lowercase hexadecimal digits", context));
        }
        return Ok(empty {});
    }
    return lock_failure<empty>(rstd::format("{} has unsupported kind '{}'", context, kind));
}

auto validate_current_lock(const Json& document) -> LockResult<empty> {
    rstd_try(reject_unknown(document, "lock root"_str, root_key));
    auto version_value   = rstd_try(required_member(document, "version"_str, "lock root"_str));
    auto packages_value  = rstd_try(required_member(document, "packages"_str, "lock root"_str));
    auto externals_value = rstd_try(required_member(document, "externals"_str, "lock root"_str));
    auto version         = version_value->as_u64();
    if (version.is_none()) {
        return lock_failure<empty>(
            rstd::format("lock.version must be integer {}", LOCK_FORMAT_VERSION));
    }
    if (*version != LOCK_FORMAT_VERSION) {
        return lock_failure<empty>(
            rstd::format("lock.version {} is not supported; this Lito supports version {}",
                         *version,
                         LOCK_FORMAT_VERSION));
    }
    auto packages  = packages_value->as_array();
    auto externals = externals_value->as_array();
    if (packages.is_none() || (**packages).is_empty()) {
        return lock_failure<empty>("lock.packages must be a non-empty array"_str);
    }
    if (externals.is_none()) {
        return lock_failure<empty>("lock.externals must be an array"_str);
    }

    auto names = StringSet::make();
    for (const auto& package : **packages) {
        rstd_try(reject_unknown(package, "lock package"_str, lock_package_key));
        auto name     = rstd_try(required_string(package, "name"_str, "lock package"_str));
        auto source   = rstd_try(required_member(package, "source"_str, "lock package"_str));
        auto manifest = rstd_try(required_string(package, "manifest"_str, "lock package"_str));
        auto dependencies =
            rstd_try(required_member(package, "dependencies"_str, "lock package"_str));
        auto runtime_dependencies =
            rstd_try(required_member(package, "runtime-dependencies"_str, "lock package"_str));
        if (! lito::manifest::valid_package_name(name)) {
            return lock_failure<empty>(rstd::format("lock package name '{}' is invalid", name));
        }
        if (names.contains_key(name)) {
            return lock_failure<empty>(rstd::format("lock repeats package name '{}'", name));
        }
        names.insert(String::make(name), empty {});
        auto version = package.get("version"_str);
        if (version.is_some()) {
            auto text = (**version).as_str();
            if (text.is_none() || text->is_empty()) {
                return lock_failure<empty>(
                    rstd::format("lock package '{}' version must be a non-empty string", name));
            }
        }
        if (! valid_source_manifest(manifest)) {
            return lock_failure<empty>(
                "lock package manifest must be a relative path without parent components"_str);
        }
        rstd_try(validate_source(*source, "lock package source"_str, false));
        auto dependency_array = dependencies->as_array();
        if (dependency_array.is_none()) {
            return lock_failure<empty>("lock package dependencies must be an array"_str);
        }
        auto dependency_names = StringSet::make();
        for (const auto& dependency : **dependency_array) {
            auto dependency_name = dependency.as_str();
            if (dependency_name.is_none() ||
                ! lito::manifest::valid_package_name(*dependency_name)) {
                return lock_failure<empty>("lock dependency name must be a valid package name"_str);
            }
            if (dependency_names.contains_key(*dependency_name)) {
                return lock_failure<empty>(rstd::format(
                    "lock package '{}' repeats dependency '{}'", name, *dependency_name));
            }
            dependency_names.insert(String::make(*dependency_name), empty {});
        }
        auto runtime_dependency_array = runtime_dependencies->as_array();
        if (runtime_dependency_array.is_none()) {
            return lock_failure<empty>("lock package runtime-dependencies must be an array"_str);
        }
        auto runtime_dependency_names = StringSet::make();
        for (const auto& dependency : **runtime_dependency_array) {
            auto dependency_name = dependency.as_str();
            if (dependency_name.is_none() ||
                ! lito::manifest::valid_package_name(*dependency_name)) {
                return lock_failure<empty>(
                    "lock runtime dependency name must be a valid package name"_str);
            }
            if (runtime_dependency_names.contains_key(*dependency_name)) {
                return lock_failure<empty>(rstd::format(
                    "lock package '{}' repeats runtime dependency '{}'", name, *dependency_name));
            }
            runtime_dependency_names.insert(String::make(*dependency_name), empty {});
        }
    }
    for (const auto& package : **packages) {
        const auto  name         = *(**package.get("name"_str)).as_str();
        const auto& dependencies = **package.get("dependencies"_str);
        for (const auto& dependency : **dependencies.as_array()) {
            if (! names.contains_key(*dependency.as_str())) {
                return lock_failure<empty>(
                    rstd::format("lock package '{}' dependency '{}' does not identify a package",
                                 name,
                                 *dependency.as_str()));
            }
        }
        const auto& runtime_dependencies = **package.get("runtime-dependencies"_str);
        for (const auto& dependency : **runtime_dependencies.as_array()) {
            if (! names.contains_key(*dependency.as_str())) {
                return lock_failure<empty>(rstd::format(
                    "lock package '{}' runtime dependency '{}' does not identify a package",
                    name,
                    *dependency.as_str()));
            }
        }
    }

    auto identities = StringSet::make();
    for (const auto& external : **externals) {
        rstd_try(reject_unknown(external, "lock external"_str, external_key));
        auto package  = rstd_try(required_string(external, "package"_str, "lock external"_str));
        auto alias    = rstd_try(required_string(external, "alias"_str, "lock external"_str));
        auto provider = rstd_try(required_string(external, "provider"_str, "lock external"_str));
        auto source   = rstd_try(required_member(external, "source"_str, "lock external"_str));
        if (! names.contains_key(package)) {
            return lock_failure<empty>(rstd::format(
                "lock external '{}:{}' references missing package '{}'", package, alias, package));
        }
        if (alias.is_empty() || provider.is_empty()) {
            return lock_failure<empty>("lock external alias and provider must not be empty"_str);
        }
        auto architecture_key = String::make();
        auto architectures    = external.get("architectures"_str);
        if (architectures.is_some()) {
            auto values = (**architectures).as_array();
            if (values.is_none() || (**values).is_empty()) {
                return lock_failure<empty>(
                    "lock external architectures must be a non-empty array when present"_str);
            }
            auto seen     = StringSet::make();
            auto previous = Option<String> {};
            for (const auto& architecture : **values) {
                auto name = architecture.as_str();
                if (name.is_none()) {
                    return lock_failure<empty>("lock external architecture must be a string"_str);
                }
                auto canonical = canonical_architecture(*name);
                if (canonical.is_err() || canonical->as_str() != *name) {
                    return lock_failure<empty>(
                        rstd::format("lock external architecture '{}' must be canonical", *name));
                }
                if (seen.contains_key(*name)) {
                    return lock_failure<empty>(
                        rstd::format("lock external repeats architecture '{}'", *name));
                }
                auto canonical_name = String::make(*name);
                if (previous.is_some() && canonical_name < *previous) {
                    return lock_failure<empty>(
                        "lock external architectures must use stable sorted order"_str);
                }
                seen.insert(canonical_name.clone(), empty {});
                previous = Some(rstd::move(canonical_name));
                if (! architecture_key.is_empty()) architecture_key.push_ascii(u8(','));
                architecture_key.push_str(*name);
            }
        }
        auto identity =
            rstd::format("{}\n{}\n{}\n{}", package, alias, provider, architecture_key.as_str());
        if (identities.contains_key(identity.as_str())) {
            return lock_failure<empty>(
                rstd::format("lock repeats external '{}:{}' for architectures '{}'",
                             package,
                             alias,
                             architecture_key.as_str()));
        }
        identities.insert(rstd::move(identity), empty {});
        rstd_try(validate_source(*source, "lock external source"_str, true));
        auto       build_tool    = external.get("build-tool"_str);
        const auto tool_provider = provider.starts_with("build-tool:"_str);
        if (tool_provider != build_tool.is_some()) {
            return lock_failure<empty>(
                "lock build-tool external must contain build-tool metadata"_str);
        }
        if (build_tool.is_some()) {
            rstd_try(reject_unknown(
                **build_tool, "lock build-tool metadata"_str, build_tool_metadata_key));
            auto version = rstd_try(
                required_string(**build_tool, "version"_str, "lock build-tool metadata"_str));
            auto executable = rstd_try(
                required_string(**build_tool, "executable"_str, "lock build-tool metadata"_str));
            auto operating_system = rstd_try(required_string(
                **build_tool, "operating-system"_str, "lock build-tool metadata"_str));
            auto schema           = rstd_try(required_member(
                **build_tool, "schema-version"_str, "lock build-tool metadata"_str));
            auto schema_version   = schema->as_u64();
            if (version.is_empty() || version.trim_ascii() != version) {
                return lock_failure<empty>(
                    "lock build-tool version must be a non-empty exact value"_str);
            }
            if (executable.is_empty() || ! PathBuf::from(executable).as_path().is_safe_relative()) {
                return lock_failure<empty>(
                    "lock build-tool executable must be a safe non-empty relative path"_str);
            }
            if (operating_system.is_empty() ||
                provider != rstd::format("build-tool:{}", operating_system).as_str()) {
                return lock_failure<empty>(
                    "lock build-tool operating system must match its provider"_str);
            }
            if (schema_version != Some(u64(1))) {
                return lock_failure<empty>("lock build-tool schema-version must be integer 1"_str);
            }
            auto source_kind = source->get("kind"_str);
            if (source_kind.is_none() || (**source_kind).as_str() != Some("archive"_str)) {
                return lock_failure<empty>("lock build-tool source must be an archive"_str);
            }
        }
    }
    return Ok(empty {});
}

auto load_existing(ref<rstd::path::Path> path) -> LockResult<Option<Json>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return lock_io_failure<Option<Json>>("inspect"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(Option<Json> {});
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return lock_io_failure<Option<Json>>("read"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(LockError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto valid    = validate_current_lock(document);
    if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
    return Ok(Some(rstd::move(document)));
}

auto parse_reference(const Json& value) -> lito::source::GitReference {
    auto kind   = *(**value.get("kind"_str)).as_str();
    auto parsed = lito::source::GitReferenceKind::DefaultBranch;
    if (kind == "branch"_str) parsed = lito::source::GitReferenceKind::Branch;
    if (kind == "tag"_str) parsed = lito::source::GitReferenceKind::Tag;
    if (kind == "rev"_str) parsed = lito::source::GitReferenceKind::Rev;
    if (kind == "commit"_str) parsed = lito::source::GitReferenceKind::Commit;
    return lito::source::GitReference {
        .kind  = parsed,
        .value = String::make(*(**value.get("value"_str)).as_str()),
    };
}

auto parse_locked_package_source(const Json& value) -> LockedPackageSource {
    auto kind = *(**value.get("kind"_str)).as_str();
    if (kind == "path"_str) {
        return LockedPackageSource::Path(PathBuf::from(*(**value.get("path"_str)).as_str()));
    }
    return LockedPackageSource::Git(String::make(*(**value.get("url"_str)).as_str()),
                                    parse_reference(**value.get("reference"_str)),
                                    String::make(*(**value.get("commit"_str)).as_str()));
}

auto parse_locked_external_source(const Json& value) -> LockedExternalSource {
    auto kind = *(**value.get("kind"_str)).as_str();
    if (kind == "path"_str) {
        return LockedExternalSource::Path(PathBuf::from(*(**value.get("path"_str)).as_str()));
    }
    if (kind == "package"_str) {
        return LockedExternalSource::Package(PathBuf::from(*(**value.get("path"_str)).as_str()));
    }
    if (kind == "git"_str) {
        return LockedExternalSource::Git(String::make(*(**value.get("url"_str)).as_str()),
                                         parse_reference(**value.get("reference"_str)),
                                         String::make(*(**value.get("commit"_str)).as_str()));
    }
    return LockedExternalSource::Archive(String::make(*(**value.get("url"_str)).as_str()),
                                         String::make(*(**value.get("sha256"_str)).as_str()));
}

auto parse_locked_build_tool_metadata(const Json& value) -> LockedBuildToolSourceMetadata {
    return LockedBuildToolSourceMetadata {
        .version          = String::make(*(**value.get("version"_str)).as_str()),
        .executable       = PathBuf::from(*(**value.get("executable"_str)).as_str()),
        .operating_system = String::make(*(**value.get("operating-system"_str)).as_str()),
        .schema_version   = *(**value.get("schema-version"_str)).as_u64(),
    };
}

auto parse_locked_project(const Json& document) -> LockedProject {
    auto        result   = LockedProject {};
    const auto& packages = **(**document.get("packages"_str)).as_array();
    result.packages      = Vec<LockedPackage>::with_capacity(packages.len());
    for (const auto& package : packages) {
        auto dependencies = Vec<String>::make();
        for (const auto& dependency : **(**package.get("dependencies"_str)).as_array()) {
            dependencies.push(String::make(*dependency.as_str()));
        }
        auto runtime_dependencies = Vec<String>::make();
        for (const auto& dependency : **(**package.get("runtime-dependencies"_str)).as_array()) {
            runtime_dependencies.push(String::make(*dependency.as_str()));
        }
        auto version       = Option<String> {};
        auto version_value = package.get("version"_str);
        if (version_value.is_some()) version = Some(String::make(*(**version_value).as_str()));
        result.packages.push(LockedPackage {
            .name                 = String::make(*(**package.get("name"_str)).as_str()),
            .version              = rstd::move(version),
            .source               = parse_locked_package_source(**package.get("source"_str)),
            .manifest             = PathBuf::from(*(**package.get("manifest"_str)).as_str()),
            .dependencies         = rstd::move(dependencies),
            .runtime_dependencies = rstd::move(runtime_dependencies),
        });
    }
    const auto& externals = **(**document.get("externals"_str)).as_array();
    result.externals      = Vec<LockedExternal>::with_capacity(externals.len());
    for (const auto& external : externals) {
        auto architectures = Vec<String>::make();
        auto values        = external.get("architectures"_str);
        if (values.is_some()) {
            for (const auto& architecture : **(**values).as_array()) {
                architectures.push(String::make(*architecture.as_str()));
            }
        }
        auto build_tool = Option<LockedBuildToolSourceMetadata> {};
        auto metadata   = external.get("build-tool"_str);
        if (metadata.is_some()) {
            build_tool = Some(parse_locked_build_tool_metadata(**metadata));
        }
        result.externals.push(LockedExternal {
            .package       = String::make(*(**external.get("package"_str)).as_str()),
            .alias         = String::make(*(**external.get("alias"_str)).as_str()),
            .provider      = String::make(*(**external.get("provider"_str)).as_str()),
            .architectures = rstd::move(architectures),
            .build_tool    = rstd::move(build_tool),
            .source        = parse_locked_external_source(**external.get("source"_str)),
        });
    }
    return result;
}

auto append_git_pin(lito::source::SourceResolutionOptions& options,
                    ref<str>                               url,
                    const lito::source::GitReference&      reference,
                    ref<str>                               commit) -> LockResult<empty> {
    for (const auto& existing : options.git_sources) {
        if (existing.git.as_str() != url ||
            ! lito::source::git_references_equal(existing.reference, reference)) {
            continue;
        }
        if (existing.commit.as_str() != commit) {
            return lock_failure<empty>(
                rstd::format("lock resolves Git requirement '{}#{}' to both '{}' and '{}'",
                             url,
                             reference.value.as_str(),
                             existing.commit.as_str(),
                             commit));
        }
        return Ok(empty {});
    }
    options.git_sources.push(lito::source::GitSourcePin {
        .git = String::make(url),
        .reference =
            lito::source::GitReference {
                .kind  = reference.kind,
                .value = reference.value.clone(),
            },
        .commit = String::make(commit),
    });
    return Ok(empty {});
}

auto append_project_pins(lito::source::SourceResolutionOptions& options,
                         const LockedProject&                   project) -> LockResult<empty> {
    for (const auto& package : project.packages) {
        if (! package.source.is_Git()) continue;
        rstd_try(append_git_pin(options,
                                package.source.as_Git().url.as_str(),
                                package.source.as_Git().reference,
                                package.source.as_Git().commit.as_str()));
    }
    for (const auto& external : project.externals) {
        if (! external.source.is_Git()) continue;
        rstd_try(append_git_pin(options,
                                external.source.as_Git().url.as_str(),
                                external.source.as_Git().reference,
                                external.source.as_Git().commit.as_str()));
    }
    return Ok(empty {});
}

auto write_lock(ref<rstd::path::Path> destination, const Json& desired) -> LockResult<empty> {
    auto text = rstd::json::to_string(
        desired, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii(u8('\n'));

    auto written = rstd::fs::write_atomic(destination, text.as_str().as_bytes());
    if (written.is_err()) {
        return lock_io_failure<empty>(
            "atomically write"_str, destination, rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto lito::lock::load_locked_project(ref<rstd::path::Path> root, const LockConfig& config)
    -> LockResult<LockedProject> {
    auto destination = lock_path(root, config);
    auto loaded      = rstd_try(load_existing(destination.as_path()));
    if (loaded.is_none()) {
        return lock_failure<LockedProject>(
            rstd::format("lock file '{}' does not exist", destination.as_path()));
    }
    return Ok(parse_locked_project(*loaded));
}

auto lito::lock::load_lock_session(ref<rstd::path::Path>           root,
                                   const LockConfig&               config,
                                   bool                            locked,
                                   lito::source::GitResolutionMode git) -> LockResult<LockSession> {
    if (locked && git == lito::source::GitResolutionMode::Refresh) {
        return lock_failure<LockSession>("--locked cannot refresh Git dependencies"_str);
    }
    auto destination = lock_path(root, config);
    auto loaded      = load_existing(destination.as_path());
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto existing = rstd::move(loaded).unwrap();
    if (existing.is_none()) {
        if (locked) {
            return lock_failure<LockSession>(rstd::format(
                "--locked requires an existing lock file at '{}'", destination.as_path()));
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.options_ = lito::source::SourceResolutionOptions { .locked = locked, .git = git };
        return Ok(rstd::move(session));
    }

    auto options = lito::source::SourceResolutionOptions { .locked = locked, .git = git };
    auto project = Some(parse_locked_project(*existing));
    rstd_try(append_project_pins(options, *project));
    auto session         = LockSession {};
    session.locked_      = locked;
    session.root_        = PathBuf::from(root);
    session.destination_ = rstd::move(destination);
    session.existing_    = rstd::move(existing);
    session.project_     = rstd::move(project);
    session.options_     = rstd::move(options);
    return Ok(rstd::move(session));
}

auto lito::lock::load_lock_session(ref<rstd::path::Path>           root,
                                   bool                            locked,
                                   lito::source::GitResolutionMode git) -> LockResult<LockSession> {
    return load_lock_session(root, LockConfig {}, locked, git);
}

auto lito::lock::sync_lock(const lito::package::ResolvedPackageGraph& graph, LockSession session)
    -> LockResult<LockStatus> {
    auto desired_result = graph_json(graph);
    if (desired_result.is_err()) return Err(rstd::move(desired_result).unwrap_err());
    auto desired = rstd::move(desired_result).unwrap();
    if (! (graph.root_directory.as_path().starts_with(session.root_.as_path()) &&
           session.root_.as_path().starts_with(graph.root_directory.as_path()))) {
        return lock_failure<LockStatus>("lock session root does not match resolved graph root"_str);
    }
    if (session.locked_) {
        if (session.existing_.is_some() && *session.existing_ == desired) {
            return Ok(LockStatus::Unchanged);
        }
        return lock_failure<LockStatus>(rstd::format(
            "--locked forbids updating stale lock file '{}'", session.destination_.as_path()));
    }

    if (session.existing_.is_some() && *session.existing_ == desired) {
        return Ok(LockStatus::Unchanged);
    }
    auto written = write_lock(session.destination_.as_path(), desired);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    return Ok(LockStatus::Updated);
}
