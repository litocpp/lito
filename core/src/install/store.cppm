module;
#include <rstd/macro.hpp>

export module lito.install.store;

import rstd;
import rstd.json;
import lito.error;
import lito.install.contract;
import lito.install.package_contract;
import lito.install.source;
import lito.package.identity;
import lito.manifest;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

inline constexpr auto INSTALL_DOCUMENT_VERSION = u64(3);

struct StoredEntry {
    PathBuf destination;
    String  origin;
};

struct StoredPackage {
    String                  name;
    String                  version;
    String                  source_identity;
    InstallSourceProvenance provenance;
    String                  profile;
    String                  target;
    Vec<StoredEntry>        entries;
    Vec<InstallRuntimeDependency> runtime_dependencies;
};

struct InstalledDocument {
    Vec<StoredPackage> packages;
};

struct PendingEntry {
    usize         package {};
    usize         entry {};
    PathBuf       staged;
    InstallAction action { InstallAction::Created };
};

struct BackupEntry {
    PathBuf destination;
    PathBuf backup;
};

template<typename T>
auto store_failure(String message) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(InstallStoreCause::Message(rstd::move(message))));
}

template<typename T>
auto store_failure(ref<str> message) -> InstallStoreResult<T> {
    return store_failure<T>(String::make(message));
}

template<typename T>
auto store_io_failure(ref<str> operation,
                      ref<rstd::path::Path> path,
                      rstd::io::error::Error error) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(InstallStoreCause::Io(
        String::make(operation), PathBuf::from(path), rstd::move(error))));
}

auto path_metadata(ref<rstd::path::Path> path) -> InstallStoreResult<Option<rstd::fs::Metadata>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_ok()) return Ok(Some(rstd::move(metadata).unwrap()));
    auto error = rstd::move(metadata).unwrap_err();
    if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return Ok(None());
    }
    return store_io_failure<Option<rstd::fs::Metadata>>(
        "inspect install path"_str, path, rstd::move(error));
}

auto normal_destination(ref<rstd::path::Path> path) -> bool {
    if (path.is_empty() || path.is_absolute() || path.has_root()) return false;
    auto components = path.components();
    auto first = true;
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal()) return false;
        if (first && component->as_os_str().to_str() == Some(".lito"_str)) return false;
        first = false;
    }
    return ! first;
}

auto validate_directory(ref<rstd::path::Path> path, ref<str> role)
    -> InstallStoreResult<empty> {
    auto metadata = rstd_try(path_metadata(path));
    if (metadata.is_none() || ! metadata->is_dir() || metadata->is_symlink()) {
        return store_failure<empty>(
            rstd::format("{} directory '{}' is not a real directory", role, path));
    }
    return Ok(empty {});
}

auto ensure_parent_tree(ref<rstd::path::Path> root,
                        ref<rstd::path::Path> relative,
                        Vec<PathBuf>* created = nullptr) -> InstallStoreResult<empty> {
    auto parent = relative.parent();
    if (parent.is_none() || parent->is_empty()) return Ok(empty {});
    auto current = PathBuf::from(root);
    auto components = parent->components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        auto child = PathBuf::from(component->as_os_str());
        current.push(child.as_path());
        auto metadata = rstd_try(path_metadata(current.as_path()));
        if (metadata.is_some()) {
            if (! metadata->is_dir() || metadata->is_symlink()) {
                return store_failure<empty>(rstd::format(
                    "install destination parent '{}' is not a real directory",
                    current.as_path()));
            }
            continue;
        }
        auto made = rstd::fs::create_dir(current.as_path());
        if (made.is_err()) {
            return store_io_failure<empty>(
                "create install destination parent"_str,
                current.as_path(),
                rstd::move(made).unwrap_err());
        }
        if (created != nullptr) created->push(current.clone());
    }
    return Ok(empty {});
}

auto origin_text(const InstallEntryOrigin& origin) -> String {
    if (origin.is_PackageFile()) {
        return rstd::format("package-file:{}:{}",
                            origin.as_PackageFile().package.as_str(),
                            origin.as_PackageFile().path.as_path());
    }
    if (origin.is_BuildArtifact()) {
        return rstd::format("build-artifact:{}",
                            package_target_id_text(origin.as_BuildArtifact().target));
    }
    if (origin.is_ExternalAsset()) {
        return rstd::format("external-asset:{}:{}:{}",
                            origin.as_ExternalAsset().dependency.as_str(),
                            origin.as_ExternalAsset().set.as_str(),
                            origin.as_ExternalAsset().path.as_path());
    }
    if (origin.is_Template()) {
        return rstd::format("template:{}", origin.as_Template().input.as_path());
    }
    return String::make("inventory"_str);
}

auto json_string(ref<str> value) -> Json { return Json::String(String::make(value)); }

auto document_json(const InstalledDocument& document) -> InstallStoreResult<Json> {
    auto packages = JsonArray::with_capacity(document.packages.len());
    for (const auto& package : document.packages) {
        auto entries = JsonArray::with_capacity(package.entries.len());
        for (const auto& entry : package.entries) {
            auto item = JsonMap::make();
            item.insert(String::make("destination"_str),
                        Json::String(entry.destination.as_path().to_string_lossy()));
            item.insert(String::make("origin"_str), json_string(entry.origin.as_str()));
            entries.push(Json::Object(rstd::move(item)));
        }
        auto item = JsonMap::make();
        item.insert(String::make("entries"_str), Json::Array(rstd::move(entries)));
        item.insert(String::make("name"_str), json_string(package.name.as_str()));
        item.insert(String::make("profile"_str), json_string(package.profile.as_str()));
        item.insert(String::make("source"_str),
                    rstd_try(serialize_install_source_provenance(package.provenance)));
        auto runtime_dependencies = JsonArray::make();
        for (const auto& dependency : package.runtime_dependencies) {
            auto runtime = JsonMap::make();
            runtime.insert(String::make("name"_str), json_string(dependency.name.as_str()));
            runtime.insert(String::make("source"_str),
                           json_string(dependency.source_identity.as_str()));
            runtime_dependencies.push(Json::Object(rstd::move(runtime)));
        }
        item.insert(String::make("runtime-dependencies"_str),
                    Json::Array(rstd::move(runtime_dependencies)));
        item.insert(String::make("target"_str), json_string(package.target.as_str()));
        item.insert(String::make("version"_str), json_string(package.version.as_str()));
        packages.push(Json::Object(rstd::move(item)));
    }
    auto root = JsonMap::make();
    root.insert(String::make("packages"_str), Json::Array(rstd::move(packages)));
    root.insert(String::make("version"_str),
                Json::Number(rstd::json::Number::from_u64(INSTALL_DOCUMENT_VERSION)));
    return Ok(Json::Object(rstd::move(root)));
}

auto required_member(const Json& value, ref<str> key, ref<str> context)
    -> InstallStoreResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return store_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context)
    -> InstallStoreResult<String> {
    auto member = rstd_try(required_member(value, key, context));
    auto text = member->as_str();
    if (text.is_none() || text->is_empty()) {
        return store_failure<String>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto parse_document(const Json& value) -> InstallStoreResult<InstalledDocument> {
    auto version = rstd_try(required_member(value, "version"_str, "installed document"_str));
    auto number = version->as_u64();
    if (number.is_none() || *number != INSTALL_DOCUMENT_VERSION) {
        return store_failure<InstalledDocument>(
            "installed metadata uses an unsupported schema; remove the .lito install state and "
            "reinstall packages"_str);
    }
    auto packages_value =
        rstd_try(required_member(value, "packages"_str, "installed document"_str));
    auto packages_array = packages_value->as_array();
    if (packages_array.is_none()) {
        return store_failure<InstalledDocument>(
            "installed document.packages must be an array"_str);
    }
    auto document = InstalledDocument {};
    auto destinations = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& item : **packages_array) {
        auto name = rstd_try(required_string(item, "name"_str, "installed package"_str));
        auto source = rstd_try(required_member(item, "source"_str, "installed package"_str));
        auto provenance = rstd_try(parse_install_source_provenance(*source));
        auto runtime_value = rstd_try(
            required_member(item, "runtime-dependencies"_str, "installed package"_str));
        auto runtime_array = runtime_value->as_array();
        if (runtime_array.is_none()) {
            return store_failure<InstalledDocument>(
                "installed package.runtime-dependencies must be an array"_str);
        }
        auto runtime_dependencies = Vec<InstallRuntimeDependency>::make();
        for (const auto& runtime : **runtime_array) {
            auto dependency = InstallRuntimeDependency {
                .name = rstd_try(required_string(runtime, "name"_str, "installed runtime dependency"_str)),
                .source_identity = rstd_try(
                    required_string(runtime, "source"_str, "installed runtime dependency"_str)),
            };
            if (! valid_package_name(dependency.name.as_str())) {
                return store_failure<InstalledDocument>(
                    "installed runtime dependency name is invalid"_str);
            }
            runtime_dependencies.push(rstd::move(dependency));
        }
        auto entries_value =
            rstd_try(required_member(item, "entries"_str, "installed package"_str));
        auto entries_array = entries_value->as_array();
        if (entries_array.is_none() || (**entries_array).is_empty()) {
            return store_failure<InstalledDocument>(
                "installed package.entries must be a non-empty array"_str);
        }
        auto entries = Vec<StoredEntry>::make();
        for (const auto& entry : **entries_array) {
            auto destination = rstd_try(
                required_string(entry, "destination"_str, "installed entry"_str));
            auto relative = PathBuf::from(destination.as_str());
            if (! normal_destination(relative.as_path())) {
                return store_failure<InstalledDocument>(rstd::format(
                    "installed destination '{}' is unsafe", relative.as_path()));
            }
            auto key = relative.as_path().to_string_lossy();
            auto owner = destinations.get(key.as_str());
            if (owner.is_some()) {
                return store_failure<InstalledDocument>(rstd::format(
                    "installed destination '{}' is owned by both '{}' and '{}'",
                    relative.as_path(), **owner, name.as_str()));
            }
            destinations.insert(rstd::move(key), name.clone());
            entries.push(StoredEntry {
                .destination = rstd::move(relative),
                .origin = rstd_try(required_string(entry, "origin"_str, "installed entry"_str)),
            });
        }
        document.packages.push(StoredPackage {
            .name            = rstd::move(name),
            .version         = rstd_try(required_string(item, "version"_str, "installed package"_str)),
            .source_identity = rstd_try(install_source_identity(provenance)),
            .provenance      = rstd::move(provenance),
            .profile         = rstd_try(required_string(item, "profile"_str, "installed package"_str)),
            .target          = rstd_try(required_string(item, "target"_str, "installed package"_str)),
            .entries         = rstd::move(entries),
            .runtime_dependencies = rstd::move(runtime_dependencies),
        });
    }
    return Ok(rstd::move(document));
}

auto validate_runtime_dependencies(const InstalledDocument& document)
    -> InstallStoreResult<empty> {
    for (const auto& package : document.packages) {
        auto names = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& dependency : package.runtime_dependencies) {
            if (names.contains_key(dependency.name.as_str())) {
                return store_failure<empty>(rstd::format(
                    "installed package '{}' repeats runtime dependency '{}'",
                    package.name.as_str(), dependency.name.as_str()));
            }
            names.insert(dependency.name.clone(), empty {});
            const StoredPackage* target = nullptr;
            for (const auto& candidate : document.packages) {
                if (candidate.name == dependency.name.as_str()) {
                    target = rstd::addressof(candidate);
                    break;
                }
            }
            if (target == nullptr) {
                return store_failure<empty>(rstd::format(
                    "installed package '{}' requires missing runtime package '{}'",
                    package.name.as_str(), dependency.name.as_str()));
            }
            if (target->source_identity != dependency.source_identity.as_str()) {
                return store_failure<empty>(rstd::format(
                    "installed package '{}' requires runtime package '{}' from source '{}', "
                    "but source '{}' is installed",
                    package.name.as_str(),
                    dependency.name.as_str(),
                    dependency.source_identity.as_str(),
                    target->source_identity.as_str()));
            }
        }
    }
    return Ok(empty {});
}

auto load_document(ref<rstd::path::Path> path) -> InstallStoreResult<InstalledDocument> {
    auto metadata = rstd_try(path_metadata(path));
    if (metadata.is_none()) return Ok(InstalledDocument {});
    if (! metadata->is_file() || metadata->is_symlink()) {
        return store_failure<InstalledDocument>(
            rstd::format("install metadata '{}' is not a regular file", path));
    }
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return store_io_failure<InstalledDocument>(
            "read install metadata"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(InstallStoreError::Cause(
            InstallStoreCause::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err())));
    }
    auto document = rstd_try(parse_document(*parsed));
    rstd_try(validate_runtime_dependencies(document));
    return Ok(rstd::move(document));
}

auto write_document(ref<rstd::path::Path> path, const InstalledDocument& document)
    -> InstallStoreResult<empty> {
    auto json = rstd_try(document_json(document));
    auto text = rstd::json::to_string(
        json, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
    if (written.is_err()) {
        return store_io_failure<empty>(
            "atomically write install metadata"_str, path, rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto package_owner(const StoredPackage& package, ref<str> name, ref<str> source) -> bool {
    return package.name == name && package.source_identity == source;
}

auto entry_owner(const InstalledDocument& document, ref<rstd::path::Path> destination)
    -> Option<usize> {
    for (usize package {}; package < document.packages.len(); ++package) {
        for (const auto& entry : document.packages[package].entries) {
            if (entry.destination.as_path() == destination) return Some(package);
        }
    }
    return None();
}

auto same_file(ref<rstd::path::Path> left, ref<rstd::path::Path> right)
    -> InstallStoreResult<bool> {
    auto left_metadata = rstd::fs::metadata(left);
    auto right_metadata = rstd::fs::metadata(right);
    if (left_metadata.is_err()) {
        return store_io_failure<bool>(
            "inspect staged entry"_str, left, rstd::move(left_metadata).unwrap_err());
    }
    if (right_metadata.is_err()) {
        return store_io_failure<bool>(
            "inspect installed entry"_str, right, rstd::move(right_metadata).unwrap_err());
    }
    if (left_metadata->len() != right_metadata->len() ||
        left_metadata->permissions().mode() != right_metadata->permissions().mode()) {
        return Ok(false);
    }
    auto left_contents = rstd::fs::read(left);
    auto right_contents = rstd::fs::read(right);
    if (left_contents.is_err()) {
        return store_io_failure<bool>(
            "read staged entry"_str, left, rstd::move(left_contents).unwrap_err());
    }
    if (right_contents.is_err()) {
        return store_io_failure<bool>(
            "read installed entry"_str, right, rstd::move(right_contents).unwrap_err());
    }
    if (left_contents->len() != right_contents->len()) return Ok(false);
    for (usize index {}; index < left_contents->len(); ++index) {
        if ((*left_contents)[index].get() != (*right_contents)[index].get()) return Ok(false);
    }
    return Ok(true);
}

auto stage_entry(const InstallEntry& entry, ref<rstd::path::Path> staged)
    -> InstallStoreResult<empty> {
    auto parent = staged.parent();
    if (parent.is_none()) return store_failure<empty>("staged entry has no parent"_str);
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return store_io_failure<empty>(
            "create staging parent"_str, *parent, rstd::move(created).unwrap_err());
    }
    if (entry.payload.is_CopyFile()) {
        const auto& source = entry.payload.as_CopyFile().source;
        auto metadata = rstd_try(path_metadata(source.as_path()));
        if (metadata.is_none() || ! metadata->is_file() || metadata->is_symlink()) {
            return store_failure<empty>(rstd::format(
                "install source '{}' is not a regular non-symlink file", source.as_path()));
        }
        auto copied = rstd::fs::copy(source.as_path(), staged);
        if (copied.is_err()) {
            return store_io_failure<empty>(
                "stage install entry"_str, source.as_path(), rstd::move(copied).unwrap_err());
        }
        auto permissions = rstd::fs::set_permissions(staged, metadata->permissions());
        if (permissions.is_err()) {
            return store_io_failure<empty>(
                "preserve install entry permissions"_str,
                source.as_path(),
                rstd::move(permissions).unwrap_err());
        }
        return Ok(empty {});
    }
    const auto& bytes = entry.payload.as_Bytes();
    auto written = rstd::fs::write(staged, bytes.contents.as_slice());
    if (written.is_err()) {
        return store_io_failure<empty>(
            "stage generated install entry"_str, staged, rstd::move(written).unwrap_err());
    }
    auto permissions =
        rstd::fs::set_permissions(staged, rstd::fs::Permissions::from_mode(bytes.permissions));
    if (permissions.is_err()) {
        return store_io_failure<empty>(
            "set generated install entry permissions"_str,
            staged,
            rstd::move(permissions).unwrap_err());
    }
    return Ok(empty {});
}

auto rollback(const Vec<PathBuf>& published, const Vec<BackupEntry>& backups)
    -> Vec<InstallRollbackFailure> {
    auto failures = Vec<InstallRollbackFailure>::make();
    for (const auto& path : published) {
        auto removed = rstd::fs::remove_file(path.as_path());
        if (removed.is_err() && rstd::fs::exists(path.as_path()).unwrap_or(false)) {
            failures.push(InstallRollbackFailure {
                .operation = String::make("remove published entry"_str),
                .path      = path.clone(),
                .source    = rstd::move(removed).unwrap_err(),
            });
        }
    }
    for (const auto& backup : backups) {
        auto restored = rstd::fs::rename(backup.backup.as_path(), backup.destination.as_path());
        if (restored.is_err()) {
            failures.push(InstallRollbackFailure {
                .operation = String::make("restore installed entry"_str),
                .path      = backup.destination.clone(),
                .source    = rstd::move(restored).unwrap_err(),
            });
        }
    }
    return failures;
}

auto transaction_failure(ref<str> operation,
                         InstallStoreError error,
                         const Vec<PathBuf>& published,
                         const Vec<BackupEntry>& backups) -> InstallStoreError {
    return InstallStoreError::Transaction(
        String::make(operation),
        rstd::boxed::Box<InstallStoreError>::make(rstd::move(error)),
        rollback(published, backups));
}

auto clean_empty_parents(ref<rstd::path::Path> root, ref<rstd::path::Path> path) -> void {
    auto parent = path.parent();
    if (parent.is_none()) return;
    auto current = PathBuf::from(*parent);
    while (current.as_path() != root) {
        if (rstd::fs::remove_dir(current.as_path()).is_err()) break;
        auto next = current.as_path().parent();
        if (next.is_none()) break;
        current = PathBuf::from(*next);
    }
}

auto clean_created_directories(const Vec<PathBuf>& directories) -> void {
    for (auto index = directories.len(); index > usize(); --index) {
        (void)rstd::fs::remove_dir(directories[index - usize(1)].as_path());
    }
}

auto normalize_legacy_entries(InstallPackageRecord& package) -> InstallStoreResult<empty> {
    for (auto& binary : package.binaries) {
        auto name = binary.source.as_path().file_name();
        if (name.is_none() || name->to_str().is_none()) {
            return store_failure<empty>(
                rstd::format("install artifact '{}' has no UTF-8 name", binary.source.as_path()));
        }
        auto destination = PathBuf::from("bin"_str);
        destination.push(PathBuf::from(*name).as_path());
        package.entries.push(InstallEntry {
            .origin = InstallEntryOrigin::BuildArtifact(binary.target.clone()),
            .payload = InstallEntryPayload::CopyFile(binary.source.clone()),
            .relative_destination = rstd::move(destination),
        });
    }
    return Ok(empty {});
}

} // namespace lito

export namespace lito
{

auto create_install_layout(InstallRoot root) -> InstallStoreResult<InstallLayout> {
    if (root.path.is_empty()) return store_failure<InstallLayout>("install root is required"_str);
    auto created = rstd::fs::create_dir_all(root.path.as_path());
    if (created.is_err()) {
        return store_io_failure<InstallLayout>(
            "create install root"_str, root.path.as_path(), rstd::move(created).unwrap_err());
    }
    auto canonical = rstd::fs::canonicalize(root.path.as_path());
    if (canonical.is_err()) {
        return store_io_failure<InstallLayout>(
            "resolve install root"_str, root.path.as_path(), rstd::move(canonical).unwrap_err());
    }
    root.path = rstd::move(canonical).unwrap();
    auto state = root.path.join(PathBuf::from(".lito"_str).as_path());
    return Ok(InstallLayout {
        .root            = InstallRoot { .path = root.path.clone() },
        .bin_directory   = root.path.join(PathBuf::from("bin"_str).as_path()),
        .state_directory = state.clone(),
        .metadata        = state.join(PathBuf::from("installed.json"_str).as_path()),
        .lock            = state.join(PathBuf::from("install.lock"_str).as_path()),
        .transactions    = state.join(PathBuf::from("transactions"_str).as_path()),
    });
}

auto install_artifacts(InstallStoreRequest request) -> InstallStoreResult<InstallStoreSummary> {
    if (request.packages.is_empty()) {
        return store_failure<InstallStoreSummary>("install request has no packages"_str);
    }
    auto requested_destinations = rstd::collections::BTreeMap<String, String>::make();
    for (usize package_index {}; package_index < request.packages.len(); ++package_index) {
        auto& package = request.packages[package_index];
        rstd_try(install_source_identity(package.provenance));
        if (! valid_package_name(package.name.as_str()) || package.version.is_empty() ||
            package.profile.is_empty() || package.target.is_empty()) {
            return store_failure<InstallStoreSummary>("install package identity is invalid"_str);
        }
        rstd_try(normalize_legacy_entries(package));
        if (package.entries.is_empty()) {
            return store_failure<InstallStoreSummary>(rstd::format(
                "install package '{}' has no entries", package.name.as_str()));
        }
        for (usize prior {}; prior < package_index; ++prior) {
            if (request.packages[prior].name == package.name.as_str()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install request repeats package '{}'", package.name.as_str()));
            }
        }
        for (const auto& entry : package.entries) {
            if (! normal_destination(entry.relative_destination.as_path())) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install destination '{}' is unsafe", entry.relative_destination.as_path()));
            }
            auto key = entry.relative_destination.as_path().to_string_lossy();
            auto prior = requested_destinations.get(key.as_str());
            if (prior.is_some()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install request contains more than one entry for destination '{}' from "
                    "packages '{}' and '{}'",
                    entry.relative_destination.as_path(), **prior, package.name.as_str()));
            }
            requested_destinations.insert(rstd::move(key), package.name.clone());
        }
    }
    auto requested_packages = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& package : request.packages) {
        requested_packages.insert(package.name.clone(),
                                  rstd_try(install_source_identity(package.provenance)));
    }
    for (const auto& package : request.packages) {
        auto runtime_names = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& dependency : package.runtime_dependencies) {
            if (runtime_names.contains_key(dependency.name.as_str())) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install package '{}' repeats runtime dependency '{}'",
                    package.name.as_str(), dependency.name.as_str()));
            }
            runtime_names.insert(dependency.name.clone(), empty {});
            auto target = requested_packages.get(dependency.name.as_str());
            if (target.is_none()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install package '{}' requires runtime package '{}' in the same transaction",
                    package.name.as_str(), dependency.name.as_str()));
            }
            if (**target != dependency.source_identity.as_str()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install package '{}' runtime dependency '{}' has source identity mismatch",
                    package.name.as_str(), dependency.name.as_str()));
            }
        }
    }

    auto layout = rstd_try(create_install_layout(rstd::move(request.root)));
    auto state_created = rstd::fs::create_dir_all(layout.state_directory.as_path());
    if (state_created.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "create install state directory"_str,
            layout.state_directory.as_path(),
            rstd::move(state_created).unwrap_err());
    }
    auto transactions_created = rstd::fs::create_dir_all(layout.transactions.as_path());
    if (transactions_created.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "create install transaction directory"_str,
            layout.transactions.as_path(),
            rstd::move(transactions_created).unwrap_err());
    }
    rstd_try(validate_directory(layout.state_directory.as_path(), "install state"_str));
    rstd_try(validate_directory(layout.transactions.as_path(), "install transaction"_str));
    auto lock = rstd::fs::File::options()
                    .read(true).write(true).create(true).open(layout.lock.as_path());
    if (lock.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "open install lock"_str, layout.lock.as_path(), rstd::move(lock).unwrap_err());
    }
    auto lock_file = rstd::move(lock).unwrap();
    auto locked = lock_file.lock();
    if (locked.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "lock install store"_str, layout.lock.as_path(), rstd::move(locked).unwrap_err());
    }
    auto document = rstd_try(load_document(layout.metadata.as_path()));

    auto transaction = layout.transactions.join(
        PathBuf::from(rstd::format("{}", rstd::process::id()).as_str()).as_path());
    auto stale = rstd_try(path_metadata(transaction.as_path()));
    if (stale.is_some()) {
        if (! stale->is_dir() || stale->is_symlink()) {
            return store_failure<InstallStoreSummary>(rstd::format(
                "stale install transaction '{}' is unsafe", transaction.as_path()));
        }
        auto removed = rstd::fs::remove_dir_all(transaction.as_path());
        if (removed.is_err()) {
            return store_io_failure<InstallStoreSummary>(
                "remove stale install transaction"_str,
                transaction.as_path(),
                rstd::move(removed).unwrap_err());
        }
    }
    auto staging = transaction.join(PathBuf::from("new"_str).as_path());
    auto backup = transaction.join(PathBuf::from("backup"_str).as_path());
    auto made = rstd::fs::create_dir_all(staging.as_path());
    if (made.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "create install staging directory"_str, staging.as_path(), rstd::move(made).unwrap_err());
    }
    made = rstd::fs::create_dir_all(backup.as_path());
    if (made.is_err()) {
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return store_io_failure<InstallStoreSummary>(
            "create install backup directory"_str, backup.as_path(), rstd::move(made).unwrap_err());
    }

    auto pending = Vec<PendingEntry>::make();
    for (usize package_index {}; package_index < request.packages.len(); ++package_index) {
        auto& package = request.packages[package_index];
        for (usize entry_index {}; entry_index < package.entries.len(); ++entry_index) {
            auto& entry = package.entries[entry_index];
            entry.destination = layout.root.path.join(entry.relative_destination.as_path());
            auto existing_result = path_metadata(entry.destination.as_path());
            if (existing_result.is_err()) {
                auto error = rstd::move(existing_result).unwrap_err();
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return Err(rstd::move(error));
            }
            auto existing = rstd::move(existing_result).unwrap();
            if (existing.is_some() && (! existing->is_file() || existing->is_symlink())) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install destination '{}' is not a regular non-symlink file",
                    entry.destination.as_path()));
            }
            auto owner = entry_owner(document, entry.relative_destination.as_path());
            auto source_identity = rstd_try(install_source_identity(package.provenance));
            if (owner.is_some() &&
                ! package_owner(document.packages[*owner],
                                package.name.as_str(), source_identity.as_str()) &&
                ! request.force) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return store_failure<InstallStoreSummary>(rstd::format(
                    "destination '{}' is already installed by package '{}'",
                    entry.relative_destination.as_path(), document.packages[*owner].name.as_str()));
            }
            if (existing.is_some() && owner.is_none() && ! request.force) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return store_failure<InstallStoreSummary>(rstd::format(
                    "destination '{}' is not managed by Lito; use --force to replace it",
                    entry.destination.as_path()));
            }
            auto staged = staging.join(entry.relative_destination.as_path());
            auto staged_result = stage_entry(entry, staged.as_path());
            if (staged_result.is_err()) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return Err(rstd::move(staged_result).unwrap_err());
            }
            auto action = InstallAction::Created;
            if (existing.is_some()) {
                auto equal = same_file(staged.as_path(), entry.destination.as_path());
                if (equal.is_err()) {
                    auto error = rstd::move(equal).unwrap_err();
                    (void)rstd::fs::remove_dir_all(transaction.as_path());
                    return Err(rstd::move(error));
                }
                action = *equal ? InstallAction::Unchanged : InstallAction::Replaced;
            }
            entry.action = action;
            pending.push(PendingEntry {
                .package = package_index,
                .entry   = entry_index,
                .staged  = rstd::move(staged),
                .action  = action,
            });
        }
    }

    auto orphans = Vec<PathBuf>::make();
    for (const auto& stored : document.packages) {
        const InstallPackageRecord* replacement = nullptr;
        for (const auto& package : request.packages) {
            auto source_identity = rstd_try(install_source_identity(package.provenance));
            if (package_owner(stored, package.name.as_str(), source_identity.as_str())) {
                replacement = rstd::addressof(package);
                break;
            }
        }
        if (replacement == nullptr) continue;
        for (const auto& old : stored.entries) {
            auto retained = false;
            for (const auto& current : replacement->entries) {
                if (current.relative_destination.as_path() == old.destination.as_path()) {
                    retained = true;
                    break;
                }
            }
            if (! retained) orphans.push(old.destination.clone());
        }
    }

    auto backups = Vec<BackupEntry>::make();
    auto backup_destination = [&](ref<rstd::path::Path> relative) -> InstallStoreResult<empty> {
        auto destination = layout.root.path.join(relative);
        auto metadata = rstd_try(path_metadata(destination.as_path()));
        if (metadata.is_none()) return Ok(empty {});
        rstd_try(ensure_parent_tree(backup.as_path(), relative));
        auto backup_path = backup.join(relative);
        auto renamed = rstd::fs::rename(destination.as_path(), backup_path.as_path());
        if (renamed.is_err()) {
            return store_io_failure<empty>(
                "back up install destination"_str,
                destination.as_path(),
                rstd::move(renamed).unwrap_err());
        }
        backups.push(BackupEntry {
            .destination = rstd::move(destination),
            .backup      = rstd::move(backup_path),
        });
        return Ok(empty {});
    };
    for (const auto& item : pending) {
        if (item.action == InstallAction::Unchanged) continue;
        auto backed = backup_destination(
            request.packages[item.package].entries[item.entry].relative_destination.as_path());
        if (backed.is_err()) {
            auto error = rstd::move(backed).unwrap_err();
            auto result = transaction_failure(
                "install backup"_str, rstd::move(error), Vec<PathBuf>::make(), backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            return Err(rstd::move(result));
        }
    }
    for (const auto& orphan : orphans) {
        auto backed = backup_destination(orphan.as_path());
        if (backed.is_err()) {
            auto error = rstd::move(backed).unwrap_err();
            auto result = transaction_failure(
                "install orphan backup"_str, rstd::move(error), Vec<PathBuf>::make(), backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            return Err(rstd::move(result));
        }
    }

    auto published = Vec<PathBuf>::make();
    auto created_directories = Vec<PathBuf>::make();
    for (const auto& item : pending) {
        if (item.action == InstallAction::Unchanged) continue;
        auto& entry = request.packages[item.package].entries[item.entry];
        auto prepared = ensure_parent_tree(layout.root.path.as_path(),
                                           entry.relative_destination.as_path(),
                                           rstd::addressof(created_directories));
        if (prepared.is_err()) {
            auto error = rstd::move(prepared).unwrap_err();
            auto result = transaction_failure(
                "install publish"_str, rstd::move(error), published, backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            clean_created_directories(created_directories);
            return Err(rstd::move(result));
        }
        auto moved = rstd::fs::rename(item.staged.as_path(), entry.destination.as_path());
        if (moved.is_err()) {
            auto error = InstallStoreError::Cause(InstallStoreCause::Io(
                String::make("publish install entry"_str),
                entry.destination.clone(),
                rstd::move(moved).unwrap_err()));
            auto result = transaction_failure(
                "install publish"_str, rstd::move(error), published, backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            clean_created_directories(created_directories);
            return Err(rstd::move(result));
        }
        published.push(entry.destination.clone());
    }

    for (usize package {}; package < document.packages.len();) {
        auto replaced = false;
        for (const auto& incoming : request.packages) {
            auto source_identity = rstd_try(install_source_identity(incoming.provenance));
            if (package_owner(document.packages[package],
                              incoming.name.as_str(), source_identity.as_str())) {
                replaced = true;
                break;
            }
        }
        if (replaced) {
            (void)document.packages.remove(package);
            continue;
        }
        if (request.force) {
            for (usize entry {}; entry < document.packages[package].entries.len();) {
                auto key = document.packages[package].entries[entry]
                               .destination.as_path().to_string_lossy();
                if (requested_destinations.contains_key(key.as_str()))
                    (void)document.packages[package].entries.remove(entry);
                else
                    ++entry;
            }
            if (document.packages[package].entries.is_empty()) {
                (void)document.packages.remove(package);
                continue;
            }
        }
        ++package;
    }

    auto installed_packages = Vec<String>::make();
    auto installed_entries = Vec<InstallEntry>::make();
    auto installed_binaries = Vec<InstallBinary>::make();
    for (auto& package : request.packages) {
        auto stored_entries = Vec<StoredEntry>::with_capacity(package.entries.len());
        for (auto& binary : package.binaries) {
            for (const auto& entry : package.entries) {
                if (! entry.origin.is_BuildArtifact() ||
                    entry.origin.as_BuildArtifact().target != binary.target) {
                    continue;
                }
                binary.destination = entry.destination.clone();
                binary.action      = entry.action;
                break;
            }
            installed_binaries.push(rstd::move(binary));
        }
        for (auto& entry : package.entries) {
            stored_entries.push(StoredEntry {
                .destination = entry.relative_destination.clone(),
                .origin      = origin_text(entry.origin),
            });
            installed_entries.push(rstd::move(entry));
        }
        installed_packages.push(package.name.clone());
        auto source_identity = rstd_try(install_source_identity(package.provenance));
        document.packages.push(StoredPackage {
            .name            = rstd::move(package.name),
            .version         = rstd::move(package.version),
            .source_identity = source_identity.clone(),
            .provenance      = rstd::move(package.provenance),
            .profile         = rstd::move(package.profile),
            .target          = rstd::move(package.target),
            .entries         = rstd::move(stored_entries),
            .runtime_dependencies = rstd::move(package.runtime_dependencies),
        });
    }
    auto runtime_valid = validate_runtime_dependencies(document);
    if (runtime_valid.is_err()) {
        auto result = transaction_failure("install metadata validation"_str,
                                          rstd::move(runtime_valid).unwrap_err(),
                                          published,
                                          backups);
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        clean_created_directories(created_directories);
        return Err(rstd::move(result));
    }
    auto written = write_document(layout.metadata.as_path(), document);
    if (written.is_err()) {
        auto error = rstd::move(written).unwrap_err();
        auto result = transaction_failure(
            "install metadata commit"_str, rstd::move(error), published, backups);
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        clean_created_directories(created_directories);
        return Err(rstd::move(result));
    }
    (void)rstd::fs::remove_dir_all(transaction.as_path());
    for (const auto& orphan : orphans) {
        clean_empty_parents(layout.root.path.as_path(),
                            layout.root.path.join(orphan.as_path()).as_path());
    }
    return Ok(InstallStoreSummary {
        .layout   = rstd::move(layout),
        .packages = rstd::move(installed_packages),
        .binaries = rstd::move(installed_binaries),
        .entries  = rstd::move(installed_entries),
    });
}

} // namespace lito
