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
using Json      = rstd::json::Value;
using Map       = rstd::json::Map;
using Array     = rstd::json::Array;
using StringSet = rstd::collections::BTreeMap<String, empty>;
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

auto locked_package_source(const lito::source::ResolvedPackageSource& source) -> LockedSource {
    if (source.kind == lito::source::PackageSourceKind::Path) {
        return LockedSource::Path(source.path.clone());
    }
    if (source.kind == lito::source::PackageSourceKind::Builtin) {
        return LockedSource::Builtin(source.builtin.clone(), source.digest.clone());
    }
    return LockedSource::Git(source.git.clone(), source.reference.clone(), source.commit.clone());
}

auto locked_external_source(const lito::dependency::ResolvedExternalSource& source)
    -> LockedSource {
    if (source.is_Path()) return LockedSource::Path(source.as_Path().path.clone());
    if (source.is_Package()) return LockedSource::Package(source.as_Package().path.clone());
    if (source.is_Git()) {
        return LockedSource::Git(source.as_Git().url.clone(),
                                 source.as_Git().reference.clone(),
                                 source.as_Git().commit.clone());
    }
    return LockedSource::Archive(source.as_Archive().url.clone(),
                                 source.as_Archive().sha256.clone());
}

auto locked_source_json(const LockedSource& source) -> LockResult<Json> {
    auto item = Map::make();
    if (source.is_Path()) {
        auto path = rstd_try(path_string(source.as_Path().path.as_path()));
        item.insert(String::make("kind"_str), string_json("path"_str));
        item.insert(String::make("path"_str), string_json(path.as_str()));
    } else if (source.is_Package()) {
        auto path = rstd_try(path_string(source.as_Package().path.as_path()));
        item.insert(String::make("kind"_str), string_json("package"_str));
        item.insert(String::make("path"_str), string_json(path.as_str()));
    } else if (source.is_Builtin()) {
        item.insert(String::make("digest"_str), string_json(source.as_Builtin().digest.as_str()));
        item.insert(String::make("id"_str), string_json(source.as_Builtin().id.as_str()));
        item.insert(String::make("kind"_str), string_json("builtin"_str));
    } else if (source.is_Git()) {
        item.insert(String::make("commit"_str), string_json(source.as_Git().commit.as_str()));
        item.insert(String::make("kind"_str), string_json("git"_str));
        item.insert(String::make("reference"_str), reference_json(source.as_Git().reference));
        item.insert(String::make("url"_str), string_json(source.as_Git().url.as_str()));
    } else {
        item.insert(String::make("kind"_str), string_json("archive"_str));
        auto sha256 = source.as_Archive().sha256.to_hex();
        item.insert(String::make("sha256"_str), string_json(sha256.as_str()));
        item.insert(String::make("url"_str), string_json(source.as_Archive().url.as_str()));
    }
    return Ok(Json::Object(rstd::move(item)));
}

auto external_order_key(const lito::dependency::ResolvedExternalSourceRecord& external) -> String {
    auto architectures = Vec<String>::with_capacity(external.architectures.len());
    for (const auto& architecture : external.architectures) {
        architectures.push(architecture.name.clone());
    }
    rstd::slice_::sort_unstable(architectures.as_mut_slice().as_mut_ref());
    auto key = external.name.clone();
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
            dependency_names.push(String::make(resolved_dependency_name(dependency)));
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
        auto source = rstd_try(locked_source_json(locked_package_source(package.source)));
        item.insert(String::make("source"_str), rstd::move(source));
        auto external_indices = Vec<usize>::with_capacity(package.externals.len());
        for (usize external {}; external < package.externals.len(); ++external) {
            external_indices.push(usize(external));
        }
        rstd::slice_::sort_unstable_by(external_indices.as_mut_slice().as_mut_ref(),
                                       [&package](usize left, usize right) {
                                           return external_order_key(package.externals[left]) <
                                                  external_order_key(package.externals[right]);
                                       });
        auto externals = Array::make();
        for (const auto external_index : external_indices) {
            const auto& external    = package.externals[external_index];
            auto architecture_names = Vec<String>::with_capacity(external.architectures.len());
            for (const auto& architecture : external.architectures) {
                architecture_names.push(architecture.name.clone());
            }
            rstd::slice_::sort_unstable(architecture_names.as_mut_slice().as_mut_ref());
            auto architectures = Array::make();
            for (const auto& architecture : architecture_names) {
                architectures.push(string_json(architecture.as_str()));
            }
            auto external_item = Map::make();
            if (! architectures.is_empty()) {
                external_item.insert(String::make("architectures"_str),
                                     Json::Array(rstd::move(architectures)));
            }
            external_item.insert(String::make("name"_str), string_json(external.name.as_str()));
            auto external_source =
                rstd_try(locked_source_json(locked_external_source(external.source)));
            external_item.insert(String::make("source"_str), rstd::move(external_source));
            externals.push(Json::Object(rstd::move(external_item)));
        }
        item.insert(String::make("externals"_str), Json::Array(rstd::move(externals)));
        if (package.manifest.version.value.is_some()) {
            item.insert(String::make("version"_str),
                        string_json(package.manifest.version.value->as_str()));
        }
        packages.push(Json::Object(rstd::move(item)));
    }

    auto root = Map::make();
    root.insert(String::make("packages"_str), Json::Array(rstd::move(packages)));
    root.insert(String::make("version"_str),
                Json::Number(rstd::json::Number::from_u64(LOCK_FORMAT_VERSION)));
    return Ok(Json::Object(rstd::move(root)));
}

auto lock_package_key(ref<str> key) -> bool {
    return key == "dependencies"_str || key == "manifest"_str || key == "name"_str ||
           key == "runtime-dependencies"_str || key == "source"_str || key == "version"_str ||
           key == "externals"_str;
}

auto path_source_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "path"_str;
}

auto git_source_key(ref<str> key) -> bool {
    return key == "commit"_str || key == "kind"_str || key == "reference"_str || key == "url"_str;
}

auto builtin_source_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "id"_str || key == "digest"_str;
}

auto reference_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "value"_str;
}

auto valid_source_manifest(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    return PathBuf::from(value).as_path().is_safe_relative();
}

auto root_key(ref<str> key) -> bool {
    return key == "packages"_str || key == "version"_str;
}

auto external_key(ref<str> key) -> bool {
    return key == "name"_str || key == "architectures"_str || key == "source"_str;
}

auto archive_source_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "sha256"_str || key == "url"_str;
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

auto parse_reference(const Json& value, const lito::parse::NodePath& path, ref<str> commit)
    -> LockResult<lito::source::GitReference> {
    rstd_try(lito::parse::json::reject_unknown(value, path, reference_key));
    auto       kind = rstd_try(lito::parse::json::required_string(value, "kind"_str, path));
    auto       text = rstd_try(lito::parse::json::required_string(value, "value"_str, path));
    const auto default_reference = kind == "default"_str;
    const auto named_reference =
        kind == "branch"_str || kind == "tag"_str || kind == "rev"_str || kind == "commit"_str;
    if (! default_reference && ! named_reference) {
        return lock_failure<lito::source::GitReference>(
            rstd::format("{}.kind must be default, branch, tag, rev, or commit", path));
    }
    if (default_reference != text.is_empty()) {
        return lock_failure<lito::source::GitReference>(
            rstd::format("{}.value must be empty only for default", path));
    }
    if (kind == "commit"_str && text.as_str() != commit) {
        return lock_failure<lito::source::GitReference>(
            rstd::format("{} commit reference must match the resolved commit", path));
    }
    auto parsed = lito::source::GitReferenceKind::DefaultBranch;
    if (kind == "branch"_str) parsed = lito::source::GitReferenceKind::Branch;
    if (kind == "tag"_str) parsed = lito::source::GitReferenceKind::Tag;
    if (kind == "rev"_str) parsed = lito::source::GitReferenceKind::Rev;
    if (kind == "commit"_str) parsed = lito::source::GitReferenceKind::Commit;
    return Ok(lito::source::GitReference { .kind = parsed, .value = rstd::move(text) });
}

auto parse_locked_source(const Json& value, const lito::parse::NodePath& path, bool external_source)
    -> LockResult<LockedSource> {
    auto kind = rstd_try(lito::parse::json::required_string(value, "kind"_str, path));
    if (kind == "path"_str) {
        rstd_try(lito::parse::json::reject_unknown(value, path, path_source_key));
        auto source_path = rstd_try(lito::parse::json::required_string(value, "path"_str, path));
        if (! valid_lock_source_path(source_path.as_str())) {
            return lock_failure<LockedSource>(
                rstd::format("{}.path must be a non-empty relative path", path));
        }
        return Ok(LockedSource::Path(PathBuf::from(source_path.as_str())));
    }
    if (kind == "package"_str && external_source) {
        rstd_try(lito::parse::json::reject_unknown(value, path, path_source_key));
        auto source_path = rstd_try(lito::parse::json::required_string(value, "path"_str, path));
        if (source_path.is_empty() ||
            ! PathBuf::from(source_path.as_str()).as_path().is_safe_relative()) {
            return lock_failure<LockedSource>(
                rstd::format("{}.path must be a safe non-empty package-relative path", path));
        }
        return Ok(LockedSource::Package(PathBuf::from(source_path.as_str())));
    }
    if (kind == "builtin"_str && ! external_source) {
        rstd_try(lito::parse::json::reject_unknown(value, path, builtin_source_key));
        auto id     = rstd_try(lito::parse::json::required_string(value, "id"_str, path));
        auto digest = rstd_try(lito::parse::json::required_sha256(
            value, "digest"_str, path, lito::parse::Sha256TextMode::Canonical));
        if (! lito::manifest::valid_package_name(id.as_str())) {
            return lock_failure<LockedSource>(
                rstd::format("{}.id must be a valid package name", path));
        }
        return Ok(LockedSource::Builtin(rstd::move(id), digest.to_hex()));
    }
    if (kind == "git"_str) {
        rstd_try(lito::parse::json::reject_unknown(value, path, git_source_key));
        auto url       = rstd_try(lito::parse::json::required_string(value, "url"_str, path));
        auto commit    = rstd_try(lito::parse::json::required_string(value, "commit"_str, path));
        auto reference = rstd_try(lito::parse::json::required_member(value, "reference"_str, path));
        if (! valid_fetch_url(url.as_str())) {
            return lock_failure<LockedSource>(rstd::format("{}.url must not be empty", path));
        }
        if (! lito::source::git_commit_is_valid(commit.as_str())) {
            return lock_failure<LockedSource>(
                rstd::format("{}.commit must be a full hexadecimal object id", path));
        }
        auto parsed_reference =
            rstd_try(parse_reference(*reference, path.field("reference"_str), commit.as_str()));
        return Ok(
            LockedSource::Git(rstd::move(url), rstd::move(parsed_reference), rstd::move(commit)));
    }
    if (kind == "archive"_str && external_source) {
        rstd_try(lito::parse::json::reject_unknown(value, path, archive_source_key));
        auto url    = rstd_try(lito::parse::json::required_fetch_url(value, "url"_str, path));
        auto sha256 = rstd_try(lito::parse::json::required_sha256(
            value, "sha256"_str, path, lito::parse::Sha256TextMode::Canonical));
        return Ok(LockedSource::Archive(rstd::move(url), rstd::move(sha256)));
    }
    return lock_failure<LockedSource>(rstd::format("{} has unsupported kind '{}'", path, kind));
}

auto parse_current_lock(const Json& document) -> LockResult<LockedProject> {
    const auto root_path = lito::parse::NodePath::root("lock"_str);
    rstd_try(lito::parse::json::reject_unknown(document, root_path, root_key));
    auto version = rstd_try(lito::parse::json::required_u64(document, "version"_str, root_path));
    auto packages =
        rstd_try(lito::parse::json::required_array(document, "packages"_str, root_path));
    if (version != LOCK_FORMAT_VERSION) {
        return lock_failure<LockedProject>(
            rstd::format("lock.version {} is not supported; this Lito supports version {}",
                         version,
                         LOCK_FORMAT_VERSION));
    }
    if (packages->is_empty()) {
        return lock_failure<LockedProject>("lock.packages must be a non-empty array"_str);
    }
    auto names  = StringSet::make();
    auto result = LockedProject { .packages = Vec<LockedPackage>::with_capacity(packages->len()) };
    for (usize package_index {}; package_index < packages->len(); ++package_index) {
        const auto& package      = (*packages)[package_index];
        const auto  package_path = root_path.field("packages"_str).index(package_index);
        rstd_try(lito::parse::json::reject_unknown(package, package_path, lock_package_key));
        auto name = rstd_try(lito::parse::json::required_string(package, "name"_str, package_path));
        auto source =
            rstd_try(lito::parse::json::required_member(package, "source"_str, package_path));
        auto manifest =
            rstd_try(lito::parse::json::required_string(package, "manifest"_str, package_path));
        auto dependencies =
            rstd_try(lito::parse::json::required_array(package, "dependencies"_str, package_path));
        auto runtime_dependencies = rstd_try(
            lito::parse::json::required_array(package, "runtime-dependencies"_str, package_path));
        auto externals =
            rstd_try(lito::parse::json::required_array(package, "externals"_str, package_path));
        if (! lito::manifest::valid_package_name(name.as_str())) {
            return lock_failure<LockedProject>(
                rstd::format("lock package name '{}' is invalid", name));
        }
        if (names.contains_key(name.as_str())) {
            return lock_failure<LockedProject>(
                rstd::format("lock repeats package name '{}'", name));
        }
        names.insert(name.clone(), empty {});
        auto package_version = Option<String> {};
        auto version         = package.get("version"_str);
        if (version.is_some()) {
            auto text =
                rstd_try(lito::parse::json::string(**version, package_path.field("version"_str)));
            if (text.is_empty()) {
                return lock_failure<LockedProject>(
                    rstd::format("lock package '{}' version must be a non-empty string", name));
            }
            package_version = Some(String::make(text));
        }
        if (! valid_source_manifest(manifest.as_str())) {
            return lock_failure<LockedProject>(
                "lock package manifest must be a relative path without parent components"_str);
        }
        auto locked_source =
            rstd_try(parse_locked_source(*source, package_path.field("source"_str), false));
        auto locked_externals = Vec<LockedPackageExternalSource>::with_capacity(externals->len());
        auto external_identities = StringSet::make();
        for (usize external_index {}; external_index < externals->len(); ++external_index) {
            const auto& external      = (*externals)[external_index];
            const auto  external_path = package_path.field("externals"_str).index(external_index);
            rstd_try(lito::parse::json::reject_unknown(external, external_path, external_key));
            auto external_name =
                rstd_try(lito::parse::json::required_string(external, "name"_str, external_path));
            auto external_source =
                rstd_try(lito::parse::json::required_member(external, "source"_str, external_path));
            if (external_name.is_empty()) {
                return lock_failure<LockedProject>(
                    "lock package external name must not be empty"_str);
            }
            auto architecture_key     = String::make();
            auto locked_architectures = Vec<String>::make();
            auto architectures        = external.get("architectures"_str);
            if (architectures.is_some()) {
                auto values = rstd_try(lito::parse::json::array(
                    **architectures, external_path.field("architectures"_str)));
                if (values->is_empty()) {
                    return lock_failure<LockedProject>(
                        "lock package external architectures must be a non-empty array when present"_str);
                }
                locked_architectures = Vec<String>::with_capacity(values->len());
                auto seen            = StringSet::make();
                auto previous        = Option<String> {};
                for (usize architecture_index {}; architecture_index < values->len();
                     ++architecture_index) {
                    auto architecture_name = rstd_try(lito::parse::json::string(
                        (*values)[architecture_index],
                        external_path.field("architectures"_str).index(architecture_index)));
                    auto canonical         = canonical_architecture(architecture_name);
                    if (canonical.is_err() || canonical->as_str() != architecture_name) {
                        return lock_failure<LockedProject>(rstd::format(
                            "lock package external architecture '{}' must be canonical",
                            architecture_name));
                    }
                    if (seen.contains_key(architecture_name)) {
                        return lock_failure<LockedProject>(rstd::format(
                            "lock package external repeats architecture '{}'", architecture_name));
                    }
                    auto canonical_name = String::make(architecture_name);
                    if (previous.is_some() && canonical_name < *previous) {
                        return lock_failure<LockedProject>(
                            "lock package external architectures must use stable sorted order"_str);
                    }
                    seen.insert(canonical_name.clone(), empty {});
                    previous = Some(canonical_name.clone());
                    if (! architecture_key.is_empty()) architecture_key.push_ascii(u8(','));
                    architecture_key.push_str(architecture_name);
                    locked_architectures.push(rstd::move(canonical_name));
                }
            }
            auto identity = rstd::format("{}\n{}", external_name, architecture_key.as_str());
            if (external_identities.contains_key(identity.as_str())) {
                return lock_failure<LockedProject>(
                    rstd::format("lock package '{}' repeats external '{}' for architectures '{}'",
                                 name,
                                 external_name,
                                 architecture_key.as_str()));
            }
            external_identities.insert(rstd::move(identity), empty {});
            locked_externals.push(LockedPackageExternalSource {
                .name          = rstd::move(external_name),
                .architectures = rstd::move(locked_architectures),
                .source        = rstd_try(
                    parse_locked_source(*external_source, external_path.field("source"_str), true)),
            });
        }
        auto dependency_names    = Vec<String>::with_capacity(dependencies->len());
        auto dependency_name_set = StringSet::make();
        for (usize dependency_index {}; dependency_index < dependencies->len();
             ++dependency_index) {
            auto dependency_name = rstd_try(lito::parse::json::string(
                (*dependencies)[dependency_index],
                package_path.field("dependencies"_str).index(dependency_index)));
            if (! lito::manifest::valid_package_name(dependency_name)) {
                return lock_failure<LockedProject>(
                    "lock dependency name must be a valid package name"_str);
            }
            if (dependency_name_set.contains_key(dependency_name)) {
                return lock_failure<LockedProject>(rstd::format(
                    "lock package '{}' repeats dependency '{}'", name, dependency_name));
            }
            auto parsed_name = String::make(dependency_name);
            dependency_name_set.insert(parsed_name.clone(), empty {});
            dependency_names.push(rstd::move(parsed_name));
        }
        auto runtime_dependency_names    = Vec<String>::with_capacity(runtime_dependencies->len());
        auto runtime_dependency_name_set = StringSet::make();
        for (usize dependency_index {}; dependency_index < runtime_dependencies->len();
             ++dependency_index) {
            auto dependency_name = rstd_try(lito::parse::json::string(
                (*runtime_dependencies)[dependency_index],
                package_path.field("runtime-dependencies"_str).index(dependency_index)));
            if (! lito::manifest::valid_package_name(dependency_name)) {
                return lock_failure<LockedProject>(
                    "lock runtime dependency name must be a valid package name"_str);
            }
            if (runtime_dependency_name_set.contains_key(dependency_name)) {
                return lock_failure<LockedProject>(rstd::format(
                    "lock package '{}' repeats runtime dependency '{}'", name, dependency_name));
            }
            auto parsed_name = String::make(dependency_name);
            runtime_dependency_name_set.insert(parsed_name.clone(), empty {});
            runtime_dependency_names.push(rstd::move(parsed_name));
        }
        result.packages.push(LockedPackage {
            .name                 = rstd::move(name),
            .version              = rstd::move(package_version),
            .source               = rstd::move(locked_source),
            .manifest             = PathBuf::from(manifest.as_str()),
            .dependencies         = rstd::move(dependency_names),
            .runtime_dependencies = rstd::move(runtime_dependency_names),
            .externals            = rstd::move(locked_externals),
        });
    }
    for (const auto& package : result.packages) {
        for (const auto& dependency : package.dependencies) {
            if (! names.contains_key(dependency.as_str())) {
                return lock_failure<LockedProject>(
                    rstd::format("lock package '{}' dependency '{}' does not identify a package",
                                 package.name,
                                 dependency));
            }
        }
        for (const auto& dependency : package.runtime_dependencies) {
            if (! names.contains_key(dependency.as_str())) {
                return lock_failure<LockedProject>(rstd::format(
                    "lock package '{}' runtime dependency '{}' does not identify a package",
                    package.name,
                    dependency));
            }
        }
    }

    return Ok(rstd::move(result));
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
    return Ok(Some(rstd::move(document)));
}

auto lock_document_version(const Json& document) -> Option<u64> {
    auto version = document.get("version"_str);
    if (version.is_none()) return None();
    return (**version).as_u64();
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
    for (const auto& package : project.packages) {
        for (const auto& external : package.externals) {
            if (! external.source.is_Git()) continue;
            rstd_try(append_git_pin(options,
                                    external.source.as_Git().url.as_str(),
                                    external.source.as_Git().reference,
                                    external.source.as_Git().commit.as_str()));
        }
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
    auto destination = resolve_lock_path(root, config);
    auto loaded      = rstd_try(load_existing(destination.as_path()));
    if (loaded.is_none()) {
        return lock_failure<LockedProject>(
            rstd::format("lock file '{}' does not exist", destination.as_path()));
    }
    return parse_current_lock(*loaded);
}

auto lito::lock::load_lock_session(ref<rstd::path::Path>           root,
                                   const LockConfig&               config,
                                   bool                            locked,
                                   lito::source::GitResolutionMode git,
                                   InvalidLockPolicy invalid) -> LockResult<LockSession> {
    if (locked && git == lito::source::GitResolutionMode::Refresh) {
        return lock_failure<LockSession>("--locked cannot refresh Git dependencies"_str);
    }
    if (locked && invalid == InvalidLockPolicy::Replace) {
        return lock_failure<LockSession>("--locked cannot replace an invalid lock file"_str);
    }
    auto destination = resolve_lock_path(root, config);
    auto loaded      = load_existing(destination.as_path());
    if (loaded.is_err()) {
        auto error = rstd::move(loaded).unwrap_err();
        if (invalid != InvalidLockPolicy::Replace || ! error.is_Json()) {
            return Err(rstd::move(error));
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }
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

    const auto version = lock_document_version(*existing);
    if (version == Some(u64(1))) {
        if (locked) {
            return lock_failure<LockSession>(rstd::format(
                "lock file '{}' uses version 1, but this Lito requires version {}; run 'lito "
                "update'",
                destination.as_path(),
                LOCK_FORMAT_VERSION));
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.existing_    = rstd::move(existing);
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }
    auto parsed_project = parse_current_lock(*existing);
    if (parsed_project.is_err()) {
        const auto version = lock_document_version(*existing);
        if (invalid != InvalidLockPolicy::Replace ||
            (version.is_some() && *version > LOCK_FORMAT_VERSION)) {
            return Err(rstd::move(parsed_project).unwrap_err());
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.existing_    = rstd::move(existing);
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }

    auto options = lito::source::SourceResolutionOptions { .locked = locked, .git = git };
    auto project = Some(rstd::move(parsed_project).unwrap());
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
                                   lito::source::GitResolutionMode git,
                                   InvalidLockPolicy invalid) -> LockResult<LockSession> {
    return load_lock_session(root, LockConfig {}, locked, git, invalid);
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
