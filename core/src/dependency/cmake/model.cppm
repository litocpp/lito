module;
#include <rstd/macro.hpp>

export module lito.dependency.cmake.model;

import rstd;
import rstd.json;
import lito.error;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.platform.contract;
import lito.dependency.contract;
import lito.dependency.error_contract;
import lito.cpp;
import lito.system.process;
import lito.system.environment;
import lito.system.storage;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;

export namespace lito
{

template<typename T>
auto cmake_failure(String message) -> DependencyResult<T> {
    return Err(DependencyError::Message(rstd::move(message)));
}

template<typename T>
auto cmake_failure(ref<str> message) -> DependencyResult<T> {
    return Err(DependencyError::Message(String::make(message)));
}

template<typename T>
auto cmake_io_failure(ref<str> operation,
                      ref<rstd::path::Path> path,
                      rstd::io::error::Error source) -> DependencyResult<T> {
    return Err(DependencyError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
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

auto path_text(ref<rstd::path::Path> path, ref<str> context) -> DependencyResult<String> {
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
    -> DependencyResult<empty> {
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

auto cmake_quoted(ref<str> value, ref<str> context) -> DependencyResult<String> {
    if (value.contains("\""_str) || value.contains("\\"_str) || value.contains(";"_str) ||
        value.contains("\n"_str) || value.contains("\r"_str)) {
        return cmake_failure<String>(rstd::format("{} contains CMake syntax", context));
    }
    return Ok(rstd::format("\"{}\"", value));
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
    ResolvedCMakeDependencyRequirement requirement;
    CMakeProviderConfig                provider;
    BuildConfiguration                 configuration;
    ProfileSpec                        profile;
    CMakeWorkArea                      area;
    String                             effective_target;
    Vec<CMakePackageOperation>         operations;
    usize                              jobs { usize(1) };
};

template<typename T>
auto with_operation_context(DependencyResult<T>               result,
                            const CMakePackagePlan& plan,
                            CMakePackageOperation   operation) -> DependencyResult<T> {
    if (result.is_ok()) return result;
    auto error = rstd::move(result).unwrap_err();
    return Err(DependencyError::CMakeOperation(
        plan.requirement.alias.clone(),
        rstd::format("{}", operation),
        plan.area.root.clone(),
        rstd::boxed::Box<DependencyError>::make(rstd::move(error))));
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
               ref<str> effective_target) -> DependencyResult<CMakeWorkArea> {
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
    if (cache.is_err()) {
        return Err(rstd::into<DependencyError>(rstd::move(cache).unwrap_err()));
    }
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
