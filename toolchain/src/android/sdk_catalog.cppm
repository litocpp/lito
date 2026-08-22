module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.toolchain.android:sdk_catalog;

import rstd;
import lito.crypto;
import rstd.json;
import rstd.serde;
import lito.core;
import :ndk;
import :sdk_catalog_wire;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

class AndroidNdkCatalogError {
    RSTD_ENUM(AndroidNdkCatalogError,
              (Json, (rstd::json::DecodeError source;)),
              (Data, (rstd::serde::Error source;)))
};

template<typename T>
using AndroidNdkCatalogResult = Result<T, AndroidNdkCatalogError>;

struct AndroidNdkLicense {
    String                     id;
    lito::parse::HttpsUrl      url;
    lito::crypto::Sha256Digest sha256;

    auto clone() const -> AndroidNdkLicense {
        return AndroidNdkLicense { .id = id.clone(), .url = url.clone(), .sha256 = sha256.clone() };
    }
};

struct AndroidNdkArchive {
    String                     format;
    lito::parse::HttpsUrl      url;
    lito::crypto::Sha256Digest sha256;
    u64                        size {};
    lito::parse::PathComponent root;

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
        return formatter.write_raw("Android NDK catalog is invalid",
                                   sizeof("Android NDK catalog is invalid") - 1);
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
        return Some(dyn<error::Error>::from_ref(error.as_Data().source));
    }
};

template<>
struct Impl<convert::From<rstd::json::DecodeError>, lito::AndroidNdkCatalogError> {
    static auto from(rstd::json::DecodeError error) -> lito::AndroidNdkCatalogError {
        return lito::AndroidNdkCatalogError::Json(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<rstd::serde::Error>, lito::AndroidNdkCatalogError> {
    static auto from(rstd::serde::Error error) -> lito::AndroidNdkCatalogError {
        return lito::AndroidNdkCatalogError::Data(rstd::move(error));
    }
};

} // namespace rstd

namespace lito
{

template<typename T>
auto android_catalog_failure(rstd::serde::DataPath path, ref<str> message)
    -> AndroidNdkCatalogResult<T> {
    return Err(
        AndroidNdkCatalogError::Data(rstd::serde::Error::invalid_value(rstd::move(path), message)));
}

template<typename T, typename Source>
    requires Impled<rstd::mtp::rm_cvf<Source>, rstd::error::Error>
auto android_catalog_failure(rstd::serde::DataPath path, ref<str> message, Source source)
    -> AndroidNdkCatalogResult<T> {
    return Err(AndroidNdkCatalogError::Data(rstd::serde::Error::invalid_value_with_source(
        rstd::move(path), message, rstd::move(source))));
}

auto parse_catalog_host(android_catalog_wire::Host value, rstd::serde::DataPath path)
    -> AndroidNdkCatalogResult<lito::system::HostInfo> {
    if (value.os.is_empty()) {
        return android_catalog_failure<lito::system::HostInfo>(
            path.with_field("os"_str), "host operating system must not be empty"_str);
    }
    if (value.architecture.is_empty()) {
        return android_catalog_failure<lito::system::HostInfo>(
            path.with_field("architecture"_str), "host architecture must not be empty"_str);
    }
    if (value.os != "linux"_str) {
        return android_catalog_failure<lito::system::HostInfo>(
            path.with_field("os"_str), "host operating system is not certified"_str);
    }
    auto canonical = lito::system::canonical_architecture(value.architecture.as_str());
    if (canonical.is_err()) {
        return android_catalog_failure<lito::system::HostInfo>(
            path.with_field("architecture"_str),
            "host architecture is invalid"_str,
            rstd::move(canonical).unwrap_err_unchecked());
    }
    auto architecture = rstd::move(canonical).unwrap_unchecked();
    if (architecture.as_str() != "x86_64"_str || value.architecture != "x86_64"_str) {
        return android_catalog_failure<lito::system::HostInfo>(
            path.with_field("architecture"_str), "host architecture is not certified"_str);
    }
    return Ok(lito::system::HostInfo {
        .architecture = rstd::move(architecture),
        .os           = rstd::move(value.os),
    });
}

auto parse_catalog_archive(android_catalog_wire::Archive value, rstd::serde::DataPath path)
    -> AndroidNdkCatalogResult<AndroidNdkArchive> {
    if (value.format != "zip"_str) {
        return android_catalog_failure<AndroidNdkArchive>(path.with_field("format"_str),
                                                          "archive format must be 'zip'"_str);
    }
    auto url = lito::parse::HttpsUrl::parse(value.url.as_str());
    if (url.is_err()) {
        return android_catalog_failure<AndroidNdkArchive>(path.with_field("url"_str),
                                                          "archive URL is invalid"_str,
                                                          rstd::move(url).unwrap_err_unchecked());
    }
    auto sha256 =
        lito::parse::parse_sha256(value.sha256.as_str(), lito::parse::Sha256TextMode::Canonical);
    if (sha256.is_err()) {
        return android_catalog_failure<AndroidNdkArchive>(
            path.with_field("sha256"_str),
            "archive SHA256 is invalid"_str,
            rstd::move(sha256).unwrap_err_unchecked());
    }
    auto root = lito::parse::PathComponent::parse(value.root.as_str());
    if (root.is_err()) {
        return android_catalog_failure<AndroidNdkArchive>(path.with_field("root"_str),
                                                          "archive root is invalid"_str,
                                                          rstd::move(root).unwrap_err_unchecked());
    }
    auto parsed_url = rstd::move(url).unwrap_unchecked();
    if (! parsed_url.as_str().starts_with("https://dl.google.com/android/repository/"_str)) {
        return android_catalog_failure<AndroidNdkArchive>(
            path.with_field("url"_str), "archive URL must use the official Android repository"_str);
    }
    if (value.size == u64 {}) {
        return android_catalog_failure<AndroidNdkArchive>(path.with_field("size"_str),
                                                          "archive size must be non-zero"_str);
    }
    return Ok(AndroidNdkArchive {
        .format = rstd::move(value.format),
        .url    = rstd::move(parsed_url),
        .sha256 = rstd::move(sha256).unwrap_unchecked(),
        .size   = value.size,
        .root   = rstd::move(root).unwrap_unchecked(),
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
    auto document = rstd_try(rstd::json::decode<android_catalog_wire::Catalog>(text));
    auto root     = rstd::serde::DataPath();
    if (document.schema != u64(1)) {
        return android_catalog_failure<AndroidNdkCatalog>(
            root.with_field("schema"_str), "Android NDK catalog schema must be 1"_str);
    }
    if (document.kind != "lito-android-ndk-repository"_str) {
        return android_catalog_failure<AndroidNdkCatalog>(
            root.with_field("kind"_str), "Android NDK catalog kind is invalid"_str);
    }
    auto license_path = root.with_field("license"_str);
    auto license_url  = lito::parse::HttpsUrl::parse(document.license.url.as_str());
    if (license_url.is_err()) {
        return android_catalog_failure<AndroidNdkCatalog>(
            license_path.with_field("url"_str),
            "license URL is invalid"_str,
            rstd::move(license_url).unwrap_err_unchecked());
    }
    auto license_sha256 = lito::parse::parse_sha256(document.license.sha256.as_str(),
                                                    lito::parse::Sha256TextMode::Canonical);
    if (license_sha256.is_err()) {
        return android_catalog_failure<AndroidNdkCatalog>(
            license_path.with_field("sha256"_str),
            "license SHA256 is invalid"_str,
            rstd::move(license_sha256).unwrap_err_unchecked());
    }
    if (document.license.id != "android-sdk-license"_str) {
        return android_catalog_failure<AndroidNdkCatalog>(
            license_path.with_field("id"_str), "Android NDK license identifier is invalid"_str);
    }
    auto parsed_license_url = rstd::move(license_url).unwrap_unchecked();
    if (! parsed_license_url.as_str().starts_with("https://developer.android.com/"_str)) {
        return android_catalog_failure<AndroidNdkCatalog>(
            license_path.with_field("url"_str),
            "license URL must use the official Android developer site"_str);
    }
    auto license = AndroidNdkLicense {
        .id     = rstd::move(document.license.id),
        .url    = rstd::move(parsed_license_url),
        .sha256 = rstd::move(license_sha256).unwrap_unchecked(),
    };
    if (document.releases.is_empty()) {
        return android_catalog_failure<AndroidNdkCatalog>(
            root.with_field("releases"_str), "Android NDK catalog releases must not be empty"_str);
    }
    auto releases = Vec<AndroidNdkRelease>::with_capacity(document.releases.len());
    for (usize index {}; index < document.releases.len(); ++index) {
        auto release_path = root.with_field("releases"_str).with_index(index);
        auto value        = rstd::move(document.releases[index]);
        auto revision     = parse_android_ndk_revision(value.revision.as_str());
        if (revision.is_err()) {
            return android_catalog_failure<AndroidNdkCatalog>(
                release_path.with_field("revision"_str),
                "Android NDK revision is invalid"_str,
                rstd::move(revision).unwrap_err_unchecked());
        }
        if (value.release_name.is_empty()) {
            return android_catalog_failure<AndroidNdkCatalog>(
                release_path.with_field("release-name"_str),
                "Android NDK release name must not be empty"_str);
        }
        if (value.artifacts.is_empty()) {
            return android_catalog_failure<AndroidNdkCatalog>(
                release_path.with_field("artifacts"_str),
                "Android NDK release artifacts must not be empty"_str);
        }
        auto artifacts = Vec<AndroidNdkArtifact>::with_capacity(value.artifacts.len());
        for (usize artifact_index {}; artifact_index < value.artifacts.len(); ++artifact_index) {
            auto artifact_path =
                release_path.with_field("artifacts"_str).with_index(artifact_index);
            auto artifact_value = rstd::move(value.artifacts[artifact_index]);
            auto artifact       = AndroidNdkArtifact {
                .host    = rstd_try(parse_catalog_host(rstd::move(artifact_value.host),
                                                       artifact_path.with_field("host"_str))),
                .archive = rstd_try(parse_catalog_archive(rstd::move(artifact_value.archive),
                                                          artifact_path.with_field("archive"_str))),
            };
            for (const auto& existing : artifacts) {
                if (existing.host.os == artifact.host.os.as_str() &&
                    existing.host.architecture == artifact.host.architecture) {
                    return android_catalog_failure<AndroidNdkCatalog>(
                        artifact_path.with_field("host"_str),
                        "Android NDK release repeats a host artifact"_str);
                }
            }
            artifacts.push(rstd::move(artifact));
        }
        auto parsed_revision = rstd::move(revision).unwrap_unchecked();
        for (const auto& existing : releases) {
            if (existing.revision.text == parsed_revision.text.as_str()) {
                return android_catalog_failure<AndroidNdkCatalog>(
                    release_path.with_field("revision"_str),
                    "Android NDK catalog repeats a revision"_str);
            }
        }
        releases.push(AndroidNdkRelease {
            .revision     = rstd::move(parsed_revision),
            .release_name = rstd::move(value.release_name),
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
