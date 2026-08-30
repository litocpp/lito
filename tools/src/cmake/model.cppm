module;
#include <rstd/macro.hpp>

export module lito.tools.cmake:model;

import rstd;
import rstd.json;
import licrypto;
import lito.tools;
import lito.system;
export import :request;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;

export namespace lito::tools::cmake
{

template<typename T>
auto cmake_failure(String message) -> lito::tools::ToolResult<T> {
    return Err(lito::tools::ToolError::Message(rstd::move(message)));
}

template<typename T>
auto cmake_failure(ref<str> message) -> lito::tools::ToolResult<T> {
    return Err(lito::tools::ToolError::Message(String::make(message)));
}

template<typename T>
auto cmake_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> lito::tools::ToolResult<T> {
    return Err(lito::tools::ToolError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto emit_cmake(const Option<EventSink>& observer,
                EventKind                kind,
                ref<str>                 target,
                ref<rstd::path::Path>    path,
                rstd::time::Duration     elapsed   = {},
                bool                     completed = false) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context, Event { kind, target, path, elapsed, completed });
}

template<typename F>
auto execute_observed(const Option<EventSink>& observer,
                      EventKind                kind,
                      ref<str>                 target,
                      ref<rstd::path::Path>    path,
                      F&&                      operation) -> decltype(operation()) {
    emit_cmake(observer, kind, target, path);
    auto started = rstd::time::Instant::now();
    auto result  = operation();
    emit_cmake(observer, kind, target, path, started.elapsed(), true);
    return result;
}

auto path_text(ref<rstd::path::Path> path, ref<str> context) -> lito::tools::ToolResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return cmake_failure<String>(
            rstd::format("{} path '{}' is not valid UTF-8", context, path));
    }
    if (text->contains(";"_str)) {
        return cmake_failure<String>(rstd::format("{} path '{}' contains ';'", context, path));
    }
    return Ok(String::make(*text));
}

auto append_identity(String& output, ref<str> value) -> void {
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto append_search_path_identity(String& output, const Provider& provider)
    -> lito::tools::ToolResult<empty> {
    for (const auto& path : provider.search_paths) {
        auto value = path_text(path.as_path(), "CMake search path"_str);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        append_identity(output, value->as_str());
    }
    return Ok(empty {});
}

auto source_identity(const Request& requirement) -> String {
    if (requirement.source.is_Directory()) {
        return requirement.source.as_Directory().identity.clone();
    }
    return String::make("find"_str);
}

auto cmake_quoted(ref<str> value, ref<str> context) -> lito::tools::ToolResult<String> {
    if (value.contains("\""_str) || value.contains(";"_str) || value.contains("\n"_str) ||
        value.contains("\r"_str)
#if ! defined(_WIN32)
        || value.contains("\\"_str)
#endif
    ) {
        return cmake_failure<String>(rstd::format("{} contains CMake syntax", context));
    }
    auto quoted = String::make("\""_str);
    for (auto byte : value.as_bytes()) {
#if defined(_WIN32)
        quoted.push_ascii(byte == u8('\\') ? u8('/') : byte);
#else
        quoted.push_ascii(byte);
#endif
    }
    quoted.push_ascii(u8('\"'));
    return Ok(rstd::move(quoted));
}

struct CMakeWorkArea {
    PathBuf root;
    PathBuf lock;
    PathBuf source;
    PathBuf build;
    PathBuf install;
    PathBuf state;
    PathBuf query_root;
    PathBuf query_source;
    PathBuf query_build;
    String  source_identity;
    String  preparation_identity;
    String  query_identity;
};

enum class CMakePackageOperation
{
    ConfigureSource,
    BuildSource,
    InstallSource,
    WriteQuery,
    ConfigureQuery,
    BuildQuery,
    ReadUsage,
};

struct CMakePackagePlan {
    Request                    requirement;
    Provider                   provider;
    ToolchainConfiguration     toolchain;
    ProfileConfiguration       profile;
    CMakeWorkArea              area;
    String                     effective_target;
    Vec<CMakePackageOperation> operations;
    usize                      jobs { usize(1) };
};

template<typename T>
auto with_operation_context(lito::tools::ToolResult<T> result,
                            const CMakePackagePlan&    plan,
                            CMakePackageOperation      operation) -> lito::tools::ToolResult<T> {
    if (result.is_ok()) return result;
    auto error = rstd::move(result).unwrap_err();
    return Err(
        lito::tools::ToolError::Context(rstd::format("CMake dependency '{}' {} failed in '{}'",
                                                     plan.requirement.alias.as_str(),
                                                     operation,
                                                     plan.area.root.as_path()),
                                        Box<lito::tools::ToolError>::make(rstd::move(error))));
}

auto clone_cmake_source(const Source& source) -> Source {
    if (source.is_Directory()) {
        return Source::Directory(source.as_Directory().root.clone(),
                                 source.as_Directory().identity.clone(),
                                 source.as_Directory().cacheable);
    }
    return Source::Find();
}

auto clone_cmake_requirement(const Request& requirement) -> Request {
    auto cache = Vec<CacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(CacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets = Vec<TargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(TargetRequirement { .name = target.name.clone() });
    }
    auto host_tools = Vec<HostToolRequirement>::with_capacity(requirement.host_tools.len());
    for (const auto& tool : requirement.host_tools) {
        host_tools.push(HostToolRequirement {
            .name   = tool.name.clone(),
            .target = tool.target.clone(),
        });
    }
    auto result = Request {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .components       = as<Clone>(requirement.components).clone(),
        .source           = clone_cmake_source(requirement.source),
        .adapter_identity = requirement.adapter_identity.clone(),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
        .host_tools       = rstd::move(host_tools),
    };
    if (requirement.adapter.is_some()) result.adapter = Some(requirement.adapter->clone());
    if (requirement.config_directory.is_some()) {
        result.config_directory = Some(requirement.config_directory->clone());
    }
    if (requirement.find_install_prefix.is_some()) {
        result.find_install_prefix = Some(requirement.find_install_prefix->clone());
    }
    return result;
}

auto clone_provider(const Provider& provider) -> Provider {
    return Provider {
        .executable   = provider.executable.clone(),
        .identity     = provider.identity.clone(),
        .generator    = provider.generator.clone(),
        .search_paths = as<Clone>(provider.search_paths).clone(),
    };
}

auto clone_toolchain(const ToolchainConfiguration& toolchain) -> ToolchainConfiguration {
    return ToolchainConfiguration {
        .cc              = toolchain.cc.clone(),
        .cxx             = toolchain.cxx.clone(),
        .linker          = toolchain.linker.clone(),
        .linker_identity = toolchain.linker_identity.clone(),
        .archiver        = toolchain.archiver.clone(),
        .target          = as<Clone>(toolchain.target).clone(),
    };
}

auto clone_profile(const ProfileConfiguration& profile) -> ProfileConfiguration {
    return ProfileConfiguration {
        .cxx_standard = profile.cxx_standard.clone(),
        .c_flags      = profile.c_flags.clone(),
        .cxx_flags    = profile.cxx_flags.clone(),
        .linker_flags = profile.linker_flags.clone(),
        .msvc_runtime = profile.msvc_runtime.clone(),
    };
}

auto cmake_package_path_component(ref<str> package) -> lito::tools::ToolResult<String> {
    if (! lito::dependency::cmake_package_name_is_valid(package)) {
        return cmake_failure<String>(
            rstd::format("CMake package '{}' cannot own a work directory", package));
    }
    auto result = String::make(package);
    auto lower  = result.clone();
    lower.as_mut_str().make_ascii_lowercase();
    constexpr ref<str> reserved[] = { "con"_str,  "prn"_str,  "aux"_str,  "nul"_str,  "com1"_str,
                                      "com2"_str, "com3"_str, "com4"_str, "com5"_str, "com6"_str,
                                      "com7"_str, "com8"_str, "com9"_str, "lpt1"_str, "lpt2"_str,
                                      "lpt3"_str, "lpt4"_str, "lpt5"_str, "lpt6"_str, "lpt7"_str,
                                      "lpt8"_str, "lpt9"_str };
    auto               unsafe     = package == "."_str || package == ".."_str;
    for (auto name : reserved) {
        unsafe = unsafe || lower.as_str() == name ||
                 lower.as_str().starts_with(rstd::format("{}.", name).as_str());
    }
    if (unsafe) {
        auto prefixed = String::make("cmake-"_str);
        prefixed.push_str(result.as_str());
        result = rstd::move(prefixed);
    }
    if (result.as_str().ends_with("."_str)) result.push_ascii('_');
    if (result.len() > usize(80)) {
        auto digest = licrypto::sha256_hex(package);
        digest.truncate(usize(16));
        result.truncate(usize(63));
        result.push_ascii('-');
        result.push_str(digest.as_str());
    }
    return Ok(rstd::move(result));
}

auto work_area(const Request&                requirement,
               const Provider&               provider,
               const ToolchainConfiguration& toolchain,
               const ProfileConfiguration&   profile,
               ref<str>                      effective_target,
               ref<rstd::path::Path> profile_cmake_root) -> lito::tools::ToolResult<CMakeWorkArea> {
    auto source_id = source_identity(requirement);
    auto recipe    = String::make("lito-cmake-preparation-v7\n"_str);
    append_identity(recipe, requirement.package.as_str());
    append_identity(recipe, source_id.as_str());
    append_identity(recipe,
                    requirement.source.is_Directory() && requirement.source.as_Directory().cacheable
                        ? "immutable-source"_str
                        : "mutable-or-installed-source"_str);
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto compiler = path_text(toolchain.cxx.as_path(), "C++ compiler"_str);
    if (compiler.is_err()) return Err(rstd::move(compiler).unwrap_err());
    auto c_compiler = path_text(toolchain.cc.as_path(), "C compiler"_str);
    if (c_compiler.is_err()) return Err(rstd::move(c_compiler).unwrap_err());
    auto archiver = path_text(toolchain.archiver.as_path(), "archiver"_str);
    if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
    append_identity(recipe, executable->as_str());
    append_identity(recipe, provider.identity.as_str());
    append_identity(recipe, provider.generator.as_str());
    rstd_try(append_search_path_identity(recipe, provider));
    append_identity(recipe, compiler->as_str());
    append_identity(recipe, c_compiler->as_str());
    append_identity(recipe, toolchain.linker_identity.as_str());
    append_identity(recipe, archiver->as_str());
    if (toolchain.target.is_some()) {
        auto file = path_text(toolchain.target->file.as_path(), "CMake toolchain file"_str);
        if (file.is_err()) return Err(rstd::move(file).unwrap_err());
        append_identity(recipe, file->as_str());
        append_identity(recipe, toolchain.target->identity.as_str());
        for (const auto& entry : toolchain.target->cache) {
            append_identity(recipe, entry.name.as_str());
            append_identity(recipe, entry.value.as_str());
        }
    } else {
        append_identity(recipe, "native-toolchain"_str);
    }
    append_identity(recipe, effective_target);
    append_identity(recipe, "lito-owned-none-configuration-v1"_str);
    append_identity(recipe, profile.cxx_standard.as_str());
    append_identity(recipe, profile.c_flags.as_str());
    append_identity(recipe, profile.cxx_flags.as_str());
    append_identity(recipe, profile.linker_flags.as_str());
    append_identity(recipe, profile.msvc_runtime.as_str());
    for (const auto& entry : requirement.cache) {
        append_identity(recipe, entry.name.as_str());
        append_identity(recipe, entry.value.as_str());
    }
    auto component = cmake_package_path_component(requirement.package.as_str());
    if (component.is_err()) return Err(rstd::move(component).unwrap_err());
    auto directory = rstd::move(component).unwrap();
    directory.push_ascii('-');
    if (requirement.source.is_Find()) {
        directory.push_str("installed"_str);
    } else {
        auto source_key = licrypto::sha256_hex(source_id.as_str());
        source_key.truncate(usize(20));
        directory.push_str(source_key.as_str());
    }
    auto root         = PathBuf::from(profile_cmake_root).join(PathBuf::from(directory).as_path());
    auto query_recipe = String::make("lito-cmake-query-v7\n"_str);
    append_identity(query_recipe, recipe.as_str());
    append_identity(query_recipe, requirement.package.as_str());
    for (const auto& component : requirement.components) {
        append_identity(query_recipe, component.as_str());
    }
    append_identity(query_recipe, requirement.source.is_Find() ? "find"_str : "source"_str);
    append_identity(query_recipe, requirement.adapter.is_some() ? "adapter"_str : "generic"_str);
    if (requirement.adapter.is_some()) {
        auto adapter_path = path_text(requirement.adapter->as_path(), "CMake adapter"_str);
        if (adapter_path.is_err()) return Err(rstd::move(adapter_path).unwrap_err());
        append_identity(query_recipe, adapter_path->as_str());
        append_identity(query_recipe, requirement.adapter_identity.as_str());
    }
    if (requirement.config_directory.is_some()) {
        auto directory =
            path_text(requirement.config_directory->as_path(), "CMake config directory"_str);
        if (directory.is_err()) return Err(rstd::move(directory).unwrap_err());
        append_identity(query_recipe, directory->as_str());
    } else {
        append_identity(query_recipe, "default-config-directory"_str);
    }
    for (const auto& target : requirement.targets) {
        append_identity(query_recipe, target.name.as_str());
    }
    for (const auto& tool : requirement.host_tools) {
        append_identity(query_recipe, tool.name.as_str());
        append_identity(query_recipe, tool.target.as_str());
    }
    if (requirement.find_install_prefix.is_some()) {
        auto prefix =
            path_text(requirement.find_install_prefix->as_path(), "CMake find install prefix"_str);
        if (prefix.is_err()) return Err(rstd::move(prefix).unwrap_err());
        append_identity(query_recipe, prefix->as_str());
    }
    append_identity(query_recipe, effective_target);
    auto query_root = root.join(PathBuf::from("query"_str).as_path());
    return Ok(CMakeWorkArea {
        .root   = root.clone(),
        .lock   = root.join(PathBuf::from("lock"_str).as_path()),
        .source = requirement.source.is_Directory() ? requirement.source.as_Directory().root.clone()
                                                    : PathBuf::make(),
        .build  = root.join(PathBuf::from("build"_str).as_path()),
        .install              = root.join(PathBuf::from("install"_str).as_path()),
        .state                = root.join(PathBuf::from("state.json"_str).as_path()),
        .query_root           = query_root.clone(),
        .query_source         = query_root.join(PathBuf::from("source"_str).as_path()),
        .query_build          = query_root.join(PathBuf::from("build"_str).as_path()),
        .source_identity      = rstd::move(source_id),
        .preparation_identity = rstd::move(recipe),
        .query_identity       = rstd::move(query_recipe),
    });
}

} // namespace lito::tools::cmake

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::tools::cmake::CMakePackageOperation>
    : ImplBase<lito::tools::cmake::CMakePackageOperation> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::tools::cmake::CMakePackageOperation::ConfigureSource:
            name = "source configure"_str;
            break;
        case lito::tools::cmake::CMakePackageOperation::BuildSource:
            name = "source build"_str;
            break;
        case lito::tools::cmake::CMakePackageOperation::InstallSource:
            name = "source install"_str;
            break;
        case lito::tools::cmake::CMakePackageOperation::WriteQuery:
            name = "query materialization"_str;
            break;
        case lito::tools::cmake::CMakePackageOperation::ConfigureQuery:
            name = "query configure"_str;
            break;
        case lito::tools::cmake::CMakePackageOperation::BuildQuery: name = "query build"_str; break;
        case lito::tools::cmake::CMakePackageOperation::ReadUsage:
            name = "usage snapshot"_str;
            break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
