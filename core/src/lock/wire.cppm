module;
#include <rstd/macro.hpp>

export module lito.core:lock.wire;

import rstd;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::lock::wire
{

struct GitReference {
    String kind;
    String value;
};

struct Source {
    String               kind;
    Option<String>       path;
    Option<String>       id;
    Option<String>       digest;
    Option<String>       url;
    Option<String>       commit;
    Option<GitReference> reference;
    Option<String>       sha256;
    Option<String>       registry;
    Option<String>       package;
    Option<String>       version;
    Option<String>       release;
    Option<String>       source;
    Option<String>       manifest;
    Option<String>       blob;
    Option<String>       blob_size;
    Option<String>       format;
};

struct External {
    String              name;
    Option<Vec<String>> architectures;
    Source              source;
};

struct Package {
    Option<String> id;
    String         name;
    Option<String> version;
    Source         source;
    String         manifest;
    Vec<String>    dependencies;
    Vec<String>    runtime_dependencies;
    Vec<External>  externals;
};

struct Document {
    u64          version {};
    Vec<Package> packages;
};

} // namespace lito::lock::wire

export namespace rstd
{

template<>
struct Impl<serde::Serialize, lito::lock::wire::GitReference> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::lock::wire::GitReference& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2)));
        rstd_try(serde::field(map, "kind"_str, value.kind));
        rstd_try(serde::field(map, "value"_str, value.value));
        return map.end();
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::GitReference> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::GitReference, typename Deserializer::error_type> {
        auto kind  = serde::RequiredField<String>("kind"_str);
        auto value = serde::RequiredField<String>("value"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, kind, value));
        return Ok(lito::lock::wire::GitReference {
            .kind  = rstd_try(kind.take(deserializer)),
            .value = rstd_try(value.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Serialize, lito::lock::wire::Source> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::lock::wire::Source& value) ->
        typename Serializer::result_type {
        auto len = usize(1);
        len += usize(value.path.is_some());
        len += usize(value.id.is_some());
        len += usize(value.digest.is_some());
        len += usize(value.url.is_some());
        len += usize(value.commit.is_some());
        len += usize(value.reference.is_some());
        len += usize(value.sha256.is_some());
        len += usize(value.registry.is_some());
        len += usize(value.package.is_some());
        len += usize(value.version.is_some());
        len += usize(value.release.is_some());
        len += usize(value.source.is_some());
        len += usize(value.manifest.is_some());
        len += usize(value.blob.is_some());
        len += usize(value.blob_size.is_some());
        len += usize(value.format.is_some());
        auto map = rstd_try(serializer.begin_map(len));
        rstd_try(serde::field(map, "kind"_str, value.kind));
        if (value.path.is_some()) rstd_try(serde::field(map, "path"_str, *value.path));
        if (value.id.is_some()) rstd_try(serde::field(map, "id"_str, *value.id));
        if (value.digest.is_some()) rstd_try(serde::field(map, "digest"_str, *value.digest));
        if (value.url.is_some()) rstd_try(serde::field(map, "url"_str, *value.url));
        if (value.commit.is_some()) rstd_try(serde::field(map, "commit"_str, *value.commit));
        if (value.reference.is_some()) {
            rstd_try(serde::field(map, "reference"_str, *value.reference));
        }
        if (value.sha256.is_some()) rstd_try(serde::field(map, "sha256"_str, *value.sha256));
        if (value.registry.is_some()) rstd_try(serde::field(map, "registry"_str, *value.registry));
        if (value.package.is_some()) rstd_try(serde::field(map, "package"_str, *value.package));
        if (value.version.is_some()) rstd_try(serde::field(map, "version"_str, *value.version));
        if (value.release.is_some()) rstd_try(serde::field(map, "release"_str, *value.release));
        if (value.source.is_some()) rstd_try(serde::field(map, "source"_str, *value.source));
        if (value.manifest.is_some()) {
            rstd_try(serde::field(map, "manifest"_str, *value.manifest));
        }
        if (value.blob.is_some()) rstd_try(serde::field(map, "blob"_str, *value.blob));
        if (value.blob_size.is_some()) {
            rstd_try(serde::field(map, "blob-size"_str, *value.blob_size));
        }
        if (value.format.is_some()) rstd_try(serde::field(map, "format"_str, *value.format));
        return map.end();
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::Source> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::Source, typename Deserializer::error_type> {
        auto kind      = serde::RequiredField<String>("kind"_str);
        auto path      = serde::OptionalField<String>("path"_str);
        auto id        = serde::OptionalField<String>("id"_str);
        auto digest    = serde::OptionalField<String>("digest"_str);
        auto url       = serde::OptionalField<String>("url"_str);
        auto commit    = serde::OptionalField<String>("commit"_str);
        auto reference = serde::OptionalField<lito::lock::wire::GitReference>("reference"_str);
        auto sha256    = serde::OptionalField<String>("sha256"_str);
        auto registry  = serde::OptionalField<String>("registry"_str);
        auto package   = serde::OptionalField<String>("package"_str);
        auto version   = serde::OptionalField<String>("version"_str);
        auto release   = serde::OptionalField<String>("release"_str);
        auto source    = serde::OptionalField<String>("source"_str);
        auto manifest  = serde::OptionalField<String>("manifest"_str);
        auto blob      = serde::OptionalField<String>("blob"_str);
        auto blob_size = serde::OptionalField<String>("blob-size"_str);
        auto format    = serde::OptionalField<String>("format"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           kind,
                                           path,
                                           id,
                                           digest,
                                           url,
                                           commit,
                                           reference,
                                           sha256,
                                           registry,
                                           package,
                                           version,
                                           release,
                                           source,
                                           manifest,
                                           blob,
                                           blob_size,
                                           format));
        return Ok(lito::lock::wire::Source {
            .kind      = rstd_try(kind.take(deserializer)),
            .path      = path.take(),
            .id        = id.take(),
            .digest    = digest.take(),
            .url       = url.take(),
            .commit    = commit.take(),
            .reference = reference.take(),
            .sha256    = sha256.take(),
            .registry  = registry.take(),
            .package   = package.take(),
            .version   = version.take(),
            .release   = release.take(),
            .source    = source.take(),
            .manifest  = manifest.take(),
            .blob      = blob.take(),
            .blob_size = blob_size.take(),
            .format    = format.take(),
        });
    }
};

template<>
struct Impl<serde::Serialize, lito::lock::wire::External> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::lock::wire::External& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2) + usize(value.architectures.is_some())));
        if (value.architectures.is_some()) {
            rstd_try(serde::field(map, "architectures"_str, *value.architectures));
        }
        rstd_try(serde::field(map, "name"_str, value.name));
        rstd_try(serde::field(map, "source"_str, value.source));
        return map.end();
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::External> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::External, typename Deserializer::error_type> {
        auto name          = serde::RequiredField<String>("name"_str);
        auto architectures = serde::OptionalField<Vec<String>>("architectures"_str);
        auto source        = serde::RequiredField<lito::lock::wire::Source>("source"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, name, architectures, source));
        return Ok(lito::lock::wire::External {
            .name          = rstd_try(name.take(deserializer)),
            .architectures = architectures.take(),
            .source        = rstd_try(source.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Serialize, lito::lock::wire::Package> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::lock::wire::Package& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(6) + usize(value.id.is_some()) +
                                                 usize(value.version.is_some())));
        rstd_try(serde::field(map, "dependencies"_str, value.dependencies));
        rstd_try(serde::field(map, "externals"_str, value.externals));
        if (value.id.is_some()) rstd_try(serde::field(map, "id"_str, *value.id));
        rstd_try(serde::field(map, "manifest"_str, value.manifest));
        rstd_try(serde::field(map, "name"_str, value.name));
        rstd_try(serde::field(map, "runtime-dependencies"_str, value.runtime_dependencies));
        rstd_try(serde::field(map, "source"_str, value.source));
        if (value.version.is_some()) rstd_try(serde::field(map, "version"_str, *value.version));
        return map.end();
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::Package> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::Package, typename Deserializer::error_type> {
        auto dependencies = serde::RequiredField<Vec<String>>("dependencies"_str);
        auto externals    = serde::RequiredField<Vec<lito::lock::wire::External>>("externals"_str);
        auto id           = serde::OptionalField<String>("id"_str);
        auto manifest     = serde::RequiredField<String>("manifest"_str);
        auto name         = serde::RequiredField<String>("name"_str);
        auto runtime_dependencies = serde::RequiredField<Vec<String>>("runtime-dependencies"_str);
        auto source               = serde::RequiredField<lito::lock::wire::Source>("source"_str);
        auto version              = serde::OptionalField<String>("version"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           dependencies,
                                           externals,
                                           id,
                                           manifest,
                                           name,
                                           runtime_dependencies,
                                           source,
                                           version));
        return Ok(lito::lock::wire::Package {
            .id                   = id.take(),
            .name                 = rstd_try(name.take(deserializer)),
            .version              = version.take(),
            .source               = rstd_try(source.take(deserializer)),
            .manifest             = rstd_try(manifest.take(deserializer)),
            .dependencies         = rstd_try(dependencies.take(deserializer)),
            .runtime_dependencies = rstd_try(runtime_dependencies.take(deserializer)),
            .externals            = rstd_try(externals.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Serialize, lito::lock::wire::Document> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::lock::wire::Document& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2)));
        rstd_try(serde::field(map, "packages"_str, value.packages));
        rstd_try(serde::field(map, "version"_str, value.version));
        return map.end();
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::Document> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::Document, typename Deserializer::error_type> {
        auto packages = serde::RequiredField<Vec<lito::lock::wire::Package>>("packages"_str);
        auto version  = serde::RequiredField<u64>("version"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, packages, version));
        return Ok(lito::lock::wire::Document {
            .version  = rstd_try(version.take(deserializer)),
            .packages = rstd_try(packages.take(deserializer)),
        });
    }
};

} // namespace rstd
