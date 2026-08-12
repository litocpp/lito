module;
#include <rstd/macro.hpp>

export module lito.install.store;

import rstd;
import rstd.json;
import lito.error;
import lito.install.contract;
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

inline constexpr auto INSTALL_DOCUMENT_VERSION = u64(1);

struct StoredBinary {
    PackageTargetId target;
    String          name;
};

struct StoredPackage {
    String                  name;
    String                  version;
    String                  source_identity;
    InstallSourceProvenance provenance;
    String                  profile;
    String                  target;
    Vec<StoredBinary>       binaries;
};

struct InstalledDocument {
    Vec<StoredPackage> packages;
};

struct BackupEntry {
    PathBuf destination;
    PathBuf backup;
};

struct PendingBinary {
    usize         package {};
    usize         binary {};
    String        name;
    InstallAction action { InstallAction::Created };
};

template<typename T>
auto store_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Filesystem, rstd::move(message)));
}

template<typename T>
auto store_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Filesystem, message));
}

auto json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto safe_binary_name(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    auto path       = PathBuf::from(value);
    auto components = path.as_path().components();
    auto first      = components.next();
    if (first.is_none() || ! first->is_normal() || components.next().is_some()) return false;
    return first->as_os_str().to_str() == Some(value);
}

auto target_json(const PackageTargetId& target) -> Json {
    auto object = JsonMap::make();
    object.insert(String::make("kind"_str), json_string("bin"_str));
    object.insert(String::make("name"_str), json_string(target.name.as_str()));
    object.insert(String::make("package"_str), json_string(target.package.as_str()));
    return Json::Object(rstd::move(object));
}

auto package_json(const StoredPackage& package) -> Result<Json> {
    auto binaries = JsonArray::make();
    for (const auto& binary : package.binaries) {
        auto item = JsonMap::make();
        item.insert(String::make("name"_str), json_string(binary.name.as_str()));
        item.insert(String::make("target"_str), target_json(binary.target));
        binaries.push(Json::Object(rstd::move(item)));
    }

    auto object = JsonMap::make();
    object.insert(String::make("binaries"_str), Json::Array(rstd::move(binaries)));
    object.insert(String::make("name"_str), json_string(package.name.as_str()));
    object.insert(String::make("profile"_str), json_string(package.profile.as_str()));
    object.insert(String::make("source"_str),
                  rstd_try(serialize_install_source_provenance(package.provenance)));
    object.insert(String::make("target"_str), json_string(package.target.as_str()));
    object.insert(String::make("version"_str), json_string(package.version.as_str()));
    return Ok(Json::Object(rstd::move(object)));
}

auto document_json(const InstalledDocument& document) -> Result<Json> {
    auto packages = JsonArray::make();
    for (const auto& package : document.packages) {
        packages.push(rstd_try(package_json(package)));
    }
    auto root = JsonMap::make();
    root.insert(String::make("packages"_str), Json::Array(rstd::move(packages)));
    root.insert(String::make("version"_str),
                Json::Number(rstd::json::Number::from_u64(INSTALL_DOCUMENT_VERSION)));
    return Ok(Json::Object(rstd::move(root)));
}

using KeyPredicate = bool (*)(ref<str>);

auto reject_unknown(const Json& value, ref<str> context, KeyPredicate allowed) -> Result<empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return store_failure<empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return store_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto root_key(ref<str> key) -> bool {
    return key == "packages"_str || key == "version"_str;
}

auto package_key(ref<str> key) -> bool {
    return key == "binaries"_str || key == "name"_str || key == "profile"_str ||
           key == "source"_str || key == "target"_str || key == "version"_str;
}

auto binary_key(ref<str> key) -> bool {
    return key == "name"_str || key == "target"_str;
}

auto target_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "name"_str || key == "package"_str;
}

auto required_member(const Json& value, ref<str> key, ref<str> context) -> Result<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return store_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context) -> Result<String> {
    auto member = rstd_try(required_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none() || text->is_empty()) {
        return store_failure<String>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto parse_target(const Json& value) -> Result<PackageTargetId> {
    rstd_try(reject_unknown(value, "installed target"_str, target_key));
    auto kind = rstd_try(required_string(value, "kind"_str, "installed target"_str));
    if (kind != "bin"_str) {
        return store_failure<PackageTargetId>("installed target.kind must be 'bin'"_str);
    }
    auto name    = rstd_try(required_string(value, "name"_str, "installed target"_str));
    auto package = rstd_try(required_string(value, "package"_str, "installed target"_str));
    if (! valid_package_name(package.as_str())) {
        return store_failure<PackageTargetId>(
            rstd::format("installed target package '{}' is invalid", package.as_str()));
    }
    return Ok(PackageTargetId {
        .package = rstd::move(package),
        .kind    = PackageTargetKind::Binary,
        .name    = rstd::move(name),
    });
}

auto parse_binary(const Json& value) -> Result<StoredBinary> {
    rstd_try(reject_unknown(value, "installed binary"_str, binary_key));
    auto name = rstd_try(required_string(value, "name"_str, "installed binary"_str));
    if (! safe_binary_name(name.as_str())) {
        return store_failure<StoredBinary>(
            rstd::format("installed binary name '{}' is unsafe", name.as_str()));
    }
    auto target = rstd_try(required_member(value, "target"_str, "installed binary"_str));
    return Ok(StoredBinary {
        .target = rstd_try(parse_target(*target)),
        .name   = rstd::move(name),
    });
}

auto parse_package(const Json& value) -> Result<StoredPackage> {
    rstd_try(reject_unknown(value, "installed package"_str, package_key));
    auto name = rstd_try(required_string(value, "name"_str, "installed package"_str));
    if (! valid_package_name(name.as_str())) {
        return store_failure<StoredPackage>(
            rstd::format("installed package name '{}' is invalid", name.as_str()));
    }
    auto source          = rstd_try(required_member(value, "source"_str, "installed package"_str));
    auto provenance      = rstd_try(parse_install_source_provenance(*source));
    auto source_identity = rstd_try(install_source_identity(provenance));
    auto binaries_value = rstd_try(required_member(value, "binaries"_str, "installed package"_str));
    auto binary_array   = binaries_value->as_array();
    if (binary_array.is_none() || (**binary_array).is_empty()) {
        return store_failure<StoredPackage>(
            "installed package.binaries must be a non-empty array"_str);
    }
    auto binaries = Vec<StoredBinary>::make();
    for (const auto& binary : **binary_array) {
        auto parsed = rstd_try(parse_binary(binary));
        for (const auto& prior : binaries) {
            if (prior.name == parsed.name.as_str()) {
                return store_failure<StoredPackage>(
                    rstd::format("installed package '{}' repeats binary '{}'",
                                 name.as_str(),
                                 parsed.name.as_str()));
            }
        }
        if (parsed.target.package != name.as_str()) {
            return store_failure<StoredPackage>(
                rstd::format("installed binary '{}' belongs to package '{}', not '{}'",
                             parsed.name.as_str(),
                             parsed.target.package.as_str(),
                             name.as_str()));
        }
        binaries.push(rstd::move(parsed));
    }
    return Ok(StoredPackage {
        .name            = rstd::move(name),
        .version         = rstd_try(required_string(value, "version"_str, "installed package"_str)),
        .source_identity = rstd::move(source_identity),
        .provenance      = rstd::move(provenance),
        .profile         = rstd_try(required_string(value, "profile"_str, "installed package"_str)),
        .target          = rstd_try(required_string(value, "target"_str, "installed package"_str)),
        .binaries        = rstd::move(binaries),
    });
}

auto parse_document(const Json& value) -> Result<InstalledDocument> {
    rstd_try(reject_unknown(value, "installed document"_str, root_key));
    auto version = rstd_try(required_member(value, "version"_str, "installed document"_str));
    auto number  = version->as_u64();
    if (number.is_none() || *number != INSTALL_DOCUMENT_VERSION) {
        return store_failure<InstalledDocument>("installed document.version must be integer 1"_str);
    }
    auto packages_value =
        rstd_try(required_member(value, "packages"_str, "installed document"_str));
    auto package_array = packages_value->as_array();
    if (package_array.is_none()) {
        return store_failure<InstalledDocument>("installed document.packages must be an array"_str);
    }
    auto packages = Vec<StoredPackage>::make();
    auto binaries = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& package_value : **package_array) {
        auto package = rstd_try(parse_package(package_value));
        for (const auto& prior : packages) {
            if (prior.name == package.name.as_str() &&
                prior.source_identity == package.source_identity.as_str()) {
                return store_failure<InstalledDocument>(
                    rstd::format("installed document repeats package '{}' from source '{}'",
                                 package.name.as_str(),
                                 package.source_identity.as_str()));
            }
        }
        for (const auto& binary : package.binaries) {
            auto owner = binaries.get(binary.name.as_str());
            if (owner.is_some()) {
                return store_failure<InstalledDocument>(
                    rstd::format("installed binary '{}' is owned by both '{}' and '{}'",
                                 binary.name.as_str(),
                                 **owner,
                                 package.name.as_str()));
            }
            binaries.insert(binary.name.clone(), package.name.clone());
        }
        packages.push(rstd::move(package));
    }
    return Ok(InstalledDocument { .packages = rstd::move(packages) });
}

auto load_document(ref<rstd::path::Path> path) -> Result<InstalledDocument> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(InstalledDocument {});
        }
        return store_failure<InstalledDocument>(
            rstd::format("cannot inspect install metadata '{}': {}", path, error));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return store_failure<InstalledDocument>(
            rstd::format("install metadata '{}' is not a regular file", path));
    }
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return store_failure<InstalledDocument>(
            rstd::format("cannot read install metadata '{}': {}", path, contents.unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return store_failure<InstalledDocument>(
            rstd::format("cannot parse install metadata '{}': {}", path, parsed.unwrap_err()));
    }
    return parse_document(*parsed);
}

auto write_document(ref<rstd::path::Path> path, const InstalledDocument& document)
    -> Result<empty> {
    auto value = rstd_try(document_json(document));
    auto text  = rstd::json::to_string(
        value, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
    if (written.is_err()) {
        return store_failure<empty>(rstd::format(
            "cannot atomically write install metadata '{}': {}", path, written.unwrap_err()));
    }
    return Ok(empty {});
}

auto package_owner(const StoredPackage& package, ref<str> name, ref<str> source) -> bool {
    return package.name == name && package.source_identity == source;
}

auto binary_owner(const InstalledDocument& document, ref<str> name) -> Option<usize> {
    for (usize package {}; package < document.packages.len(); ++package) {
        for (const auto& binary : document.packages[package].binaries) {
            if (binary.name == name) return Some(package);
        }
    }
    return None();
}

auto path_metadata(ref<rstd::path::Path> path) -> Result<Option<rstd::fs::Metadata>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_ok()) return Ok(Some(rstd::move(metadata).unwrap()));
    auto error = rstd::move(metadata).unwrap_err();
    if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return Ok(Option<rstd::fs::Metadata> {});
    }
    return store_failure<Option<rstd::fs::Metadata>>(
        rstd::format("cannot inspect install destination '{}': {}", path, error));
}

auto same_install_file(ref<rstd::path::Path> left, ref<rstd::path::Path> right) -> Result<bool> {
    auto left_metadata = rstd::fs::metadata(left);
    if (left_metadata.is_err()) {
        return store_failure<bool>(rstd::format(
            "cannot inspect install artifact '{}': {}", left, left_metadata.unwrap_err()));
    }
    auto right_metadata = rstd::fs::metadata(right);
    if (right_metadata.is_err()) {
        return store_failure<bool>(rstd::format(
            "cannot inspect install destination '{}': {}", right, right_metadata.unwrap_err()));
    }
    if (left_metadata->len() != right_metadata->len() ||
        left_metadata->permissions().mode() != right_metadata->permissions().mode()) {
        return Ok(false);
    }
    auto left_contents = rstd::fs::read(left);
    if (left_contents.is_err()) {
        return store_failure<bool>(rstd::format(
            "cannot read install artifact '{}': {}", left, left_contents.unwrap_err()));
    }
    auto right_contents = rstd::fs::read(right);
    if (right_contents.is_err()) {
        return store_failure<bool>(rstd::format(
            "cannot read install destination '{}': {}", right, right_contents.unwrap_err()));
    }
    if (left_contents->len() != right_contents->len()) return Ok(false);
    auto left_slice  = left_contents->as_slice();
    auto right_slice = right_contents->as_slice();
    for (usize index {}; index < left_slice.len(); ++index) {
        if (left_slice[index] != right_slice[index]) return Ok(false);
    }
    return Ok(true);
}

auto validate_store_directory(ref<rstd::path::Path> path, ref<str> role) -> Result<empty> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        return store_failure<empty>(rstd::format(
            "cannot inspect {} directory '{}': {}", role, path, metadata.unwrap_err()));
    }
    if (! metadata->is_dir() || metadata->is_symlink()) {
        return store_failure<empty>(
            rstd::format("{} directory '{}' is not a real directory", role, path));
    }
    return Ok(empty {});
}

auto contains_name(const Vec<String>& values, ref<str> name) -> bool {
    for (const auto& value : values) {
        if (value == name) return true;
    }
    return false;
}

auto remove_binary(StoredPackage& package, ref<str> name) -> void {
    for (usize index {}; index < package.binaries.len();) {
        if (package.binaries[index].name == name)
            (void)package.binaries.remove(index);
        else
            ++index;
    }
}

auto rollback_install(const Vec<PathBuf>& published, const Vec<BackupEntry>& backups)
    -> Option<String> {
    auto error = Option<String> {};
    for (const auto& path : published) {
        auto exists = rstd::fs::exists(path.as_path());
        if (exists.is_ok() && *exists) {
            auto removed = rstd::fs::remove_file(path.as_path());
            if (removed.is_err() && error.is_none()) {
                error = Some(rstd::format("cannot remove published binary '{}': {}",
                                          path.as_path(),
                                          removed.unwrap_err()));
            }
        }
    }
    for (const auto& entry : backups) {
        auto restored = rstd::fs::rename(entry.backup.as_path(), entry.destination.as_path());
        if (restored.is_err() && error.is_none()) {
            error = Some(rstd::format("cannot restore binary '{}': {}",
                                      entry.destination.as_path(),
                                      restored.unwrap_err()));
        }
    }
    return error;
}

auto transaction_failure(ref<str>                operation,
                         const Error&            error,
                         const Vec<PathBuf>&     published,
                         const Vec<BackupEntry>& backups) -> Error {
    auto rollback = rollback_install(published, backups);
    if (rollback.is_some()) {
        return Error::make(ErrorKind::Filesystem,
                           rstd::format("{} failed: {}; rollback failed: {}",
                                        operation,
                                        error.message.as_str(),
                                        rollback->as_str()));
    }
    return Error::make(ErrorKind::Filesystem,
                       rstd::format("{} failed: {}", operation, error.message.as_str()));
}

auto fs_failure(ref<str> operation, ref<rstd::path::Path> path, const auto& error) -> Error {
    return Error::make(ErrorKind::Filesystem,
                       rstd::format("cannot {} '{}': {}", operation, path, error));
}

} // namespace lito

export namespace lito
{

auto create_install_layout(InstallRoot root) -> Result<InstallLayout>;

auto install_artifacts(InstallStoreRequest request) -> Result<InstallStoreSummary>;

auto create_install_layout(InstallRoot root) -> Result<InstallLayout> {
    if (root.path.is_empty()) {
        return store_failure<InstallLayout>("install root is required"_str);
    }
    auto created = rstd::fs::create_dir_all(root.path.as_path());
    if (created.is_err()) {
        return store_failure<InstallLayout>(rstd::format(
            "cannot create install root '{}': {}", root.path.as_path(), created.unwrap_err()));
    }
    auto canonical = rstd::fs::canonicalize(root.path.as_path());
    if (canonical.is_err()) {
        return store_failure<InstallLayout>(rstd::format(
            "cannot resolve install root '{}': {}", root.path.as_path(), canonical.unwrap_err()));
    }
    root.path            = rstd::move(canonical).unwrap();
    auto state_directory = root.path.join(PathBuf::from(".lito"_str).as_path());
    return Ok(InstallLayout {
        .root            = InstallRoot { .path = root.path.clone() },
        .bin_directory   = root.path.join(PathBuf::from("bin"_str).as_path()),
        .state_directory = state_directory.clone(),
        .metadata        = state_directory.join(PathBuf::from("installed.json"_str).as_path()),
        .lock            = state_directory.join(PathBuf::from("install.lock"_str).as_path()),
        .transactions    = state_directory.join(PathBuf::from("transactions"_str).as_path()),
    });
}

auto install_artifacts(InstallStoreRequest request) -> Result<InstallStoreSummary> {
    if (request.packages.is_empty()) {
        return store_failure<InstallStoreSummary>("install request has no packages"_str);
    }
    auto source_identity = rstd_try(install_source_identity(request.provenance));
    for (usize package {}; package < request.packages.len(); ++package) {
        const auto& value = request.packages[package];
        if (! valid_package_name(value.name.as_str()) || value.version.is_empty() ||
            value.profile.is_empty() || value.target.is_empty()) {
            return store_failure<InstallStoreSummary>("install package identity is invalid"_str);
        }
        if (value.binaries.is_empty()) {
            return store_failure<InstallStoreSummary>(
                rstd::format("install package '{}' has no binaries", value.name.as_str()));
        }
        for (usize prior {}; prior < package; ++prior) {
            if (request.packages[prior].name == value.name.as_str()) {
                return store_failure<InstallStoreSummary>(
                    rstd::format("install request repeats package '{}'", value.name.as_str()));
            }
        }
    }

    auto layout      = rstd_try(create_install_layout(rstd::move(request.root)));
    auto bin_created = rstd::fs::create_dir_all(layout.bin_directory.as_path());
    if (bin_created.is_err()) {
        return Err(fs_failure("create install bin directory"_str,
                              layout.bin_directory.as_path(),
                              bin_created.unwrap_err()));
    }
    rstd_try(validate_store_directory(layout.bin_directory.as_path(), "install bin"_str));
    auto state_created = rstd::fs::create_dir_all(layout.state_directory.as_path());
    if (state_created.is_err()) {
        return Err(fs_failure("create install state directory"_str,
                              layout.state_directory.as_path(),
                              state_created.unwrap_err()));
    }
    rstd_try(validate_store_directory(layout.state_directory.as_path(), "install state"_str));
    auto transactions_created = rstd::fs::create_dir_all(layout.transactions.as_path());
    if (transactions_created.is_err()) {
        return Err(fs_failure("create install transaction directory"_str,
                              layout.transactions.as_path(),
                              transactions_created.unwrap_err()));
    }
    rstd_try(validate_store_directory(layout.transactions.as_path(), "install transaction"_str));

    auto lock_metadata = rstd_try(path_metadata(layout.lock.as_path()));
    if (lock_metadata.is_some() && (! lock_metadata->is_file() || lock_metadata->is_symlink())) {
        return store_failure<InstallStoreSummary>(
            rstd::format("install lock '{}' is not a regular file", layout.lock.as_path()));
    }

    auto lock =
        rstd::fs::File::options().read(true).write(true).create(true).open(layout.lock.as_path());
    if (lock.is_err()) {
        return store_failure<InstallStoreSummary>(rstd::format(
            "cannot open install lock '{}': {}", layout.lock.as_path(), lock.unwrap_err()));
    }
    auto lock_file = rstd::move(lock).unwrap();
    auto locked    = lock_file.lock();
    if (locked.is_err()) {
        return store_failure<InstallStoreSummary>(rstd::format(
            "cannot lock install store '{}': {}", layout.lock.as_path(), locked.unwrap_err()));
    }

    auto document = rstd_try(load_document(layout.metadata.as_path()));
    auto pending  = Vec<PendingBinary>::make();
    auto names    = Vec<String>::make();
    for (usize package {}; package < request.packages.len(); ++package) {
        auto& owner_package = request.packages[package];
        for (usize binary_index {}; binary_index < owner_package.binaries.len(); ++binary_index) {
            auto& binary = owner_package.binaries[binary_index];
            if (binary.target.kind != PackageTargetKind::Binary ||
                binary.target.package != owner_package.name.as_str()) {
                return store_failure<InstallStoreSummary>(
                    rstd::format("install artifact '{}' does not belong to selected package '{}'",
                                 package_target_id_text(binary.target).as_str(),
                                 owner_package.name.as_str()));
            }
            auto file_name = binary.source.as_path().file_name();
            if (file_name.is_none() || file_name->to_str().is_none()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install artifact '{}' has no UTF-8 file name", binary.source.as_path()));
            }
            auto name = String::make(*file_name->to_str());
            if (! safe_binary_name(name.as_str())) {
                return store_failure<InstallStoreSummary>(
                    rstd::format("install artifact name '{}' is unsafe", name.as_str()));
            }
            if (contains_name(names, name.as_str())) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install request contains more than one binary named '{}'; use --package or "
                    "--bin to select one owner",
                    name.as_str()));
            }
            auto artifact_metadata = rstd_try(path_metadata(binary.source.as_path()));
            if (artifact_metadata.is_none() || ! artifact_metadata->is_file() ||
                artifact_metadata->is_symlink()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install artifact '{}' is not a regular file", binary.source.as_path()));
            }
            auto destination = layout.bin_directory.join(PathBuf::from(name.as_str()).as_path());
            auto destination_metadata = rstd_try(path_metadata(destination.as_path()));
            if (destination_metadata.is_some() && ! destination_metadata->is_file()) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install destination '{}' is not a regular file", destination.as_path()));
            }
            auto owner = binary_owner(document, name.as_str());
            if (owner.is_some() &&
                ! package_owner(document.packages[*owner],
                                owner_package.name.as_str(),
                                source_identity.as_str()) &&
                ! request.force) {
                return store_failure<InstallStoreSummary>(
                    rstd::format("binary '{}' is already installed by package '{}'",
                                 name.as_str(),
                                 document.packages[*owner].name.as_str()));
            }
            if (destination_metadata.is_some() && owner.is_none() && ! request.force) {
                return store_failure<InstallStoreSummary>(rstd::format(
                    "binary '{}' already exists and is not managed by Lito; use --force to "
                    "replace it",
                    destination.as_path()));
            }
            auto action = InstallAction::Created;
            if (destination_metadata.is_some()) {
                action = rstd_try(same_install_file(binary.source.as_path(), destination.as_path()))
                             ? InstallAction::Unchanged
                             : InstallAction::Replaced;
            }
            binary.destination = rstd::move(destination);
            pending.push(PendingBinary {
                .package = package,
                .binary  = binary_index,
                .name    = name.clone(),
                .action  = action,
            });
            names.push(rstd::move(name));
        }
    }

    auto orphan_names = Vec<String>::make();
    for (const auto& stored : document.packages) {
        const InstallPackageRecord* replacement = nullptr;
        for (const auto& package : request.packages) {
            if (package_owner(stored, package.name.as_str(), source_identity.as_str())) {
                replacement = rstd::addressof(package);
                break;
            }
        }
        if (replacement == nullptr) continue;
        for (const auto& binary : stored.binaries) {
            auto retained = false;
            for (const auto& current : replacement->binaries) {
                auto current_name = current.source.as_path().file_name();
                if (current_name.is_some() &&
                    current_name->to_str() == Some(binary.name.as_str())) {
                    retained = true;
                    break;
                }
            }
            if (! retained && ! contains_name(names, binary.name.as_str()) &&
                ! contains_name(orphan_names, binary.name.as_str())) {
                orphan_names.push(binary.name.clone());
            }
        }
    }

    auto transaction = layout.transactions.join(
        PathBuf::from(rstd::format("{}", rstd::process::id()).as_str()).as_path());
    auto transaction_exists = rstd_try(path_metadata(transaction.as_path()));
    if (transaction_exists.is_some()) {
        if (! transaction_exists->is_dir() || transaction_exists->is_symlink()) {
            return store_failure<InstallStoreSummary>(rstd::format(
                "stale install transaction '{}' is not a real directory", transaction.as_path()));
        }
        auto removed = rstd::fs::remove_dir_all(transaction.as_path());
        if (removed.is_err()) {
            return store_failure<InstallStoreSummary>(
                rstd::format("cannot remove stale install transaction '{}': {}",
                             transaction.as_path(),
                             removed.unwrap_err()));
        }
    }
    auto staging         = transaction.join(PathBuf::from("new"_str).as_path());
    auto backup          = transaction.join(PathBuf::from("backup"_str).as_path());
    auto staging_created = rstd::fs::create_dir_all(staging.as_path());
    if (staging_created.is_err()) {
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return Err(fs_failure("create install staging directory"_str,
                              staging.as_path(),
                              staging_created.unwrap_err()));
    }
    auto backup_created = rstd::fs::create_dir_all(backup.as_path());
    if (backup_created.is_err()) {
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return Err(fs_failure(
            "create install backup directory"_str, backup.as_path(), backup_created.unwrap_err()));
    }

    for (const auto& item : pending) {
        if (item.action == InstallAction::Unchanged) continue;
        const auto& binary = request.packages[item.package].binaries[item.binary];
        auto        staged = staging.join(PathBuf::from(item.name.as_str()).as_path());
        auto        copied = rstd::fs::copy(binary.source.as_path(), staged.as_path());
        if (copied.is_err()) {
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            return store_failure<InstallStoreSummary>(
                rstd::format("cannot stage install artifact '{}': {}",
                             binary.source.as_path(),
                             copied.unwrap_err()));
        }
        auto source_metadata = rstd::fs::metadata(binary.source.as_path());
        if (source_metadata.is_err()) {
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            return store_failure<InstallStoreSummary>(
                rstd::format("cannot inspect staged install artifact '{}': {}",
                             binary.source.as_path(),
                             source_metadata.unwrap_err()));
        }
        auto permissions =
            rstd::fs::set_permissions(staged.as_path(), source_metadata->permissions());
        if (permissions.is_err()) {
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            return store_failure<InstallStoreSummary>(
                rstd::format("cannot preserve install artifact permissions '{}': {}",
                             binary.source.as_path(),
                             permissions.unwrap_err()));
        }
    }

    auto backups            = Vec<BackupEntry>::make();
    auto backup_destination = [&](ref<rstd::path::Path> destination,
                                  ref<str>              name) -> Result<empty> {
        auto metadata = rstd_try(path_metadata(destination));
        if (metadata.is_none()) return Ok(empty {});
        auto backup_path = backup.join(PathBuf::from(name).as_path());
        auto renamed     = rstd::fs::rename(destination, backup_path.as_path());
        if (renamed.is_err()) {
            return store_failure<empty>(rstd::format(
                "cannot back up install destination '{}': {}", destination, renamed.unwrap_err()));
        }
        backups.push(BackupEntry {
            .destination = PathBuf::from(destination),
            .backup      = rstd::move(backup_path),
        });
        return Ok(empty {});
    };
    for (const auto& item : pending) {
        if (item.action == InstallAction::Unchanged) continue;
        const auto& binary = request.packages[item.package].binaries[item.binary];
        auto        backed = backup_destination(binary.destination.as_path(), item.name.as_str());
        if (backed.is_err()) {
            auto error    = rstd::move(backed).unwrap_err();
            auto rollback = rollback_install(Vec<PathBuf>::make(), backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            if (rollback.is_some()) error.message.push_str(rollback->as_str());
            return Err(rstd::move(error));
        }
    }
    for (const auto& name : orphan_names) {
        auto destination = layout.bin_directory.join(PathBuf::from(name.as_str()).as_path());
        auto backed      = backup_destination(destination.as_path(), name.as_str());
        if (backed.is_err()) {
            auto error    = rstd::move(backed).unwrap_err();
            auto rollback = rollback_install(Vec<PathBuf>::make(), backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            if (rollback.is_some()) error.message.push_str(rollback->as_str());
            return Err(rstd::move(error));
        }
    }

    auto published = Vec<PathBuf>::make();
    for (const auto& item : pending) {
        if (item.action == InstallAction::Unchanged) continue;
        const auto& binary = request.packages[item.package].binaries[item.binary];
        auto        staged = staging.join(PathBuf::from(item.name.as_str()).as_path());
        auto        moved  = rstd::fs::rename(staged.as_path(), binary.destination.as_path());
        if (moved.is_err()) {
            auto error = fs_failure(
                "publish installed binary"_str, binary.destination.as_path(), moved.unwrap_err());
            auto result = transaction_failure("install publish"_str, error, published, backups);
            (void)rstd::fs::remove_dir_all(transaction.as_path());
            return Err(rstd::move(result));
        }
        published.push(binary.destination.clone());
    }

    for (usize package {}; package < document.packages.len();) {
        auto replaced = false;
        for (const auto& incoming : request.packages) {
            if (package_owner(
                    document.packages[package], incoming.name.as_str(), source_identity.as_str())) {
                replaced = true;
                break;
            }
        }
        if (replaced) {
            (void)document.packages.remove(package);
            continue;
        }
        if (request.force) {
            for (const auto& name : names) remove_binary(document.packages[package], name.as_str());
            if (document.packages[package].binaries.is_empty()) {
                (void)document.packages.remove(package);
                continue;
            }
        }
        ++package;
    }

    auto installed          = Vec<InstallBinary>::make();
    auto installed_packages = Vec<String>::make();
    for (usize package_index {}; package_index < request.packages.len(); ++package_index) {
        auto& package         = request.packages[package_index];
        auto  stored_binaries = Vec<StoredBinary>::make();
        for (usize binary_index {}; binary_index < package.binaries.len(); ++binary_index) {
            auto& binary  = package.binaries[binary_index];
            auto  matched = static_cast<const PendingBinary*>(nullptr);
            for (const auto& item : pending) {
                if (item.package != package_index || item.binary != binary_index) continue;
                matched = rstd::addressof(item);
                break;
            }
            if (matched == nullptr) {
                return store_failure<InstallStoreSummary>(
                    "install transaction lost a selected binary"_str);
            }
            binary.action = matched->action;
            stored_binaries.push(StoredBinary {
                .target = binary.target.clone(),
                .name   = matched->name.clone(),
            });
            installed.push(InstallBinary {
                .target      = binary.target.clone(),
                .source      = binary.source.clone(),
                .destination = binary.destination.clone(),
                .action      = binary.action,
            });
        }
        installed_packages.push(package.name.clone());
        document.packages.push(StoredPackage {
            .name            = rstd::move(package.name),
            .version         = rstd::move(package.version),
            .source_identity = source_identity.clone(),
            .provenance      = request.provenance.clone(),
            .profile         = rstd::move(package.profile),
            .target          = rstd::move(package.target),
            .binaries        = rstd::move(stored_binaries),
        });
    }
    auto written = write_document(layout.metadata.as_path(), document);
    if (written.is_err()) {
        auto error  = rstd::move(written).unwrap_err();
        auto result = transaction_failure("install metadata commit"_str, error, published, backups);
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return Err(rstd::move(result));
    }

    (void)rstd::fs::remove_dir_all(transaction.as_path());
    return Ok(InstallStoreSummary {
        .layout   = rstd::move(layout),
        .packages = rstd::move(installed_packages),
        .binaries = rstd::move(installed),
    });
}

} // namespace lito
