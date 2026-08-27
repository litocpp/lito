module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.toolchain.clang:sdk_catalog;

import rstd;
import lito.crypto;
import rstd.json;
import rstd.serde;
import lito.core;
import :sdk_catalog_wire;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

class LlvmSdkCatalogError {
    RSTD_ENUM(LlvmSdkCatalogError,
              (Json, (rstd::json::DecodeError source;)),
              (Data, (rstd::serde::Error source;)),
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
    String                     format;
    lito::parse::HttpsUrl      url;
    lito::crypto::Sha256Digest sha256;
    u64                        size {};
    lito::parse::PathComponent root;

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

enum class LlvmSdkRuntimeRecipe
{
    Libxml2_2_13_8_MinimalElfV1,
};

constexpr auto llvm_sdk_runtime_recipe_name(LlvmSdkRuntimeRecipe recipe) noexcept -> ref<str> {
    switch (recipe) {
    case LlvmSdkRuntimeRecipe::Libxml2_2_13_8_MinimalElfV1:
        return "libxml2-2.13.8-minimal-elf-v1"_str;
    }
    return ""_str;
}

struct LlvmSdkRuntimeComponent {
    String               name;
    String               version;
    LlvmSdkRuntimeRecipe recipe { LlvmSdkRuntimeRecipe::Libxml2_2_13_8_MinimalElfV1 };
    PathBuf              file;
    String               soname;
    Vec<PathBuf>         links;
    PathBuf              license;

    auto clone() const -> LlvmSdkRuntimeComponent {
        return LlvmSdkRuntimeComponent {
            .name    = name.clone(),
            .version = version.clone(),
            .recipe  = recipe,
            .file    = file.clone(),
            .soname  = soname.clone(),
            .links   = links.clone(),
            .license = license.clone(),
        };
    }
};

struct LlvmSdkArtifact {
    lito::system::HostInfo host;
    LlvmSdkArchive         archive;
    LlvmSdkPaths           paths;
    Vec<String>            runtime_components;

    auto clone() const -> LlvmSdkArtifact {
        return LlvmSdkArtifact {
            .host               = host.clone(),
            .archive            = archive.clone(),
            .paths              = paths.clone(),
            .runtime_components = runtime_components.clone(),
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
    Vec<LlvmSdkRuntimeComponent> runtime_components;
    Vec<LlvmSdkRelease>          releases;
};

auto parse_llvm_version(ref<str> value) -> LlvmSdkCatalogResult<LlvmVersion>;
auto parse_llvm_sdk_catalog(ref<str> text) -> LlvmSdkCatalogResult<LlvmSdkCatalog>;
auto validate_llvm_sdk_archive_identity(const lito::parse::HttpsUrl&      url,
                                        const lito::crypto::Sha256Digest& sha256,
                                        u64                               size,
                                        ref<str> context) -> LlvmSdkCatalogResult<empty>;
auto validate_llvm_sdk_paths(const LlvmSdkPaths& paths, ref<str> context)
    -> LlvmSdkCatalogResult<empty>;
auto embedded_llvm_sdk_catalog_text() noexcept -> ref<str>;
auto load_embedded_llvm_sdk_catalog() -> LlvmSdkCatalogResult<LlvmSdkCatalog>;
auto find_llvm_sdk_release(const LlvmSdkCatalog& catalog, ref<str> version)
    -> Option<ref<LlvmSdkRelease>>;
auto find_llvm_sdk_artifact(const LlvmSdkRelease& release, const lito::system::HostInfo& host)
    -> Option<ref<LlvmSdkArtifact>>;
auto find_llvm_sdk_runtime_component(const LlvmSdkCatalog& catalog, ref<str> name)
    -> Option<ref<LlvmSdkRuntimeComponent>>;
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
        if (error.is_Data()) {
            return formatter.write_raw("LLVM SDK catalog is invalid",
                                       sizeof("LLVM SDK catalog is invalid") - 1);
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
        if (error.is_Data()) return Some(dyn<error::Error>::from_ref(error.as_Data().source));
        return None();
    }
};

template<>
struct Impl<convert::From<rstd::json::DecodeError>, lito::LlvmSdkCatalogError> {
    static auto from(rstd::json::DecodeError error) -> lito::LlvmSdkCatalogError {
        return lito::LlvmSdkCatalogError::Json(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<rstd::serde::Error>, lito::LlvmSdkCatalogError> {
    static auto from(rstd::serde::Error error) -> lito::LlvmSdkCatalogError {
        return lito::LlvmSdkCatalogError::Data(rstd::move(error));
    }
};

} // namespace rstd

namespace lito
{

template<typename T>
auto catalog_failure(String message) -> LlvmSdkCatalogResult<T> {
    return Err(LlvmSdkCatalogError::Message(rstd::move(message)));
}

template<typename T>
auto llvm_catalog_failure(rstd::serde::DataPath path, ref<str> message) -> LlvmSdkCatalogResult<T> {
    return Err(
        LlvmSdkCatalogError::Data(rstd::serde::Error::invalid_value(rstd::move(path), message)));
}

template<typename T, typename Source>
    requires Impled<rstd::mtp::rm_cvf<Source>, rstd::error::Error>
auto llvm_catalog_failure(rstd::serde::DataPath path, ref<str> message, Source source)
    -> LlvmSdkCatalogResult<T> {
    return Err(LlvmSdkCatalogError::Data(rstd::serde::Error::invalid_value_with_source(
        rstd::move(path), message, rstd::move(source))));
}

auto parse_decimal_at(ref<str> text, rstd::serde::DataPath path) -> LlvmSdkCatalogResult<u64> {
    auto parsed = lito::parse::parse_canonical_u64_decimal(text);
    if (parsed.is_err()) {
        return llvm_catalog_failure<u64>(rstd::move(path),
                                         "version component is invalid"_str,
                                         rstd::move(parsed).unwrap_err_unchecked());
    }
    return Ok(rstd::move(parsed).unwrap_unchecked());
}

auto parse_llvm_version_at(ref<str> value, rstd::serde::DataPath path)
    -> LlvmSdkCatalogResult<LlvmVersion> {
    auto first = value.split_once("."_str);
    if (first.is_none()) {
        return llvm_catalog_failure<LlvmVersion>(rstd::move(path),
                                                 "LLVM version must have three components"_str);
    }
    auto second = first->get<1>().split_once("."_str);
    if (second.is_none() || second->get<1>().contains("."_str)) {
        return llvm_catalog_failure<LlvmVersion>(rstd::move(path),
                                                 "LLVM version must have three components"_str);
    }
    auto major = rstd_try(parse_decimal_at(first->get<0>(), path.clone()));
    auto minor = rstd_try(parse_decimal_at(second->get<0>(), path.clone()));
    auto patch = rstd_try(parse_decimal_at(second->get<1>(), path.clone()));
    return Ok(LlvmVersion {
        .major = major,
        .minor = minor,
        .patch = patch,
        .text  = String::make(value),
    });
}

auto parse_host(llvm_catalog_wire::Host value, rstd::serde::DataPath path)
    -> LlvmSdkCatalogResult<lito::system::HostInfo> {
    if (value.os != "linux"_str && value.os != "macos"_str && value.os != "windows"_str) {
        return llvm_catalog_failure<lito::system::HostInfo>(
            path.with_field("os"_str), "host operating system is not canonical"_str);
    }
    auto canonical = lito::system::require_architecture(value.architecture.as_str());
    if (canonical.is_err()) {
        return llvm_catalog_failure<lito::system::HostInfo>(
            path.with_field("architecture"_str),
            "host architecture is invalid"_str,
            rstd::move(canonical).unwrap_err_unchecked());
    }
    auto parsed_architecture = rstd::move(canonical).unwrap_unchecked();
    return Ok(lito::system::HostInfo {
        .architecture = rstd::move(parsed_architecture),
        .os           = rstd::move(value.os),
    });
}

auto parse_archive(llvm_catalog_wire::Archive value, rstd::serde::DataPath path)
    -> LlvmSdkCatalogResult<LlvmSdkArchive> {
    if (value.format != "tar.xz"_str) {
        return llvm_catalog_failure<LlvmSdkArchive>(path.with_field("format"_str),
                                                    "archive format is not supported"_str);
    }
    auto url = lito::parse::HttpsUrl::parse(value.url.as_str());
    if (url.is_err()) {
        return llvm_catalog_failure<LlvmSdkArchive>(path.with_field("url"_str),
                                                    "archive URL is invalid"_str,
                                                    rstd::move(url).unwrap_err_unchecked());
    }
    auto sha256 =
        lito::parse::parse_sha256(value.sha256.as_str(), lito::parse::Sha256TextMode::Canonical);
    if (sha256.is_err()) {
        return llvm_catalog_failure<LlvmSdkArchive>(path.with_field("sha256"_str),
                                                    "archive SHA256 is invalid"_str,
                                                    rstd::move(sha256).unwrap_err_unchecked());
    }
    auto root = lito::parse::PathComponent::parse(value.root.as_str());
    if (root.is_err()) {
        return llvm_catalog_failure<LlvmSdkArchive>(path.with_field("root"_str),
                                                    "archive root is invalid"_str,
                                                    rstd::move(root).unwrap_err_unchecked());
    }
    if (value.size == u64 {}) {
        return llvm_catalog_failure<LlvmSdkArchive>(path.with_field("size"_str),
                                                    "archive size must be non-zero"_str);
    }
    return Ok(LlvmSdkArchive {
        .format = rstd::move(value.format),
        .url    = rstd::move(url).unwrap_unchecked(),
        .sha256 = rstd::move(sha256).unwrap_unchecked(),
        .size   = value.size,
        .root   = rstd::move(root).unwrap_unchecked(),
    });
}

auto parse_paths(llvm_catalog_wire::Paths value, rstd::serde::DataPath path)
    -> LlvmSdkCatalogResult<LlvmSdkPaths> {
    auto paths = LlvmSdkPaths {
        .cc          = PathBuf::from(rstd::move(value.cc)),
        .cxx         = PathBuf::from(rstd::move(value.cxx)),
        .linker      = PathBuf::from(rstd::move(value.linker)),
        .archiver    = PathBuf::from(rstd::move(value.archiver)),
        .strip       = PathBuf::from(rstd::move(value.strip)),
        .format      = PathBuf::from(rstd::move(value.format)),
        .llvm_config = PathBuf::from(rstd::move(value.llvm_config)),
        .cmake       = PathBuf::from(rstd::move(value.cmake)),
        .clang_cpp   = PathBuf::from(rstd::move(value.clang_cpp)),
    };
    const ref<str>              names[]  = { "cc"_str,          "cxx"_str,   "linker"_str,
                                             "archiver"_str,    "strip"_str, "format"_str,
                                             "llvm-config"_str, "cmake"_str, "clang-cpp"_str };
    const ref<rstd::path::Path> values[] = {
        paths.cc.as_path(),          paths.cxx.as_path(),   paths.linker.as_path(),
        paths.archiver.as_path(),    paths.strip.as_path(), paths.format.as_path(),
        paths.llvm_config.as_path(), paths.cmake.as_path(), paths.clang_cpp.as_path(),
    };
    for (usize index {}; index < usize(9); ++index) {
        auto text = values[index.to_primitive()].to_str();
        if (text.is_none()) {
            return llvm_catalog_failure<LlvmSdkPaths>(path.with_field(names[index.to_primitive()]),
                                                      "SDK path is not UTF-8"_str);
        }
        auto parsed = lito::parse::NormalRelativePath::parse(*text);
        if (parsed.is_err()) {
            return llvm_catalog_failure<LlvmSdkPaths>(path.with_field(names[index.to_primitive()]),
                                                      "SDK path is not a normal relative path"_str,
                                                      rstd::move(parsed).unwrap_err_unchecked());
        }
    }
    for (usize left {}; left < usize(9); ++left) {
        for (usize right = left + usize(1); right < usize(9); ++right) {
            if (values[left.to_primitive()].as_os_str().as_encoded_bytes() ==
                values[right.to_primitive()].as_os_str().as_encoded_bytes()) {
                return llvm_catalog_failure<LlvmSdkPaths>(
                    path.with_field(names[right.to_primitive()]), "SDK path is repeated"_str);
            }
        }
    }
    return Ok(rstd::move(paths));
}

auto parse_runtime_component(llvm_catalog_wire::RuntimeComponent value, rstd::serde::DataPath path)
    -> LlvmSdkCatalogResult<LlvmSdkRuntimeComponent> {
    auto name = lito::parse::PathComponent::parse(value.name.as_str());
    if (name.is_err()) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
            path.with_field("name"_str),
            "runtime component name is invalid"_str,
            rstd::move(name).unwrap_err_unchecked());
    }
    rstd_try(parse_llvm_version_at(value.version.as_str(), path.with_field("version"_str)));
    auto recipe = Option<LlvmSdkRuntimeRecipe> {};
    if (value.recipe.as_str() ==
        llvm_sdk_runtime_recipe_name(LlvmSdkRuntimeRecipe::Libxml2_2_13_8_MinimalElfV1)) {
        recipe = Some(LlvmSdkRuntimeRecipe::Libxml2_2_13_8_MinimalElfV1);
    }
    if (recipe.is_none()) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
            path.with_field("recipe"_str), "runtime component recipe is unknown"_str);
    }
    auto runtime_path = path.with_field("runtime"_str);
    auto file         = lito::parse::NormalRelativePath::parse(value.runtime.file.as_str());
    if (file.is_err()) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
            runtime_path.with_field("file"_str),
            "runtime file is not a normal relative path"_str,
            rstd::move(file).unwrap_err_unchecked());
    }
    if (! value.runtime.file.as_str().starts_with("lib/"_str)) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(runtime_path.with_field("file"_str),
                                                             "runtime file must be under lib"_str);
    }
    auto soname = lito::parse::PathComponent::parse(value.runtime.soname.as_str());
    if (soname.is_err()) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
            runtime_path.with_field("soname"_str),
            "runtime soname is not a file name"_str,
            rstd::move(soname).unwrap_err_unchecked());
    }
    auto license = lito::parse::NormalRelativePath::parse(value.runtime.license.as_str());
    if (license.is_err()) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
            runtime_path.with_field("license"_str),
            "runtime license is not a normal relative path"_str,
            rstd::move(license).unwrap_err_unchecked());
    }
    if (! value.runtime.license.as_str().starts_with("share/licenses/"_str)) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
            runtime_path.with_field("license"_str),
            "runtime license must be under share/licenses"_str);
    }
    if (value.runtime.links.is_empty()) {
        return llvm_catalog_failure<LlvmSdkRuntimeComponent>(runtime_path.with_field("links"_str),
                                                             "runtime links must not be empty"_str);
    }
    auto links = Vec<PathBuf>::with_capacity(value.runtime.links.len());
    for (usize index {}; index < value.runtime.links.len(); ++index) {
        auto link_path = runtime_path.with_field("links"_str).with_index(index);
        auto parsed = lito::parse::NormalRelativePath::parse(value.runtime.links[index].as_str());
        if (parsed.is_err()) {
            return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
                rstd::move(link_path),
                "runtime link is not a normal relative path"_str,
                rstd::move(parsed).unwrap_err_unchecked());
        }
        if (! value.runtime.links[index].as_str().starts_with("lib/"_str)) {
            return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
                rstd::move(link_path), "runtime link must be under lib"_str);
        }
        auto parsed_path = PathBuf::from(rstd::move(value.runtime.links[index]));
        for (const auto& existing : links) {
            if (existing.as_path() == parsed_path.as_path()) {
                return llvm_catalog_failure<LlvmSdkRuntimeComponent>(
                    rstd::move(link_path), "runtime link is repeated"_str);
            }
        }
        links.push(rstd::move(parsed_path));
    }
    return Ok(LlvmSdkRuntimeComponent {
        .name    = rstd::move(value.name),
        .version = rstd::move(value.version),
        .recipe  = *recipe,
        .file    = PathBuf::from(rstd::move(value.runtime.file)),
        .soname  = rstd::move(value.runtime.soname),
        .links   = rstd::move(links),
        .license = PathBuf::from(rstd::move(value.runtime.license)),
    });
}

auto find_runtime_component(const Vec<LlvmSdkRuntimeComponent>& components, ref<str> name)
    -> Option<ref<LlvmSdkRuntimeComponent>> {
    for (const auto& component : components) {
        if (component.name.as_str() == name) {
            return Some(ref<LlvmSdkRuntimeComponent>::from_raw_parts(rstd::addressof(component)));
        }
    }
    return None();
}

auto parse_artifact(llvm_catalog_wire::Artifact         value,
                    rstd::serde::DataPath               path,
                    const Vec<LlvmSdkRuntimeComponent>& available_components)
    -> LlvmSdkCatalogResult<LlvmSdkArtifact> {
    auto parsed_host = rstd_try(parse_host(rstd::move(value.host), path.with_field("host"_str)));
    auto components  = Vec<String>::with_capacity(value.runtime_components.len());
    for (usize index {}; index < value.runtime_components.len(); ++index) {
        auto component_path = path.with_field("runtime-components"_str).with_index(index);
        auto component      = rstd::move(value.runtime_components[index]);
        if (component.is_empty()) {
            return llvm_catalog_failure<LlvmSdkArtifact>(
                rstd::move(component_path), "runtime component name must not be empty"_str);
        }
        auto definition = find_runtime_component(available_components, component.as_str());
        if (definition.is_none()) {
            return llvm_catalog_failure<LlvmSdkArtifact>(rstd::move(component_path),
                                                         "runtime component is not defined"_str);
        }
        if ((**definition).recipe == LlvmSdkRuntimeRecipe::Libxml2_2_13_8_MinimalElfV1 &&
            parsed_host.os.as_str() != "linux"_str) {
            return llvm_catalog_failure<LlvmSdkArtifact>(
                rstd::move(component_path), "runtime component requires a Linux host"_str);
        }
        for (const auto& existing : components) {
            if (existing == component.as_str()) {
                return llvm_catalog_failure<LlvmSdkArtifact>(rstd::move(component_path),
                                                             "runtime component is repeated"_str);
            }
        }
        components.push(rstd::move(component));
    }
    return Ok(LlvmSdkArtifact {
        .host = rstd::move(parsed_host),
        .archive =
            rstd_try(parse_archive(rstd::move(value.archive), path.with_field("archive"_str))),
        .paths = rstd_try(parse_paths(rstd::move(value.paths), path.with_field("paths"_str))),
        .runtime_components = rstd::move(components),
    });
}

auto parse_release(llvm_catalog_wire::Release          value,
                   rstd::serde::DataPath               path,
                   const Vec<LlvmSdkRuntimeComponent>& runtime_components)
    -> LlvmSdkCatalogResult<LlvmSdkRelease> {
    auto version =
        rstd_try(parse_llvm_version_at(value.version.as_str(), path.with_field("version"_str)));
    auto expected_tag = rstd::format("llvmorg-{}", version.text.as_str());
    if (value.upstream_tag != expected_tag.as_str()) {
        return llvm_catalog_failure<LlvmSdkRelease>(path.with_field("upstream-tag"_str),
                                                    "LLVM upstream tag does not match version"_str);
    }
    if (value.artifacts.is_empty()) {
        return llvm_catalog_failure<LlvmSdkRelease>(path.with_field("artifacts"_str),
                                                    "release artifacts must not be empty"_str);
    }
    auto artifacts = Vec<LlvmSdkArtifact>::with_capacity(value.artifacts.len());
    for (usize artifact_index {}; artifact_index < value.artifacts.len(); ++artifact_index) {
        auto artifact_path = path.with_field("artifacts"_str).with_index(artifact_index);
        auto artifact      = rstd_try(parse_artifact(rstd::move(value.artifacts[artifact_index]),
                                                     artifact_path.clone(),
                                                     runtime_components));
        for (const auto& existing : artifacts) {
            if (existing.host.os == artifact.host.os.as_str() &&
                existing.host.architecture == artifact.host.architecture) {
                return llvm_catalog_failure<LlvmSdkRelease>(
                    artifact_path.with_field("host"_str), "release host artifact is repeated"_str);
            }
        }
        artifacts.push(rstd::move(artifact));
    }
    rstd::slice_::sort_unstable_by(artifacts.as_mut_slice().as_mut_ref(),
                                   [](const LlvmSdkArtifact& left, const LlvmSdkArtifact& right) {
                                       if (left.host.os != right.host.os.as_str())
                                           return left.host.os < right.host.os;
                                       return left.host.architecture < right.host.architecture;
                                   });
    return Ok(LlvmSdkRelease {
        .version      = rstd::move(version),
        .upstream_tag = rstd::move(value.upstream_tag),
        .artifacts    = rstd::move(artifacts),
    });
}

static constexpr unsigned char EMBEDDED_LLVM_SDK_CATALOG[] = {
#embed "../../../data/llvm-sdk.json"
};

} // namespace lito

export namespace lito
{

auto parse_llvm_version(ref<str> value) -> LlvmSdkCatalogResult<LlvmVersion> {
    return parse_llvm_version_at(value, rstd::serde::DataPath());
}

auto llvm_version_less(const LlvmVersion& left, const LlvmVersion& right) noexcept -> bool {
    if (left.major != right.major) return left.major < right.major;
    if (left.minor != right.minor) return left.minor < right.minor;
    return left.patch < right.patch;
}

auto parse_llvm_sdk_catalog(ref<str> text) -> LlvmSdkCatalogResult<LlvmSdkCatalog> {
    auto document = rstd_try(rstd::json::decode<llvm_catalog_wire::Catalog>(text));
    auto root     = rstd::serde::DataPath();
    if (document.schema != u64(1)) {
        return llvm_catalog_failure<LlvmSdkCatalog>(root.with_field("schema"_str),
                                                    "LLVM SDK catalog schema must be 1"_str);
    }
    if (document.kind != "lito-llvm-sdk"_str) {
        return llvm_catalog_failure<LlvmSdkCatalog>(root.with_field("kind"_str),
                                                    "LLVM SDK catalog kind is invalid"_str);
    }
    if (document.runtime_components.is_empty()) {
        return llvm_catalog_failure<LlvmSdkCatalog>(
            root.with_field("runtime-components"_str),
            "LLVM SDK catalog runtime components must not be empty"_str);
    }
    auto runtime_components =
        Vec<LlvmSdkRuntimeComponent>::with_capacity(document.runtime_components.len());
    for (usize index {}; index < document.runtime_components.len(); ++index) {
        auto component_path = root.with_field("runtime-components"_str).with_index(index);
        auto component      = rstd_try(parse_runtime_component(
            rstd::move(document.runtime_components[index]), component_path.clone()));
        for (const auto& existing : runtime_components) {
            if (existing.name == component.name.as_str() || existing.recipe == component.recipe) {
                return llvm_catalog_failure<LlvmSdkCatalog>(
                    rstd::move(component_path), "runtime component name or recipe is repeated"_str);
            }
        }
        runtime_components.push(rstd::move(component));
    }
    auto releases = Vec<LlvmSdkRelease>::with_capacity(document.releases.len());
    for (usize index {}; index < document.releases.len(); ++index) {
        auto release_path = root.with_field("releases"_str).with_index(index);
        auto release      = rstd_try(parse_release(
            rstd::move(document.releases[index]), release_path.clone(), runtime_components));
        for (const auto& existing : releases) {
            if (existing.version.text == release.version.text.as_str()) {
                return llvm_catalog_failure<LlvmSdkCatalog>(release_path.with_field("version"_str),
                                                            "LLVM version is repeated"_str);
            }
        }
        releases.push(rstd::move(release));
    }
    rstd::slice_::sort_unstable_by(releases.as_mut_slice().as_mut_ref(),
                                   [](const LlvmSdkRelease& left, const LlvmSdkRelease& right) {
                                       return llvm_version_less(right.version, left.version);
                                   });
    return Ok(LlvmSdkCatalog {
        .runtime_components = rstd::move(runtime_components),
        .releases           = rstd::move(releases),
    });
}

auto validate_llvm_sdk_archive_identity(const lito::parse::HttpsUrl&,
                                        const lito::crypto::Sha256Digest&,
                                        u64      size,
                                        ref<str> context) -> LlvmSdkCatalogResult<empty> {
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
        if (text.is_none() || lito::parse::NormalRelativePath::parse(*text).is_err()) {
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

auto find_llvm_sdk_runtime_component(const LlvmSdkCatalog& catalog, ref<str> name)
    -> Option<ref<LlvmSdkRuntimeComponent>> {
    return find_runtime_component(catalog.runtime_components, name);
}

} // namespace lito
