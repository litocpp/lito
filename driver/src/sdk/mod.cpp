module;
#include <initializer_list>
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import rstd.json;
import lito.core;
import lito.toolchain;
import :build;
import :sdk;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf   = rstd::path::PathBuf;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

static constexpr unsigned char LIBXML2_RECIPE_MANIFEST[] = {
#embed "../../../data/sdk/libxml2/2.13.8-minimal-elf-v1/lito.toml"
};
static constexpr unsigned char LIBXML2_RECIPE_LOCK[] = {
#embed "../../../data/sdk/libxml2/2.13.8-minimal-elf-v1/lito.lock"
};
static constexpr unsigned char LIBXML2_RECIPE_SCRIPT[] = {
#embed "../../../data/sdk/libxml2/2.13.8-minimal-elf-v1/build.lua"
};
static constexpr unsigned char LIBXML2_RECIPE_CONFIG[] = {
#embed "../../../data/sdk/libxml2/2.13.8-minimal-elf-v1/config/config.h.in"
};
static constexpr unsigned char LIBXML2_RECIPE_XMLVERSION[] = {
#embed "../../../data/sdk/libxml2/2.13.8-minimal-elf-v1/config/xmlversion.h.in"
};

template<rstd::size_t Size>
auto embedded_text(const unsigned char (&contents)[Size]) noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(contents), usize(Size));
}

struct EmbeddedSdkRecipe {
    String                   id;
    String                   version;
    String                   digest;
    String                   package;
    String                   external_source;
    String                   target;
    String                   profile;
    PathBuf                  version_script;
    PathBuf                  license;
    lito::source::SourceTree tree;
};

auto libxml2_recipe() -> lito::SdkResult<EmbeddedSdkRecipe> {
    auto tree = lito::source::SourceTree::make();
    const struct RecipeFile {
        ref<str> path;
        ref<str> contents;
    } files[] = {
        { "lito.toml"_str, embedded_text(LIBXML2_RECIPE_MANIFEST) },
        { "lito.lock"_str, embedded_text(LIBXML2_RECIPE_LOCK) },
        { "build.lua"_str, embedded_text(LIBXML2_RECIPE_SCRIPT) },
        { "config/config.h.in"_str, embedded_text(LIBXML2_RECIPE_CONFIG) },
        { "config/xmlversion.h.in"_str, embedded_text(LIBXML2_RECIPE_XMLVERSION) },
    };
    auto recipe_id =
        lito::llvm_sdk_runtime_recipe_name(lito::LlvmSdkRuntimeRecipe::Libxml2_2_13_8_MinimalElfV1);
    auto identity = rstd::format("lito-sdk-recipe-v3\n"
                                 "id:{}\n"
                                 "version:2.13.8\n"
                                 "package:lito-llvm-sdk-libxml2\n"
                                 "external-source:libxml2\n"
                                 "target:libxml2-bootstrap\n"
                                 "profile:sdk\n"
                                 "version-script:libxml2.syms\n"
                                 "license:Copyright\n"
                                 "strip:all\n",
                                 recipe_id);
    for (const auto& file : files) {
        auto added = tree.add_text(file.path, file.contents);
        if (added.is_err()) {
            return Err(lito::SdkError::SourceTree(rstd::move(added).unwrap_err()));
        }
        identity.push_str(file.path);
        identity.push_ascii('\n');
        identity.push_str(file.contents);
        identity.push_ascii('\n');
    }
    return Ok(EmbeddedSdkRecipe {
        .id              = String::make(recipe_id),
        .version         = String::make("2.13.8"_str),
        .digest          = rstd::crypto::sha256_hex(identity.as_str()),
        .package         = String::make("lito-llvm-sdk-libxml2"_str),
        .external_source = String::make("libxml2"_str),
        .target          = String::make("libxml2-bootstrap"_str),
        .profile         = String::make("sdk"_str),
        .version_script  = PathBuf::from("libxml2.syms"_str),
        .license         = PathBuf::from("Copyright"_str),
        .tree            = rstd::move(tree),
    });
}

struct InstalledFileRecord {
    PathBuf path;
    u64     size {};
    String  sha256;
};

struct InstalledLinkRecord {
    PathBuf path;
    PathBuf target;
};

struct InstalledRuntimeComponent {
    String                   name;
    String                   version;
    String                   recipe;
    String                   recipe_digest;
    String                   source_identity;
    InstalledFileRecord      runtime;
    Vec<InstalledLinkRecord> links;
    InstalledFileRecord      license;
    String                   compiler_version;
    String                   compiler_target;
    String                   linker_family;
    String                   linker_version;
    String                   archiver_version;
    String                   strip_version;
    String                   link_identity;
};

struct InstalledSdkDescriptor {
    String                         version;
    lito::system::HostInfo         host;
    String                         url;
    String                         sha256;
    u64                            size {};
    lito::LlvmSdkPaths             paths;
    lito::LlvmSdkCertification     certification;
    Vec<InstalledRuntimeComponent> components;
};

auto installed_file_record(ref<rstd::path::Path> root, ref<rstd::path::Path> relative)
    -> lito::SdkResult<InstalledFileRecord>;

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

auto required_array(const Json& value, ref<str> key, ref<str> context)
    -> lito::SdkResult<ref<JsonArray>> {
    auto member = rstd_try(required_member(value, key, context));
    auto array  = member->as_array();
    if (array.is_none()) {
        return sdk_failure<ref<JsonArray>>(rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto descriptor_path(String text, ref<str> context) -> lito::SdkResult<PathBuf> {
    auto path = PathBuf::from(rstd::move(text));
    if (path.is_empty() || ! path.as_path().is_safe_relative()) {
        return sdk_failure<PathBuf>(rstd::format("{} must be a safe relative path", context));
    }
    auto components = path.as_path().components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal()) {
            return sdk_failure<PathBuf>(rstd::format("{} must be a normal relative path", context));
        }
    }
    return Ok(rstd::move(path));
}

auto descriptor_file_name(String text, ref<str> context) -> lito::SdkResult<PathBuf> {
    auto path       = PathBuf::from(rstd::move(text));
    auto components = path.as_path().components();
    auto first      = components.next();
    if (first.is_none() || ! first->is_normal() || components.next().is_some()) {
        return sdk_failure<PathBuf>(rstd::format("{} must be a relative file name", context));
    }
    return Ok(rstd::move(path));
}

auto descriptor_sha256(ref<str> value, ref<str> context) -> lito::SdkResult<String> {
    if (value.len() != usize(64)) {
        return sdk_failure<String>(rstd::format("{} must be a lowercase SHA-256", context));
    }
    for (const auto byte : value.as_bytes()) {
        if ((byte >= u8('0') && byte <= u8('9')) || (byte >= u8('a') && byte <= u8('f'))) continue;
        return sdk_failure<String>(rstd::format("{} must be a lowercase SHA-256", context));
    }
    return Ok(String::make(value));
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

auto installed_file_json(const InstalledFileRecord& file) -> Json {
    auto value = JsonMap::make();
    value.insert(String::make("path"_str), json_string(file.path.as_path().to_str().unwrap()));
    value.insert(String::make("size"_str), Json::Number(rstd::json::Number::from_u64(file.size)));
    value.insert(String::make("sha256"_str), json_string(file.sha256.as_str()));
    return Json::Object(rstd::move(value));
}

auto installed_component_json(const InstalledRuntimeComponent& component) -> Json {
    auto builder = JsonMap::make();
    builder.insert(String::make("compiler-version"_str),
                   json_string(component.compiler_version.as_str()));
    builder.insert(String::make("compiler-target"_str),
                   json_string(component.compiler_target.as_str()));
    builder.insert(String::make("linker-family"_str),
                   json_string(component.linker_family.as_str()));
    builder.insert(String::make("linker-version"_str),
                   json_string(component.linker_version.as_str()));
    builder.insert(String::make("archiver-version"_str),
                   json_string(component.archiver_version.as_str()));
    builder.insert(String::make("strip-version"_str),
                   json_string(component.strip_version.as_str()));
    builder.insert(String::make("link-identity"_str),
                   json_string(component.link_identity.as_str()));
    auto links = JsonArray::make();
    for (const auto& link : component.links) {
        auto value = JsonMap::make();
        value.insert(String::make("path"_str), json_string(link.path.as_path().to_str().unwrap()));
        value.insert(String::make("target"_str),
                     json_string(link.target.as_path().to_str().unwrap()));
        links.push(Json::Object(rstd::move(value)));
    }
    auto value = JsonMap::make();
    value.insert(String::make("name"_str), json_string(component.name.as_str()));
    value.insert(String::make("version"_str), json_string(component.version.as_str()));
    value.insert(String::make("recipe"_str), json_string(component.recipe.as_str()));
    value.insert(String::make("recipe-digest"_str), json_string(component.recipe_digest.as_str()));
    value.insert(String::make("source-identity"_str),
                 json_string(component.source_identity.as_str()));
    value.insert(String::make("runtime"_str), installed_file_json(component.runtime));
    value.insert(String::make("links"_str), Json::Array(rstd::move(links)));
    value.insert(String::make("license"_str), installed_file_json(component.license));
    value.insert(String::make("builder"_str), Json::Object(rstd::move(builder)));
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

    auto root       = JsonMap::make();
    auto components = JsonArray::make();
    for (const auto& component : descriptor.components) {
        components.push(installed_component_json(component));
    }
    root.insert(String::make("schema"_str), Json::Number(rstd::json::Number::from_u64(u64(2))));
    root.insert(String::make("kind"_str), json_string("lito-llvm-sdk"_str));
    root.insert(String::make("version"_str), json_string(descriptor.version.as_str()));
    root.insert(String::make("host"_str), Json::Object(rstd::move(host)));
    root.insert(String::make("archive"_str), Json::Object(rstd::move(archive)));
    root.insert(String::make("paths"_str), paths_json(descriptor.paths));
    root.insert(String::make("certification"_str), Json::Object(rstd::move(certification)));
    root.insert(String::make("runtime-components"_str), Json::Array(rstd::move(components)));
    return Json::Object(rstd::move(root));
}

auto serialize_descriptor(const InstalledSdkDescriptor& descriptor) -> String {
    auto text =
        rstd::json::to_string(descriptor_json(descriptor),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return text;
}

auto parse_installed_file(const Json& value, ref<str> context)
    -> lito::SdkResult<InstalledFileRecord> {
    rstd_try(known_fields(value, context, { "path"_str, "size"_str, "sha256"_str }));
    auto path   = rstd_try(descriptor_path(rstd_try(required_string(value, "path"_str, context)),
                                           rstd::format("{}.path", context).as_str()));
    auto sha256 = rstd_try(required_string(value, "sha256"_str, context));
    return Ok(InstalledFileRecord {
        .path   = rstd::move(path),
        .size   = rstd_try(required_u64(value, "size"_str, context)),
        .sha256 = rstd_try(
            descriptor_sha256(sha256.as_str(), rstd::format("{}.sha256", context).as_str())),
    });
}

auto parse_installed_component(const Json& value, ref<str> context)
    -> lito::SdkResult<InstalledRuntimeComponent> {
    rstd_try(known_fields(value,
                          context,
                          { "name"_str,
                            "version"_str,
                            "recipe"_str,
                            "recipe-digest"_str,
                            "source-identity"_str,
                            "runtime"_str,
                            "links"_str,
                            "license"_str,
                            "builder"_str }));
    auto name            = rstd_try(required_string(value, "name"_str, context));
    auto version         = rstd_try(required_string(value, "version"_str, context));
    auto recipe          = rstd_try(required_string(value, "recipe"_str, context));
    auto recipe_digest   = rstd_try(required_string(value, "recipe-digest"_str, context));
    auto source_identity = rstd_try(required_string(value, "source-identity"_str, context));
    recipe_digest = rstd_try(descriptor_sha256(recipe_digest.as_str(),
                                               rstd::format("{}.recipe-digest", context).as_str()));
    auto runtime_value   = rstd_try(required_member(value, "runtime"_str, context));
    auto license_value   = rstd_try(required_member(value, "license"_str, context));
    auto builder         = rstd_try(required_member(value, "builder"_str, context));
    auto builder_context = rstd::format("{}.builder", context);
    rstd_try(known_fields(*builder,
                          builder_context.as_str(),
                          { "compiler-version"_str,
                            "compiler-target"_str,
                            "linker-family"_str,
                            "linker-version"_str,
                            "archiver-version"_str,
                            "strip-version"_str,
                            "link-identity"_str }));
    auto link_values = rstd_try(required_array(value, "links"_str, context));
    auto links       = Vec<InstalledLinkRecord>::with_capacity(link_values->len());
    for (usize index {}; index < link_values->len(); ++index) {
        auto link_context = rstd::format("{}.links[{}]", context, index);
        rstd_try(known_fields(
            (*link_values)[index], link_context.as_str(), { "path"_str, "target"_str }));
        auto path   = rstd_try(descriptor_path(
            rstd_try(required_string((*link_values)[index], "path"_str, link_context.as_str())),
            rstd::format("{}.path", link_context.as_str()).as_str()));
        auto target = rstd_try(descriptor_file_name(
            rstd_try(required_string((*link_values)[index], "target"_str, link_context.as_str())),
            rstd::format("{}.target", link_context.as_str()).as_str()));
        links.push(InstalledLinkRecord {
            .path   = rstd::move(path),
            .target = rstd::move(target),
        });
    }
    return Ok(InstalledRuntimeComponent {
        .name            = rstd::move(name),
        .version         = rstd::move(version),
        .recipe          = rstd::move(recipe),
        .recipe_digest   = rstd::move(recipe_digest),
        .source_identity = rstd::move(source_identity),
        .runtime         = rstd_try(
            parse_installed_file(*runtime_value, rstd::format("{}.runtime", context).as_str())),
        .links   = rstd::move(links),
        .license = rstd_try(
            parse_installed_file(*license_value, rstd::format("{}.license", context).as_str())),
        .compiler_version =
            rstd_try(required_string(*builder, "compiler-version"_str, builder_context.as_str())),
        .compiler_target =
            rstd_try(required_string(*builder, "compiler-target"_str, builder_context.as_str())),
        .linker_family =
            rstd_try(required_string(*builder, "linker-family"_str, builder_context.as_str())),
        .linker_version =
            rstd_try(required_string(*builder, "linker-version"_str, builder_context.as_str())),
        .archiver_version =
            rstd_try(required_string(*builder, "archiver-version"_str, builder_context.as_str())),
        .strip_version =
            rstd_try(required_string(*builder, "strip-version"_str, builder_context.as_str())),
        .link_identity = rstd_try(descriptor_sha256(
            rstd_try(required_string(*builder, "link-identity"_str, builder_context.as_str()))
                .as_str(),
            rstd::format("{}.link-identity", builder_context.as_str()).as_str())),
    });
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
                            "certification"_str,
                            "runtime-components"_str }));
    auto schema = rstd_try(required_u64(value, "schema"_str, "LLVM SDK descriptor root"_str));
    if (schema != u64(2)) {
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
    auto component_values =
        rstd_try(required_array(value, "runtime-components"_str, "LLVM SDK descriptor root"_str));
    if (component_values->is_empty()) {
        return sdk_failure<InstalledSdkDescriptor>(
            "LLVM SDK descriptor runtime-components must not be empty"_str);
    }
    auto components = Vec<InstalledRuntimeComponent>::with_capacity(component_values->len());
    for (usize index {}; index < component_values->len(); ++index) {
        auto component = rstd_try(parse_installed_component(
            (*component_values)[index],
            rstd::format("LLVM SDK descriptor runtime-components[{}]", index).as_str()));
        for (const auto& existing : components) {
            if (existing.name == component.name.as_str()) {
                return sdk_failure<InstalledSdkDescriptor>(rstd::format(
                    "LLVM SDK descriptor repeats runtime component '{}'", component.name));
            }
        }
        components.push(rstd::move(component));
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
        .components = rstd::move(components),
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
                        const lito::LlvmSdkCatalog&   catalog,
                        const lito::LlvmSdkRelease&   release,
                        const lito::LlvmSdkArtifact&  artifact) -> bool {
    if (descriptor.version != release.version.text.as_str() ||
        descriptor.host.os != artifact.host.os.as_str() ||
        descriptor.host.architecture != artifact.host.architecture ||
        descriptor.sha256 != artifact.archive.sha256.as_str() ||
        descriptor.size != artifact.archive.size ||
        ! paths_equal(descriptor.paths, artifact.paths) ||
        descriptor.components.len() != artifact.runtime_components.len()) {
        return false;
    }
    auto recipe = libxml2_recipe();
    if (recipe.is_err()) return false;
    for (const auto& reference : artifact.runtime_components) {
        auto expected = lito::find_llvm_sdk_runtime_component(catalog, reference.as_str());
        if (expected.is_none()) return false;
        const InstalledRuntimeComponent* installed = nullptr;
        for (const auto& candidate : descriptor.components) {
            if (candidate.name == (**expected).name.as_str()) {
                installed = rstd::addressof(candidate);
            }
        }
        if (installed == nullptr || installed->version != (**expected).version.as_str() ||
            installed->recipe != lito::llvm_sdk_runtime_recipe_name((**expected).recipe) ||
            installed->recipe_digest != recipe->digest.as_str() ||
            installed->runtime.path.as_path() != (**expected).file.as_path() ||
            installed->license.path.as_path() != (**expected).license.as_path() ||
            installed->links.len() != (**expected).links.len()) {
            return false;
        }
        for (usize index {}; index < (**expected).links.len(); ++index) {
            auto expected_target = PathBuf::from((**expected).file.as_path().file_name().unwrap());
            if (installed->links[index].path.as_path() != (**expected).links[index].as_path() ||
                installed->links[index].target.as_path() != expected_target.as_path()) {
                return false;
            }
        }
    }
    return true;
}

auto validate_installed_components(ref<rstd::path::Path>                 prefix,
                                   const Vec<InstalledRuntimeComponent>& components)
    -> lito::SdkResult<empty> {
    for (const auto& component : components) {
        const InstalledFileRecord* files[] = { rstd::addressof(component.runtime),
                                               rstd::addressof(component.license) };
        for (const auto* expected : files) {
            auto actual = rstd_try(installed_file_record(prefix, expected->path.as_path()));
            if (actual.size != expected->size || actual.sha256 != expected->sha256.as_str()) {
                return sdk_failure<empty>(rstd::format(
                    "installed SDK file '{}' differs from sdk.json", expected->path.as_path()));
            }
        }
        for (const auto& link : component.links) {
            auto path     = PathBuf::from(prefix).join(link.path.as_path());
            auto metadata = rstd::fs::symlink_metadata(path.as_path());
            if (metadata.is_err()) {
                return sdk_io_failure<empty>("inspect installed SDK link"_str,
                                             path.as_path(),
                                             rstd::move(metadata).unwrap_err());
            }
            if (! metadata->is_symlink()) {
                return sdk_failure<empty>(
                    rstd::format("installed SDK link '{}' is not a symlink", path.as_path()));
            }
            auto target = rstd::fs::read_link(path.as_path());
            if (target.is_err()) {
                return sdk_io_failure<empty>(
                    "read installed SDK link"_str, path.as_path(), rstd::move(target).unwrap_err());
            }
            if (target->as_path() != link.target.as_path()) {
                return sdk_failure<empty>(
                    rstd::format("installed SDK link '{}' targets '{}', expected '{}'",
                                 path.as_path(),
                                 target->as_path(),
                                 link.target.as_path()));
            }
        }
    }
    return Ok(empty {});
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

auto emit_sdk_event(const Option<lito::SdkEventSink>& observer,
                    lito::SdkEventKind                kind,
                    ref<str>                          version,
                    ref<str>                          source,
                    ref<rstd::path::Path>             destination) -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     lito::SdkEvent {
                         .kind        = kind,
                         .version     = version,
                         .source      = source,
                         .destination = destination,
                     });
}

auto sdk_tool_version(ref<rstd::path::Path>                           executable,
                      ref<str>                                        description,
                      const lito::system::ResolvedProcessEnvironment& environment)
    -> lito::SdkResult<String> {
    auto command = Vec<String>::make();
    auto pushed  = lito::toolchain::command::push_path(command, executable);
    if (pushed.is_err()) {
        return Err(lito::SdkError::Toolchain(rstd::move(pushed).unwrap_err()));
    }
    lito::toolchain::command::push_option(command, "--version"_str);
    auto version =
        lito::toolchain::command::tool_output(rstd::move(command), description, environment);
    if (version.is_err()) {
        return Err(lito::SdkError::Toolchain(rstd::move(version).unwrap_err()));
    }
    return Ok(rstd::move(version).unwrap());
}

struct SdkComponentBuildObserver {
    ref<str>                   version;
    ref<str>                   component;
    Option<lito::SdkEventSink> sink;
};

void observe_component_build(void* raw, const lito::BuildEvent& event) noexcept {
    auto& observer = *static_cast<SdkComponentBuildObserver*>(raw);
    if (event.kind == lito::BuildEventKind::Fetch) {
        emit_sdk_event(observer.sink,
                       lito::SdkEventKind::Fetch,
                       observer.version,
                       observer.component,
                       event.path);
        return;
    }
    if (event.kind == lito::BuildEventKind::Extract) {
        emit_sdk_event(observer.sink,
                       lito::SdkEventKind::Extract,
                       observer.version,
                       observer.component,
                       event.path);
    }
}

auto checked_source_file(ref<rstd::path::Path> root,
                         ref<rstd::path::Path> relative,
                         ref<str>              context) -> lito::SdkResult<PathBuf> {
    auto requested = PathBuf::from(root).join(relative);
    auto resolved  = rstd::fs::canonicalize(requested.as_path());
    if (resolved.is_err()) {
        return sdk_io_failure<PathBuf>(
            context, requested.as_path(), rstd::move(resolved).unwrap_err());
    }
    if (resolved->as_path().strip_prefix(root).is_none()) {
        return sdk_failure<PathBuf>(
            rstd::format("{} '{}' escapes external source root '{}'", context, relative, root));
    }
    auto metadata = rstd::fs::symlink_metadata(resolved->as_path());
    if (metadata.is_err()) {
        return sdk_io_failure<PathBuf>(
            context, resolved->as_path(), rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return sdk_failure<PathBuf>(
            rstd::format("{} '{}' must be an ordinary file", context, resolved->as_path()));
    }
    return Ok(rstd::move(resolved).unwrap());
}

auto installed_file_record(ref<rstd::path::Path> root, ref<rstd::path::Path> relative)
    -> lito::SdkResult<InstalledFileRecord> {
    auto path     = PathBuf::from(root).join(relative);
    auto metadata = rstd::fs::symlink_metadata(path.as_path());
    if (metadata.is_err()) {
        return sdk_io_failure<InstalledFileRecord>(
            "inspect installed SDK file"_str, path.as_path(), rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return sdk_failure<InstalledFileRecord>(
            rstd::format("installed SDK file '{}' must be an ordinary file", path.as_path()));
    }
    auto contents = rstd::fs::read(path.as_path());
    if (contents.is_err()) {
        return sdk_io_failure<InstalledFileRecord>(
            "read installed SDK file"_str, path.as_path(), rstd::move(contents).unwrap_err());
    }
    return Ok(InstalledFileRecord {
        .path   = PathBuf::from(relative),
        .size   = metadata->size(),
        .sha256 = rstd::crypto::sha256_hex(contents->as_slice()),
    });
}

auto install_runtime_component(const lito::LlvmSdkRuntimeComponent&            component,
                               const EmbeddedSdkRecipe&                        recipe,
                               ref<str>                                        sdk_version,
                               ref<rstd::path::Path>                           sdk_root,
                               const lito::SdkInstallRequest&                  request,
                               const lito::config::ToolchainSpec&              bootstrap_toolchain,
                               lito::system::ToolResolver&                     resolver,
                               const lito::system::ResolvedProcessEnvironment& environment)
    -> lito::SdkResult<InstalledRuntimeComponent> {
    auto component_recipe = lito::llvm_sdk_runtime_recipe_name(component.recipe);
    if (component_recipe != recipe.id.as_str() || component.version != recipe.version.as_str()) {
        return sdk_failure<InstalledRuntimeComponent>(
            rstd::format("runtime component '{}@{}' selected recipe '{}', expected '{}@{}'",
                         component.name.as_str(),
                         component.version.as_str(),
                         component_recipe,
                         recipe.id.as_str(),
                         recipe.version.as_str()));
    }
    auto components_root = PathBuf::from(sdk_root).join(PathBuf::from(".components"_str).as_path());
    auto component_root  = components_root.join(PathBuf::from(component.name.as_str()).as_path());
    auto recipe_root     = component_root.join(PathBuf::from("recipe"_str).as_path());
    auto created         = rstd::fs::create_dir_all(component_root.as_path());
    if (created.is_err()) {
        return sdk_io_failure<InstalledRuntimeComponent>("create SDK component work directory"_str,
                                                         component_root.as_path(),
                                                         rstd::move(created).unwrap_err());
    }
    auto materialized = lito::source::materialize_source_tree(recipe.tree, recipe_root.as_path());
    if (materialized.is_err()) {
        return Err(lito::SdkError::SourceTree(rstd::move(materialized).unwrap_err()));
    }
    auto profile = lito::manifest::parse_build_profile(recipe.profile.as_str());
    if (profile.is_err()) {
        return sdk_failure<InstalledRuntimeComponent>(
            rstd::format("embedded SDK recipe '{}' has invalid profile '{}': {}",
                         recipe.id.as_str(),
                         recipe.profile.as_str(),
                         rstd::move(profile).unwrap_err()));
    }
    auto build_observer = SdkComponentBuildObserver {
        .version   = sdk_version,
        .component = component.name.as_str(),
        .sink      = request.observer,
    };
    auto packages = Vec<String>::make();
    packages.push(recipe.package.clone());
    emit_sdk_event(request.observer,
                   lito::SdkEventKind::Build,
                   sdk_version,
                   component.name.as_str(),
                   component_root.as_path());
    auto build = lito::build_with_environment(
        lito::BuildRequest {
            .selection =
                lito::package::PackageSelection {
                    .root     = recipe_root.clone(),
                    .packages = rstd::move(packages),
                },
            .output = component_root.join(PathBuf::from("build"_str).as_path()),
            .tools  = request.tools.clone(),
            .configuration =
                lito::cpp::BuildConfiguration {
                    .toolchain         = bootstrap_toolchain.clone(),
                    .standard_library  = lito::config::StandardLibrary::Libcxx,
                    .bmi_mode          = lito::cpp::BmiMode::Reduced,
                    .language_standard = String::make("c++20"_str),
                },
            .profile  = Some(rstd::move(profile).unwrap()),
            .locked   = true,
            .observer = Some(lito::BuildEventSink {
                .context = rstd::addressof(build_observer),
                .notify  = observe_component_build,
            }),
        },
        environment);
    if (build.is_err()) return Err(lito::SdkError::Build(rstd::move(build).unwrap_err()));

    const lito::BuiltArtifact* archive = nullptr;
    for (const auto& artifact : build->artifacts) {
        if (artifact.kind != lito::cpp::ArtifactKind::StaticLibrary ||
            artifact.target.package != recipe.package.as_str() ||
            artifact.target.kind != lito::package::PackageTargetKind::Library ||
            artifact.target.name != recipe.target.as_str()) {
            continue;
        }
        if (archive != nullptr) {
            return sdk_failure<InstalledRuntimeComponent>(
                rstd::format("embedded SDK recipe '{}' produced duplicate static target '{}'",
                             recipe.id.as_str(),
                             recipe.target.as_str()));
        }
        archive = rstd::addressof(artifact);
    }
    if (archive == nullptr) {
        return sdk_failure<InstalledRuntimeComponent>(
            rstd::format("embedded SDK recipe '{}' did not produce static target '{}'",
                         recipe.id.as_str(),
                         recipe.target.as_str()));
    }
    const lito::ExternalSourceProvenance* source = nullptr;
    for (const auto& candidate : build->external_source_provenance) {
        if (candidate.package != recipe.package.as_str() ||
            candidate.name != recipe.external_source.as_str()) {
            continue;
        }
        if (source != nullptr) {
            return sdk_failure<InstalledRuntimeComponent>(
                rstd::format("embedded SDK recipe '{}' resolved duplicate external source '{}'",
                             recipe.id.as_str(),
                             recipe.external_source.as_str()));
        }
        source = rstd::addressof(candidate);
    }
    if (source == nullptr) {
        return sdk_failure<InstalledRuntimeComponent>(
            rstd::format("embedded SDK recipe '{}' did not resolve external source '{}'",
                         recipe.id.as_str(),
                         recipe.external_source.as_str()));
    }
    auto version_script = rstd_try(checked_source_file(source->materialized_root.as_path(),
                                                       recipe.version_script.as_path(),
                                                       "resolve libxml2 version script"_str));
    auto license        = rstd_try(checked_source_file(source->materialized_root.as_path(),
                                                       recipe.license.as_path(),
                                                       "resolve libxml2 license"_str));
    auto toolchain      = lito::ClangToolchain::create(bootstrap_toolchain, resolver, environment);
    if (toolchain.is_err()) {
        return Err(lito::SdkError::Toolchain(rstd::move(toolchain).unwrap_err()));
    }
    if (toolchain->ld_path().starts_with(sdk_root)) {
        return sdk_failure<InstalledRuntimeComponent>(
            "bootstrap linker must not come from the LLVM SDK being installed"_str);
    }
    auto archiver_version = rstd_try(
        sdk_tool_version(toolchain->ar_path(), "bootstrap archiver --version"_str, environment));
    auto strip_tool = resolver.resolve(lito::system::Tool::Strip);
    if (strip_tool.is_err()) {
        return Err(lito::SdkError::System(rstd::move(strip_tool).unwrap_err()));
    }
    auto strip_version = rstd_try(sdk_tool_version(
        strip_tool->executable.as_path(), "bootstrap strip --version"_str, environment));
    auto linked_file =
        component_root.join(PathBuf::from(component.file.as_path().file_name().unwrap()).as_path());
    emit_sdk_event(request.observer,
                   lito::SdkEventKind::Link,
                   sdk_version,
                   component.name.as_str(),
                   linked_file.as_path());
    auto linked = toolchain->link_elf_shared_library(lito::ElfSharedLibraryLinkRequest {
        .output = linked_file.clone(),
        .archive =
            lito::LinkArchive {
                .path = archive->path.clone(),
                .mode = lito::LinkArchiveMode::Whole,
            },
        .soname            = component.soname.clone(),
        .version_script    = rstd::move(version_script),
        .working_directory = component_root.clone(),
    });
    if (linked.is_err()) return Err(lito::SdkError::Toolchain(rstd::move(linked).unwrap_err()));
    auto link_identity = rstd::crypto::sha256_hex(linked->link_identity.as_str());
    auto stripped      = toolchain->strip_artifact(linked_file.as_path(),
                                                   strip_tool->executable.as_path(),
                                                   lito::artifact::StripMode::Symbols,
                                                   component_root.as_path());
    if (stripped.is_err()) {
        return Err(lito::SdkError::Toolchain(rstd::move(stripped).unwrap_err()));
    }

    auto installed_file   = PathBuf::from(sdk_root).join(component.file.as_path());
    auto installed_parent = installed_file.as_path().parent().unwrap();
    created               = rstd::fs::create_dir_all(installed_parent);
    if (created.is_err()) {
        return sdk_io_failure<InstalledRuntimeComponent>(
            "create SDK runtime directory"_str, installed_parent, rstd::move(created).unwrap_err());
    }
    auto copied = rstd::fs::copy(linked_file.as_path(), installed_file.as_path());
    if (copied.is_err()) {
        return sdk_io_failure<InstalledRuntimeComponent>(
            "install SDK runtime"_str, installed_file.as_path(), rstd::move(copied).unwrap_err());
    }
    auto target_name = installed_file.as_path().file_name().unwrap();
    for (const auto& link : component.links) {
        auto link_path = PathBuf::from(sdk_root).join(link.as_path());
        auto linked =
            rstd::fs::soft_link(PathBuf::from(target_name).as_path(), link_path.as_path());
        if (linked.is_err()) {
            return sdk_io_failure<InstalledRuntimeComponent>("install SDK runtime link"_str,
                                                             link_path.as_path(),
                                                             rstd::move(linked).unwrap_err());
        }
    }
    auto installed_license = PathBuf::from(sdk_root).join(component.license.as_path());
    auto license_parent    = installed_license.as_path().parent().unwrap();
    created                = rstd::fs::create_dir_all(license_parent);
    if (created.is_err()) {
        return sdk_io_failure<InstalledRuntimeComponent>(
            "create SDK license directory"_str, license_parent, rstd::move(created).unwrap_err());
    }
    copied = rstd::fs::copy(license.as_path(), installed_license.as_path());
    if (copied.is_err()) {
        return sdk_io_failure<InstalledRuntimeComponent>("install SDK license"_str,
                                                         installed_license.as_path(),
                                                         rstd::move(copied).unwrap_err());
    }
    emit_sdk_event(request.observer,
                   lito::SdkEventKind::Install,
                   sdk_version,
                   component.name.as_str(),
                   installed_file.as_path());
    auto links = Vec<InstalledLinkRecord>::with_capacity(component.links.len());
    for (const auto& link : component.links) {
        links.push(InstalledLinkRecord {
            .path   = link.clone(),
            .target = PathBuf::from(target_name),
        });
    }
    auto linker_version = toolchain->linker_identity().version.as_str();
    auto newline        = linker_version.find("\n"_str);
    if (newline.is_some()) linker_version = linker_version.split_at(*newline).get<0>();
    return Ok(InstalledRuntimeComponent {
        .name             = component.name.clone(),
        .version          = component.version.clone(),
        .recipe           = recipe.id.clone(),
        .recipe_digest    = recipe.digest.clone(),
        .source_identity  = source->stable_source_identity.clone(),
        .runtime          = rstd_try(installed_file_record(sdk_root, component.file.as_path())),
        .links            = rstd::move(links),
        .license          = rstd_try(installed_file_record(sdk_root, component.license.as_path())),
        .compiler_version = build->compiler.c_version.clone(),
        .compiler_target  = build->compiler.target.clone(),
        .linker_family =
            String::make(lito::linker_family_name(toolchain->linker_identity().family)),
        .linker_version   = String::make(linker_version),
        .archiver_version = rstd::move(archiver_version),
        .strip_version    = rstd::move(strip_version),
        .link_identity    = rstd::move(link_identity),
    });
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
                ! descriptor_matches(**descriptor, *catalog, release, **artifact)) {
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
        *environment, request.tools.clone(), rstd::move(request.tool_reporter));
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
        if (! descriptor_matches(*descriptor, *catalog, **release, **artifact)) {
            return sdk_failure<SdkInstallSummary>(rstd::format(
                "LLVM SDK destination '{}' conflicts with the catalog artifact", prefix.as_path()));
        }
        rstd_try(validate_installed_components(prefix.as_path(), descriptor->components));
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
        .sink    = request.observer,
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
    auto recipe = libxml2_recipe();
    if (recipe.is_err()) {
        remove_staging(staging.as_path());
        return Err(rstd::move(recipe).unwrap_err());
    }
    auto bootstrap_toolchain =
        lito::config::apply_toolchain_override(request.toolchain.clone(),
                                               lito::config::ToolchainOverride {
                                                   .ld = Some(PathBuf::from("/usr/bin/ld"_str)),
                                               });
    auto installed_components = Vec<InstalledRuntimeComponent>::make();
    for (const auto& reference : (**artifact).runtime_components) {
        auto component = lito::find_llvm_sdk_runtime_component(*catalog, reference.as_str());
        if (component.is_none()) {
            remove_staging(staging.as_path());
            return sdk_failure<SdkInstallSummary>(rstd::format(
                "LLVM SDK artifact references unknown runtime component '{}'", reference));
        }
        auto installed = install_runtime_component(**component,
                                                   *recipe,
                                                   request.version.as_str(),
                                                   extracted->root.as_path(),
                                                   request,
                                                   bootstrap_toolchain,
                                                   resolver,
                                                   *environment);
        if (installed.is_err()) {
            remove_staging(staging.as_path());
            return Err(rstd::move(installed).unwrap_err());
        }
        installed_components.push(rstd::move(installed).unwrap());
    }
    auto components_root = extracted->root.join(PathBuf::from(".components"_str).as_path());
    auto removed         = rstd::fs::remove_dir_all(components_root.as_path());
    if (removed.is_err()) {
        auto error = sdk_io_failure<SdkInstallSummary>("remove SDK component workspace"_str,
                                                       components_root.as_path(),
                                                       rstd::move(removed).unwrap_err());
        remove_staging(staging.as_path());
        return error;
    }
    emit_sdk_event(request.observer,
                   SdkEventKind::Certify,
                   request.version.as_str(),
                   "toolchain"_str,
                   extracted->root.as_path());
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
        .components    = rstd::move(installed_components),
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
