module;
#include <initializer_list>
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import rstd.json;
import lito.core;
import lito.toolchain;
import :sdk;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf   = rstd::path::PathBuf;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

struct InstalledSdkDescriptor {
    String                     version;
    lito::system::HostInfo     host;
    String                     url;
    String                     sha256;
    u64                        size {};
    lito::LlvmSdkPaths         paths;
    lito::LlvmSdkCertification certification;
};

struct SdkStoreLayout {
    PathBuf root;
    PathBuf locks;
    PathBuf staging;

    auto version(ref<str> value) const -> PathBuf {
        return root.join(PathBuf::from(value).as_path());
    }

    auto lock(ref<str> value) const -> PathBuf {
        return locks.join(PathBuf::from(rstd::format("{}.lock", value)).as_path());
    }

    auto staging_area(ref<str> version, const lito::system::HostInfo& host) const -> PathBuf {
        return staging.join(
            PathBuf::from(
                rstd::format("{}-{}-{}", version, host.os.as_str(), host.architecture.as_str()))
                .as_path());
    }
};

template<typename T>
auto sdk_failure(String message) -> lito::SdkResult<T> {
    return Err(lito::SdkError::Message(rstd::move(message)));
}

template<typename T>
auto sdk_failure(ref<str> message) -> lito::SdkResult<T> {
    return sdk_failure<T>(String::make(message));
}

template<typename T>
auto sdk_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> lito::SdkResult<T> {
    return Err(
        lito::SdkError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto host_text(const lito::system::HostInfo& host) -> String {
    return rstd::format("{}-{}", host.os.as_str(), host.architecture.as_str());
}

auto json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto known_fields(const Json& value, ref<str> context, std::initializer_list<ref<str>> names)
    -> lito::SdkResult<empty> {
    auto object = value.as_object();
    if (object.is_none()) return sdk_failure<empty>(rstd::format("{} must be an object", context));
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        auto known = false;
        for (const auto name : names) {
            if ((**key).as_str() == name) known = true;
        }
        if (! known) {
            return sdk_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto required_member(const Json& value, ref<str> key, ref<str> context)
    -> lito::SdkResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return sdk_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context) -> lito::SdkResult<String> {
    auto member = rstd_try(required_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none() || text->is_empty()) {
        return sdk_failure<String>(rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto required_u64(const Json& value, ref<str> key, ref<str> context) -> lito::SdkResult<u64> {
    auto member = rstd_try(required_member(value, key, context));
    auto number = member->as_u64();
    if (number.is_none()) {
        return sdk_failure<u64>(rstd::format("{}.{} must be an unsigned integer", context, key));
    }
    return Ok(*number);
}

auto required_bool(const Json& value, ref<str> key, ref<str> context) -> lito::SdkResult<bool> {
    auto member  = rstd_try(required_member(value, key, context));
    auto boolean = member->as_bool();
    if (boolean.is_none()) {
        return sdk_failure<bool>(rstd::format("{}.{} must be a boolean", context, key));
    }
    return Ok(*boolean);
}

auto paths_json(const lito::LlvmSdkPaths& paths) -> Json {
    auto value = JsonMap::make();
    value.insert(String::make("cc"_str), json_string(paths.cc.as_path().to_str().unwrap()));
    value.insert(String::make("cxx"_str), json_string(paths.cxx.as_path().to_str().unwrap()));
    value.insert(String::make("linker"_str), json_string(paths.linker.as_path().to_str().unwrap()));
    value.insert(String::make("archiver"_str),
                 json_string(paths.archiver.as_path().to_str().unwrap()));
    value.insert(String::make("strip"_str), json_string(paths.strip.as_path().to_str().unwrap()));
    value.insert(String::make("format"_str), json_string(paths.format.as_path().to_str().unwrap()));
    value.insert(String::make("llvm-config"_str),
                 json_string(paths.llvm_config.as_path().to_str().unwrap()));
    value.insert(String::make("cmake"_str), json_string(paths.cmake.as_path().to_str().unwrap()));
    value.insert(String::make("clang-cpp"_str),
                 json_string(paths.clang_cpp.as_path().to_str().unwrap()));
    return Json::Object(rstd::move(value));
}

auto parse_paths(const Json& value) -> lito::SdkResult<lito::LlvmSdkPaths> {
    rstd_try(known_fields(value,
                          "LLVM SDK descriptor paths"_str,
                          { "cc"_str,
                            "cxx"_str,
                            "linker"_str,
                            "archiver"_str,
                            "strip"_str,
                            "format"_str,
                            "llvm-config"_str,
                            "cmake"_str,
                            "clang-cpp"_str }));
    auto result = lito::LlvmSdkPaths {
        .cc = PathBuf::from(
            rstd_try(required_string(value, "cc"_str, "LLVM SDK descriptor paths"_str))),
        .cxx = PathBuf::from(
            rstd_try(required_string(value, "cxx"_str, "LLVM SDK descriptor paths"_str))),
        .linker = PathBuf::from(
            rstd_try(required_string(value, "linker"_str, "LLVM SDK descriptor paths"_str))),
        .archiver = PathBuf::from(
            rstd_try(required_string(value, "archiver"_str, "LLVM SDK descriptor paths"_str))),
        .strip = PathBuf::from(
            rstd_try(required_string(value, "strip"_str, "LLVM SDK descriptor paths"_str))),
        .format = PathBuf::from(
            rstd_try(required_string(value, "format"_str, "LLVM SDK descriptor paths"_str))),
        .llvm_config = PathBuf::from(
            rstd_try(required_string(value, "llvm-config"_str, "LLVM SDK descriptor paths"_str))),
        .cmake = PathBuf::from(
            rstd_try(required_string(value, "cmake"_str, "LLVM SDK descriptor paths"_str))),
        .clang_cpp = PathBuf::from(
            rstd_try(required_string(value, "clang-cpp"_str, "LLVM SDK descriptor paths"_str))),
    };
    auto valid = lito::validate_llvm_sdk_paths(result, "LLVM SDK descriptor paths"_str);
    if (valid.is_err()) return Err(lito::SdkError::Catalog(rstd::move(valid).unwrap_err()));
    return Ok(rstd::move(result));
}

auto descriptor_json(const InstalledSdkDescriptor& descriptor) -> Json {
    auto host = JsonMap::make();
    host.insert(String::make("os"_str), json_string(descriptor.host.os.as_str()));
    host.insert(String::make("architecture"_str),
                json_string(descriptor.host.architecture.as_str()));

    auto archive = JsonMap::make();
    archive.insert(String::make("url"_str), json_string(descriptor.url.as_str()));
    archive.insert(String::make("sha256"_str), json_string(descriptor.sha256.as_str()));
    archive.insert(String::make("size"_str),
                   Json::Number(rstd::json::Number::from_u64(descriptor.size)));

    auto certification = JsonMap::make();
    certification.insert(String::make("compiler-version"_str),
                         json_string(descriptor.certification.compiler_version.as_str()));
    certification.insert(String::make("standard-library"_str),
                         json_string(lito::config::standard_library_name(
                             descriptor.certification.standard_library)));
    certification.insert(String::make("exceptions"_str),
                         Json::Bool(descriptor.certification.exceptions));
    certification.insert(String::make("rtti"_str), Json::Bool(descriptor.certification.rtti));

    auto root = JsonMap::make();
    root.insert(String::make("schema"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));
    root.insert(String::make("kind"_str), json_string("lito-llvm-sdk"_str));
    root.insert(String::make("version"_str), json_string(descriptor.version.as_str()));
    root.insert(String::make("host"_str), Json::Object(rstd::move(host)));
    root.insert(String::make("archive"_str), Json::Object(rstd::move(archive)));
    root.insert(String::make("paths"_str), paths_json(descriptor.paths));
    root.insert(String::make("certification"_str), Json::Object(rstd::move(certification)));
    return Json::Object(rstd::move(root));
}

auto serialize_descriptor(const InstalledSdkDescriptor& descriptor) -> String {
    auto text =
        rstd::json::to_string(descriptor_json(descriptor),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return text;
}

auto parse_descriptor(const Json& value) -> lito::SdkResult<InstalledSdkDescriptor> {
    rstd_try(known_fields(value,
                          "LLVM SDK descriptor root"_str,
                          { "schema"_str,
                            "kind"_str,
                            "version"_str,
                            "host"_str,
                            "archive"_str,
                            "paths"_str,
                            "certification"_str }));
    auto schema = rstd_try(required_u64(value, "schema"_str, "LLVM SDK descriptor root"_str));
    if (schema != u64(1)) {
        return sdk_failure<InstalledSdkDescriptor>(
            rstd::format("LLVM SDK descriptor schema {} is not supported", schema));
    }
    auto kind = rstd_try(required_string(value, "kind"_str, "LLVM SDK descriptor root"_str));
    if (kind.as_str() != "lito-llvm-sdk"_str) {
        return sdk_failure<InstalledSdkDescriptor>(
            rstd::format("LLVM SDK descriptor kind '{}' is not supported", kind));
    }
    auto version = rstd_try(required_string(value, "version"_str, "LLVM SDK descriptor root"_str));
    auto parsed_version = lito::parse_llvm_version(version.as_str());
    if (parsed_version.is_err()) {
        return Err(lito::SdkError::Catalog(rstd::move(parsed_version).unwrap_err()));
    }

    auto host_value = rstd_try(required_member(value, "host"_str, "LLVM SDK descriptor root"_str));
    rstd_try(known_fields(
        *host_value, "LLVM SDK descriptor host"_str, { "os"_str, "architecture"_str }));
    auto os = rstd_try(required_string(*host_value, "os"_str, "LLVM SDK descriptor host"_str));
    if (os.as_str() != "linux"_str && os.as_str() != "macos"_str && os.as_str() != "windows"_str) {
        return sdk_failure<InstalledSdkDescriptor>(
            rstd::format("LLVM SDK descriptor host OS '{}' is not canonical", os));
    }
    auto architecture_text =
        rstd_try(required_string(*host_value, "architecture"_str, "LLVM SDK descriptor host"_str));
    auto architecture = lito::system::canonical_architecture(architecture_text.as_str());
    if (architecture.is_err() ||
        (architecture.is_ok() && architecture->as_str() != architecture_text.as_str())) {
        return sdk_failure<InstalledSdkDescriptor>(rstd::format(
            "LLVM SDK descriptor architecture '{}' is not canonical", architecture_text));
    }

    auto archive = rstd_try(required_member(value, "archive"_str, "LLVM SDK descriptor root"_str));
    rstd_try(known_fields(
        *archive, "LLVM SDK descriptor archive"_str, { "url"_str, "sha256"_str, "size"_str }));
    auto url = rstd_try(required_string(*archive, "url"_str, "LLVM SDK descriptor archive"_str));
    auto sha256 =
        rstd_try(required_string(*archive, "sha256"_str, "LLVM SDK descriptor archive"_str));
    auto size = rstd_try(required_u64(*archive, "size"_str, "LLVM SDK descriptor archive"_str));
    auto archive_identity = lito::validate_llvm_sdk_archive_identity(
        url.as_str(), sha256.as_str(), size, "LLVM SDK descriptor archive"_str);
    if (archive_identity.is_err()) {
        return Err(lito::SdkError::Catalog(rstd::move(archive_identity).unwrap_err()));
    }
    auto paths_value =
        rstd_try(required_member(value, "paths"_str, "LLVM SDK descriptor root"_str));
    auto paths = rstd_try(parse_paths(*paths_value));

    auto certification =
        rstd_try(required_member(value, "certification"_str, "LLVM SDK descriptor root"_str));
    rstd_try(known_fields(
        *certification,
        "LLVM SDK descriptor certification"_str,
        { "compiler-version"_str, "standard-library"_str, "exceptions"_str, "rtti"_str }));
    auto compiler_version = rstd_try(required_string(
        *certification, "compiler-version"_str, "LLVM SDK descriptor certification"_str));
    if (compiler_version.as_str() != version.as_str()) {
        return sdk_failure<InstalledSdkDescriptor>(
            rstd::format("LLVM SDK descriptor compiler version '{}' differs from version '{}'",
                         compiler_version,
                         version));
    }
    auto standard_library_text = rstd_try(required_string(
        *certification, "standard-library"_str, "LLVM SDK descriptor certification"_str));
    auto standard_library = lito::config::parse_standard_library(standard_library_text.as_str());
    if (standard_library.is_none()) {
        return sdk_failure<InstalledSdkDescriptor>(rstd::format(
            "LLVM SDK descriptor standard library '{}' is invalid", standard_library_text));
    }
    return Ok(InstalledSdkDescriptor {
        .version = rstd::move(version),
        .host =
            lito::system::HostInfo {
                .architecture = rstd::move(architecture).unwrap(),
                .os           = rstd::move(os),
            },
        .url    = rstd::move(url),
        .sha256 = rstd::move(sha256),
        .size   = size,
        .paths  = rstd::move(paths),
        .certification =
            lito::LlvmSdkCertification {
                .compiler_version = rstd::move(compiler_version),
                .standard_library = *standard_library,
                .exceptions       = rstd_try(required_bool(
                    *certification, "exceptions"_str, "LLVM SDK descriptor certification"_str)),
                .rtti             = rstd_try(required_bool(
                    *certification, "rtti"_str, "LLVM SDK descriptor certification"_str)),
            },
    });
}

auto load_descriptor(ref<rstd::path::Path> prefix)
    -> lito::SdkResult<Option<InstalledSdkDescriptor>> {
    auto path     = PathBuf::from(prefix).join(PathBuf::from("sdk.json"_str).as_path());
    auto metadata = rstd::fs::symlink_metadata(path.as_path());
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(None());
        }
        return sdk_io_failure<Option<InstalledSdkDescriptor>>(
            "inspect LLVM SDK descriptor"_str, path.as_path(), rstd::move(error));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return sdk_failure<Option<InstalledSdkDescriptor>>(
            rstd::format("LLVM SDK descriptor '{}' must be an ordinary file", path.as_path()));
    }
    auto contents = rstd::fs::read_to_string(path.as_path());
    if (contents.is_err()) {
        return sdk_io_failure<Option<InstalledSdkDescriptor>>(
            "read LLVM SDK descriptor"_str, path.as_path(), rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(lito::SdkError::Json(rstd::move(path), rstd::move(parsed).unwrap_err()));
    }
    return Ok(Some(rstd_try(parse_descriptor(*parsed))));
}

auto paths_equal(const lito::LlvmSdkPaths& left, const lito::LlvmSdkPaths& right) -> bool {
    return left.cc.as_path() == right.cc.as_path() && left.cxx.as_path() == right.cxx.as_path() &&
           left.linker.as_path() == right.linker.as_path() &&
           left.archiver.as_path() == right.archiver.as_path() &&
           left.strip.as_path() == right.strip.as_path() &&
           left.format.as_path() == right.format.as_path() &&
           left.llvm_config.as_path() == right.llvm_config.as_path() &&
           left.cmake.as_path() == right.cmake.as_path() &&
           left.clang_cpp.as_path() == right.clang_cpp.as_path();
}

auto descriptor_matches(const InstalledSdkDescriptor& descriptor,
                        const lito::LlvmSdkRelease&   release,
                        const lito::LlvmSdkArtifact&  artifact) -> bool {
    return descriptor.version == release.version.text.as_str() &&
           descriptor.host.os == artifact.host.os.as_str() &&
           descriptor.host.architecture == artifact.host.architecture &&
           descriptor.sha256 == artifact.archive.sha256.as_str() &&
           descriptor.size == artifact.archive.size &&
           paths_equal(descriptor.paths, artifact.paths);
}

auto certification_matches(const lito::LlvmSdkCertification& left,
                           const lito::LlvmSdkCertification& right) -> bool {
    return left.compiler_version == right.compiler_version.as_str() &&
           left.standard_library == right.standard_library && left.exceptions == right.exceptions &&
           left.rtti == right.rtti;
}

auto sdk_store_layout() -> lito::SdkResult<SdkStoreLayout> {
    auto data = lito::system::LitoDataRoot::resolve();
    if (data.is_err()) return Err(lito::SdkError::System(rstd::move(data).unwrap_err()));
    auto root = PathBuf::from(data->root()).join(PathBuf::from("llvm"_str).as_path());
    return Ok(SdkStoreLayout {
        .root    = root.clone(),
        .locks   = root.join(PathBuf::from(".locks"_str).as_path()),
        .staging = root.join(PathBuf::from(".staging"_str).as_path()),
    });
}

auto ensure_store(const SdkStoreLayout& layout) -> lito::SdkResult<empty> {
    const ref<rstd::path::Path> directories[] = { layout.root.as_path(),
                                                  layout.locks.as_path(),
                                                  layout.staging.as_path() };
    for (const auto directory : directories) {
        auto created = rstd::fs::create_dir_all(directory);
        if (created.is_err()) {
            return sdk_io_failure<empty>(
                "create LLVM SDK store"_str, directory, rstd::move(created).unwrap_err());
        }
        auto metadata = rstd::fs::symlink_metadata(directory);
        if (metadata.is_err()) {
            return sdk_io_failure<empty>(
                "inspect LLVM SDK store"_str, directory, rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_dir() || metadata->is_symlink()) {
            return sdk_failure<empty>(
                rstd::format("LLVM SDK store '{}' must be a real directory", directory));
        }
    }
    return Ok(empty {});
}

auto acquire_version_lock(const SdkStoreLayout& layout, ref<str> version)
    -> lito::SdkResult<rstd::fs::FileLock> {
    rstd_try(ensure_store(layout));
    auto path = layout.lock(version);
    auto opened =
        rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(path.as_path());
    if (opened.is_err()) {
        return sdk_io_failure<rstd::fs::FileLock>(
            "open LLVM SDK version lock"_str, path.as_path(), rstd::move(opened).unwrap_err());
    }
    auto metadata = opened->metadata();
    if (metadata.is_err()) {
        return sdk_io_failure<rstd::fs::FileLock>(
            "inspect LLVM SDK version lock"_str, path.as_path(), rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file()) {
        return sdk_failure<rstd::fs::FileLock>(
            rstd::format("LLVM SDK version lock '{}' must be an ordinary file", path.as_path()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return sdk_io_failure<rstd::fs::FileLock>(
            "lock LLVM SDK version"_str, path.as_path(), rstd::move(locked).unwrap_err());
    }
    return Ok(rstd::move(locked).unwrap());
}

struct SdkAcquisitionObserver {
    ref<str>                   version;
    Option<lito::SdkEventSink> sink;
};

void observe_acquisition(void* raw, const lito::acquisition::AcquisitionEvent& event) noexcept {
    auto& observer = *static_cast<SdkAcquisitionObserver*>(raw);
    if (observer.sink.is_none() || observer.sink->notify == nullptr) return;
    observer.sink->notify(observer.sink->context,
                          lito::SdkEvent {
                              .kind = event.kind == lito::acquisition::AcquisitionEventKind::Fetch
                                          ? lito::SdkEventKind::Fetch
                                          : lito::SdkEventKind::Extract,
                              .version     = observer.version,
                              .source      = event.source,
                              .destination = event.destination,
                          });
}

auto remove_staging(ref<rstd::path::Path> path) -> void {
    auto exists = rstd::fs::exists(path);
    if (exists.is_ok() && *exists) (void)rstd::fs::remove_dir_all(path);
}

auto installed_list_entry(ref<str>            version,
                          ref<str>            host,
                          lito::SdkListStatus status,
                          PathBuf             prefix,
                          Option<String>      issue = None()) -> lito::SdkListEntry {
    return lito::SdkListEntry {
        .version = String::make(version),
        .host    = String::make(host),
        .status  = status,
        .prefix  = Some(rstd::move(prefix)),
        .issue   = rstd::move(issue),
    };
}

auto scan_installed(const SdkStoreLayout& layout, const lito::system::HostInfo& current_host)
    -> lito::SdkResult<Vec<lito::SdkListEntry>> {
    auto result   = Vec<lito::SdkListEntry>::make();
    auto metadata = rstd::fs::symlink_metadata(layout.root.as_path());
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(rstd::move(result));
        }
        return sdk_io_failure<Vec<lito::SdkListEntry>>(
            "inspect LLVM SDK store"_str, layout.root.as_path(), rstd::move(error));
    }
    if (! metadata->is_dir() || metadata->is_symlink()) {
        return sdk_failure<Vec<lito::SdkListEntry>>(
            rstd::format("LLVM SDK store '{}' must be a real directory", layout.root.as_path()));
    }
    auto opened = rstd::fs::read_dir(layout.root.as_path());
    if (opened.is_err()) {
        return sdk_io_failure<Vec<lito::SdkListEntry>>(
            "read LLVM SDK store"_str, layout.root.as_path(), rstd::move(opened).unwrap_err());
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        if (next->is_err()) {
            return sdk_io_failure<Vec<lito::SdkListEntry>>(
                "read LLVM SDK store"_str, layout.root.as_path(), rstd::move(*next).unwrap_err());
        }
        auto entry = rstd::move(*next).unwrap();
        auto name  = entry.file_name().as_os_str().to_string_lossy();
        if (name.as_str().starts_with("."_str)) continue;
        auto path = entry.path();
        auto type = entry.file_type();
        if (type.is_err()) {
            return sdk_io_failure<Vec<lito::SdkListEntry>>(
                "inspect LLVM SDK entry"_str, path.as_path(), rstd::move(type).unwrap_err());
        }
        if (! type->is_dir()) {
            result.push(
                installed_list_entry(name.as_str(),
                                     "unknown"_str,
                                     lito::SdkListStatus::Invalid,
                                     rstd::move(path),
                                     Some(String::make("version entry is not a directory"_str))));
            continue;
        }
        auto descriptor = load_descriptor(path.as_path());
        if (descriptor.is_err()) {
            result.push(installed_list_entry(
                name.as_str(),
                "unknown"_str,
                lito::SdkListStatus::Invalid,
                rstd::move(path),
                Some(rstd::format("{}", rstd::move(descriptor).unwrap_err()))));
            continue;
        }
        if (descriptor->is_none()) {
            result.push(installed_list_entry(name.as_str(),
                                             "unknown"_str,
                                             lito::SdkListStatus::Invalid,
                                             rstd::move(path),
                                             Some(String::make("sdk.json is missing"_str))));
            continue;
        }
        auto value = rstd::move(descriptor).unwrap().unwrap();
        if (value.version.as_str() != name.as_str()) {
            result.push(installed_list_entry(
                name.as_str(),
                host_text(value.host).as_str(),
                lito::SdkListStatus::Invalid,
                rstd::move(path),
                Some(rstd::format("descriptor version is '{}'", value.version.as_str()))));
            continue;
        }
        if (value.host.os != current_host.os.as_str() ||
            value.host.architecture != current_host.architecture) {
            result.push(installed_list_entry(
                value.version.as_str(),
                host_text(value.host).as_str(),
                lito::SdkListStatus::Invalid,
                rstd::move(path),
                Some(rstd::format("descriptor host differs from current host {}",
                                  host_text(current_host).as_str()))));
            continue;
        }
        result.push(installed_list_entry(value.version.as_str(),
                                         host_text(value.host).as_str(),
                                         lito::SdkListStatus::InstalledUnavailable,
                                         rstd::move(path)));
    }
    return Ok(rstd::move(result));
}

auto sort_list_entries(Vec<lito::SdkListEntry>& entries) -> void {
    rstd::slice_::sort_unstable_by(
        entries.as_mut_slice().as_mut_ref(),
        [](const lito::SdkListEntry& left, const lito::SdkListEntry& right) {
            auto left_version  = lito::parse_llvm_version(left.version.as_str());
            auto right_version = lito::parse_llvm_version(right.version.as_str());
            if (left_version.is_ok() && right_version.is_ok()) {
                return lito::llvm_version_less(*right_version, *left_version);
            }
            if (left_version.is_ok()) return true;
            if (right_version.is_ok()) return false;
            return left.version > right.version;
        });
}

namespace lito
{

auto list_llvm_sdks() -> SdkResult<SdkListSummary> {
    auto catalog = load_embedded_llvm_sdk_catalog();
    if (catalog.is_err()) return Err(SdkError::Catalog(rstd::move(catalog).unwrap_err()));
    auto host = lito::system::detect_host_info();
    if (host.is_err()) return Err(SdkError::Platform(rstd::move(host).unwrap_err()));
    auto layout    = rstd_try(sdk_store_layout());
    auto installed = rstd_try(scan_installed(layout, *host));

    auto entries = Vec<SdkListEntry>::make();
    for (const auto& release : catalog->releases) {
        auto artifact = find_llvm_sdk_artifact(release, *host);
        if (artifact.is_none()) continue;
        SdkListEntry* existing = nullptr;
        for (auto& candidate : installed) {
            if (candidate.version == release.version.text.as_str()) {
                existing = rstd::addressof(candidate);
                break;
            }
        }
        if (existing == nullptr) {
            entries.push(SdkListEntry {
                .version = release.version.text.clone(),
                .host    = host_text(*host),
                .status  = SdkListStatus::Available,
            });
            continue;
        }
        if (existing->status != SdkListStatus::Invalid) {
            auto descriptor = load_descriptor(existing->prefix->as_path());
            if (descriptor.is_err() || descriptor->is_none() ||
                ! descriptor_matches(**descriptor, release, **artifact)) {
                existing->status = SdkListStatus::Invalid;
                existing->issue =
                    Some(String::make("installed artifact identity differs from catalog"_str));
            } else {
                existing->status = SdkListStatus::Installed;
            }
        }
        entries.push(rstd::move(*existing));
        existing->version = String::make();
    }
    for (auto& entry : installed) {
        if (! entry.version.is_empty()) entries.push(rstd::move(entry));
    }
    sort_list_entries(entries);
    return Ok(SdkListSummary {
        .host    = host_text(*host),
        .entries = rstd::move(entries),
    });
}

auto install_llvm_sdk(SdkInstallRequest request) -> SdkResult<SdkInstallSummary> {
    auto parsed_version = parse_llvm_version(request.version.as_str());
    if (parsed_version.is_err())
        return Err(SdkError::Catalog(rstd::move(parsed_version).unwrap_err()));
    auto catalog = load_embedded_llvm_sdk_catalog();
    if (catalog.is_err()) return Err(SdkError::Catalog(rstd::move(catalog).unwrap_err()));
    auto release = find_llvm_sdk_release(*catalog, request.version.as_str());
    if (release.is_none()) {
        return sdk_failure<SdkInstallSummary>(
            rstd::format("LLVM SDK version '{}' is not available", request.version));
    }
    auto host = lito::system::detect_host_info();
    if (host.is_err()) return Err(SdkError::Platform(rstd::move(host).unwrap_err()));
    auto artifact = find_llvm_sdk_artifact(**release, *host);
    if (artifact.is_none()) {
        return sdk_failure<SdkInstallSummary>(
            rstd::format("LLVM SDK version '{}' is not available for {}",
                         request.version,
                         host_text(*host).as_str()));
    }
    auto environment = lito::system::ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) return Err(SdkError::System(rstd::move(environment).unwrap_err()));
    auto resolver = lito::system::ToolResolver(
        *environment, rstd::move(request.tools), rstd::move(request.tool_reporter));
    auto layout = rstd_try(sdk_store_layout());
    auto lock   = rstd_try(acquire_version_lock(layout, request.version.as_str()));
    (void)lock;
    auto prefix          = layout.version(request.version.as_str());
    auto prefix_metadata = rstd::fs::symlink_metadata(prefix.as_path());
    if (prefix_metadata.is_ok()) {
        if (! prefix_metadata->is_dir() || prefix_metadata->is_symlink()) {
            return sdk_failure<SdkInstallSummary>(rstd::format(
                "LLVM SDK destination '{}' is not a real directory", prefix.as_path()));
        }
        auto descriptor = rstd_try(load_descriptor(prefix.as_path()));
        if (descriptor.is_none()) {
            return sdk_failure<SdkInstallSummary>(rstd::format(
                "LLVM SDK destination '{}' exists without sdk.json", prefix.as_path()));
        }
        if (! descriptor_matches(*descriptor, **release, **artifact)) {
            return sdk_failure<SdkInstallSummary>(rstd::format(
                "LLVM SDK destination '{}' conflicts with the catalog artifact", prefix.as_path()));
        }
        auto certification = certify_llvm_sdk(
            prefix.as_path(), request.version.as_str(), (**artifact).paths, *environment);
        if (certification.is_err()) {
            return Err(SdkError::Toolchain(rstd::move(certification).unwrap_err()));
        }
        if (! certification_matches(descriptor->certification, *certification)) {
            return sdk_failure<SdkInstallSummary>(rstd::format(
                "LLVM SDK destination '{}' certification differs from sdk.json", prefix.as_path()));
        }
        return Ok(SdkInstallSummary {
            .version = request.version.clone(),
            .host    = host_text(*host),
            .prefix  = rstd::move(prefix),
            .reused  = true,
        });
    }
    auto prefix_error = rstd::move(prefix_metadata).unwrap_err();
    if (prefix_error.kind() !=
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return sdk_io_failure<SdkInstallSummary>(
            "inspect LLVM SDK destination"_str, prefix.as_path(), rstd::move(prefix_error));
    }

    auto observer = SdkAcquisitionObserver {
        .version = request.version.as_str(),
        .sink    = rstd::move(request.observer),
    };
    auto download_requirement = lito::system::command_tool_requirement(
        lito::system::HostToolCapability::HttpDownload, "sdk install"_str);
    auto extraction_requirement = lito::system::command_tool_requirement(
        lito::system::HostToolCapability::ArchiveExtraction, "sdk install"_str);
    auto acquisitions = Vec<lito::acquisition::VerifiedArchiveRequest>::make();
    acquisitions.push(lito::acquisition::VerifiedArchiveRequest {
        .label                  = rstd::format("llvm-sdk@{}", request.version.as_str()),
        .url                    = (**artifact).archive.url.clone(),
        .sha256                 = (**artifact).archive.sha256.clone(),
        .expected_size          = Some(u64((**artifact).archive.size.to_primitive())),
        .download_requirement   = download_requirement.clone(),
        .extraction_requirement = extraction_requirement.clone(),
    });
    auto files = lito::acquisition::acquire_verified_files(rstd::move(acquisitions),
                                                           usize(1),
                                                           resolver,
                                                           *environment,
                                                           false,
                                                           lito::acquisition::AcquisitionEventSink {
                                                               .context = rstd::addressof(observer),
                                                               .notify  = observe_acquisition,
                                                           });
    if (files.is_err()) return Err(SdkError::Acquisition(rstd::move(files).unwrap_err()));
    auto verified_files = rstd::move(files).unwrap();
    auto extractor = lito::acquisition::select_archive_extractor(resolver, extraction_requirement);
    if (extractor.is_err()) {
        return Err(SdkError::Acquisition(rstd::move(extractor).unwrap_err()));
    }
    auto staging = layout.staging_area(request.version.as_str(), *host);
    auto extracted =
        lito::acquisition::extract_verified_archive(rstd::move(verified_files[usize {}]),
                                                    staging.as_path(),
                                                    Some((**artifact).archive.root.as_str()),
                                                    *extractor,
                                                    *environment,
                                                    lito::acquisition::AcquisitionEventSink {
                                                        .context = rstd::addressof(observer),
                                                        .notify  = observe_acquisition,
                                                    });
    if (extracted.is_err()) {
        remove_staging(staging.as_path());
        return Err(SdkError::Acquisition(rstd::move(extracted).unwrap_err()));
    }
    auto certification = certify_llvm_sdk(
        extracted->root.as_path(), request.version.as_str(), (**artifact).paths, *environment);
    if (certification.is_err()) {
        remove_staging(staging.as_path());
        return Err(SdkError::Toolchain(rstd::move(certification).unwrap_err()));
    }
    auto descriptor = InstalledSdkDescriptor {
        .version       = request.version.clone(),
        .host          = host->clone(),
        .url           = (**artifact).archive.url.clone(),
        .sha256        = (**artifact).archive.sha256.clone(),
        .size          = (**artifact).archive.size,
        .paths         = (**artifact).paths.clone(),
        .certification = rstd::move(certification).unwrap(),
    };
    auto descriptor_path = extracted->root.join(PathBuf::from("sdk.json"_str).as_path());
    auto text            = serialize_descriptor(descriptor);
    auto written = rstd::fs::write_atomic(descriptor_path.as_path(), text.as_str().as_bytes());
    if (written.is_err()) {
        auto error = sdk_io_failure<SdkInstallSummary>("write LLVM SDK descriptor"_str,
                                                       descriptor_path.as_path(),
                                                       rstd::move(written).unwrap_err());
        remove_staging(staging.as_path());
        return error;
    }
    auto published = rstd::fs::rename(extracted->root.as_path(), prefix.as_path());
    if (published.is_err()) {
        auto error = sdk_io_failure<SdkInstallSummary>(
            "publish LLVM SDK"_str, prefix.as_path(), rstd::move(published).unwrap_err());
        remove_staging(staging.as_path());
        return error;
    }
    remove_staging(staging.as_path());
    return Ok(SdkInstallSummary {
        .version = rstd::move(request.version),
        .host    = host_text(*host),
        .prefix  = rstd::move(prefix),
    });
}

} // namespace lito
