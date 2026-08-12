module;
#include <rstd/macro.hpp>

export module lito.dependency:cmake;

import rstd;
import rstd.json;
import lito.model;
import lito.process;
import lito.environment;
import lito.storage;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;

namespace lito
{

template<typename T>
auto cmake_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto cmake_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto emit_cmake(const Option<BuildObserver>& observer,
                BuildEventKind               kind,
                ref<str>                     target,
                ref<rstd::path::Path>        path,
                rstd::time::Duration         elapsed   = {},
                bool                         completed = false) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context, BuildEvent { kind, target, path, elapsed, completed });
}

template<typename F>
auto execute_observed(const Option<BuildObserver>& observer,
                      BuildEventKind               kind,
                      ref<str>                     target,
                      ref<rstd::path::Path>        path,
                      F&&                          operation) -> decltype(operation()) {
    emit_cmake(observer, kind, target, path);
    auto started = rstd::time::Instant::now();
    auto result  = operation();
    emit_cmake(observer, kind, target, path, started.elapsed(), true);
    return result;
}

auto path_text(ref<rstd::path::Path> path, ref<str> context) -> Result<String> {
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

auto cmake_build_type(const ProfileSpec& profile) -> ref<str> {
    return profile.family == BuildProfileFamily::Debug ? "Debug"_str : "Release"_str;
}

auto append_search_path_identity(String& output, const CMakeProviderConfig& provider)
    -> Result<empty> {
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
    if (requirement.source.is_Archive()) {
        return rstd::format("archive+{}#sha256:{}",
                            requirement.source.as_Archive().url.as_str(),
                            requirement.source.as_Archive().sha256.as_str());
    }
    return String::make("installed"_str);
}

auto cmake_quoted(ref<str> value, ref<str> context) -> Result<String> {
    if (value.contains("\""_str) || value.contains("\\"_str) || value.contains(";"_str) ||
        value.contains("\n"_str) || value.contains("\r"_str)) {
        return cmake_failure<String>(rstd::format("{} contains CMake syntax", context));
    }
    return Ok(rstd::format("\"{}\"", value));
}

export struct CMakeWorkArea {
    PathBuf root;
    PathBuf source;
    PathBuf build;
    PathBuf install;
    PathBuf query_root;
    PathBuf query_source;
    PathBuf query_build;
};

export enum class CMakePackageOperation {
    ConfigureSource,
    BuildSource,
    InstallSource,
    WriteQuery,
    ConfigureQuery,
    BuildQuery,
    ReadUsage,
};

export struct CMakePackagePlan {
    ResolvedCMakeDependencyRequirement requirement;
    CMakeProviderConfig                provider;
    BuildConfiguration                 configuration;
    ProfileSpec                        profile;
    CMakeWorkArea                      area;
    String                             effective_target;
    Vec<CMakePackageOperation>         operations;
    usize                              jobs { usize(1) };
};

auto cmake_operation_name(CMakePackageOperation operation) noexcept -> ref<str> {
    switch (operation) {
    case CMakePackageOperation::ConfigureSource: return "source configure"_str;
    case CMakePackageOperation::BuildSource: return "source build"_str;
    case CMakePackageOperation::InstallSource: return "source install"_str;
    case CMakePackageOperation::WriteQuery: return "query materialization"_str;
    case CMakePackageOperation::ConfigureQuery: return "query configure"_str;
    case CMakePackageOperation::BuildQuery: return "query build"_str;
    case CMakePackageOperation::ReadUsage: return "usage snapshot"_str;
    }
    return "unknown"_str;
}

template<typename T>
auto with_operation_context(Result<T>               result,
                            const CMakePackagePlan& plan,
                            CMakePackageOperation   operation) -> Result<T> {
    if (result.is_ok()) return result;
    auto error = rstd::move(result).unwrap_err();
    return cmake_failure<T>(rstd::format("CMake dependency '{}' {} failed in '{}': {}",
                                         plan.requirement.alias.as_str(),
                                         cmake_operation_name(operation),
                                         plan.area.root.as_path(),
                                         error.message.as_str()));
}

auto clone_cmake_source(const ResolvedCMakeDependencySource& source)
    -> ResolvedCMakeDependencySource {
    if (source.is_Directory()) {
        return ResolvedCMakeDependencySource::Directory(source.as_Directory().root.clone(),
                                                        source.as_Directory().identity.clone(),
                                                        source.as_Directory().add_subdirectory,
                                                        source.as_Directory().cacheable);
    }
    if (source.is_Archive()) {
        return ResolvedCMakeDependencySource::Archive(source.as_Archive().url.clone(),
                                                      source.as_Archive().sha256.clone());
    }
    return ResolvedCMakeDependencySource::Installed();
}

auto clone_cmake_requirement(const ResolvedCMakeDependencyRequirement& requirement)
    -> ResolvedCMakeDependencyRequirement {
    auto cache = Vec<CMakeCacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets = Vec<CMakeTargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto result = ResolvedCMakeDependencyRequirement {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .source           = clone_cmake_source(requirement.source),
        .integration      = requirement.integration,
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

auto clone_profile(const ProfileSpec& profile) -> ProfileSpec {
    return ProfileSpec {
        .name           = profile.name.clone(),
        .family         = profile.family,
        .bmi            = profile.bmi,
        .cpp            = profile.cpp.clone(),
        .strip          = profile.strip,
        .linker_options = as<rstd::clone::Clone>(profile.linker_options).clone(),
    };
}

auto work_area(const ResolvedCMakeDependencyRequirement& requirement,
               const CMakeProviderConfig&                provider,
               const BuildConfiguration&                 build,
               const ProfileSpec&                        profile,
               ref<str> effective_target) -> Result<CMakeWorkArea> {
    auto recipe = String::make("lito-cmake-install-v4\n"_str);
    append_identity(recipe, source_identity(requirement).as_str());
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto compiler = path_text(build.toolchain.compiler.as_path(), "C++ compiler"_str);
    if (compiler.is_err()) return Err(rstd::move(compiler).unwrap_err());
    auto c_compiler = path_text(build.toolchain.c_compiler.as_path(), "C compiler"_str);
    if (c_compiler.is_err()) return Err(rstd::move(c_compiler).unwrap_err());
    auto archiver = path_text(build.toolchain.archiver.as_path(), "archiver"_str);
    if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
    auto linker = path_text(build.toolchain.linker.as_path(), "LLD linker"_str);
    if (linker.is_err()) return Err(rstd::move(linker).unwrap_err());
    append_identity(recipe, executable->as_str());
    append_identity(recipe, provider.identity.as_str());
    append_identity(recipe, provider.generator.as_str());
    rstd_try(append_search_path_identity(recipe, provider));
    append_identity(recipe, compiler->as_str());
    append_identity(recipe, c_compiler->as_str());
    append_identity(recipe, linker->as_str());
    append_identity(recipe, archiver->as_str());
    append_identity(recipe, effective_target);
    append_identity(recipe, cmake_build_type(profile));
    append_identity(recipe, profile.cpp.language.standard.as_str());
    append_identity(recipe,
                    profile.cpp.abi.standard_library == StandardLibrary::Libcxx ? "libc++"_str
                                                                                : "libstdc++"_str);
    append_identity(recipe,
                    profile.cpp.language.exceptions ? "exceptions"_str : "no-exceptions"_str);
    append_identity(recipe, profile.cpp.language.rtti ? "rtti"_str : "no-rtti"_str);
    append_identity(recipe, cpp_optimization_option(profile.cpp.codegen.optimization));
    append_identity(recipe, cpp_debug_option(profile.cpp.codegen.debug_info));
    append_identity(recipe, cpp_lto_option(profile.cpp.codegen.lto));
    for (const auto& entry : requirement.cache) {
        append_identity(recipe, entry.name.as_str());
        append_identity(recipe, entry.value.as_str());
    }
    auto cache =
        lito_cache_directory(PathBuf::from("cmake"_str).as_path(), "CMake dependencies"_str);
    if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
    auto root         = cache->join(PathBuf::from(identity_hash(recipe.as_str())).as_path());
    auto query_recipe = String::make("lito-cmake-query-v4\n"_str);
    append_identity(query_recipe, requirement.alias.as_str());
    append_identity(query_recipe, requirement.package.as_str());
    append_identity(query_recipe,
                    requirement.integration == CMakeIntegration::BuildTree ? "build-tree"_str
                                                                           : "install"_str);
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

auto run_cmake(Vec<String>                       arguments,
               ref<str>                          operation,
               const ResolvedProcessEnvironment& environment,
               Option<ref<rstd::path::Path>>     working_directory = None(),
               bool                              stream_output     = true) -> Result<empty> {
    auto output = [&]() -> Result<CommandOutput> {
        if (! stream_output) return run_command(arguments, environment, working_directory);
        auto observer = rstd::process::OutputObserver {
            .notify =
                +[](void*, rstd::process::OutputStream, slice<u8> chunk) noexcept {
                    auto output = rstd::io::stderr();
                    (void)rstd::io::write_all(output, chunk);
                },
        };
        return run_command_observed(arguments, environment, observer, working_directory);
    }();
    if (output.is_err()) {
        return cmake_failure<empty>(rstd::format("{} could not execute: {}",
                                                 operation,
                                                 rstd::move(output).unwrap_err().message.as_str()));
    }
    if (output->exit_code != i32 {}) {
        auto diagnostics = output->standard_output.clone();
        if (! diagnostics.is_empty() && ! output->standard_error.is_empty()) {
            diagnostics.push('\n');
        }
        diagnostics.push_str(output->standard_error.as_str());
        return cmake_failure<empty>(rstd::format("{} failed with exit code {}:\n{}",
                                                 operation,
                                                 output->exit_code,
                                                 diagnostics.as_str()));
    }
    return Ok(empty {});
}

auto push_path_argument(Vec<String>&          arguments,
                        ref<str>              prefix,
                        ref<rstd::path::Path> path,
                        ref<str>              context) -> Result<empty> {
    auto text = path_text(path, context);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    auto argument = String::make(prefix);
    argument.push_str(text->as_str());
    arguments.push(rstd::move(argument));
    return Ok(empty {});
}

auto push_cmake_toolchain(Vec<String>& arguments, const BuildConfiguration& configuration)
    -> Result<empty> {
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_C_COMPILER="_str,
                                configuration.toolchain.c_compiler.as_path(),
                                "C compiler"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_COMPILER="_str,
                                configuration.toolchain.compiler.as_path(),
                                "C++ compiler"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_C_USING_LINKER_lito_lld=-fuse-ld="_str,
                                configuration.toolchain.linker.as_path(),
                                "LLD linker"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_USING_LINKER_lito_lld=-fuse-ld="_str,
                                configuration.toolchain.linker.as_path(),
                                "LLD linker"_str));
    arguments.push(String::make("-DCMAKE_LINKER_TYPE=lito_lld"_str));
    rstd_try(push_path_argument(
        arguments, "-DCMAKE_AR="_str, configuration.toolchain.archiver.as_path(), "archiver"_str));
    return Ok(empty {});
}

auto push_cmake_search_path(Vec<String>& arguments, const CMakeProviderConfig& provider)
    -> Result<empty> {
    if (provider.search_paths.is_empty()) return Ok(empty {});
    auto value = String::make("-DCMAKE_PREFIX_PATH="_str);
    for (usize index {}; index < provider.search_paths.len(); ++index) {
        auto path = path_text(provider.search_paths[index].as_path(), "CMake search path"_str);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (index != usize {}) value.push_ascii(u8(';'));
        value.push_str(path->as_str());
    }
    arguments.push(rstd::move(value));
    return Ok(empty {});
}

auto cmake_cxx_standard(ref<str> value) -> ref<str> {
    if (value.starts_with("c++"_str)) return *value.strip_prefix("c++"_str);
    if (value.starts_with("gnu++"_str)) return *value.strip_prefix("gnu++"_str);
    return value;
}

auto cmake_cxx_flags(const ProfileSpec& profile) -> String {
    auto result = String::make(profile.cpp.abi.standard_library == StandardLibrary::Libcxx
                                   ? "-stdlib=libc++"_str
                                   : "-stdlib=libstdc++"_str);
    result.push_str(profile.cpp.language.exceptions ? " -fexceptions"_str : " -fno-exceptions"_str);
    result.push_str(profile.cpp.language.rtti ? " -frtti"_str : " -fno-rtti"_str);
    result.push_ascii(' ');
    result.push_str(cpp_optimization_option(profile.cpp.codegen.optimization));
    result.push_ascii(' ');
    result.push_str(cpp_debug_option(profile.cpp.codegen.debug_info));
    result.push_ascii(' ');
    result.push_str(cpp_lto_option(profile.cpp.codegen.lto));
    return result;
}

auto source_install_receipt(const CMakeWorkArea& area) -> PathBuf {
    return area.root.join(PathBuf::from("install-receipt-v1"_str).as_path());
}

auto source_install_current(const CMakeWorkArea& area) -> Result<bool> {
    auto marker = source_install_receipt(area);
    auto ready  = rstd::fs::exists(marker.as_path());
    if (ready.is_err()) {
        return cmake_failure<bool>(rstd::format("cannot inspect CMake install receipt '{}': {}",
                                                marker.as_path(),
                                                rstd::move(ready).unwrap_err()));
    }
    if (! *ready) return Ok(false);
    auto contents = rstd::fs::read_to_string(marker.as_path());
    if (contents.is_err()) {
        return cmake_failure<bool>(rstd::format("cannot read CMake install receipt '{}': {}",
                                                marker.as_path(),
                                                rstd::move(contents).unwrap_err()));
    }
    if (contents->as_str() != "lito-cmake-install-receipt-v1\n"_str) {
        return cmake_failure<bool>(
            rstd::format("CMake install receipt '{}' has invalid contents", marker.as_path()));
    }
    return Ok(true);
}

auto configure_source(const ResolvedCMakeDependencyRequirement& requirement,
                      const CMakeProviderConfig&                provider,
                      const BuildConfiguration&                 configuration,
                      const ProfileSpec&                        profile,
                      const CMakeWorkArea&                      area,
                      const ResolvedProcessEnvironment&         environment) -> Result<empty> {
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("-S"_str));
    auto source = path_text(area.source.as_path(), "CMake source"_str);
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    arguments.push(rstd::move(source).unwrap());
    arguments.push(String::make("-B"_str));
    auto build = path_text(area.build.as_path(), "CMake build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("-G"_str));
    arguments.push(provider.generator.clone());
    rstd_try(push_cmake_search_path(arguments, provider));
    rstd_try(push_path_argument(
        arguments, "-DCMAKE_INSTALL_PREFIX="_str, area.install.as_path(), "CMake install"_str));
    rstd_try(push_cmake_toolchain(arguments, configuration));
    auto build_type = cmake_build_type(profile);
    arguments.push(rstd::format("-DCMAKE_BUILD_TYPE={}", build_type));
    arguments.push(rstd::format("-DCMAKE_CXX_STANDARD={}",
                                cmake_cxx_standard(profile.cpp.language.standard.as_str())));
    arguments.push(String::make("-DCMAKE_CXX_EXTENSIONS=OFF"_str));
    arguments.push(rstd::format("-DCMAKE_CXX_FLAGS={}", cmake_cxx_flags(profile).as_str()));
    for (const auto& entry : requirement.cache) {
        arguments.push(rstd::format("-D{}={}", entry.name.as_str(), entry.value.as_str()));
    }
    rstd_try(run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' configure", requirement.package.as_str()).as_str(),
        environment));
    return Ok(empty {});
}

auto build_source(const ResolvedCMakeDependencyRequirement& requirement,
                  const CMakeProviderConfig&                provider,
                  const ProfileSpec&                        profile,
                  const CMakeWorkArea&                      area,
                  usize                                     jobs,
                  const ResolvedProcessEnvironment&         environment) -> Result<empty> {
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--build"_str));
    auto build = path_text(area.build.as_path(), "CMake build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("--config"_str));
    arguments.push(String::make(cmake_build_type(profile)));
    arguments.push(String::make("--parallel"_str));
    arguments.push(rstd::format("{}", jobs));
    return run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' build", requirement.package.as_str()).as_str(),
        environment,
        None(),
        true);
}

auto install_source(const ResolvedCMakeDependencyRequirement& requirement,
                    const CMakeProviderConfig&                provider,
                    const ProfileSpec&                        profile,
                    const CMakeWorkArea&                      area,
                    bool                                      publish_receipt,
                    const ResolvedProcessEnvironment&         environment) -> Result<empty> {
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--install"_str));
    auto build = path_text(area.build.as_path(), "CMake build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("--config"_str));
    arguments.push(String::make(cmake_build_type(profile)));
    rstd_try(run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' install", requirement.package.as_str()).as_str(),
        environment));
    if (! publish_receipt) return Ok(empty {});
    auto marker = source_install_receipt(area);
    auto marked = rstd::fs::write_atomic(marker.as_path(),
                                         ("lito-cmake-install-receipt-v1\n"_str).as_bytes());
    if (marked.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot write CMake install receipt '{}': {}",
                                                 marker.as_path(),
                                                 rstd::move(marked).unwrap_err()));
    }
    return Ok(empty {});
}

auto probe_project(const ResolvedCMakeDependencyRequirement& requirement, const CMakeWorkArea& area)
    -> Result<String> {
    auto result = String::make("cmake_minimum_required(VERSION 3.29)\n"
                               "project(lito_cmake_probe LANGUAGES CXX)\n"
                               "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"_str);
    if (requirement.integration == CMakeIntegration::BuildTree) {
        auto source = path_text(area.source.as_path(), "CMake build-tree source"_str);
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        auto quoted_source = cmake_quoted(source->as_str(), "CMake build-tree source"_str);
        if (quoted_source.is_err()) return Err(rstd::move(quoted_source).unwrap_err());
        result.push_str("set("_str);
        result.push_str(requirement.alias.as_str());
        result.push_str("_SOURCE_DIR "_str);
        result.push_str(quoted_source->as_str());
        result.push_str(")\n"_str);
        if (requirement.source.as_Directory().add_subdirectory) {
            result.push_str("add_subdirectory("_str);
            result.push_str(quoted_source->as_str());
            result.push_str(" \"${CMAKE_BINARY_DIR}/lito-dependency\")\n"_str);
        }
        if (requirement.adapter.is_some()) {
            auto adapter = path_text(requirement.adapter->as_path(), "CMake adapter"_str);
            if (adapter.is_err()) return Err(rstd::move(adapter).unwrap_err());
            auto quoted_adapter = cmake_quoted(adapter->as_str(), "CMake adapter"_str);
            if (quoted_adapter.is_err()) return Err(rstd::move(quoted_adapter).unwrap_err());
            result.push_str("include("_str);
            result.push_str(quoted_adapter->as_str());
            result.push_str(")\n"_str);
        }
    } else if (requirement.source.is_Directory()) {
        result.push_str("find_package("_str);
        result.push_str(requirement.package.as_str());
        result.push_str(
            " REQUIRED CONFIG PATHS \"${LITO_CMAKE_DEPENDENCY_PREFIX}\" NO_DEFAULT_PATH)\n"_str);
    } else {
        result.push_str("find_package("_str);
        result.push_str(requirement.package.as_str());
        result.push_str(" REQUIRED)\n"_str);
    }
    for (const auto& target : requirement.targets) {
        result.push_str("if(NOT TARGET "_str);
        result.push_str(target.name.as_str());
        result.push_str(")\n  message(FATAL_ERROR \"required imported target "_str);
        result.push_str(target.name.as_str());
        result.push_str(" is unavailable\")\nendif()\n"_str);
    }
    result.push_str("if(DEFINED "_str);
    result.push_str(requirement.package.as_str());
    result.push_str(
        "_VERSION)\n  file(WRITE \"${CMAKE_BINARY_DIR}/lito-package-version.txt\" \"${"_str);
    result.push_str(requirement.package.as_str());
    result.push_str(
        "_VERSION}\")\nelse()\n  file(WRITE \"${CMAKE_BINARY_DIR}/lito-package-version.txt\" \"\")\nendif()\n"_str);
    result.push_str("add_executable(lito_cmake_baseline probe.cpp)\n"_str);
    for (usize index {}; index < requirement.targets.len(); ++index) {
        result.push_str(
            rstd::format("add_executable(lito_cmake_dependency_{} probe.cpp)\n", index).as_str());
        result.push_str(
            rstd::format("target_link_libraries(lito_cmake_dependency_{} PRIVATE ", index)
                .as_str());
        result.push_str(requirement.targets[index].name.as_str());
        result.push_str(")\n"_str);
    }
    result.push_str("add_executable(lito_cmake_combined probe.cpp)\n"
                    "target_link_libraries(lito_cmake_combined PRIVATE"_str);
    for (const auto& target : requirement.targets) {
        result.push_ascii(u8(' '));
        result.push_str(target.name.as_str());
    }
    result.push_str(")\n"_str);
    return Ok(rstd::move(result));
}

auto write_probe_files(const ResolvedCMakeDependencyRequirement& requirement,
                       const CMakeWorkArea&                      area) -> Result<empty> {
    auto query =
        area.query_build.join(PathBuf::from(".cmake/api/v1/query/client-lito"_str).as_path());
    auto directories = Vec<PathBuf>::make();
    directories.push(area.root.clone());
    directories.push(area.build.clone());
    directories.push(area.install.clone());
    directories.push(area.query_root.clone());
    directories.push(area.query_source.clone());
    directories.push(area.query_build.clone());
    directories.push(query.clone());
    for (const auto& directory : directories) {
        auto created = rstd::fs::create_dir_all(directory.as_path());
        if (created.is_err()) {
            return cmake_failure<empty>(rstd::format("cannot create CMake directory '{}': {}",
                                                     directory.as_path(),
                                                     rstd::move(created).unwrap_err()));
        }
    }
    auto cmake_lists = area.query_source.join(PathBuf::from("CMakeLists.txt"_str).as_path());
    auto source      = area.query_source.join(PathBuf::from("probe.cpp"_str).as_path());
    auto query_file  = query.join(PathBuf::from("query.json"_str).as_path());
    auto project     = probe_project(requirement, area);
    if (project.is_err()) return Err(rstd::move(project).unwrap_err());
    auto written = rstd::fs::write_atomic(cmake_lists.as_path(), project->as_str().as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(
            rstd::format("cannot write CMake probe project: {}", rstd::move(written).unwrap_err()));
    }
    written =
        rstd::fs::write_atomic(source.as_path(), ("int main() { return 0; }\n"_str).as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(
            rstd::format("cannot write CMake probe source: {}", rstd::move(written).unwrap_err()));
    }
    written = rstd::fs::write_atomic(
        query_file.as_path(),
        ("{\"requests\":[{\"kind\":\"codemodel\",\"version\":2}]}\n"_str).as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot write CMake File API query: {}",
                                                 rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto configure_probe(const ResolvedCMakeDependencyRequirement& requirement,
                     const CMakeProviderConfig&                provider,
                     const BuildConfiguration&                 configuration,
                     const ProfileSpec&                        profile,
                     const CMakeWorkArea&                      area,
                     const ResolvedProcessEnvironment&         environment) -> Result<empty> {
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("-S"_str));
    auto source = path_text(area.query_source.as_path(), "CMake query source"_str);
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    arguments.push(rstd::move(source).unwrap());
    arguments.push(String::make("-B"_str));
    auto build = path_text(area.query_build.as_path(), "CMake query build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("-G"_str));
    arguments.push(provider.generator.clone());
    rstd_try(push_cmake_search_path(arguments, provider));
    rstd_try(push_cmake_toolchain(arguments, configuration));
    auto build_type = cmake_build_type(profile);
    arguments.push(rstd::format("-DCMAKE_BUILD_TYPE={}", build_type));
    arguments.push(rstd::format("-DCMAKE_CXX_STANDARD={}",
                                cmake_cxx_standard(profile.cpp.language.standard.as_str())));
    arguments.push(String::make("-DCMAKE_CXX_EXTENSIONS=OFF"_str));
    arguments.push(rstd::format("-DCMAKE_CXX_FLAGS={}", cmake_cxx_flags(profile).as_str()));
    if (requirement.integration == CMakeIntegration::Install && requirement.source.is_Directory()) {
        rstd_try(push_path_argument(arguments,
                                    "-DLITO_CMAKE_DEPENDENCY_PREFIX="_str,
                                    area.install.as_path(),
                                    "CMake install"_str));
        if (requirement.config_directory.is_some()) {
            auto prefix    = rstd::format("-D{}_DIR=", requirement.package.as_str());
            auto directory = area.install.join(requirement.config_directory->as_path());
            rstd_try(push_path_argument(
                arguments, prefix.as_str(), directory.as_path(), "CMake config directory"_str));
        }
    }
    if (requirement.integration == CMakeIntegration::BuildTree) {
        for (const auto& entry : requirement.cache) {
            arguments.push(rstd::format("-D{}={}", entry.name.as_str(), entry.value.as_str()));
        }
    }
    return run_cmake(
        rstd::move(arguments),
        rstd::format("CMake package '{}' query", requirement.package.as_str()).as_str(),
        environment);
}

auto build_probe(const ResolvedCMakeDependencyRequirement& requirement,
                 const CMakeProviderConfig&                provider,
                 const BuildConfiguration&,
                 const ProfileSpec&                profile,
                 const CMakeWorkArea&              area,
                 usize                             jobs,
                 const ResolvedProcessEnvironment& environment) -> Result<empty> {
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--build"_str));
    auto build = path_text(area.query_build.as_path(), "CMake query build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("--target"_str));
    arguments.push(String::make("lito_cmake_combined"_str));
    arguments.push(String::make("--config"_str));
    arguments.push(String::make(cmake_build_type(profile)));
    arguments.push(String::make("--parallel"_str));
    arguments.push(rstd::format("{}", jobs));
    return run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' query build", requirement.alias.as_str()).as_str(),
        environment,
        None(),
        true);
}

auto read_json(ref<rstd::path::Path> path, ref<str> context) -> Result<Json> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cmake_failure<Json>(rstd::format(
            "cannot read {} '{}': {}", context, path, rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return cmake_failure<Json>(rstd::format(
            "cannot parse {} '{}': {}", context, path, rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto required_json_member(const Json& value, ref<str> key, ref<str> context) -> Result<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return cmake_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_json_string(const Json& value, ref<str> key, ref<str> context) -> Result<ref<str>> {
    auto member = required_json_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) {
        return cmake_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto required_json_array(const Json& value, ref<str> key, ref<str> context)
    -> Result<ref<JsonArray>> {
    auto member = required_json_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto array = (**member).as_array();
    if (array.is_none()) {
        return cmake_failure<ref<JsonArray>>(rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto current_reply_index(ref<rstd::path::Path> reply) -> Result<PathBuf> {
    auto opened = rstd::fs::read_dir(reply);
    if (opened.is_err()) {
        return cmake_failure<PathBuf>(rstd::format(
            "cannot read CMake File API reply '{}': {}", reply, rstd::move(opened).unwrap_err()));
    }
    auto selected      = Option<PathBuf> {};
    auto selected_name = String::make();
    auto entries       = rstd::move(opened).unwrap();
    for (auto entry = entries.next(); entry.is_some(); entry = entries.next()) {
        if (entry->is_err()) {
            return cmake_failure<PathBuf>(rstd::format("cannot read CMake File API entry: {}",
                                                       rstd::move(*entry).unwrap_err()));
        }
        auto value = rstd::move(*entry).unwrap();
        auto name  = value.file_name().into_string();
        if (name.is_err()) continue;
        auto text = rstd::move(name).unwrap();
        if (! text.as_str().starts_with("index-"_str) || ! text.as_str().ends_with(".json"_str)) {
            continue;
        }
        if (selected.is_none() || selected_name < text) {
            selected_name = rstd::move(text);
            selected      = Some(value.path());
        }
    }
    if (selected.is_none()) {
        return cmake_failure<PathBuf>("CMake File API produced no reply index"_str);
    }
    return Ok(rstd::move(selected).unwrap());
}

auto codemodel_path(const CMakeWorkArea& area) -> Result<PathBuf> {
    auto reply      = area.query_build.join(PathBuf::from(".cmake/api/v1/reply"_str).as_path());
    auto index_path = current_reply_index(reply.as_path());
    if (index_path.is_err()) return Err(rstd::move(index_path).unwrap_err());
    auto index = read_json(index_path->as_path(), "CMake File API index"_str);
    if (index.is_err()) return Err(rstd::move(index).unwrap_err());
    auto reply_member = required_json_member(*index, "reply"_str, "CMake File API index"_str);
    if (reply_member.is_err()) return Err(rstd::move(reply_member).unwrap_err());
    auto client =
        required_json_member(**reply_member, "client-lito"_str, "CMake File API reply"_str);
    if (client.is_err()) return Err(rstd::move(client).unwrap_err());
    auto query =
        required_json_member(**client, "query.json"_str, "CMake File API client reply"_str);
    if (query.is_err()) return Err(rstd::move(query).unwrap_err());
    auto responses =
        required_json_array(**query, "responses"_str, "CMake File API query reply"_str);
    if (responses.is_err()) return Err(rstd::move(responses).unwrap_err());
    if ((**responses).is_empty()) {
        return cmake_failure<PathBuf>("CMake File API query returned no response"_str);
    }
    auto file = required_json_string(
        (**responses)[usize {}], "jsonFile"_str, "CMake File API codemodel response"_str);
    if (file.is_err()) return Err(rstd::move(file).unwrap_err());
    return Ok(reply.join(PathBuf::from(*file).as_path()));
}

struct ProbeTargets {
    PathBuf      baseline;
    Vec<PathBuf> dependencies;
    PathBuf      combined;
};

auto probe_target_paths(const CMakeWorkArea&                      area,
                        const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<ProbeTargets> {
    auto path = codemodel_path(area);
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    auto model = read_json(path->as_path(), "CMake File API codemodel"_str);
    if (model.is_err()) return Err(rstd::move(model).unwrap_err());
    auto configurations = required_json_array(*model, "configurations"_str, "CMake codemodel"_str);
    if (configurations.is_err()) return Err(rstd::move(configurations).unwrap_err());
    if ((**configurations).is_empty()) {
        return cmake_failure<ProbeTargets>("CMake codemodel has no configuration"_str);
    }
    auto targets = required_json_array(
        (**configurations)[usize {}], "targets"_str, "CMake codemodel configuration"_str);
    if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
    auto baseline     = Option<PathBuf> {};
    auto combined     = Option<PathBuf> {};
    auto dependencies = Vec<Option<PathBuf>>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        dependencies.emplace_back(None());
    }
    auto reply = area.query_build.join(PathBuf::from(".cmake/api/v1/reply"_str).as_path());
    for (const auto& target : **targets) {
        auto name = required_json_string(target, "name"_str, "CMake codemodel target"_str);
        auto file = required_json_string(target, "jsonFile"_str, "CMake codemodel target"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (file.is_err()) return Err(rstd::move(file).unwrap_err());
        if (*name == "lito_cmake_baseline"_str)
            baseline = Some(reply.join(PathBuf::from(*file).as_path()));
        else if (*name == "lito_cmake_combined"_str)
            combined = Some(reply.join(PathBuf::from(*file).as_path()));
        else {
            for (usize index {}; index < dependencies.len(); ++index) {
                if (*name == rstd::format("lito_cmake_dependency_{}", index).as_str()) {
                    dependencies[index] = Some(reply.join(PathBuf::from(*file).as_path()));
                    break;
                }
            }
        }
    }
    if (baseline.is_none() || combined.is_none()) {
        return cmake_failure<ProbeTargets>("CMake codemodel is missing probe targets"_str);
    }
    auto resolved_dependencies = Vec<PathBuf>::with_capacity(dependencies.len());
    for (auto& dependency : dependencies) {
        if (dependency.is_none()) {
            return cmake_failure<ProbeTargets>("CMake codemodel is missing probe targets"_str);
        }
        resolved_dependencies.push(rstd::move(dependency).unwrap());
    }
    return Ok(ProbeTargets {
        .baseline     = rstd::move(baseline).unwrap(),
        .dependencies = rstd::move(resolved_dependencies),
        .combined     = rstd::move(combined).unwrap(),
    });
}

auto append_fragment_tokens(Vec<String>& output, ref<str> fragment, ref<str> context)
    -> Result<empty> {
    auto tokens = tokenize_command_fragments(fragment, context);
    if (tokens.is_err()) return Err(rstd::move(tokens).unwrap_err());
    for (auto& token : *tokens) output.push(rstd::move(token));
    return Ok(empty {});
}

auto compile_tokens(const Json& target) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto groups = target.get("compileGroups"_str);
    if (groups.is_none()) return Ok(rstd::move(result));
    auto array = (**groups).as_array();
    if (array.is_none())
        return cmake_failure<Vec<String>>("CMake compileGroups is not an array"_str);
    for (const auto& group : **array) {
        auto language = required_json_string(group, "language"_str, "CMake compile group"_str);
        if (language.is_err()) return Err(rstd::move(language).unwrap_err());
        if (*language != "CXX"_str) continue;
        auto fragments = group.get("compileCommandFragments"_str);
        if (fragments.is_some()) {
            auto values = (**fragments).as_array();
            if (values.is_none()) {
                return cmake_failure<Vec<String>>("CMake compile fragments is not an array"_str);
            }
            for (const auto& fragment : **values) {
                auto text =
                    required_json_string(fragment, "fragment"_str, "CMake compile fragment"_str);
                if (text.is_err()) return Err(rstd::move(text).unwrap_err());
                rstd_try(append_fragment_tokens(result, *text, "CMake compile fragment"_str));
            }
        }
        auto definitions = group.get("defines"_str);
        if (definitions.is_some()) {
            auto values = (**definitions).as_array();
            if (values.is_none())
                return cmake_failure<Vec<String>>("CMake defines is not an array"_str);
            for (const auto& definition : **values) {
                auto text = required_json_string(definition, "define"_str, "CMake definition"_str);
                if (text.is_err()) return Err(rstd::move(text).unwrap_err());
                result.push(rstd::format("-D{}", *text));
            }
        }
        auto includes = group.get("includes"_str);
        if (includes.is_some()) {
            auto values = (**includes).as_array();
            if (values.is_none())
                return cmake_failure<Vec<String>>("CMake includes is not an array"_str);
            for (const auto& include : **values) {
                auto path = required_json_string(include, "path"_str, "CMake include"_str);
                if (path.is_err()) return Err(rstd::move(path).unwrap_err());
                auto system    = include.get("isSystem"_str);
                auto is_system = false;
                if (system.is_some()) {
                    auto value = (**system).as_bool();
                    if (value.is_none())
                        return cmake_failure<Vec<String>>("CMake isSystem is not a boolean"_str);
                    is_system = *value;
                }
                if (is_system) {
                    result.push(String::make("-isystem"_str));
                    result.push(String::make(*path));
                } else {
                    result.push(rstd::format("-I{}", *path));
                }
            }
        }
        break;
    }
    return Ok(rstd::move(result));
}

auto link_tokens(const Json& target) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto link   = target.get("link"_str);
    if (link.is_none()) return Ok(rstd::move(result));
    auto fragments = (**link).get("commandFragments"_str);
    if (fragments.is_none()) return Ok(rstd::move(result));
    auto values = (**fragments).as_array();
    if (values.is_none())
        return cmake_failure<Vec<String>>("CMake link fragments is not an array"_str);
    for (const auto& fragment : **values) {
        auto text = required_json_string(fragment, "fragment"_str, "CMake link fragment"_str);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        rstd_try(append_fragment_tokens(result, *text, "CMake link fragment"_str));
    }
    return Ok(rstd::move(result));
}

auto subtract_baseline(const Vec<String>& baseline, Vec<String> values) -> Vec<String> {
    auto consumed = Vec<bool>::with_capacity(baseline.len());
    for (usize index {}; index < baseline.len(); ++index) consumed.emplace_back(false);
    auto result = Vec<String>::make();
    for (auto& value : values) {
        auto matched = false;
        for (usize index {}; index < baseline.len(); ++index) {
            if (! consumed[index] && baseline[index] == value) {
                consumed[index] = true;
                matched         = true;
                break;
            }
        }
        if (! matched) result.push(rstd::move(value));
    }
    return result;
}

export struct CMakeTargetUsageSnapshot {
    Vec<String> compile;
    Vec<String> link;
};

export struct CMakeUsageSnapshot {
    String                        version;
    Vec<CMakeTargetUsageSnapshot> targets;
    CMakeTargetUsageSnapshot      combined;
};

auto snapshot_from_targets(const Json& baseline, const Json& dependency)
    -> Result<CMakeTargetUsageSnapshot> {
    auto baseline_compile   = compile_tokens(baseline);
    auto baseline_link      = link_tokens(baseline);
    auto dependency_compile = compile_tokens(dependency);
    auto dependency_link    = link_tokens(dependency);
    if (baseline_compile.is_err()) return Err(rstd::move(baseline_compile).unwrap_err());
    if (baseline_link.is_err()) return Err(rstd::move(baseline_link).unwrap_err());
    if (dependency_compile.is_err()) return Err(rstd::move(dependency_compile).unwrap_err());
    if (dependency_link.is_err()) return Err(rstd::move(dependency_link).unwrap_err());
    return Ok(CMakeTargetUsageSnapshot {
        .compile = subtract_baseline(*baseline_compile, rstd::move(dependency_compile).unwrap()),
        .link    = subtract_baseline(*baseline_link, rstd::move(dependency_link).unwrap()),
    });
}

auto read_probe_snapshots(const CMakeWorkArea&                      area,
                          const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<CMakeUsageSnapshot> {
    auto paths = probe_target_paths(area, requirement);
    if (paths.is_err()) return Err(rstd::move(paths).unwrap_err());
    auto baseline = read_json(paths->baseline.as_path(), "CMake baseline target"_str);
    if (baseline.is_err()) return Err(rstd::move(baseline).unwrap_err());
    auto snapshots = Vec<CMakeTargetUsageSnapshot>::with_capacity(paths->dependencies.len());
    for (const auto& path : paths->dependencies) {
        auto dependency = read_json(path.as_path(), "CMake dependency target"_str);
        if (dependency.is_err()) return Err(rstd::move(dependency).unwrap_err());
        auto snapshot = snapshot_from_targets(*baseline, *dependency);
        if (snapshot.is_err()) return Err(rstd::move(snapshot).unwrap_err());
        snapshots.push(rstd::move(snapshot).unwrap());
    }
    auto combined = read_json(paths->combined.as_path(), "CMake combined target"_str);
    if (combined.is_err()) return Err(rstd::move(combined).unwrap_err());
    auto combined_snapshot = snapshot_from_targets(*baseline, *combined);
    if (combined_snapshot.is_err()) return Err(rstd::move(combined_snapshot).unwrap_err());
    return Ok(CMakeUsageSnapshot {
        .targets  = rstd::move(snapshots),
        .combined = rstd::move(combined_snapshot).unwrap(),
    });
}

auto usage_snapshot_path(const CMakeWorkArea& area) -> PathBuf {
    return area.query_root.join(PathBuf::from("usage-snapshot-v1.json"_str).as_path());
}

auto json_strings(const Vec<String>& values) -> Json {
    auto result = JsonArray::with_capacity(values.len());
    for (const auto& value : values) result.push(Json::String(value.clone()));
    return Json::Array(rstd::move(result));
}

auto snapshot_json(const CMakeTargetUsageSnapshot& snapshot) -> Json {
    auto result = JsonMap::make();
    result.insert(String::make("compile"_str), json_strings(snapshot.compile));
    result.insert(String::make("link"_str), json_strings(snapshot.link));
    return Json::Object(rstd::move(result));
}

auto write_usage_snapshot(const CMakeWorkArea& area, const CMakeUsageSnapshot& snapshot)
    -> Result<empty> {
    auto targets = JsonArray::with_capacity(snapshot.targets.len());
    for (const auto& target : snapshot.targets) targets.push(snapshot_json(target));
    auto document = JsonMap::make();
    document.insert(String::make("schema"_str),
                    Json::String(String::make("lito-cmake-usage-v1"_str)));
    document.insert(String::make("version"_str), Json::String(snapshot.version.clone()));
    document.insert(String::make("targets"_str), Json::Array(rstd::move(targets)));
    document.insert(String::make("combined"_str), snapshot_json(snapshot.combined));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push('\n');
    auto path    = usage_snapshot_path(area);
    auto written = rstd::fs::write_atomic(path.as_path(), text.as_str().as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot write CMake usage snapshot '{}': {}",
                                                 path.as_path(),
                                                 rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto parse_snapshot_strings(const Json& value, ref<str> key, ref<str> context)
    -> Result<Vec<String>> {
    auto array = required_json_array(value, key, context);
    if (array.is_err()) return Err(rstd::move(array).unwrap_err());
    auto result = Vec<String>::with_capacity((**array).len());
    for (const auto& item : **array) {
        auto text = item.as_str();
        if (text.is_none()) {
            return cmake_failure<Vec<String>>(
                rstd::format("{}.{} contains a non-string value", context, key));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto parse_usage_target(const Json& value, ref<str> context) -> Result<CMakeTargetUsageSnapshot> {
    auto compile = parse_snapshot_strings(value, "compile"_str, context);
    if (compile.is_err()) return Err(rstd::move(compile).unwrap_err());
    auto link = parse_snapshot_strings(value, "link"_str, context);
    if (link.is_err()) return Err(rstd::move(link).unwrap_err());
    return Ok(CMakeTargetUsageSnapshot {
        .compile = rstd::move(compile).unwrap(),
        .link    = rstd::move(link).unwrap(),
    });
}

auto materialize_link_tokens(const Vec<String>& tokens, ref<rstd::path::Path> query_build)
    -> Result<Vec<String>> {
    auto result = Vec<String>::with_capacity(tokens.len());
    auto root   = PathBuf::from(query_build);
    for (const auto& token : tokens) {
        auto path = PathBuf::from(token.as_str());
        if (token.as_str().starts_with("-"_str) || path.as_path().is_absolute()) {
            result.push(token.clone());
            continue;
        }
        auto candidate = root.join(path.as_path());
        auto exists    = rstd::fs::exists(candidate.as_path());
        if (exists.is_err()) {
            return cmake_failure<Vec<String>>(
                rstd::format("cannot inspect CMake link input '{}': {}",
                             candidate.as_path(),
                             rstd::move(exists).unwrap_err()));
        }
        if (! *exists) {
            result.push(token.clone());
            continue;
        }
        auto canonical = rstd::fs::canonicalize(candidate.as_path());
        if (canonical.is_err()) {
            return cmake_failure<Vec<String>>(
                rstd::format("cannot resolve CMake link input '{}': {}",
                             candidate.as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        auto text = path_text(canonical->as_path(), "CMake link input"_str);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        result.push(rstd::move(text).unwrap());
    }
    return Ok(rstd::move(result));
}

auto read_usage_snapshot(const CMakeWorkArea&                      area,
                         const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<Option<CMakeUsageSnapshot>> {
    auto path   = usage_snapshot_path(area);
    auto exists = rstd::fs::exists(path.as_path());
    if (exists.is_err()) {
        return cmake_failure<Option<CMakeUsageSnapshot>>(
            rstd::format("cannot inspect CMake usage snapshot '{}': {}",
                         path.as_path(),
                         rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(None());
    auto value = read_json(path.as_path(), "CMake usage snapshot"_str);
    if (value.is_err()) return Err(rstd::move(value).unwrap_err());
    auto schema = required_json_string(*value, "schema"_str, "CMake usage snapshot"_str);
    if (schema.is_err()) return Err(rstd::move(schema).unwrap_err());
    if (*schema != "lito-cmake-usage-v1"_str) {
        return cmake_failure<Option<CMakeUsageSnapshot>>(rstd::format(
            "CMake usage snapshot '{}' has unsupported schema '{}'", path.as_path(), *schema));
    }
    auto version = required_json_string(*value, "version"_str, "CMake usage snapshot"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    auto targets = required_json_array(*value, "targets"_str, "CMake usage snapshot"_str);
    if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
    if ((**targets).len() != requirement.targets.len()) {
        return cmake_failure<Option<CMakeUsageSnapshot>>(
            rstd::format("CMake usage snapshot '{}' has {} targets, expected {}",
                         path.as_path(),
                         (**targets).len(),
                         requirement.targets.len()));
    }
    auto parsed_targets = Vec<CMakeTargetUsageSnapshot>::with_capacity((**targets).len());
    for (usize index {}; index < (**targets).len(); ++index) {
        auto parsed = parse_usage_target((**targets)[index], "CMake usage target"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        parsed_targets.push(rstd::move(parsed).unwrap());
    }
    auto combined = required_json_member(*value, "combined"_str, "CMake usage snapshot"_str);
    if (combined.is_err()) return Err(rstd::move(combined).unwrap_err());
    auto parsed_combined = parse_usage_target(**combined, "CMake combined usage"_str);
    if (parsed_combined.is_err()) return Err(rstd::move(parsed_combined).unwrap_err());
    return Ok(Some(CMakeUsageSnapshot {
        .version  = String::make(*version),
        .targets  = rstd::move(parsed_targets),
        .combined = rstd::move(parsed_combined).unwrap(),
    }));
}

auto target_snapshot_identity(const CMakeProviderConfig&                provider,
                              const ResolvedCMakeDependencyRequirement& requirement,
                              ref<str>                                  target,
                              ref<str>                                  version,
                              const CMakeTargetUsageSnapshot&           snapshot,
                              ref<str> effective_target) -> Result<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("lito-cmake-dependency-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.identity.as_str());
    append_identity(result, provider.generator.as_str());
    rstd_try(append_search_path_identity(result, provider));
    append_identity(result, requirement.package.as_str());
    append_identity(result, target);
    append_identity(result, version);
    append_identity(result, effective_target);
    append_identity(result, source_identity(requirement).as_str());
    for (const auto& token : snapshot.compile) append_identity(result, token.as_str());
    return Ok(rstd::move(result));
}

auto dependency_identity(const CMakeProviderConfig&                provider,
                         const ResolvedCMakeDependencyRequirement& requirement,
                         ref<str>                                  version,
                         const CMakeUsageSnapshot&                 snapshots,
                         ref<str> effective_target) -> Result<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("lito-cmake-declaration-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.identity.as_str());
    append_identity(result, provider.generator.as_str());
    rstd_try(append_search_path_identity(result, provider));
    append_identity(result, requirement.package.as_str());
    append_identity(result, version);
    append_identity(result, effective_target);
    append_identity(result, source_identity(requirement).as_str());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        append_identity(result, requirement.targets[index].name.as_str());
        for (const auto& token : snapshots.targets[index].compile) {
            append_identity(result, token.as_str());
        }
    }
    for (const auto& token : snapshots.combined.link) append_identity(result, token.as_str());
    return Ok(rstd::move(result));
}

} // namespace lito

export namespace lito
{

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement& requirement,
                        const CMakeProviderConfig&                provider,
                        const BuildConfiguration&                 configuration,
                        const ProfileSpec&                        profile,
                        const TargetInfo&                         default_target,
                        ref<str>                                  effective_target,
                        usize jobs = usize(1)) -> Result<CMakePackagePlan> {
    if (jobs == usize {}) {
        return cmake_failure<CMakePackagePlan>("CMake build jobs must be greater than zero"_str);
    }
    if (effective_target != default_target.triple.as_str()) {
        return cmake_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' cannot resolve cross target '{}' without an explicit CMake "
            "toolchain contract",
            requirement.alias.as_str(),
            effective_target));
    }
    if (requirement.source.is_Archive()) {
        return cmake_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' archive source must be materialized before planning",
            requirement.alias.as_str()));
    }
    auto area = work_area(requirement, provider, configuration, profile, effective_target);
    if (area.is_err()) return Err(rstd::move(area).unwrap_err());
    auto operations = Vec<CMakePackageOperation>::make();
    if (requirement.integration == CMakeIntegration::Install &&
        ! requirement.source.is_Installed()) {
        operations.push(CMakePackageOperation::ConfigureSource);
        operations.push(CMakePackageOperation::BuildSource);
        operations.push(CMakePackageOperation::InstallSource);
    }
    operations.push(CMakePackageOperation::WriteQuery);
    operations.push(CMakePackageOperation::ConfigureQuery);
    operations.push(CMakePackageOperation::BuildQuery);
    operations.push(CMakePackageOperation::ReadUsage);
    return Ok(CMakePackagePlan {
        .requirement      = clone_cmake_requirement(requirement),
        .provider         = provider.clone(),
        .configuration    = configuration.clone(),
        .profile          = clone_profile(profile),
        .area             = rstd::move(area).unwrap(),
        .effective_target = String::make(effective_target),
        .operations       = rstd::move(operations),
        .jobs             = jobs,
    });
}

auto identify_cmake_provider(CMakeProviderConfig               provider,
                             const ResolvedProcessEnvironment& environment)
    -> Result<CMakeProviderConfig> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto arguments = Vec<String>::make();
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--version"_str));
    auto output = run_command(arguments, environment);
    if (output.is_err()) {
        return cmake_failure<CMakeProviderConfig>(
            rstd::format("CMake provider identity could not execute: {}",
                         rstd::move(output).unwrap_err().message.as_str()));
    }
    if (output->exit_code != i32 {}) {
        return cmake_failure<CMakeProviderConfig>(
            rstd::format("CMake provider identity failed with exit code {}:\n{}{}",
                         output->exit_code,
                         output->standard_output.as_str(),
                         output->standard_error.as_str()));
    }
    provider.identity = String::make(output->standard_output.as_str().trim_ascii());
    if (provider.identity.is_empty()) {
        return cmake_failure<CMakeProviderConfig>("CMake provider returned an empty identity"_str);
    }
    return Ok(rstd::move(provider));
}

auto execute_cmake_package(const CMakePackagePlan&           plan,
                           const ResolvedProcessEnvironment& environment,
                           const Option<BuildObserver>&      observer = None())
    -> Result<CMakeUsageSnapshot> {
    const auto& requirement   = plan.requirement;
    const auto& provider      = plan.provider;
    const auto& configuration = plan.configuration;
    const auto& profile       = plan.profile;
    const auto& area          = plan.area;
    auto        created       = rstd::fs::create_dir_all(area.root.as_path());
    if (created.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot create CMake work directory '{}': {}",
                         area.root.as_path(),
                         rstd::move(created).unwrap_err()));
    }
    auto lock_path = area.root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot open CMake dependency lock '{}': {}",
                         lock_path.as_path(),
                         rstd::move(lock_file).unwrap_err()));
    }
    auto locked = lock_file->lock();
    if (locked.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot lock CMake dependency '{}': {}",
                         requirement.alias.as_str(),
                         rstd::move(locked).unwrap_err()));
    }
    auto cacheable =
        requirement.source.is_Directory() && requirement.source.as_Directory().cacheable;
    if (cacheable) {
        auto cached = read_usage_snapshot(area, requirement);
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->is_some()) {
            emit_cmake(observer,
                       BuildEventKind::CMakeReuse,
                       requirement.alias.as_str(),
                       area.query_root.as_path());
            return Ok(rstd::move(cached).unwrap().unwrap());
        }
    }
    auto install_current = cacheable ? rstd_try(source_install_current(area)) : false;
    auto snapshots       = Option<CMakeUsageSnapshot> {};
    for (auto operation : plan.operations) {
        switch (operation) {
        case CMakePackageOperation::ConfigureSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        BuildEventKind::CMakeConfigure,
                        requirement.alias.as_str(),
                        area.build.as_path(),
                        [&] {
                            return configure_source(
                                requirement, provider, configuration, profile, area, environment);
                        }),
                    plan,
                    operation));
            }
            break;
        case CMakePackageOperation::BuildSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        BuildEventKind::CMakeBuild,
                        requirement.alias.as_str(),
                        area.build.as_path(),
                        [&] {
                            return build_source(
                                requirement, provider, profile, area, plan.jobs, environment);
                        }),
                    plan,
                    operation));
            }
            break;
        case CMakePackageOperation::InstallSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        BuildEventKind::CMakeInstall,
                        requirement.alias.as_str(),
                        area.install.as_path(),
                        [&] {
                            return install_source(
                                requirement, provider, profile, area, cacheable, environment);
                        }),
                    plan,
                    operation));
                install_current = true;
            } else {
                emit_cmake(observer,
                           BuildEventKind::CMakeReuse,
                           requirement.alias.as_str(),
                           area.install.as_path());
            }
            break;
        case CMakePackageOperation::WriteQuery:
            rstd_try(with_operation_context(write_probe_files(requirement, area), plan, operation));
            break;
        case CMakePackageOperation::ConfigureQuery:
            rstd_try(with_operation_context(
                execute_observed(
                    observer,
                    BuildEventKind::CMakeQuery,
                    requirement.alias.as_str(),
                    area.query_build.as_path(),
                    [&] {
                        return configure_probe(
                            requirement, provider, configuration, profile, area, environment);
                    }),
                plan,
                operation));
            break;
        case CMakePackageOperation::BuildQuery:
            rstd_try(with_operation_context(execute_observed(observer,
                                                             BuildEventKind::CMakeQueryBuild,
                                                             requirement.alias.as_str(),
                                                             area.query_build.as_path(),
                                                             [&] {
                                                                 return build_probe(requirement,
                                                                                    provider,
                                                                                    configuration,
                                                                                    profile,
                                                                                    area,
                                                                                    plan.jobs,
                                                                                    environment);
                                                             }),
                                            plan,
                                            operation));
            break;
        case CMakePackageOperation::ReadUsage:
            snapshots = Some(rstd_try(with_operation_context(
                execute_observed(observer,
                                 BuildEventKind::CMakeSnapshot,
                                 requirement.alias.as_str(),
                                 area.query_root.as_path(),
                                 [&] {
                                     return read_probe_snapshots(area, requirement);
                                 }),
                plan,
                operation)));
            break;
        }
    }
    if (snapshots.is_none()) {
        return cmake_failure<CMakeUsageSnapshot>(rstd::format(
            "CMake package '{}' plan produced no usage snapshot", requirement.package.as_str()));
    }
    auto version_path =
        area.query_build.join(PathBuf::from("lito-package-version.txt"_str).as_path());
    auto version = rstd::fs::read_to_string(version_path.as_path());
    if (version.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot read CMake package '{}' version: {}",
                         requirement.package.as_str(),
                         rstd::move(version).unwrap_err()));
    }
    auto normalized_version = String::make(version->as_str().trim_ascii());
    if (normalized_version.is_empty()) normalized_version = String::make("unknown"_str);
    snapshots->version = rstd::move(normalized_version);
    if (cacheable) rstd_try(write_usage_snapshot(area, *snapshots));
    return Ok(rstd::move(snapshots).unwrap());
}

auto materialize_cmake_usage(const CMakePackagePlan&   plan,
                             const CMakeUsageSnapshot& snapshots,
                             const CppArgumentParser&  parser)
    -> Result<ResolvedExternalDependency> {
    const auto& requirement        = plan.requirement;
    const auto& provider           = plan.provider;
    const auto& effective_target   = plan.effective_target;
    const auto& normalized_version = snapshots.version;
    if (snapshots.targets.len() != requirement.targets.len()) {
        return cmake_failure<ResolvedExternalDependency>(
            rstd::format("CMake package '{}' usage snapshot has {} targets, expected {}",
                         requirement.package.as_str(),
                         snapshots.targets.len(),
                         requirement.targets.len()));
    }
    auto targets = Vec<ResolvedExternalTargetUsage>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        const auto& target   = requirement.targets[index];
        const auto& snapshot = snapshots.targets[index];
        auto        source   = rstd::format("CMake dependency '{}' package '{}' target '{}'",
                                            requirement.alias.as_str(),
                                            requirement.package.as_str(),
                                            target.name.as_str());
        auto        compile  = parser.parse(snapshot.compile, source.as_str());
        if (compile.is_err()) {
            return cmake_failure<ResolvedExternalDependency>(
                rstd::format("{} has invalid compile requirements: {}",
                             source.as_str(),
                             rstd::move(compile).unwrap_err()));
        }
        auto identity = target_snapshot_identity(provider,
                                                 requirement,
                                                 target.name.as_str(),
                                                 normalized_version.as_str(),
                                                 snapshot,
                                                 effective_target.as_str());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        targets.push(ResolvedExternalTargetUsage {
            .name              = target.name.clone(),
            .visibility        = target.visibility,
            .compile_arguments = rstd::move(compile).unwrap(),
            .identity          = rstd::move(identity).unwrap(),
        });
    }
    auto identity = dependency_identity(
        provider, requirement, normalized_version.as_str(), snapshots, effective_target.as_str());
    if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
    auto link_arguments =
        materialize_link_tokens(snapshots.combined.link, plan.area.query_build.as_path());
    if (link_arguments.is_err()) return Err(rstd::move(link_arguments).unwrap_err());
    return Ok(ResolvedExternalDependency {
        .alias    = requirement.alias.clone(),
        .provider = String::make("cmake"_str),
        .version  = normalized_version.clone(),
        .targets  = rstd::move(targets),
        .link_arguments =
            LinkArgumentSequence {
                .tokens   = rstd::move(link_arguments).unwrap(),
                .source   = rstd::format("CMake dependency '{}' package '{}'",
                                         requirement.alias.as_str(),
                                         requirement.package.as_str()),
                .identity = identity->clone(),
            },
        .identity = rstd::move(identity).unwrap(),
    });
}

} // namespace lito
