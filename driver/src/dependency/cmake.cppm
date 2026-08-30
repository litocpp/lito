module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.driver:dependency.cmake;

import rstd;
import licrypto;
import lito.tools;
import lito.tools.cmake;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito
{

class ResolvedCMakeDependencySource {
    RSTD_ENUM(ResolvedCMakeDependencySource,
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)))

public:
    auto clone() const -> ResolvedCMakeDependencySource {
        if (is_Directory()) {
            return Directory(as_Directory().root.clone(),
                             as_Directory().identity.clone(),
                             as_Directory().cacheable);
        }
        return Find();
    }
};

struct ResolvedCMakeDependencyRequirement {
    String                                          alias;
    String                                          package;
    Vec<String>                                     components;
    ResolvedCMakeDependencySource                   source;
    Option<PathBuf>                                 adapter;
    String                                          adapter_identity;
    Option<PathBuf>                                 config_directory;
    Vec<lito::dependency::CMakeCacheEntry>          cache;
    Vec<lito::dependency::CMakeTargetRequirement>   targets;
    Vec<lito::dependency::CMakeHostToolRequirement> host_tools;

    auto clone() const -> ResolvedCMakeDependencyRequirement;
};

struct ResolvedCMakePackage {
    ResolvedCMakeDependencyRequirement requirement;
};

using ExternalAssetEntry       = lito::dependency::ExternalAssetEntry;
using ExternalAssetDisposition = lito::dependency::ExternalAssetDisposition;
using ExternalAssetSet         = lito::dependency::ExternalAssetSet;
using CMakePackageOperation    = lito::tools::cmake::CMakePackageOperation;
using CMakeUsageSnapshot       = lito::tools::cmake::CMakeUsageSnapshot;

struct CMakePackagePlan {
    lito::tools::cmake::CMakePackagePlan        tool;
    Vec<lito::dependency::DependencyVisibility> visibilities;
};

auto cmake_profile_configuration(const cpp::ProfileSpec& profile)
    -> lito::tools::cmake::ProfileConfiguration;

auto identify_cmake_provider(lito::dependency::CMakeProviderConfig provider,
                             const ResolvedProcessEnvironment&     environment)
    -> lito::dependency::DependencyResult<lito::dependency::CMakeProviderConfig>;

auto resolve_cmake_package(const Vec<ResolvedCMakeDependencyRequirement>& requirements)
    -> lito::dependency::DependencyResult<ResolvedCMakePackage>;

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement&    requirement,
                        const lito::dependency::CMakeProviderConfig& provider,
                        const cpp::BuildConfiguration&               configuration,
                        const cpp::ProfileSpec&                      profile,
                        const LinkerIdentity&                        linker,
                        const TargetInfo&                            default_target,
                        ref<str>                                     effective_target,
                        ref<rstd::path::Path>                        profile_cmake_root,
                        usize                                        jobs                = usize(1),
                        const Option<AndroidCmakeProjection>&        android             = None(),
                        const Option<PathBuf>&                       find_install_prefix = None())
    -> lito::dependency::DependencyResult<CMakePackagePlan>;

auto execute_cmake_package(const CMakePackagePlan&                      plan,
                           const ResolvedProcessEnvironment&            environment,
                           const Option<lito::tools::cmake::EventSink>& observer = None())
    -> lito::dependency::DependencyResult<CMakeUsageSnapshot>;

auto materialize_cmake_usage(const CMakePackagePlan& plan, const CMakeUsageSnapshot& snapshots)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage>;

auto materialize_cmake_usage(const CMakePackagePlan&                   plan,
                             const CMakeUsageSnapshot&                 snapshots,
                             const ResolvedCMakeDependencyRequirement& consumer)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage>;

} // namespace lito

namespace lito
{

auto ResolvedCMakeDependencyRequirement::clone() const -> ResolvedCMakeDependencyRequirement {
    auto cache_copy = Vec<lito::dependency::CMakeCacheEntry>::with_capacity(cache.len());
    for (const auto& entry : cache) {
        cache_copy.push(lito::dependency::CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto target_copy = Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(targets.len());
    for (const auto& target : targets) {
        target_copy.push(lito::dependency::CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto tool_copy =
        Vec<lito::dependency::CMakeHostToolRequirement>::with_capacity(host_tools.len());
    for (const auto& tool : host_tools) tool_copy.push(tool.clone());
    auto result = ResolvedCMakeDependencyRequirement {
        .alias            = alias.clone(),
        .package          = package.clone(),
        .components       = as<Clone>(components).clone(),
        .source           = source.clone(),
        .adapter_identity = adapter_identity.clone(),
        .cache            = rstd::move(cache_copy),
        .targets          = rstd::move(target_copy),
        .host_tools       = rstd::move(tool_copy),
    };
    if (adapter.is_some()) result.adapter = Some(adapter->clone());
    if (config_directory.is_some()) result.config_directory = Some(config_directory->clone());
    return result;
}

auto cmake_error(ref<str> context, lito::tools::ToolError error)
    -> lito::dependency::DependencyError {
    return lito::dependency::DependencyError::Provider(
        String::make(context), Box<dyn<rstd::error::Error>>::make(rstd::move(error)));
}

template<typename T>
auto cmake_result(ref<str> context, lito::tools::ToolResult<T> result)
    -> lito::dependency::DependencyResult<T> {
    if (result.is_err()) return Err(cmake_error(context, rstd::move(result).unwrap_err()));
    return Ok(rstd::move(result).unwrap());
}

auto cmake_source(const ResolvedCMakeDependencySource& source) -> lito::tools::cmake::Source {
    if (source.is_Directory()) {
        return lito::tools::cmake::Source::Directory(source.as_Directory().root.clone(),
                                                     source.as_Directory().identity.clone(),
                                                     source.as_Directory().cacheable);
    }
    return lito::tools::cmake::Source::Find();
}

auto cmake_request(const ResolvedCMakeDependencyRequirement& requirement,
                   const Option<PathBuf>& find_install_prefix) -> lito::tools::cmake::Request {
    auto cache = Vec<lito::tools::cmake::CacheEntry>::with_capacity(requirement.cache.len());
    for (const auto& entry : requirement.cache) {
        cache.push(lito::tools::cmake::CacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets =
        Vec<lito::tools::cmake::TargetRequirement>::with_capacity(requirement.targets.len());
    for (const auto& target : requirement.targets) {
        targets.push(lito::tools::cmake::TargetRequirement { .name = target.name.clone() });
    }
    auto host_tools = Vec<lito::tools::cmake::HostToolRequirement>::make();
    for (const auto& tool : requirement.host_tools) {
        host_tools.push(lito::tools::cmake::HostToolRequirement {
            .name   = tool.name.clone(),
            .target = tool.target.clone(),
        });
    }
    auto result = lito::tools::cmake::Request {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .components       = as<Clone>(requirement.components).clone(),
        .source           = cmake_source(requirement.source),
        .adapter_identity = requirement.adapter_identity.clone(),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
        .host_tools       = rstd::move(host_tools),
    };
    if (requirement.adapter.is_some()) result.adapter = Some(requirement.adapter->clone());
    if (requirement.config_directory.is_some()) {
        result.config_directory = Some(requirement.config_directory->clone());
    }
    if (requirement.source.is_Find() && find_install_prefix.is_some()) {
        result.find_install_prefix = Some(find_install_prefix->clone());
    }
    return result;
}

auto cmake_provider(const lito::dependency::CMakeProviderConfig& provider)
    -> lito::tools::cmake::Provider {
    return lito::tools::cmake::Provider {
        .executable   = provider.executable.clone(),
        .identity     = provider.identity.clone(),
        .generator    = provider.generator.clone(),
        .search_paths = as<Clone>(provider.search_paths).clone(),
    };
}

auto same_optional_path(const Option<PathBuf>& left, const Option<PathBuf>& right) noexcept
    -> bool {
    if (left.is_some() != right.is_some()) return false;
    return left.is_none() || left->as_path() == right->as_path();
}

auto same_cmake_source(const ResolvedCMakeDependencySource& left,
                       const ResolvedCMakeDependencySource& right) noexcept -> bool {
    if (left.is_Find() || right.is_Find()) return left.is_Find() && right.is_Find();
    return left.as_Directory().root.as_path() == right.as_Directory().root.as_path() &&
           left.as_Directory().identity == right.as_Directory().identity.as_str() &&
           left.as_Directory().cacheable == right.as_Directory().cacheable;
}

auto same_cmake_cache(const Vec<lito::dependency::CMakeCacheEntry>& left,
                      const Vec<lito::dependency::CMakeCacheEntry>& right) noexcept -> bool {
    if (left.len() != right.len()) return false;
    for (const auto& entry : left) {
        auto matched = false;
        for (const auto& candidate : right) {
            if (entry.name == candidate.name.as_str() && entry.value == candidate.value.as_str()) {
                matched = true;
                break;
            }
        }
        if (! matched) return false;
    }
    return true;
}

auto cmake_package_conflict(ref<str> package, ref<str> left, ref<str> right, ref<str> field)
    -> lito::dependency::DependencyError {
    return lito::dependency::dependency_failure<empty>(
               rstd::format("CMake package '{}' has conflicting {} in aliases '{}' and '{}'",
                            package,
                            field,
                            left,
                            right))
        .unwrap_err();
}

auto append_unique_string(Vec<String>& output, ref<str> value) -> void {
    for (const auto& current : output) {
        if (current.as_str() == value) return;
    }
    output.push(String::make(value));
}

auto cmake_toolchain(const cpp::BuildConfiguration&        configuration,
                     const LinkerIdentity&                 linker,
                     const Option<AndroidCmakeProjection>& android)
    -> lito::tools::cmake::ToolchainConfiguration {
    auto result = lito::tools::cmake::ToolchainConfiguration {
        .cc              = configuration.toolchain.cc.clone(),
        .cxx             = configuration.toolchain.cxx.clone(),
        .linker          = linker.executable.clone(),
        .linker_identity = linker.build_identity.clone(),
        .archiver        = configuration.toolchain.ar.clone(),
    };
    if (android.is_some()) {
        auto cache = Vec<lito::tools::cmake::CacheEntry>::make();
        cache.push(lito::tools::cmake::CacheEntry {
            .name  = String::make("ANDROID_ABI"_str),
            .value = android->abi.clone(),
        });
        cache.push(lito::tools::cmake::CacheEntry {
            .name  = String::make("ANDROID_PLATFORM"_str),
            .value = android->platform.clone(),
        });
        cache.push(lito::tools::cmake::CacheEntry {
            .name  = String::make("ANDROID_STL"_str),
            .value = android->standard_library.clone(),
        });
        result.target = Some(lito::tools::cmake::TargetToolchainConfiguration {
            .file     = android->toolchain_file.clone(),
            .cache    = rstd::move(cache),
            .identity = android->identity.clone(),
        });
    }
    return result;
}

auto cmake_profile_configuration(const cpp::ProfileSpec& profile)
    -> lito::tools::cmake::ProfileConfiguration {
    const auto append_flag = [](String& output, ref<str> value) {
        if (value.is_empty()) return;
        if (! output.is_empty()) output.push_ascii(' ');
        output.push_str(value);
    };
    const auto append_ndebug = [&](String& output, const Option<bool>& ndebug) {
        if (ndebug.is_some()) append_flag(output, *ndebug ? "-DNDEBUG"_str : "-UNDEBUG"_str);
    };
    auto c_profile   = cpp::effective_native_profile(profile, lito::manifest::PackageLanguage::C);
    auto cpp_profile = cpp::effective_native_profile(profile, lito::manifest::PackageLanguage::Cpp);
    auto c_flags     = String::make();
    append_flag(c_flags, cpp::cpp_optimization_option(c_profile.optimization));
    append_flag(c_flags, cpp::cpp_debug_option(c_profile.debug_info));
    append_flag(c_flags, cpp::cpp_lto_option(c_profile.compile_lto));
    if (profile.c.common.microsoft_runtime_library.is_some()) {
        append_flag(c_flags,
                    rstd::format("-fms-runtime-lib={}",
                                 lito::compiler::microsoft_runtime_library_name(
                                     *profile.c.common.microsoft_runtime_library))
                        .as_str());
    }
    append_ndebug(c_flags, c_profile.ndebug);
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
    append_flag(cxx_flags, cpp::cpp_optimization_option(cpp_profile.optimization));
    append_flag(cxx_flags, cpp::cpp_debug_option(cpp_profile.debug_info));
    append_flag(cxx_flags, cpp::cpp_lto_option(cpp_profile.compile_lto));
    if (profile.cpp.common.microsoft_runtime_library.is_some()) {
        append_flag(cxx_flags,
                    rstd::format("-fms-runtime-lib={}",
                                 lito::compiler::microsoft_runtime_library_name(
                                     *profile.cpp.common.microsoft_runtime_library))
                        .as_str());
    }
    append_ndebug(cxx_flags, cpp_profile.ndebug);
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
    return lito::tools::cmake::ProfileConfiguration {
        .cxx_standard = profile.cpp.language.standard.clone(),
        .c_flags      = rstd::move(c_flags),
        .cxx_flags    = rstd::move(cxx_flags),
        .linker_flags = rstd::move(linker_flags),
        .msvc_runtime = rstd::move(msvc_runtime),
    };
}

} // namespace lito

export namespace lito
{

auto identify_cmake_provider(lito::dependency::CMakeProviderConfig provider,
                             const ResolvedProcessEnvironment&     environment)
    -> lito::dependency::DependencyResult<lito::dependency::CMakeProviderConfig> {
    auto identified =
        lito::tools::cmake::identify_cmake_provider(cmake_provider(provider), environment);
    if (identified.is_err()) {
        return Err(cmake_error("identify CMake provider"_str, rstd::move(identified).unwrap_err()));
    }
    provider.identity = rstd::move(identified).unwrap().identity;
    return Ok(rstd::move(provider));
}

auto resolve_cmake_package(const Vec<ResolvedCMakeDependencyRequirement>& requirements)
    -> lito::dependency::DependencyResult<ResolvedCMakePackage> {
    if (requirements.is_empty()) {
        return lito::dependency::dependency_failure<ResolvedCMakePackage>(
            "CMake package resolution requires at least one declaration"_str);
    }
    const auto& first  = requirements[usize {}];
    auto        merged = first.clone();
    merged.alias       = first.package.clone();
    merged.components.clear();
    merged.targets.clear();
    merged.host_tools.clear();
    for (const auto& requirement : requirements) {
        if (requirement.package != first.package.as_str()) {
            return lito::dependency::dependency_failure<ResolvedCMakePackage>(
                rstd::format("cannot merge CMake packages '{}' and '{}'",
                             first.package.as_str(),
                             requirement.package.as_str()));
        }
        if (! same_cmake_source(first.source, requirement.source)) {
            return Err(cmake_package_conflict(first.package.as_str(),
                                              first.alias.as_str(),
                                              requirement.alias.as_str(),
                                              "source contract"_str));
        }
        if (! same_optional_path(first.adapter, requirement.adapter) ||
            first.adapter_identity != requirement.adapter_identity.as_str()) {
            return Err(cmake_package_conflict(first.package.as_str(),
                                              first.alias.as_str(),
                                              requirement.alias.as_str(),
                                              "adapter contract"_str));
        }
        if (! same_optional_path(first.config_directory, requirement.config_directory)) {
            return Err(cmake_package_conflict(first.package.as_str(),
                                              first.alias.as_str(),
                                              requirement.alias.as_str(),
                                              "config directory"_str));
        }
        if (! same_cmake_cache(first.cache, requirement.cache)) {
            return Err(cmake_package_conflict(first.package.as_str(),
                                              first.alias.as_str(),
                                              requirement.alias.as_str(),
                                              "build cache contract"_str));
        }
        for (const auto& component : requirement.components) {
            append_unique_string(merged.components, component.as_str());
        }
        for (const auto& target : requirement.targets) {
            auto present = false;
            for (const auto& current : merged.targets) {
                if (current.name == target.name.as_str()) {
                    present = true;
                    break;
                }
            }
            if (! present) {
                merged.targets.push(lito::dependency::CMakeTargetRequirement {
                    .name       = target.name.clone(),
                    .visibility = lito::dependency::DependencyVisibility::Private,
                });
            }
        }
        for (const auto& tool : requirement.host_tools) {
            auto present = false;
            for (const auto& current : merged.host_tools) {
                if (current.name != tool.name.as_str()) continue;
                if (current.target != tool.target.as_str()) {
                    return Err(cmake_package_conflict(first.package.as_str(),
                                                      first.alias.as_str(),
                                                      requirement.alias.as_str(),
                                                      "host tool contract"_str));
                }
                present = true;
                break;
            }
            if (! present) merged.host_tools.push(tool.clone());
        }
    }
    return Ok(ResolvedCMakePackage { .requirement = rstd::move(merged) });
}

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement&    requirement,
                        const lito::dependency::CMakeProviderConfig& provider,
                        const cpp::BuildConfiguration&               configuration,
                        const cpp::ProfileSpec&                      profile,
                        const LinkerIdentity&                        linker,
                        const TargetInfo&                            default_target,
                        ref<str>                                     effective_target,
                        ref<rstd::path::Path>                        profile_cmake_root,
                        usize                                        jobs,
                        const Option<AndroidCmakeProjection>&        android,
                        const Option<PathBuf>&                       find_install_prefix)
    -> lito::dependency::DependencyResult<CMakePackagePlan> {
    if (effective_target != default_target.triple.as_str() && android.is_none()) {
        return lito::dependency::dependency_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' cannot resolve cross target '{}' without an explicit CMake "
            "toolchain file",
            requirement.alias.as_str(),
            effective_target));
    }
    auto planned =
        lito::tools::cmake::plan_cmake_package(cmake_request(requirement, find_install_prefix),
                                               cmake_provider(provider),
                                               cmake_toolchain(configuration, linker, android),
                                               cmake_profile_configuration(profile),
                                               effective_target,
                                               profile_cmake_root,
                                               jobs);
    if (planned.is_err()) {
        return Err(cmake_error("plan CMake dependency"_str, rstd::move(planned).unwrap_err()));
    }
    auto visibilities = Vec<lito::dependency::DependencyVisibility>::make();
    for (const auto& target : requirement.targets) {
        visibilities.push(lito::dependency::DependencyVisibility(target.visibility));
    }
    return Ok(CMakePackagePlan {
        .tool         = rstd::move(planned).unwrap(),
        .visibilities = rstd::move(visibilities),
    });
}

auto execute_cmake_package(const CMakePackagePlan&                      plan,
                           const ResolvedProcessEnvironment&            environment,
                           const Option<lito::tools::cmake::EventSink>& observer)
    -> lito::dependency::DependencyResult<CMakeUsageSnapshot> {
    return cmake_result(
        "execute CMake dependency"_str,
        lito::tools::cmake::execute_cmake_package(plan.tool, environment, observer));
}

auto materialize_cmake_usage_impl(const CMakePackagePlan&                            plan,
                                  const lito::tools::cmake::Request&                 requirement,
                                  const Vec<lito::dependency::DependencyVisibility>& visibilities,
                                  const CMakeUsageSnapshot&                          snapshots)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage> {
    const auto& tool = plan.tool;
    if (snapshots.targets.len() != requirement.targets.len() ||
        visibilities.len() != requirement.targets.len()) {
        return lito::dependency::dependency_failure<cpp::ExternalDependencyUsage>(
            rstd::format("CMake package '{}' usage snapshot has {} targets, expected {}",
                         requirement.package.as_str(),
                         snapshots.targets.len(),
                         requirement.targets.len()));
    }
    auto targets = Vec<cpp::ExternalTargetUsage>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        const auto& target   = requirement.targets[index];
        const auto& snapshot = snapshots.targets[index];
        auto        source   = rstd::format("CMake dependency '{}' package '{}' target '{}'",
                                            requirement.alias.as_str(),
                                            requirement.package.as_str(),
                                            target.name.as_str());
        auto        identity =
            lito::tools::cmake::target_snapshot_identity(tool.provider,
                                                         requirement,
                                                         target.name.as_str(),
                                                         snapshots.version.as_str(),
                                                         snapshot,
                                                         tool.effective_target.as_str());
        if (identity.is_err()) {
            return Err(
                cmake_error("identify CMake target usage"_str, rstd::move(identity).unwrap_err()));
        }
        targets.push(cpp::ExternalTargetUsage {
            .name            = target.name.clone(),
            .visibility      = visibilities[index],
            .compile_options = as<Clone>(snapshot.compile).clone(),
            .compile_source  = rstd::move(source),
            .identity        = rstd::move(identity).unwrap(),
        });
    }
    auto identity = lito::tools::cmake::dependency_identity(tool.provider,
                                                            requirement,
                                                            snapshots.version.as_str(),
                                                            snapshots,
                                                            tool.effective_target.as_str());
    if (identity.is_err()) {
        return Err(
            cmake_error("identify CMake dependency usage"_str, rstd::move(identity).unwrap_err()));
    }
    auto host_tools = Vec<cpp::ExternalHostToolUsage>::make();
    for (const auto& snapshot : snapshots.host_tools) {
        auto tool_identity =
            licrypto::sha256_hex(rstd::format("lito-cmake-host-tool-v1\n{}\n{}\n{}\n{}\n{}",
                                              identity->as_str(),
                                              snapshot.name.as_str(),
                                              snapshot.target.as_str(),
                                              snapshot.executable.as_path(),
                                              snapshot.digest.as_str())
                                     .as_str());
        host_tools.push(cpp::ExternalHostToolUsage {
            .name       = snapshot.name.clone(),
            .target     = snapshot.target.clone(),
            .executable = snapshot.executable.clone(),
            .digest     = snapshot.digest.clone(),
            .identity   = rstd::move(tool_identity),
        });
    }
    auto links = lito::tools::cmake::materialize_link_tokens(snapshots.combined.link,
                                                             tool.area.query_build.as_path());
    if (links.is_err()) {
        return Err(cmake_error("materialize CMake link usage"_str, rstd::move(links).unwrap_err()));
    }
    return Ok(cpp::ExternalDependencyUsage {
        .alias      = requirement.alias.clone(),
        .provider   = String::make("cmake"_str),
        .version    = snapshots.version.clone(),
        .targets    = rstd::move(targets),
        .host_tools = rstd::move(host_tools),
        .link_arguments =
            lito::link::ArgumentSequence {
                .tokens   = rstd::move(links).unwrap(),
                .source   = rstd::format("CMake dependency '{}' package '{}'",
                                         requirement.alias.as_str(),
                                         requirement.package.as_str()),
                .identity = identity->clone(),
            },
        .identity = rstd::move(identity).unwrap(),
    });
}

auto materialize_cmake_usage(const CMakePackagePlan& plan, const CMakeUsageSnapshot& snapshots)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage> {
    return materialize_cmake_usage_impl(plan, plan.tool.requirement, plan.visibilities, snapshots);
}

auto materialize_cmake_usage(const CMakePackagePlan&                   plan,
                             const CMakeUsageSnapshot&                 snapshots,
                             const ResolvedCMakeDependencyRequirement& consumer)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage> {
    if (snapshots.targets.len() != plan.tool.requirement.targets.len()) {
        return lito::dependency::dependency_failure<cpp::ExternalDependencyUsage>(
            rstd::format("CMake package '{}' usage snapshot has {} targets, expected {}",
                         plan.tool.requirement.package.as_str(),
                         snapshots.targets.len(),
                         plan.tool.requirement.targets.len()));
    }
    auto requirement       = cmake_request(consumer, plan.tool.requirement.find_install_prefix);
    requirement.components = as<Clone>(plan.tool.requirement.components).clone();
    auto projected_targets = Vec<lito::tools::cmake::CMakeTargetUsageSnapshot>::make();
    auto visibilities      = Vec<lito::dependency::DependencyVisibility>::make();
    auto projected_link    = Vec<String>::make();
    for (const auto& target : consumer.targets) {
        auto index = Option<usize> {};
        for (usize candidate {}; candidate < plan.tool.requirement.targets.len(); ++candidate) {
            if (plan.tool.requirement.targets[candidate].name == target.name.as_str()) {
                index = Some(candidate);
                break;
            }
        }
        if (index.is_none()) {
            return lito::dependency::dependency_failure<cpp::ExternalDependencyUsage>(
                rstd::format("CMake package '{}' projection is missing target '{}'",
                             consumer.package.as_str(),
                             target.name.as_str()));
        }
        const auto& snapshot = snapshots.targets[*index];
        projected_targets.push(lito::tools::cmake::CMakeTargetUsageSnapshot {
            .compile = as<Clone>(snapshot.compile).clone(),
            .link    = as<Clone>(snapshot.link).clone(),
        });
        for (const auto& token : snapshot.link) projected_link.push(token.clone());
        visibilities.push(lito::dependency::DependencyVisibility(target.visibility));
    }
    auto projected_tools = Vec<lito::tools::cmake::CMakeHostToolSnapshot>::make();
    for (const auto& tool : consumer.host_tools) {
        const lito::tools::cmake::CMakeHostToolSnapshot* selected = nullptr;
        for (const auto& snapshot : snapshots.host_tools) {
            if (snapshot.name == tool.name.as_str() && snapshot.target == tool.target.as_str()) {
                selected = rstd::addressof(snapshot);
                break;
            }
        }
        if (selected == nullptr) {
            return lito::dependency::dependency_failure<cpp::ExternalDependencyUsage>(
                rstd::format("CMake package '{}' projection is missing host tool '{}'",
                             consumer.package.as_str(),
                             tool.name.as_str()));
        }
        projected_tools.push(lito::tools::cmake::CMakeHostToolSnapshot {
            .name       = selected->name.clone(),
            .target     = selected->target.clone(),
            .executable = selected->executable.clone(),
            .digest     = selected->digest.clone(),
        });
    }
    auto projected_assets = Vec<ExternalAssetSet>::with_capacity(snapshots.assets.len());
    for (const auto& asset : snapshots.assets) projected_assets.push(asset.clone());
    auto projected = CMakeUsageSnapshot {
        .version    = snapshots.version.clone(),
        .targets    = rstd::move(projected_targets),
        .host_tools = rstd::move(projected_tools),
        .combined =
            lito::tools::cmake::CMakeTargetUsageSnapshot {
                .compile = Vec<String>::make(),
                .link    = rstd::move(projected_link),
            },
        .assets = rstd::move(projected_assets),
    };
    return materialize_cmake_usage_impl(plan, requirement, visibilities, projected);
}

} // namespace lito
