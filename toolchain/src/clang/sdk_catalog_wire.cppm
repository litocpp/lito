module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:sdk_catalog_wire;

import rstd;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::llvm_catalog_wire
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

struct Paths {
    String cc;
    String cxx;
    String linker;
    String archiver;
    String strip;
    String format;
    String llvm_config;
    String cmake;
    String clang_cpp;
};

struct Runtime {
    String      file;
    String      soname;
    Vec<String> links;
    String      license;
};

struct RuntimeComponent {
    String  name;
    String  version;
    String  recipe;
    Runtime runtime;
};

struct Artifact {
    Host        host;
    Archive     archive;
    Paths       paths;
    Vec<String> runtime_components;
};

struct Release {
    String        version;
    String        upstream_tag;
    Vec<Artifact> artifacts;
};

struct Catalog {
    u64                   schema {};
    String                kind;
    Vec<RuntimeComponent> runtime_components;
    Vec<Release>          releases;
};

} // namespace lito::llvm_catalog_wire

export namespace rstd
{

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Host> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Host, typename Deserializer::error_type> {
        auto os           = serde::RequiredField<String>("os"_str);
        auto architecture = serde::RequiredField<String>("architecture"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, os, architecture));
        return Ok(lito::llvm_catalog_wire::Host {
            .os           = rstd_try(os.take(deserializer)),
            .architecture = rstd_try(architecture.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Archive> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Archive, typename Deserializer::error_type> {
        auto format = serde::RequiredField<String>("format"_str);
        auto url    = serde::RequiredField<String>("url"_str);
        auto sha256 = serde::RequiredField<String>("sha256"_str);
        auto size   = serde::RequiredField<u64>("size"_str);
        auto root   = serde::RequiredField<String>("root"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, format, url, sha256, size, root));
        return Ok(lito::llvm_catalog_wire::Archive {
            .format = rstd_try(format.take(deserializer)),
            .url    = rstd_try(url.take(deserializer)),
            .sha256 = rstd_try(sha256.take(deserializer)),
            .size   = rstd_try(size.take(deserializer)),
            .root   = rstd_try(root.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Paths> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Paths, typename Deserializer::error_type> {
        auto cc          = serde::RequiredField<String>("cc"_str);
        auto cxx         = serde::RequiredField<String>("cxx"_str);
        auto linker      = serde::RequiredField<String>("linker"_str);
        auto archiver    = serde::RequiredField<String>("archiver"_str);
        auto strip       = serde::RequiredField<String>("strip"_str);
        auto format      = serde::RequiredField<String>("format"_str);
        auto llvm_config = serde::RequiredField<String>("llvm-config"_str);
        auto cmake       = serde::RequiredField<String>("cmake"_str);
        auto clang_cpp   = serde::RequiredField<String>("clang-cpp"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           cc,
                                           cxx,
                                           linker,
                                           archiver,
                                           strip,
                                           format,
                                           llvm_config,
                                           cmake,
                                           clang_cpp));
        return Ok(lito::llvm_catalog_wire::Paths {
            .cc          = rstd_try(cc.take(deserializer)),
            .cxx         = rstd_try(cxx.take(deserializer)),
            .linker      = rstd_try(linker.take(deserializer)),
            .archiver    = rstd_try(archiver.take(deserializer)),
            .strip       = rstd_try(strip.take(deserializer)),
            .format      = rstd_try(format.take(deserializer)),
            .llvm_config = rstd_try(llvm_config.take(deserializer)),
            .cmake       = rstd_try(cmake.take(deserializer)),
            .clang_cpp   = rstd_try(clang_cpp.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Runtime> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Runtime, typename Deserializer::error_type> {
        auto file    = serde::RequiredField<String>("file"_str);
        auto soname  = serde::RequiredField<String>("soname"_str);
        auto links   = serde::RequiredField<Vec<String>>("links"_str);
        auto license = serde::RequiredField<String>("license"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, file, soname, links, license));
        return Ok(lito::llvm_catalog_wire::Runtime {
            .file    = rstd_try(file.take(deserializer)),
            .soname  = rstd_try(soname.take(deserializer)),
            .links   = rstd_try(links.take(deserializer)),
            .license = rstd_try(license.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::RuntimeComponent> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::RuntimeComponent, typename Deserializer::error_type> {
        auto name    = serde::RequiredField<String>("name"_str);
        auto version = serde::RequiredField<String>("version"_str);
        auto recipe  = serde::RequiredField<String>("recipe"_str);
        auto runtime = serde::RequiredField<lito::llvm_catalog_wire::Runtime>("runtime"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, name, version, recipe, runtime));
        return Ok(lito::llvm_catalog_wire::RuntimeComponent {
            .name    = rstd_try(name.take(deserializer)),
            .version = rstd_try(version.take(deserializer)),
            .recipe  = rstd_try(recipe.take(deserializer)),
            .runtime = rstd_try(runtime.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Artifact> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Artifact, typename Deserializer::error_type> {
        auto host    = serde::RequiredField<lito::llvm_catalog_wire::Host>("host"_str);
        auto archive = serde::RequiredField<lito::llvm_catalog_wire::Archive>("archive"_str);
        auto paths   = serde::RequiredField<lito::llvm_catalog_wire::Paths>("paths"_str);
        auto runtime_components = serde::RequiredField<Vec<String>>("runtime-components"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           host,
                                           archive,
                                           paths,
                                           runtime_components));
        return Ok(lito::llvm_catalog_wire::Artifact {
            .host               = rstd_try(host.take(deserializer)),
            .archive            = rstd_try(archive.take(deserializer)),
            .paths              = rstd_try(paths.take(deserializer)),
            .runtime_components = rstd_try(runtime_components.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Release> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Release, typename Deserializer::error_type> {
        auto version      = serde::RequiredField<String>("version"_str);
        auto upstream_tag = serde::RequiredField<String>("upstream-tag"_str);
        auto artifacts =
            serde::RequiredField<Vec<lito::llvm_catalog_wire::Artifact>>("artifacts"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, version, upstream_tag, artifacts));
        return Ok(lito::llvm_catalog_wire::Release {
            .version      = rstd_try(version.take(deserializer)),
            .upstream_tag = rstd_try(upstream_tag.take(deserializer)),
            .artifacts    = rstd_try(artifacts.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::llvm_catalog_wire::Catalog> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::llvm_catalog_wire::Catalog, typename Deserializer::error_type> {
        auto schema = serde::RequiredField<u64>("schema"_str);
        auto kind   = serde::RequiredField<String>("kind"_str);
        auto runtime_components =
            serde::RequiredField<Vec<lito::llvm_catalog_wire::RuntimeComponent>>(
                "runtime-components"_str);
        auto releases = serde::RequiredField<Vec<lito::llvm_catalog_wire::Release>>("releases"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           schema,
                                           kind,
                                           runtime_components,
                                           releases));
        return Ok(lito::llvm_catalog_wire::Catalog {
            .schema             = rstd_try(schema.take(deserializer)),
            .kind               = rstd_try(kind.take(deserializer)),
            .runtime_components = rstd_try(runtime_components.take(deserializer)),
            .releases           = rstd_try(releases.take(deserializer)),
        });
    }
};

} // namespace rstd
