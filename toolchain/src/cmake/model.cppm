module;
#include <rstd/macro.hpp>

export module lito.toolchain.cmake:model;

import rstd;
import rstd.json;
import lito.core;
import lito.toolchain.common;
import lito.system;
export import :dependency;
import lito.cpp;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;

export namespace lito
{

template<typename T>
auto cmake_failure(String message) -> lito::dependency::DependencyResult<T> {
    return Err(lito::dependency::DependencyError::Message(rstd::move(message)));
}

template<typename T>
auto cmake_failure(ref<str> message) -> lito::dependency::DependencyResult<T> {
    return Err(lito::dependency::DependencyError::Message(String::make(message)));
}

template<typename T>
auto cmake_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> lito::dependency::DependencyResult<T> {
    return Err(lito::dependency::DependencyError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto emit_cmake(const Option<ToolchainEventSink>& observer,
                ToolchainEventKind                kind,
                ref<str>                          target,
                ref<rstd::path::Path>             path,
                rstd::time::Duration              elapsed   = {},
                bool                              completed = false) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context, ToolchainEvent { kind, target, path, elapsed, completed });
}

template<typename F>
auto execute_observed(const Option<ToolchainEventSink>& observer,
                      ToolchainEventKind                kind,
                      ref<str>                          target,
                      ref<rstd::path::Path>             path,
                      F&&                               operation) -> decltype(operation()) {
    emit_cmake(observer, kind, target, path);
    auto started = rstd::time::Instant::now();
    auto result  = operation();
    emit_cmake(observer, kind, target, path, started.elapsed(), true);
    return result;
}

auto path_text(ref<rstd::path::Path> path, ref<str> context)
    -> lito::dependency::DependencyResult<String> {
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

auto cmake_build_type(const cpp::ProfileSpec& profile) -> ref<str> {
    switch (profile.family) {
    case lito::manifest::BuildProfileFamily::Debug: return "Debug"_str;
    case lito::manifest::BuildProfileFamily::Release: return "Release"_str;
    case lito::manifest::BuildProfileFamily::Plain: return "None"_str;
    }
    return "None"_str;
}

struct CMakeProfileConfiguration {
    String build_type;
    String c_flags;
    String cxx_flags;
    String linker_flags;
    String msvc_runtime;
    bool   neutral_configuration {};
};

auto cmake_profile_configuration(const cpp::ProfileSpec& profile) -> CMakeProfileConfiguration {
    const auto append_flag = [](String& output, ref<str> value) {
        if (value.is_empty()) return;
        if (! output.is_empty()) output.push_ascii(' ');
        output.push_str(value);
    };
    const auto append_ndebug = [&](String& output, const Option<bool>& ndebug) {
        if (ndebug.is_none()) return;
        append_flag(output, *ndebug ? "-DNDEBUG"_str : "-UNDEBUG"_str);
    };

    auto c_flags = String::make();
    append_flag(c_flags, cpp::cpp_optimization_option(profile.c.common.codegen.optimization));
    append_flag(c_flags, cpp::cpp_debug_option(profile.c.common.codegen.debug_info));
    append_flag(c_flags, cpp::cpp_lto_option(profile.c.common.codegen.lto));
    if (profile.c.common.microsoft_runtime_library.is_some()) {
        append_flag(c_flags,
                    rstd::format("-fms-runtime-lib={}",
                                 lito::compiler::microsoft_runtime_library_name(
                                     *profile.c.common.microsoft_runtime_library))
                        .as_str());
    }
    append_ndebug(c_flags, profile.c_ndebug);

    auto cxx_flags = String::make();
    switch (profile.cpp.abi.standard_library) {
    case lito::config::StandardLibrary::Libcxx: append_flag(cxx_flags, "-stdlib=libc++"_str); break;
    case lito::config::StandardLibrary::Libstdcxx:
        append_flag(cxx_flags, "-stdlib=libstdc++"_str);
        break;
    case lito::config::StandardLibrary::Msvc: break;
    }
    append_flag(cxx_flags,
                profile.cpp.language.exceptions ? "-fexceptions"_str : "-fno-exceptions"_str);
    append_flag(cxx_flags, profile.cpp.language.rtti ? "-frtti"_str : "-fno-rtti"_str);
    append_flag(cxx_flags, cpp::cpp_optimization_option(profile.cpp.common.codegen.optimization));
    append_flag(cxx_flags, cpp::cpp_debug_option(profile.cpp.common.codegen.debug_info));
    append_flag(cxx_flags, cpp::cpp_lto_option(profile.cpp.common.codegen.lto));
    if (profile.cpp.common.microsoft_runtime_library.is_some()) {
        append_flag(cxx_flags,
                    rstd::format("-fms-runtime-lib={}",
                                 lito::compiler::microsoft_runtime_library_name(
                                     *profile.cpp.common.microsoft_runtime_library))
                        .as_str());
    }
    append_ndebug(cxx_flags, profile.cpp_ndebug);

    auto linker_flags = String::make();
    append_flag(linker_flags, cpp::cpp_lto_option(profile.link_lto));
    if (profile.linker_strip.is_some()) {
        append_flag(linker_flags,
                    *profile.linker_strip == lito::artifact::StripMode::Symbols
                        ? "-s"_str
                        : "-Wl,--strip-debug"_str);
    }

    auto msvc_runtime = String::make();
    if (profile.cpp.common.microsoft_runtime_library.is_some()) {
        switch (*profile.cpp.common.microsoft_runtime_library) {
        case lito::compiler::MicrosoftRuntimeLibrary::Dynamic:
            msvc_runtime = String::make("MultiThreadedDLL"_str);
            break;
        case lito::compiler::MicrosoftRuntimeLibrary::DynamicDebug:
            msvc_runtime = String::make("MultiThreadedDebugDLL"_str);
            break;
        case lito::compiler::MicrosoftRuntimeLibrary::Static:
        case lito::compiler::MicrosoftRuntimeLibrary::StaticDebug: break;
        }
    }

    return CMakeProfileConfiguration {
        .build_type            = String::make(cmake_build_type(profile)),
        .c_flags               = rstd::move(c_flags),
        .cxx_flags             = rstd::move(cxx_flags),
        .linker_flags          = rstd::move(linker_flags),
        .msvc_runtime          = rstd::move(msvc_runtime),
        .neutral_configuration = profile.family == lito::manifest::BuildProfileFamily::Plain,
    };
}

auto append_search_path_identity(String&                                      output,
                                 const lito::dependency::CMakeProviderConfig& provider)
    -> lito::dependency::DependencyResult<empty> {
    for (const auto& path : provider.search_paths) {
        auto value = path_text(path.as_path(), "CMake search path"_str);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        append_identity(output, value->as_str());
    }
    return Ok(empty {});
}

auto identity_hash(ref<str> value) -> String {
    auto hash = uint64_t(14695981039346656037ull);
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= uint64_t(1099511628211ull);
    }
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto source_identity(const ResolvedCMakeDependencyRequirement& requirement) -> String {
    if (requirement.source.is_Directory()) {
        return requirement.source.as_Directory().identity.clone();
    }
    return String::make("find"_str);
}

auto cmake_quoted(ref<str> value, ref<str> context) -> lito::dependency::DependencyResult<String> {
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
    PathBuf source;
    PathBuf build;
    PathBuf install;
    PathBuf query_root;
    PathBuf query_source;
    PathBuf query_build;
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
    ResolvedCMakeDependencyRequirement    requirement;
    lito::dependency::CMakeProviderConfig provider;
    cpp::BuildConfiguration               configuration;
    cpp::ProfileSpec                      profile;
    LinkerIdentity                        linker;
    CMakeWorkArea                         area;
    String                                effective_target;
    Vec<CMakePackageOperation>            operations;
    usize                                 jobs { usize(1) };
};

template<typename T>
auto with_operation_context(lito::dependency::DependencyResult<T> result,
                            const CMakePackagePlan&               plan,
                            CMakePackageOperation                 operation)
    -> lito::dependency::DependencyResult<T> {
    if (result.is_ok()) return result;
    auto error = rstd::move(result).unwrap_err();
    return Err(lito::dependency::DependencyError::CMakeOperation(
        plan.requirement.alias.clone(),
        rstd::format("{}", operation),
        plan.area.root.clone(),
        Box<lito::dependency::DependencyError>::make(rstd::move(error))));
}

auto clone_cmake_source(const ResolvedCMakeDependencySource& source)
    -> ResolvedCMakeDependencySource {
    if (source.is_Directory()) {
        return ResolvedCMakeDependencySource::Directory(source.as_Directory().root.clone(),
                                                        source.as_Directory().identity.clone(),
                                                        source.as_Directory().cacheable);
    }
    return ResolvedCMakeDependencySource::Find();
}

auto clone_cmake_requirement(const ResolvedCMakeDependencyRequirement& requirement)
    -> ResolvedCMakeDependencyRequirement {
    auto cache = Vec<lito::dependency::CMakeCacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(lito::dependency::CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets =
        Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(lito::dependency::CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto result = ResolvedCMakeDependencyRequirement {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .source           = clone_cmake_source(requirement.source),
        .adapter_identity = requirement.adapter_identity.clone(),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
    };
    if (requirement.adapter.is_some()) result.adapter = Some(requirement.adapter->clone());
    if (requirement.config_directory.is_some()) {
        result.config_directory = Some(requirement.config_directory->clone());
    }
    return result;
}

auto clone_profile(const cpp::ProfileSpec& profile) -> cpp::ProfileSpec {
    return cpp::ProfileSpec {
        .name                  = profile.name.clone(),
        .family                = profile.family,
        .bmi                   = profile.bmi,
        .c                     = profile.c.clone(),
        .cpp                   = profile.cpp.clone(),
        .c_link_requirements   = profile.c_link_requirements.clone(),
        .cpp_link_requirements = profile.cpp_link_requirements.clone(),
        .strip                 = profile.strip,
        .c_ndebug              = profile.c_ndebug,
        .cpp_ndebug            = profile.cpp_ndebug,
        .link_lto              = profile.link_lto,
        .linker_strip          = profile.linker_strip,
        .c_sources             = profile.c_sources.clone(),
        .cpp_sources           = profile.cpp_sources.clone(),
        .cpp_language_sources  = profile.cpp_language_sources.clone(),
        .strip_source          = profile.strip_source.clone(),
        .link_lto_source       = profile.link_lto_source.clone(),
        .linker_strip_source   = profile.linker_strip_source.clone(),
        .linker_options        = as<Clone>(profile.linker_options).clone(),
    };
}

auto work_area(const ResolvedCMakeDependencyRequirement&    requirement,
               const lito::dependency::CMakeProviderConfig& provider,
               const cpp::BuildConfiguration&               build,
               const cpp::ProfileSpec&                      profile,
               const LinkerIdentity&                        linker,
               ref<str>                                     effective_target,
               ref<rstd::path::Path>                        profile_cmake_root)
    -> lito::dependency::DependencyResult<CMakeWorkArea> {
    auto recipe = String::make("lito-cmake-install-v5\n"_str);
    append_identity(recipe, source_identity(requirement).as_str());
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto compiler = path_text(build.toolchain.cxx.as_path(), "C++ compiler"_str);
    if (compiler.is_err()) return Err(rstd::move(compiler).unwrap_err());
    auto c_compiler = path_text(build.toolchain.cc.as_path(), "C compiler"_str);
    if (c_compiler.is_err()) return Err(rstd::move(c_compiler).unwrap_err());
    auto archiver = path_text(build.toolchain.ar.as_path(), "archiver"_str);
    if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
    append_identity(recipe, executable->as_str());
    append_identity(recipe, provider.identity.as_str());
    append_identity(recipe, provider.generator.as_str());
    rstd_try(append_search_path_identity(recipe, provider));
    append_identity(recipe, compiler->as_str());
    append_identity(recipe, c_compiler->as_str());
    append_identity(recipe, linker.build_identity.as_str());
    append_identity(recipe, archiver->as_str());
    append_identity(recipe, effective_target);
    append_identity(recipe, cmake_build_type(profile));
    append_identity(recipe, profile.cpp.language.standard.as_str());
    append_identity(recipe, lito::config::standard_library_name(profile.cpp.abi.standard_library));
    append_identity(recipe, "stdlib-runtime:dynamic"_str);
    append_identity(recipe,
                    profile.cpp.common.microsoft_runtime_library.is_some()
                        ? lito::compiler::microsoft_runtime_library_name(
                              *profile.cpp.common.microsoft_runtime_library)
                        : "ms-runtime-lib:default"_str);
    append_identity(recipe,
                    profile.cpp.language.exceptions ? "exceptions"_str : "no-exceptions"_str);
    append_identity(recipe, profile.cpp.language.rtti ? "rtti"_str : "no-rtti"_str);
    append_identity(recipe, cpp::cpp_optimization_option(profile.cpp.common.codegen.optimization));
    append_identity(recipe, cpp::cpp_debug_option(profile.cpp.common.codegen.debug_info));
    append_identity(recipe, cpp::cpp_lto_option(profile.cpp.common.codegen.lto));
    append_identity(recipe, cpp::cpp_optimization_option(profile.c.common.codegen.optimization));
    append_identity(recipe, cpp::cpp_debug_option(profile.c.common.codegen.debug_info));
    append_identity(recipe, cpp::cpp_lto_option(profile.c.common.codegen.lto));
    append_identity(recipe,
                    profile.c_ndebug.is_none()
                        ? "c-ndebug:unspecified"_str
                        : (*profile.c_ndebug ? "c-ndebug:on"_str : "c-ndebug:off"_str));
    append_identity(recipe,
                    profile.cpp_ndebug.is_none()
                        ? "cpp-ndebug:unspecified"_str
                        : (*profile.cpp_ndebug ? "cpp-ndebug:on"_str : "cpp-ndebug:off"_str));
    append_identity(recipe, cpp::cpp_lto_option(profile.link_lto));
    append_identity(recipe,
                    profile.linker_strip.is_none()
                        ? "link-strip:unspecified"_str
                        : (*profile.linker_strip == lito::artifact::StripMode::Symbols
                               ? "link-strip:symbols"_str
                               : "link-strip:debuginfo"_str));
    for (const auto& entry : requirement.cache) {
        append_identity(recipe, entry.name.as_str());
        append_identity(recipe, entry.value.as_str());
    }
    auto root         = PathBuf::from(profile_cmake_root)
                            .join(PathBuf::from(identity_hash(recipe.as_str())).as_path());
    auto query_recipe = String::make("lito-cmake-query-v5\n"_str);
    append_identity(query_recipe, requirement.alias.as_str());
    append_identity(query_recipe, requirement.package.as_str());
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
    append_identity(query_recipe, effective_target);
    auto query_root = root.join(PathBuf::from("queries"_str).as_path())
                          .join(PathBuf::from(identity_hash(query_recipe.as_str())).as_path());
    return Ok(CMakeWorkArea {
        .root   = root.clone(),
        .source = requirement.source.is_Directory() ? requirement.source.as_Directory().root.clone()
                                                    : PathBuf::make(),
        .build  = root.join(PathBuf::from("build"_str).as_path()),
        .install      = root.join(PathBuf::from("install"_str).as_path()),
        .query_root   = query_root.clone(),
        .query_source = query_root.join(PathBuf::from("source"_str).as_path()),
        .query_build  = query_root.join(PathBuf::from("build"_str).as_path()),
    });
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::CMakePackageOperation> : ImplBase<lito::CMakePackageOperation> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::CMakePackageOperation::ConfigureSource: name = "source configure"_str; break;
        case lito::CMakePackageOperation::BuildSource: name = "source build"_str; break;
        case lito::CMakePackageOperation::InstallSource: name = "source install"_str; break;
        case lito::CMakePackageOperation::WriteQuery: name = "query materialization"_str; break;
        case lito::CMakePackageOperation::ConfigureQuery: name = "query configure"_str; break;
        case lito::CMakePackageOperation::BuildQuery: name = "query build"_str; break;
        case lito::CMakePackageOperation::ReadUsage: name = "usage snapshot"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
