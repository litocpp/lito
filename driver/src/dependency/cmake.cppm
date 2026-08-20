module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.driver:dependency.cmake;

import rstd;
import lito.tools;
import lito.tools.cmake;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.common;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito
{

class ResolvedCMakeDependencySource {
    RSTD_ENUM(ResolvedCMakeDependencySource,
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)))
};

struct ResolvedCMakeDependencyRequirement {
    String                                        alias;
    String                                        package;
    Vec<String>                                   components;
    ResolvedCMakeDependencySource                 source;
    Option<PathBuf>                               adapter;
    String                                        adapter_identity;
    Option<PathBuf>                               config_directory;
    Vec<lito::dependency::CMakeCacheEntry>        cache;
    Vec<lito::dependency::CMakeTargetRequirement> targets;
};

using ExternalAssetEntry       = lito::tools::cmake::ExternalAssetEntry;
using ExternalAssetDisposition = lito::tools::cmake::ExternalAssetDisposition;
using ExternalAssetSet         = lito::tools::cmake::ExternalAssetSet;
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

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement&    requirement,
                        const lito::dependency::CMakeProviderConfig& provider,
                        const cpp::BuildConfiguration&               configuration,
                        const cpp::ProfileSpec&                      profile,
                        const LinkerIdentity&                        linker,
                        const TargetInfo&                            default_target,
                        ref<str>                                     effective_target,
                        ref<rstd::path::Path>                        profile_cmake_root,
                        usize                                        jobs = usize(1))
    -> lito::dependency::DependencyResult<CMakePackagePlan>;

auto execute_cmake_package(const CMakePackagePlan&                      plan,
                           const ResolvedProcessEnvironment&            environment,
                           const Option<lito::tools::cmake::EventSink>& observer = None())
    -> lito::dependency::DependencyResult<CMakeUsageSnapshot>;

auto materialize_cmake_usage(const CMakePackagePlan& plan, const CMakeUsageSnapshot& snapshots)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage>;

} // namespace lito

namespace lito
{

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

auto cmake_request(const ResolvedCMakeDependencyRequirement& requirement)
    -> lito::tools::cmake::Request {
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
    auto result = lito::tools::cmake::Request {
        .alias            = requirement.alias.clone(),
        .package          = requirement.package.clone(),
        .components       = as<Clone>(requirement.components).clone(),
        .source           = cmake_source(requirement.source),
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

auto cmake_provider(const lito::dependency::CMakeProviderConfig& provider)
    -> lito::tools::cmake::Provider {
    return lito::tools::cmake::Provider {
        .executable   = provider.executable.clone(),
        .identity     = provider.identity.clone(),
        .generator    = provider.generator.clone(),
        .search_paths = as<Clone>(provider.search_paths).clone(),
    };
}

auto cmake_toolchain(const cpp::BuildConfiguration& configuration, const LinkerIdentity& linker)
    -> lito::tools::cmake::ToolchainConfiguration {
    return lito::tools::cmake::ToolchainConfiguration {
        .cc              = configuration.toolchain.cc.clone(),
        .cxx             = configuration.toolchain.cxx.clone(),
        .linker          = linker.executable.clone(),
        .linker_identity = linker.build_identity.clone(),
        .archiver        = configuration.toolchain.ar.clone(),
    };
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
    auto build_type = "None"_str;
    switch (profile.family) {
    case lito::manifest::BuildProfileFamily::Debug: build_type = "Debug"_str; break;
    case lito::manifest::BuildProfileFamily::Release: build_type = "Release"_str; break;
    case lito::manifest::BuildProfileFamily::Plain: break;
    }
    return lito::tools::cmake::ProfileConfiguration {
        .cxx_standard          = profile.cpp.language.standard.clone(),
        .build_type            = String::make(build_type),
        .c_flags               = rstd::move(c_flags),
        .cxx_flags             = rstd::move(cxx_flags),
        .linker_flags          = rstd::move(linker_flags),
        .msvc_runtime          = rstd::move(msvc_runtime),
        .neutral_configuration = profile.family == lito::manifest::BuildProfileFamily::Plain,
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

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement&    requirement,
                        const lito::dependency::CMakeProviderConfig& provider,
                        const cpp::BuildConfiguration&               configuration,
                        const cpp::ProfileSpec&                      profile,
                        const LinkerIdentity&                        linker,
                        const TargetInfo&                            default_target,
                        ref<str>                                     effective_target,
                        ref<rstd::path::Path>                        profile_cmake_root,
                        usize jobs) -> lito::dependency::DependencyResult<CMakePackagePlan> {
    if (effective_target != default_target.triple.as_str()) {
        return lito::dependency::dependency_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' cannot resolve cross target '{}' without an explicit CMake "
            "toolchain file",
            requirement.alias.as_str(),
            effective_target));
    }
    auto planned = lito::tools::cmake::plan_cmake_package(cmake_request(requirement),
                                                          cmake_provider(provider),
                                                          cmake_toolchain(configuration, linker),
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

auto materialize_cmake_usage(const CMakePackagePlan& plan, const CMakeUsageSnapshot& snapshots)
    -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage> {
    const auto& tool        = plan.tool;
    const auto& requirement = tool.requirement;
    if (snapshots.targets.len() != requirement.targets.len() ||
        plan.visibilities.len() != requirement.targets.len()) {
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
            .visibility      = plan.visibilities[index],
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
    auto links = lito::tools::cmake::materialize_link_tokens(snapshots.combined.link,
                                                             tool.area.query_build.as_path());
    if (links.is_err()) {
        return Err(cmake_error("materialize CMake link usage"_str, rstd::move(links).unwrap_err()));
    }
    return Ok(cpp::ExternalDependencyUsage {
        .alias    = requirement.alias.clone(),
        .provider = String::make("cmake"_str),
        .version  = snapshots.version.clone(),
        .targets  = rstd::move(targets),
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

} // namespace lito
