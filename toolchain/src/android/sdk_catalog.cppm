module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.toolchain.android:sdk_catalog;

import rstd;
import rstd.json;
import lito.core;
import :ndk;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;

export namespace lito
{

class AndroidNdkCatalogError {
    RSTD_ENUM(AndroidNdkCatalogError,
              (Json, (rstd::json::Error source;)),
              (Ndk, (AndroidNdkError source;)),
              (Message, (String message;)))
};

template<typename T>
using AndroidNdkCatalogResult = Result<T, AndroidNdkCatalogError>;

struct AndroidNdkLicense {
    String id;
    String url;
    String sha256;

    auto clone() const -> AndroidNdkLicense {
        return AndroidNdkLicense { .id = id.clone(), .url = url.clone(), .sha256 = sha256.clone() };
    }
};

struct AndroidNdkArchive {
    String format;
    String url;
    String sha256;
    u64    size {};
    String root;

    auto clone() const -> AndroidNdkArchive {
        return AndroidNdkArchive {
            .format = format.clone(),
            .url    = url.clone(),
            .sha256 = sha256.clone(),
            .size   = size,
            .root   = root.clone(),
        };
    }
};

struct AndroidNdkArtifact {
    lito::system::HostInfo host;
    AndroidNdkArchive      archive;

    auto clone() const -> AndroidNdkArtifact {
        return AndroidNdkArtifact { .host = host.clone(), .archive = archive.clone() };
    }
};

struct AndroidNdkRelease {
    AndroidNdkRevision      revision;
    String                  release_name;
    Vec<AndroidNdkArtifact> artifacts;

    auto clone() const -> AndroidNdkRelease {
        auto copied = Vec<AndroidNdkArtifact>::with_capacity(artifacts.len());
        for (const auto& artifact : artifacts) copied.push(artifact.clone());
        return AndroidNdkRelease {
            .revision     = revision.clone(),
            .release_name = release_name.clone(),
            .artifacts    = rstd::move(copied),
        };
    }
};

struct AndroidNdkCatalog {
    AndroidNdkLicense      license;
    Vec<AndroidNdkRelease> releases;
};

auto parse_android_ndk_catalog(ref<str> text) -> AndroidNdkCatalogResult<AndroidNdkCatalog>;
auto embedded_android_ndk_catalog_text() noexcept -> ref<str>;
auto load_embedded_android_ndk_catalog() -> AndroidNdkCatalogResult<AndroidNdkCatalog>;
auto find_android_ndk_release(const AndroidNdkCatalog& catalog, ref<str> revision)
    -> Option<ref<AndroidNdkRelease>>;
auto find_android_ndk_artifact(const AndroidNdkRelease& release, const lito::system::HostInfo& host)
    -> Option<ref<AndroidNdkArtifact>>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::AndroidNdkCatalogError> : ImplBase<lito::AndroidNdkCatalogError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Json()) {
            return formatter.write_raw("cannot parse Android NDK catalog",
                                       sizeof("cannot parse Android NDK catalog") - 1);
        }
        if (error.is_Ndk()) return as<fmt::Display>(error.as_Ndk().source).fmt(formatter);
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::AndroidNdkCatalogError> : ImplBase<lito::AndroidNdkCatalogError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::AndroidNdkCatalogError> : ImplBase<lito::AndroidNdkCatalogError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Ndk()) return Some(dyn<error::Error>::from_ref(error.as_Ndk().source));
        return None();
    }
};

} // namespace rstd

namespace lito
{

template<typename T>
auto android_catalog_failure(String message) -> AndroidNdkCatalogResult<T> {
    return Err(AndroidNdkCatalogError::Message(rstd::move(message)));
}

template<typename T>
auto android_catalog_failure(ref<str> message) -> AndroidNdkCatalogResult<T> {
    return android_catalog_failure<T>(String::make(message));
}

auto catalog_known_fields(const Json& value, ref<str> context, initializer_list<ref<str>> names)
    -> AndroidNdkCatalogResult<empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return android_catalog_failure<empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        auto known = false;
        for (const auto name : names) {
            if ((**key).as_str() == name) known = true;
        }
        if (! known) {
            return android_catalog_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto catalog_member(const Json& value, ref<str> key, ref<str> context)
    -> AndroidNdkCatalogResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return android_catalog_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto catalog_string(const Json& value, ref<str> key, ref<str> context)
    -> AndroidNdkCatalogResult<String> {
    auto member = rstd_try(catalog_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none() || text->is_empty()) {
        return android_catalog_failure<String>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto catalog_u64(const Json& value, ref<str> key, ref<str> context)
    -> AndroidNdkCatalogResult<u64> {
    auto member = rstd_try(catalog_member(value, key, context));
    auto number = member->as_u64();
    if (number.is_none()) {
        return android_catalog_failure<u64>(
            rstd::format("{}.{} must be an unsigned integer", context, key));
    }
    return Ok(*number);
}

auto catalog_array(const Json& value, ref<str> key, ref<str> context)
    -> AndroidNdkCatalogResult<ref<rstd::json::Array>> {
    auto member = rstd_try(catalog_member(value, key, context));
    auto array  = member->as_array();
    if (array.is_none()) {
        return android_catalog_failure<ref<rstd::json::Array>>(
            rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto lowercase_sha256(ref<str> value) -> bool {
    if (value.len() != usize(64)) return false;
    for (const auto byte : value.as_bytes()) {
        if ((byte >= u8('0') && byte <= u8('9')) || (byte >= u8('a') && byte <= u8('f'))) continue;
        return false;
    }
    return true;
}

auto one_normal_component(ref<str> value) -> bool {
    auto path       = PathBuf::from(value);
    auto components = path.as_path().components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && components.next().is_none();
}

auto parse_catalog_host(const Json& value, ref<str> context)
    -> AndroidNdkCatalogResult<lito::system::HostInfo> {
    rstd_try(catalog_known_fields(value, context, { "os"_str, "architecture"_str }));
    auto os   = rstd_try(catalog_string(value, "os"_str, context));
    auto arch = rstd_try(catalog_string(value, "architecture"_str, context));
    if (os != "linux"_str) {
        return android_catalog_failure<lito::system::HostInfo>(
            rstd::format("{}.os '{}' is not certified", context, os));
    }
    auto canonical = lito::system::canonical_architecture(arch.as_str());
    if (canonical.is_err()) {
        return android_catalog_failure<lito::system::HostInfo>(
            rstd::format("{}.architecture '{}' is invalid", context, arch));
    }
    if (canonical->as_str() != "x86_64"_str || arch != "x86_64"_str) {
        return android_catalog_failure<lito::system::HostInfo>(
            rstd::format("{}.architecture '{}' is not certified", context, arch));
    }
    return Ok(lito::system::HostInfo {
        .architecture = rstd::move(canonical).unwrap(),
        .os           = rstd::move(os),
    });
}

auto parse_catalog_archive(const Json& value, ref<str> context)
    -> AndroidNdkCatalogResult<AndroidNdkArchive> {
    rstd_try(catalog_known_fields(
        value, context, { "format"_str, "url"_str, "sha256"_str, "size"_str, "root"_str }));
    auto format = rstd_try(catalog_string(value, "format"_str, context));
    auto url    = rstd_try(catalog_string(value, "url"_str, context));
    auto sha256 = rstd_try(catalog_string(value, "sha256"_str, context));
    auto size   = rstd_try(catalog_u64(value, "size"_str, context));
    auto root   = rstd_try(catalog_string(value, "root"_str, context));
    if (format != "zip"_str) {
        return android_catalog_failure<AndroidNdkArchive>(
            rstd::format("{}.format must be 'zip'", context));
    }
    if (! url.as_str().starts_with("https://dl.google.com/android/repository/"_str) ||
        url.as_str().contains("#"_str)) {
        return android_catalog_failure<AndroidNdkArchive>(
            rstd::format("{}.url must be an official Android repository HTTPS URL", context));
    }
    if (! lowercase_sha256(sha256.as_str()) || size == u64 {}) {
        return android_catalog_failure<AndroidNdkArchive>(
            rstd::format("{}.sha256 and size must identify a non-empty archive", context));
    }
    if (! one_normal_component(root.as_str())) {
        return android_catalog_failure<AndroidNdkArchive>(
            rstd::format("{}.root must be one normal path component", context));
    }
    return Ok(AndroidNdkArchive {
        .format = rstd::move(format),
        .url    = rstd::move(url),
        .sha256 = rstd::move(sha256),
        .size   = size,
        .root   = rstd::move(root),
    });
}

auto android_revision_less(const AndroidNdkRevision& left, const AndroidNdkRevision& right) noexcept
    -> bool {
    if (left.major != right.major) return left.major < right.major;
    if (left.minor != right.minor) return left.minor < right.minor;
    return left.build < right.build;
}

static constexpr unsigned char EMBEDDED_ANDROID_NDK_CATALOG[] = {
#embed "../../../data/android-ndk.json"
};

} // namespace lito

export namespace lito
{

auto parse_android_ndk_catalog(ref<str> text) -> AndroidNdkCatalogResult<AndroidNdkCatalog> {
    auto parsed = rstd::json::from_str(text);
    if (parsed.is_err()) return Err(AndroidNdkCatalogError::Json(rstd::move(parsed).unwrap_err()));
    auto document = rstd::move(parsed).unwrap();
    rstd_try(catalog_known_fields(document,
                                  "Android NDK catalog root"_str,
                                  { "schema"_str, "kind"_str, "license"_str, "releases"_str }));
    if (rstd_try(catalog_u64(document, "schema"_str, "Android NDK catalog root"_str)) != u64(1)) {
        return android_catalog_failure<AndroidNdkCatalog>(
            "Android NDK catalog schema must be 1"_str);
    }
    auto kind = rstd_try(catalog_string(document, "kind"_str, "Android NDK catalog root"_str));
    if (kind != "lito-android-ndk-repository"_str) {
        return android_catalog_failure<AndroidNdkCatalog>(
            "Android NDK catalog kind is invalid"_str);
    }
    auto license_value =
        rstd_try(catalog_member(document, "license"_str, "Android NDK catalog root"_str));
    rstd_try(catalog_known_fields(
        *license_value, "Android NDK catalog license"_str, { "id"_str, "url"_str, "sha256"_str }));
    auto license = AndroidNdkLicense {
        .id = rstd_try(catalog_string(*license_value, "id"_str, "Android NDK catalog license"_str)),
        .url =
            rstd_try(catalog_string(*license_value, "url"_str, "Android NDK catalog license"_str)),
        .sha256 = rstd_try(
            catalog_string(*license_value, "sha256"_str, "Android NDK catalog license"_str)),
    };
    if (license.id != "android-sdk-license"_str ||
        ! license.url.as_str().starts_with("https://developer.android.com/"_str) ||
        ! lowercase_sha256(license.sha256.as_str())) {
        return android_catalog_failure<AndroidNdkCatalog>(
            "Android NDK catalog license identity is invalid"_str);
    }
    auto values = rstd_try(catalog_array(document, "releases"_str, "Android NDK catalog root"_str));
    if (values->is_empty()) {
        return android_catalog_failure<AndroidNdkCatalog>(
            "Android NDK catalog releases must not be empty"_str);
    }
    auto releases = Vec<AndroidNdkRelease>::with_capacity(values->len());
    for (usize index {}; index < values->len(); ++index) {
        const auto  context = rstd::format("Android NDK catalog release {}", index);
        const auto& value   = (*values)[index];
        rstd_try(catalog_known_fields(
            value, context.as_str(), { "revision"_str, "release-name"_str, "artifacts"_str }));
        auto revision_text = rstd_try(catalog_string(value, "revision"_str, context.as_str()));
        auto revision      = parse_android_ndk_revision(revision_text.as_str());
        if (revision.is_err()) {
            return Err(AndroidNdkCatalogError::Ndk(rstd::move(revision).unwrap_err()));
        }
        auto release_name = rstd_try(catalog_string(value, "release-name"_str, context.as_str()));
        auto artifacts_value = rstd_try(catalog_array(value, "artifacts"_str, context.as_str()));
        if (artifacts_value->is_empty()) {
            return android_catalog_failure<AndroidNdkCatalog>(
                rstd::format("{}.artifacts must not be empty", context.as_str()));
        }
        auto artifacts = Vec<AndroidNdkArtifact>::with_capacity(artifacts_value->len());
        for (usize artifact_index {}; artifact_index < artifacts_value->len(); ++artifact_index) {
            const auto artifact_context =
                rstd::format("{}.artifacts[{}]", context.as_str(), artifact_index);
            const auto& artifact_value = (*artifacts_value)[artifact_index];
            rstd_try(catalog_known_fields(
                artifact_value, artifact_context.as_str(), { "host"_str, "archive"_str }));
            auto host_value =
                rstd_try(catalog_member(artifact_value, "host"_str, artifact_context.as_str()));
            auto archive_value =
                rstd_try(catalog_member(artifact_value, "archive"_str, artifact_context.as_str()));
            auto artifact = AndroidNdkArtifact {
                .host    = rstd_try(parse_catalog_host(
                    *host_value, rstd::format("{}.host", artifact_context.as_str()).as_str())),
                .archive = rstd_try(parse_catalog_archive(
                    *archive_value,
                    rstd::format("{}.archive", artifact_context.as_str()).as_str())),
            };
            for (const auto& existing : artifacts) {
                if (existing.host.os == artifact.host.os.as_str() &&
                    existing.host.architecture == artifact.host.architecture) {
                    return android_catalog_failure<AndroidNdkCatalog>(
                        rstd::format("{} repeats host {}-{}",
                                     context.as_str(),
                                     artifact.host.os.as_str(),
                                     artifact.host.architecture.as_str()));
                }
            }
            artifacts.push(rstd::move(artifact));
        }
        for (const auto& existing : releases) {
            if (existing.revision.text == revision->text.as_str()) {
                return android_catalog_failure<AndroidNdkCatalog>(
                    rstd::format("Android NDK catalog repeats revision '{}'", revision->text));
            }
        }
        releases.push(AndroidNdkRelease {
            .revision     = rstd::move(revision).unwrap(),
            .release_name = rstd::move(release_name),
            .artifacts    = rstd::move(artifacts),
        });
    }
    rstd::slice_::sort_unstable_by(
        releases.as_mut_slice().as_mut_ref(),
        [](const AndroidNdkRelease& left, const AndroidNdkRelease& right) {
            return android_revision_less(left.revision, right.revision);
        });
    return Ok(AndroidNdkCatalog {
        .license  = rstd::move(license),
        .releases = rstd::move(releases),
    });
}

auto embedded_android_ndk_catalog_text() noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(
        reinterpret_cast<const byte*>(EMBEDDED_ANDROID_NDK_CATALOG),
        usize(sizeof(EMBEDDED_ANDROID_NDK_CATALOG)));
}

auto load_embedded_android_ndk_catalog() -> AndroidNdkCatalogResult<AndroidNdkCatalog> {
    return parse_android_ndk_catalog(embedded_android_ndk_catalog_text());
}

auto find_android_ndk_release(const AndroidNdkCatalog& catalog, ref<str> revision)
    -> Option<ref<AndroidNdkRelease>> {
    for (const auto& release : catalog.releases) {
        if (release.revision.text.as_str() == revision) {
            return Some(ref<AndroidNdkRelease>::from_raw_parts(rstd::addressof(release)));
        }
    }
    return None();
}

auto find_android_ndk_artifact(const AndroidNdkRelease& release, const lito::system::HostInfo& host)
    -> Option<ref<AndroidNdkArtifact>> {
    for (const auto& artifact : release.artifacts) {
        if (artifact.host.os == host.os.as_str() &&
            artifact.host.architecture == host.architecture) {
            return Some(ref<AndroidNdkArtifact>::from_raw_parts(rstd::addressof(artifact)));
        }
    }
    return None();
}

} // namespace lito
