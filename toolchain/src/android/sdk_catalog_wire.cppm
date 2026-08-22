module;
#include <rstd/macro.hpp>

export module lito.toolchain.android:sdk_catalog_wire;

import rstd;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::android_catalog_wire
{

struct Host {
    String os;
    String architecture;
};

struct Archive {
    String format;
    String url;
    String sha256;
    u64    size {};
    String root;
};

struct Artifact {
    Host    host;
    Archive archive;
};

struct Release {
    String        revision;
    String        release_name;
    Vec<Artifact> artifacts;
};

struct License {
    String id;
    String url;
    String sha256;
};

struct Catalog {
    u64          schema {};
    String       kind;
    License      license;
    Vec<Release> releases;
};

} // namespace lito::android_catalog_wire

export namespace rstd
{

template<>
struct Impl<serde::Deserialize, lito::android_catalog_wire::Host> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::android_catalog_wire::Host, typename Deserializer::error_type> {
        auto os           = serde::RequiredField<String>("os"_str);
        auto architecture = serde::RequiredField<String>("architecture"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, os, architecture));
        return Ok(lito::android_catalog_wire::Host {
            .os           = rstd_try(os.take(deserializer)),
            .architecture = rstd_try(architecture.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::android_catalog_wire::Archive> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::android_catalog_wire::Archive, typename Deserializer::error_type> {
        auto format = serde::RequiredField<String>("format"_str);
        auto url    = serde::RequiredField<String>("url"_str);
        auto sha256 = serde::RequiredField<String>("sha256"_str);
        auto size   = serde::RequiredField<u64>("size"_str);
        auto root   = serde::RequiredField<String>("root"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, format, url, sha256, size, root));
        return Ok(lito::android_catalog_wire::Archive {
            .format = rstd_try(format.take(deserializer)),
            .url    = rstd_try(url.take(deserializer)),
            .sha256 = rstd_try(sha256.take(deserializer)),
            .size   = rstd_try(size.take(deserializer)),
            .root   = rstd_try(root.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::android_catalog_wire::Artifact> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::android_catalog_wire::Artifact, typename Deserializer::error_type> {
        auto host    = serde::RequiredField<lito::android_catalog_wire::Host>("host"_str);
        auto archive = serde::RequiredField<lito::android_catalog_wire::Archive>("archive"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, host, archive));
        return Ok(lito::android_catalog_wire::Artifact {
            .host    = rstd_try(host.take(deserializer)),
            .archive = rstd_try(archive.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::android_catalog_wire::Release> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::android_catalog_wire::Release, typename Deserializer::error_type> {
        auto revision     = serde::RequiredField<String>("revision"_str);
        auto release_name = serde::RequiredField<String>("release-name"_str);
        auto artifacts =
            serde::RequiredField<Vec<lito::android_catalog_wire::Artifact>>("artifacts"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, revision, release_name, artifacts));
        return Ok(lito::android_catalog_wire::Release {
            .revision     = rstd_try(revision.take(deserializer)),
            .release_name = rstd_try(release_name.take(deserializer)),
            .artifacts    = rstd_try(artifacts.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::android_catalog_wire::License> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::android_catalog_wire::License, typename Deserializer::error_type> {
        auto id     = serde::RequiredField<String>("id"_str);
        auto url    = serde::RequiredField<String>("url"_str);
        auto sha256 = serde::RequiredField<String>("sha256"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, id, url, sha256));
        return Ok(lito::android_catalog_wire::License {
            .id     = rstd_try(id.take(deserializer)),
            .url    = rstd_try(url.take(deserializer)),
            .sha256 = rstd_try(sha256.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::android_catalog_wire::Catalog> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::android_catalog_wire::Catalog, typename Deserializer::error_type> {
        auto schema  = serde::RequiredField<u64>("schema"_str);
        auto kind    = serde::RequiredField<String>("kind"_str);
        auto license = serde::RequiredField<lito::android_catalog_wire::License>("license"_str);
        auto releases =
            serde::RequiredField<Vec<lito::android_catalog_wire::Release>>("releases"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, schema, kind, license, releases));
        return Ok(lito::android_catalog_wire::Catalog {
            .schema   = rstd_try(schema.take(deserializer)),
            .kind     = rstd_try(kind.take(deserializer)),
            .license  = rstd_try(license.take(deserializer)),
            .releases = rstd_try(releases.take(deserializer)),
        });
    }
};

} // namespace rstd
