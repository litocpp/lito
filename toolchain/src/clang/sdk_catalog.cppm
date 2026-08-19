module;
#include <initializer_list>
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.toolchain.clang:sdk_catalog;

import rstd;
import rstd.json;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;

export namespace lito
{

class LlvmSdkCatalogError {
    RSTD_ENUM(LlvmSdkCatalogError,
              (Json, (rstd::json::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using LlvmSdkCatalogResult = Result<T, LlvmSdkCatalogError>;

struct LlvmVersion {
    u64    major {};
    u64    minor {};
    u64    patch {};
    String text;

    auto clone() const -> LlvmVersion {
        return LlvmVersion {
            .major = major,
            .minor = minor,
            .patch = patch,
            .text  = text.clone(),
        };
    }
};

struct LlvmSdkPaths {
    PathBuf cc;
    PathBuf cxx;
    PathBuf linker;
    PathBuf archiver;
    PathBuf strip;
    PathBuf format;
    PathBuf llvm_config;
    PathBuf cmake;
    PathBuf clang_cpp;

    auto clone() const -> LlvmSdkPaths {
        return LlvmSdkPaths {
            .cc          = cc.clone(),
            .cxx         = cxx.clone(),
            .linker      = linker.clone(),
            .archiver    = archiver.clone(),
            .strip       = strip.clone(),
            .format      = format.clone(),
            .llvm_config = llvm_config.clone(),
            .cmake       = cmake.clone(),
            .clang_cpp   = clang_cpp.clone(),
        };
    }
};

struct LlvmSdkArchive {
    String format;
    String url;
    String sha256;
    u64    size {};
    String root;

    auto clone() const -> LlvmSdkArchive {
        return LlvmSdkArchive {
            .format = format.clone(),
            .url    = url.clone(),
            .sha256 = sha256.clone(),
            .size   = size,
            .root   = root.clone(),
        };
    }
};

struct LlvmSdkArtifact {
    lito::system::HostInfo host;
    LlvmSdkArchive         archive;
    LlvmSdkPaths           paths;

    auto clone() const -> LlvmSdkArtifact {
        return LlvmSdkArtifact {
            .host    = host.clone(),
            .archive = archive.clone(),
            .paths   = paths.clone(),
        };
    }
};

struct LlvmSdkRelease {
    LlvmVersion          version;
    String               upstream_tag;
    Vec<LlvmSdkArtifact> artifacts;

    auto clone() const -> LlvmSdkRelease {
        auto copied = Vec<LlvmSdkArtifact>::with_capacity(artifacts.len());
        for (const auto& artifact : artifacts) copied.push(artifact.clone());
        return LlvmSdkRelease {
            .version      = version.clone(),
            .upstream_tag = upstream_tag.clone(),
            .artifacts    = rstd::move(copied),
        };
    }
};

struct LlvmSdkCatalog {
    Vec<LlvmSdkRelease> releases;
};

auto parse_llvm_version(ref<str> value) -> LlvmSdkCatalogResult<LlvmVersion>;
auto parse_llvm_sdk_catalog(ref<str> text) -> LlvmSdkCatalogResult<LlvmSdkCatalog>;
auto validate_llvm_sdk_archive_identity(ref<str> url, ref<str> sha256, u64 size, ref<str> context)
    -> LlvmSdkCatalogResult<empty>;
auto validate_llvm_sdk_paths(const LlvmSdkPaths& paths, ref<str> context)
    -> LlvmSdkCatalogResult<empty>;
auto embedded_llvm_sdk_catalog_text() noexcept -> ref<str>;
auto load_embedded_llvm_sdk_catalog() -> LlvmSdkCatalogResult<LlvmSdkCatalog>;
auto find_llvm_sdk_release(const LlvmSdkCatalog& catalog, ref<str> version)
    -> Option<ref<LlvmSdkRelease>>;
auto find_llvm_sdk_artifact(const LlvmSdkRelease& release, const lito::system::HostInfo& host)
    -> Option<ref<LlvmSdkArtifact>>;
auto llvm_version_less(const LlvmVersion& left, const LlvmVersion& right) noexcept -> bool;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::LlvmSdkCatalogError> : ImplBase<lito::LlvmSdkCatalogError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Json()) {
            return formatter.write_raw("cannot parse LLVM SDK catalog",
                                       sizeof("cannot parse LLVM SDK catalog") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::LlvmSdkCatalogError> : ImplBase<lito::LlvmSdkCatalogError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::LlvmSdkCatalogError> : ImplBase<lito::LlvmSdkCatalogError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        return None();
    }
};

} // namespace rstd

namespace lito
{

template<typename T>
auto catalog_failure(String message) -> LlvmSdkCatalogResult<T> {
    return Err(LlvmSdkCatalogError::Message(rstd::move(message)));
}

auto known_fields(const Json& value, ref<str> context, std::initializer_list<ref<str>> names)
    -> LlvmSdkCatalogResult<empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return catalog_failure<empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        auto known = false;
        for (const auto name : names) {
            if ((**key).as_str() == name) known = true;
        }
        if (! known) {
            return catalog_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto required_member(const Json& value, ref<str> key, ref<str> context)
    -> LlvmSdkCatalogResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return catalog_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context)
    -> LlvmSdkCatalogResult<String> {
    auto member = rstd_try(required_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none() || text->is_empty()) {
        return catalog_failure<String>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto required_u64(const Json& value, ref<str> key, ref<str> context) -> LlvmSdkCatalogResult<u64> {
    auto member = rstd_try(required_member(value, key, context));
    auto number = member->as_u64();
    if (number.is_none()) {
        return catalog_failure<u64>(
            rstd::format("{}.{} must be an unsigned integer", context, key));
    }
    return Ok(*number);
}

auto required_array(const Json& value, ref<str> key, ref<str> context)
    -> LlvmSdkCatalogResult<ref<rstd::json::Array>> {
    auto member = rstd_try(required_member(value, key, context));
    auto array  = member->as_array();
    if (array.is_none()) {
        return catalog_failure<ref<rstd::json::Array>>(
            rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto parse_decimal(ref<str> text, ref<str> context) -> LlvmSdkCatalogResult<u64> {
    if (text.is_empty()) return catalog_failure<u64>(rstd::format("{} is empty", context));
    if (text.len() > usize(1) && text.as_bytes()[usize {}] == u8('0')) {
        return catalog_failure<u64>(rstd::format("{} has a leading zero", context));
    }
    u64 value {};
    for (const auto byte : text.as_bytes()) {
        if (byte < u8('0') || byte > u8('9')) {
            return catalog_failure<u64>(rstd::format("{} is not numeric", context));
        }
        const auto digit = u64((byte - u8('0')).to_primitive());
        if (value > (u64::MAX - digit) / u64(10)) {
            return catalog_failure<u64>(rstd::format("{} is out of range", context));
        }
        value = value * u64(10) + digit;
    }
    return Ok(value);
}

auto normal_relative_path(ref<str> value) -> bool {
    auto path = PathBuf::from(value);
    if (path.is_empty() || path.as_path().is_absolute() || path.as_path().has_root()) return false;
    auto components = path.as_path().components();
    auto count      = usize {};
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal()) return false;
        ++count;
    }
    return count != usize {};
}

auto single_component(ref<str> value) -> bool {
    auto path       = PathBuf::from(value);
    auto components = path.as_path().components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && components.next().is_none();
}

auto lowercase_sha256(ref<str> value) -> bool {
    if (value.len() != usize(64)) return false;
    for (const auto byte : value.as_bytes()) {
        if ((byte >= u8('0') && byte <= u8('9')) || (byte >= u8('a') && byte <= u8('f'))) {
            continue;
        }
        return false;
    }
    return true;
}

auto valid_https_url(ref<str> value) -> bool {
    if (! value.starts_with("https://"_str) || value.len() <= usize(8) ||
        value.as_bytes()[usize(8)] == u8('/') || value.as_bytes()[usize(8)] == u8('-') ||
        value.contains("#"_str)) {
        return false;
    }
    for (const auto byte : value.as_bytes()) {
        if (byte <= u8(0x20) || byte == u8(0x7f) || byte == u8('\\')) return false;
    }
    return true;
}

auto parse_host(const Json& value, ref<str> context)
    -> LlvmSdkCatalogResult<lito::system::HostInfo> {
    rstd_try(known_fields(value, context, { "os"_str, "architecture"_str }));
    auto os           = rstd_try(required_string(value, "os"_str, context));
    auto architecture = rstd_try(required_string(value, "architecture"_str, context));
    if (os.as_str() != "linux"_str && os.as_str() != "macos"_str && os.as_str() != "windows"_str) {
        return catalog_failure<lito::system::HostInfo>(
            rstd::format("{}.os '{}' is not canonical", context, os));
    }
    auto canonical = lito::system::canonical_architecture(architecture.as_str());
    if (canonical.is_err()) {
        return catalog_failure<lito::system::HostInfo>(
            rstd::format("{}.architecture '{}' is invalid", context, architecture));
    }
    if (canonical->as_str() != architecture.as_str()) {
        return catalog_failure<lito::system::HostInfo>(
            rstd::format("{}.architecture '{}' is not canonical", context, architecture));
    }
    return Ok(lito::system::HostInfo {
        .architecture = rstd::move(canonical).unwrap(),
        .os           = rstd::move(os),
    });
}

auto parse_archive(const Json& value, ref<str> context) -> LlvmSdkCatalogResult<LlvmSdkArchive> {
    rstd_try(known_fields(
        value, context, { "format"_str, "url"_str, "sha256"_str, "size"_str, "root"_str }));
    auto format = rstd_try(required_string(value, "format"_str, context));
    auto url    = rstd_try(required_string(value, "url"_str, context));
    auto sha256 = rstd_try(required_string(value, "sha256"_str, context));
    auto size   = rstd_try(required_u64(value, "size"_str, context));
    auto root   = rstd_try(required_string(value, "root"_str, context));
    if (format.as_str() != "tar.xz"_str) {
        return catalog_failure<LlvmSdkArchive>(
            rstd::format("{}.format '{}' is not supported", context, format));
    }
    rstd_try(validate_llvm_sdk_archive_identity(url.as_str(), sha256.as_str(), size, context));
    if (! single_component(root.as_str())) {
        return catalog_failure<LlvmSdkArchive>(
            rstd::format("{}.root must be one normal path component", context));
    }
    return Ok(LlvmSdkArchive {
        .format = rstd::move(format),
        .url    = rstd::move(url),
        .sha256 = rstd::move(sha256),
        .size   = size,
        .root   = rstd::move(root),
    });
}

auto parse_paths(const Json& value, ref<str> context) -> LlvmSdkCatalogResult<LlvmSdkPaths> {
    rstd_try(known_fields(value,
                          context,
                          { "cc"_str,
                            "cxx"_str,
                            "linker"_str,
                            "archiver"_str,
                            "strip"_str,
                            "format"_str,
                            "llvm-config"_str,
                            "cmake"_str,
                            "clang-cpp"_str }));
    auto cc          = rstd_try(required_string(value, "cc"_str, context));
    auto cxx         = rstd_try(required_string(value, "cxx"_str, context));
    auto linker      = rstd_try(required_string(value, "linker"_str, context));
    auto archiver    = rstd_try(required_string(value, "archiver"_str, context));
    auto strip       = rstd_try(required_string(value, "strip"_str, context));
    auto format      = rstd_try(required_string(value, "format"_str, context));
    auto llvm_config = rstd_try(required_string(value, "llvm-config"_str, context));
    auto cmake       = rstd_try(required_string(value, "cmake"_str, context));
    auto clang_cpp   = rstd_try(required_string(value, "clang-cpp"_str, context));
    auto paths       = LlvmSdkPaths {
        .cc          = PathBuf::from(rstd::move(cc)),
        .cxx         = PathBuf::from(rstd::move(cxx)),
        .linker      = PathBuf::from(rstd::move(linker)),
        .archiver    = PathBuf::from(rstd::move(archiver)),
        .strip       = PathBuf::from(rstd::move(strip)),
        .format      = PathBuf::from(rstd::move(format)),
        .llvm_config = PathBuf::from(rstd::move(llvm_config)),
        .cmake       = PathBuf::from(rstd::move(cmake)),
        .clang_cpp   = PathBuf::from(rstd::move(clang_cpp)),
    };
    rstd_try(validate_llvm_sdk_paths(paths, context));
    return Ok(rstd::move(paths));
}

auto parse_artifact(const Json& value, ref<str> context) -> LlvmSdkCatalogResult<LlvmSdkArtifact> {
    rstd_try(known_fields(value, context, { "host"_str, "archive"_str, "paths"_str }));
    auto host    = rstd_try(required_member(value, "host"_str, context));
    auto archive = rstd_try(required_member(value, "archive"_str, context));
    auto paths   = rstd_try(required_member(value, "paths"_str, context));
    return Ok(LlvmSdkArtifact {
        .host    = rstd_try(parse_host(*host, rstd::format("{}.host", context).as_str())),
        .archive = rstd_try(parse_archive(*archive, rstd::format("{}.archive", context).as_str())),
        .paths   = rstd_try(parse_paths(*paths, rstd::format("{}.paths", context).as_str())),
    });
}

auto parse_release(const Json& value, usize index) -> LlvmSdkCatalogResult<LlvmSdkRelease> {
    auto context = rstd::format("LLVM SDK catalog release {}", index);
    rstd_try(known_fields(
        value, context.as_str(), { "version"_str, "upstream-tag"_str, "artifacts"_str }));
    auto version_text = rstd_try(required_string(value, "version"_str, context.as_str()));
    auto version      = rstd_try(parse_llvm_version(version_text.as_str()));
    auto tag          = rstd_try(required_string(value, "upstream-tag"_str, context.as_str()));
    auto expected_tag = rstd::format("llvmorg-{}", version.text.as_str());
    if (tag != expected_tag.as_str()) {
        return catalog_failure<LlvmSdkRelease>(
            rstd::format("{}.upstream-tag must be '{}'", context.as_str(), expected_tag.as_str()));
    }
    auto values = rstd_try(required_array(value, "artifacts"_str, context.as_str()));
    if (values->is_empty()) {
        return catalog_failure<LlvmSdkRelease>(
            rstd::format("{}.artifacts must not be empty", context.as_str()));
    }
    auto artifacts = Vec<LlvmSdkArtifact>::with_capacity(values->len());
    for (usize artifact_index {}; artifact_index < values->len(); ++artifact_index) {
        auto artifact_context = rstd::format("{}.artifacts[{}]", context.as_str(), artifact_index);
        auto artifact =
            rstd_try(parse_artifact((*values)[artifact_index], artifact_context.as_str()));
        for (const auto& existing : artifacts) {
            if (existing.host.os == artifact.host.os.as_str() &&
                existing.host.architecture == artifact.host.architecture) {
                return catalog_failure<LlvmSdkRelease>(
                    rstd::format("{} repeats host {}-{}",
                                 context.as_str(),
                                 artifact.host.os.as_str(),
                                 artifact.host.architecture.as_str()));
            }
        }
        artifacts.push(rstd::move(artifact));
    }
    rstd::slice_::sort_unstable_by(artifacts.as_mut_slice().as_mut_ref(),
                                   [](const LlvmSdkArtifact& left, const LlvmSdkArtifact& right) {
                                       if (left.host.os != right.host.os.as_str())
                                           return left.host.os < right.host.os;
                                       return left.host.architecture.name <
                                              right.host.architecture.name;
                                   });
    return Ok(LlvmSdkRelease {
        .version      = rstd::move(version),
        .upstream_tag = rstd::move(tag),
        .artifacts    = rstd::move(artifacts),
    });
}

static constexpr unsigned char EMBEDDED_LLVM_SDK_CATALOG[] = {
#embed "../../../data/llvm-sdk-catalog.json"
};

} // namespace lito

export namespace lito
{

auto parse_llvm_version(ref<str> value) -> LlvmSdkCatalogResult<LlvmVersion> {
    auto first = value.split_once("."_str);
    if (first.is_none()) {
        return catalog_failure<LlvmVersion>(
            rstd::format("LLVM version '{}' must have three numeric components", value));
    }
    auto second = first->get<1>().split_once("."_str);
    if (second.is_none() || second->get<1>().contains("."_str)) {
        return catalog_failure<LlvmVersion>(
            rstd::format("LLVM version '{}' must have three numeric components", value));
    }
    auto major = rstd_try(parse_decimal(first->get<0>(), "LLVM major version"_str));
    auto minor = rstd_try(parse_decimal(second->get<0>(), "LLVM minor version"_str));
    auto patch = rstd_try(parse_decimal(second->get<1>(), "LLVM patch version"_str));
    return Ok(LlvmVersion {
        .major = major,
        .minor = minor,
        .patch = patch,
        .text  = String::make(value),
    });
}

auto llvm_version_less(const LlvmVersion& left, const LlvmVersion& right) noexcept -> bool {
    if (left.major != right.major) return left.major < right.major;
    if (left.minor != right.minor) return left.minor < right.minor;
    return left.patch < right.patch;
}

auto parse_llvm_sdk_catalog(ref<str> text) -> LlvmSdkCatalogResult<LlvmSdkCatalog> {
    auto parsed = rstd::json::from_str(text);
    if (parsed.is_err()) return Err(LlvmSdkCatalogError::Json(rstd::move(parsed).unwrap_err()));
    auto document = rstd::move(parsed).unwrap();
    rstd_try(known_fields(
        document, "LLVM SDK catalog root"_str, { "schema"_str, "kind"_str, "releases"_str }));
    auto schema = rstd_try(required_u64(document, "schema"_str, "LLVM SDK catalog root"_str));
    if (schema != u64(1)) {
        return catalog_failure<LlvmSdkCatalog>(
            rstd::format("LLVM SDK catalog schema {} is not supported", schema));
    }
    auto kind = rstd_try(required_string(document, "kind"_str, "LLVM SDK catalog root"_str));
    if (kind.as_str() != "lito-llvm-sdk-catalog"_str) {
        return catalog_failure<LlvmSdkCatalog>(
            rstd::format("LLVM SDK catalog kind '{}' is not supported", kind));
    }
    auto values   = rstd_try(required_array(document, "releases"_str, "LLVM SDK catalog root"_str));
    auto releases = Vec<LlvmSdkRelease>::with_capacity(values->len());
    for (usize index {}; index < values->len(); ++index) {
        auto release = rstd_try(parse_release((*values)[index], index));
        for (const auto& existing : releases) {
            if (existing.version.text == release.version.text.as_str()) {
                return catalog_failure<LlvmSdkCatalog>(rstd::format(
                    "LLVM SDK catalog repeats version '{}'", release.version.text.as_str()));
            }
        }
        releases.push(rstd::move(release));
    }
    rstd::slice_::sort_unstable_by(releases.as_mut_slice().as_mut_ref(),
                                   [](const LlvmSdkRelease& left, const LlvmSdkRelease& right) {
                                       return llvm_version_less(right.version, left.version);
                                   });
    return Ok(LlvmSdkCatalog { .releases = rstd::move(releases) });
}

auto validate_llvm_sdk_archive_identity(ref<str> url, ref<str> sha256, u64 size, ref<str> context)
    -> LlvmSdkCatalogResult<empty> {
    if (! valid_https_url(url)) {
        return catalog_failure<empty>(rstd::format("{}.url must be an HTTPS URL", context));
    }
    if (! lowercase_sha256(sha256)) {
        return catalog_failure<empty>(
            rstd::format("{}.sha256 must be 64 lowercase hexadecimal digits", context));
    }
    if (size == u64 {}) {
        return catalog_failure<empty>(rstd::format("{}.size must be greater than zero", context));
    }
    return Ok(empty {});
}

auto validate_llvm_sdk_paths(const LlvmSdkPaths& paths, ref<str> context)
    -> LlvmSdkCatalogResult<empty> {
    const ref<rstd::path::Path> path_values[] = {
        paths.cc.as_path(),          paths.cxx.as_path(),   paths.linker.as_path(),
        paths.archiver.as_path(),    paths.strip.as_path(), paths.format.as_path(),
        paths.llvm_config.as_path(), paths.cmake.as_path(), paths.clang_cpp.as_path(),
    };
    for (const auto path : path_values) {
        auto text = path.to_str();
        if (text.is_none() || ! normal_relative_path(*text)) {
            return catalog_failure<empty>(
                rstd::format("{} must contain normal relative paths", context));
        }
    }
    for (usize left {}; left < usize(9); ++left) {
        for (usize right = left + usize(1); right < usize(9); ++right) {
            if (path_values[left.to_primitive()].as_os_str().as_encoded_bytes() ==
                path_values[right.to_primitive()].as_os_str().as_encoded_bytes()) {
                return catalog_failure<empty>(rstd::format(
                    "{} repeats path '{}'", context, path_values[left.to_primitive()]));
            }
        }
    }
    return Ok(empty {});
}

auto embedded_llvm_sdk_catalog_text() noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(
        reinterpret_cast<const byte*>(EMBEDDED_LLVM_SDK_CATALOG),
        usize(sizeof(EMBEDDED_LLVM_SDK_CATALOG)));
}

auto load_embedded_llvm_sdk_catalog() -> LlvmSdkCatalogResult<LlvmSdkCatalog> {
    return parse_llvm_sdk_catalog(embedded_llvm_sdk_catalog_text());
}

auto find_llvm_sdk_release(const LlvmSdkCatalog& catalog, ref<str> version)
    -> Option<ref<LlvmSdkRelease>> {
    for (const auto& release : catalog.releases) {
        if (release.version.text.as_str() == version) {
            return Some(ref<LlvmSdkRelease>::from_raw_parts(rstd::addressof(release)));
        }
    }
    return None();
}

auto find_llvm_sdk_artifact(const LlvmSdkRelease& release, const lito::system::HostInfo& host)
    -> Option<ref<LlvmSdkArtifact>> {
    for (const auto& artifact : release.artifacts) {
        if (artifact.host.os == host.os.as_str() &&
            artifact.host.architecture == host.architecture) {
            return Some(ref<LlvmSdkArtifact>::from_raw_parts(rstd::addressof(artifact)));
        }
    }
    return None();
}

} // namespace lito
