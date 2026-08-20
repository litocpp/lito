module;
#include <rstd/macro.hpp>

export module lito.toolchain.cmake:invocation;

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain.common;
import lito.system;
import :model;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

auto cmake_process_environment() -> CommandEnvironment {
    constexpr lito::config::ToolchainEnvironmentVariable variables[] = {
        lito::config::ToolchainEnvironmentVariable::CFlags,
        lito::config::ToolchainEnvironmentVariable::CxxFlags,
        lito::config::ToolchainEnvironmentVariable::LdFlags,
    };
    auto result = CommandEnvironment {};
    for (auto variable : variables) {
        result.entries.push(CommandEnvironmentEntry {
            .key = String::make(lito::config::toolchain_environment_variable_name(variable)),
        });
    }
    return result;
}

auto invoke_cmake(const Vec<String>&                arguments,
                  const ResolvedProcessEnvironment& environment,
                  Option<ref<rstd::path::Path>>     working_directory = None(),
                  bool stream_output = true) -> SystemResult<CommandOutput> {
    auto overrides    = cmake_process_environment();
    auto override_ref = Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(overrides)));
    if (! stream_output) {
        return run_command(arguments, environment, working_directory, override_ref);
    }
    auto observer = rstd::process::OutputObserver {
        .notify =
            +[](void*, rstd::process::OutputStream, slice<u8> chunk) noexcept {
                auto output = rstd::io::stderr();
                (void)rstd::io::write_all(output, chunk);
            },
    };
    return run_command_observed(arguments, environment, observer, working_directory, override_ref);
}

auto run_cmake(Vec<String>                       arguments,
               ref<str>                          operation,
               const ResolvedProcessEnvironment& environment,
               Option<ref<rstd::path::Path>>     working_directory = None(),
               bool stream_output = true) -> lito::dependency::DependencyResult<empty> {
    auto output = invoke_cmake(arguments, environment, working_directory, stream_output);
    if (output.is_err()) {
        return Err(lito::dependency::DependencyError::Operation(String::make(operation),
                                                                rstd::move(output).unwrap_err()));
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
                        ref<str> context) -> lito::dependency::DependencyResult<empty> {
    auto text = path_text(path, context);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    auto argument = String::make(prefix);
    argument.push_str(text->as_str());
    arguments.push(rstd::move(argument));
    return Ok(empty {});
}

auto push_cmake_toolchain(Vec<String>&                   arguments,
                          const cpp::BuildConfiguration& configuration,
                          const LinkerIdentity&          linker)
    -> lito::dependency::DependencyResult<empty> {
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_C_COMPILER="_str,
                                configuration.toolchain.cc.as_path(),
                                "C compiler"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_COMPILER="_str,
                                configuration.toolchain.cxx.as_path(),
                                "C++ compiler"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_C_USING_LINKER_lito_configured=-fuse-ld="_str,
                                linker.executable.as_path(),
                                "linker"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_USING_LINKER_lito_configured=-fuse-ld="_str,
                                linker.executable.as_path(),
                                "linker"_str));
    arguments.push(String::make("-DCMAKE_LINKER_TYPE=lito_configured"_str));
    rstd_try(push_path_argument(
        arguments, "-DCMAKE_AR="_str, configuration.toolchain.ar.as_path(), "archiver"_str));
    return Ok(empty {});
}

auto push_cmake_search_path(Vec<String>&                                 arguments,
                            const lito::dependency::CMakeProviderConfig& provider)
    -> lito::dependency::DependencyResult<empty> {
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

auto cmake_generator_uses_multiple_configurations(ref<str> generator) noexcept -> bool {
    return generator == "Ninja Multi-Config"_str || generator == "Xcode"_str ||
           generator.starts_with("Visual Studio "_str);
}

auto push_cmake_profile_configuration(Vec<String>&                     arguments,
                                      const CMakeProfileConfiguration& profile,
                                      ref<str>                         generator) -> void {
    const auto multi_config = cmake_generator_uses_multiple_configurations(generator);
    if (! multi_config) {
        arguments.push(rstd::format("-DCMAKE_BUILD_TYPE={}", profile.build_type.as_str()));
    }
    if (! profile.neutral_configuration) return;
    if (multi_config) arguments.push(String::make("-DCMAKE_CONFIGURATION_TYPES=None"_str));
    arguments.push(String::make("-DCMAKE_C_FLAGS_NONE="_str));
    arguments.push(String::make("-DCMAKE_CXX_FLAGS_NONE="_str));
    arguments.push(String::make("-DCMAKE_EXE_LINKER_FLAGS_NONE="_str));
    arguments.push(String::make("-DCMAKE_SHARED_LINKER_FLAGS_NONE="_str));
    arguments.push(String::make("-DCMAKE_MODULE_LINKER_FLAGS_NONE="_str));
}

auto source_install_receipt(const CMakeWorkArea& area) -> PathBuf {
    return area.root.join(PathBuf::from("install-receipt-v1"_str).as_path());
}

auto source_install_current(const CMakeWorkArea& area) -> lito::dependency::DependencyResult<bool> {
    auto marker = source_install_receipt(area);
    auto ready  = rstd::fs::exists(marker.as_path());
    if (ready.is_err()) {
        return cmake_io_failure<bool>(
            "inspect CMake install receipt"_str, marker.as_path(), rstd::move(ready).unwrap_err());
    }
    if (! *ready) return Ok(false);
    auto contents = rstd::fs::read_to_string(marker.as_path());
    if (contents.is_err()) {
        return cmake_io_failure<bool>(
            "read CMake install receipt"_str, marker.as_path(), rstd::move(contents).unwrap_err());
    }
    if (contents->as_str() != "lito-cmake-install-receipt-v1\n"_str) {
        return cmake_failure<bool>(
            rstd::format("CMake install receipt '{}' has invalid contents", marker.as_path()));
    }
    return Ok(true);
}

auto configure_source(const ResolvedCMakeDependencyRequirement&    requirement,
                      const lito::dependency::CMakeProviderConfig& provider,
                      const cpp::BuildConfiguration&               configuration,
                      const cpp::ProfileSpec&                      profile,
                      const LinkerIdentity&                        linker,
                      const CMakeWorkArea&                         area,
                      const ResolvedProcessEnvironment&            environment)
    -> lito::dependency::DependencyResult<empty> {
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
    rstd_try(push_cmake_toolchain(arguments, configuration, linker));
    auto cmake_profile = cmake_profile_configuration(profile);
    push_cmake_profile_configuration(arguments, cmake_profile, provider.generator.as_str());
    arguments.push(rstd::format("-DCMAKE_CXX_STANDARD={}",
                                cmake_cxx_standard(profile.cpp.language.standard.as_str())));
    arguments.push(String::make("-DCMAKE_CXX_EXTENSIONS=OFF"_str));
    arguments.push(rstd::format("-DCMAKE_C_FLAGS={}", cmake_profile.c_flags.as_str()));
    arguments.push(rstd::format("-DCMAKE_CXX_FLAGS={}", cmake_profile.cxx_flags.as_str()));
    if (! cmake_profile.msvc_runtime.is_empty()) {
        arguments.push(
            rstd::format("-DCMAKE_MSVC_RUNTIME_LIBRARY={}", cmake_profile.msvc_runtime.as_str()));
    }
    arguments.push(
        rstd::format("-DCMAKE_EXE_LINKER_FLAGS={}", cmake_profile.linker_flags.as_str()));
    arguments.push(
        rstd::format("-DCMAKE_SHARED_LINKER_FLAGS={}", cmake_profile.linker_flags.as_str()));
    arguments.push(
        rstd::format("-DCMAKE_MODULE_LINKER_FLAGS={}", cmake_profile.linker_flags.as_str()));
    for (const auto& entry : requirement.cache) {
        arguments.push(rstd::format("-D{}={}", entry.name.as_str(), entry.value.as_str()));
    }
    rstd_try(run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' configure", requirement.package.as_str()).as_str(),
        environment));
    return Ok(empty {});
}

auto build_source(const ResolvedCMakeDependencyRequirement&    requirement,
                  const lito::dependency::CMakeProviderConfig& provider,
                  const cpp::ProfileSpec&                      profile,
                  const CMakeWorkArea&                         area,
                  usize                                        jobs,
                  const ResolvedProcessEnvironment&            environment)
    -> lito::dependency::DependencyResult<empty> {
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

auto install_source(const ResolvedCMakeDependencyRequirement&    requirement,
                    const lito::dependency::CMakeProviderConfig& provider,
                    const cpp::ProfileSpec&                      profile,
                    const CMakeWorkArea&                         area,
                    bool                                         publish_receipt,
                    const ResolvedProcessEnvironment&            environment)
    -> lito::dependency::DependencyResult<empty> {
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
        return cmake_io_failure<empty>(
            "write CMake install receipt"_str, marker.as_path(), rstd::move(marked).unwrap_err());
    }
    return Ok(empty {});
}

auto probe_project(const ResolvedCMakeDependencyRequirement& requirement, const CMakeWorkArea& area)
    -> lito::dependency::DependencyResult<String> {
    auto result = String::make(R"cmake(cmake_minimum_required(VERSION 3.29)
project(lito_cmake_probe LANGUAGES C CXX)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
set(_LITO_ASSET_RECEIPT "${CMAKE_BINARY_DIR}/lito-assets-v2.txt")
file(WRITE "${_LITO_ASSET_RECEIPT}" "lito-cmake-assets-v2\n")
function(lito_export_asset_set)
  cmake_parse_arguments(LITO_ASSET "PROVIDED" "NAME;ROOT" "FILES" ${ARGN})
  if(NOT LITO_ASSET_NAME)
    message(FATAL_ERROR "lito_export_asset_set requires NAME")
  endif()
  if(LITO_ASSET_NAME MATCHES "[\t\r\n]")
    message(FATAL_ERROR "Lito asset set name contains control characters")
  endif()
  if(LITO_ASSET_PROVIDED)
    if(LITO_ASSET_ROOT OR LITO_ASSET_FILES)
      message(FATAL_ERROR "provided Lito asset set cannot declare ROOT or FILES")
    endif()
    file(APPEND "${_LITO_ASSET_RECEIPT}" "set\t${LITO_ASSET_NAME}\tprovided\n")
    return()
  endif()
  if(NOT LITO_ASSET_ROOT OR NOT LITO_ASSET_FILES)
    message(FATAL_ERROR "materialized Lito asset set requires ROOT and FILES")
  endif()
  file(APPEND "${_LITO_ASSET_RECEIPT}" "set\t${LITO_ASSET_NAME}\tmaterialized\n")
  foreach(_lito_asset_file IN LISTS LITO_ASSET_FILES)
    if(IS_ABSOLUTE "${_lito_asset_file}" OR
       _lito_asset_file MATCHES "(^|/)\.\.?(/|$)" OR
       _lito_asset_file MATCHES "[\t\r\n]")
      message(FATAL_ERROR "Lito asset path is not a normal relative path: ${_lito_asset_file}")
    endif()
    cmake_path(ABSOLUTE_PATH _lito_asset_file
               BASE_DIRECTORY "${LITO_ASSET_ROOT}"
               NORMALIZE
               OUTPUT_VARIABLE _lito_asset_source)
    if(NOT EXISTS "${_lito_asset_source}" OR IS_DIRECTORY "${_lito_asset_source}")
      message(FATAL_ERROR "Lito asset source is not a file: ${_lito_asset_source}")
    endif()
    file(APPEND "${_LITO_ASSET_RECEIPT}"
         "entry\t${LITO_ASSET_NAME}\t${_lito_asset_file}\t${_lito_asset_source}\n")
  endforeach()
endfunction()
)cmake"_str);
    if (requirement.adapter.is_some()) {
        result.push_str("set(LITO_CMAKE_DEPENDENCY_MODE \""_str);
        result.push_str(requirement.source.is_Find() ? "find"_str : "source"_str);
        result.push_str("\")\nset(LITO_CMAKE_DEPENDENCY_PACKAGE \""_str);
        result.push_str(requirement.package.as_str());
        result.push_str("\")\n"_str);
        if (requirement.source.is_Directory()) {
            auto source = path_text(area.source.as_path(), "CMake dependency source"_str);
            if (source.is_err()) return Err(rstd::move(source).unwrap_err());
            auto quoted_source = cmake_quoted(source->as_str(), "CMake dependency source"_str);
            if (quoted_source.is_err()) return Err(rstd::move(quoted_source).unwrap_err());
            result.push_str("set(LITO_CMAKE_DEPENDENCY_SOURCE_DIR "_str);
            result.push_str(quoted_source->as_str());
            result.push_str(")\n"_str);
        }
        auto adapter = path_text(requirement.adapter->as_path(), "CMake adapter"_str);
        if (adapter.is_err()) return Err(rstd::move(adapter).unwrap_err());
        auto quoted_adapter = cmake_quoted(adapter->as_str(), "CMake adapter"_str);
        if (quoted_adapter.is_err()) return Err(rstd::move(quoted_adapter).unwrap_err());
        result.push_str("include("_str);
        result.push_str(quoted_adapter->as_str());
        result.push_str(")\n"_str);
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
    auto mode  = requirement.source.is_Find() ? "find"_str : "source"_str;
    auto owner = requirement.adapter.is_some() ? "adapter"_str : "generic"_str;
    for (const auto& target : requirement.targets) {
        result.push_str("if(NOT TARGET "_str);
        result.push_str(target.name.as_str());
        result.push_str(")\n  message(FATAL_ERROR \"CMake dependency "_str);
        result.push_str(requirement.package.as_str());
        result.push_str(" (alias "_str);
        result.push_str(requirement.alias.as_str());
        result.push_str(", "_str);
        result.push_str(mode);
        result.push_ascii(' ');
        result.push_str(owner);
        result.push_str(") required target "_str);
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
                       const CMakeWorkArea& area) -> lito::dependency::DependencyResult<empty> {
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
            return cmake_io_failure<empty>("create CMake directory"_str,
                                           directory.as_path(),
                                           rstd::move(created).unwrap_err());
        }
    }
    auto cmake_lists = area.query_source.join(PathBuf::from("CMakeLists.txt"_str).as_path());
    auto source      = area.query_source.join(PathBuf::from("probe.cpp"_str).as_path());
    auto query_file  = query.join(PathBuf::from("query.json"_str).as_path());
    auto project     = probe_project(requirement, area);
    if (project.is_err()) return Err(rstd::move(project).unwrap_err());
    auto written = rstd::fs::write_atomic(cmake_lists.as_path(), project->as_str().as_bytes());
    if (written.is_err()) {
        return cmake_io_failure<empty>("write CMake probe project"_str,
                                       cmake_lists.as_path(),
                                       rstd::move(written).unwrap_err());
    }
    written =
        rstd::fs::write_atomic(source.as_path(), ("int main() { return 0; }\n"_str).as_bytes());
    if (written.is_err()) {
        return cmake_io_failure<empty>(
            "write CMake probe source"_str, source.as_path(), rstd::move(written).unwrap_err());
    }
    written = rstd::fs::write_atomic(
        query_file.as_path(),
        ("{\"requests\":[{\"kind\":\"codemodel\",\"version\":2}]}\n"_str).as_bytes());
    if (written.is_err()) {
        return cmake_io_failure<empty>("write CMake File API query"_str,
                                       query_file.as_path(),
                                       rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto configure_probe(const ResolvedCMakeDependencyRequirement&    requirement,
                     const lito::dependency::CMakeProviderConfig& provider,
                     const cpp::BuildConfiguration&               configuration,
                     const cpp::ProfileSpec&                      profile,
                     const LinkerIdentity&                        linker,
                     const CMakeWorkArea&                         area,
                     const ResolvedProcessEnvironment&            environment)
    -> lito::dependency::DependencyResult<empty> {
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
    rstd_try(push_cmake_toolchain(arguments, configuration, linker));
    auto cmake_profile = cmake_profile_configuration(profile);
    push_cmake_profile_configuration(arguments, cmake_profile, provider.generator.as_str());
    arguments.push(rstd::format("-DCMAKE_CXX_STANDARD={}",
                                cmake_cxx_standard(profile.cpp.language.standard.as_str())));
    arguments.push(String::make("-DCMAKE_CXX_EXTENSIONS=OFF"_str));
    arguments.push(rstd::format("-DCMAKE_C_FLAGS={}", cmake_profile.c_flags.as_str()));
    arguments.push(rstd::format("-DCMAKE_CXX_FLAGS={}", cmake_profile.cxx_flags.as_str()));
    if (! cmake_profile.msvc_runtime.is_empty()) {
        arguments.push(
            rstd::format("-DCMAKE_MSVC_RUNTIME_LIBRARY={}", cmake_profile.msvc_runtime.as_str()));
    }
    arguments.push(
        rstd::format("-DCMAKE_EXE_LINKER_FLAGS={}", cmake_profile.linker_flags.as_str()));
    arguments.push(
        rstd::format("-DCMAKE_SHARED_LINKER_FLAGS={}", cmake_profile.linker_flags.as_str()));
    arguments.push(
        rstd::format("-DCMAKE_MODULE_LINKER_FLAGS={}", cmake_profile.linker_flags.as_str()));
    if (requirement.source.is_Directory() && requirement.adapter.is_none()) {
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
    if (requirement.source.is_Directory() && requirement.adapter.is_some()) {
        for (const auto& entry : requirement.cache) {
            arguments.push(rstd::format("-D{}={}", entry.name.as_str(), entry.value.as_str()));
        }
    }
    return run_cmake(
        rstd::move(arguments),
        rstd::format("CMake package '{}' query", requirement.package.as_str()).as_str(),
        environment);
}

auto build_probe(const ResolvedCMakeDependencyRequirement&    requirement,
                 const lito::dependency::CMakeProviderConfig& provider,
                 const cpp::BuildConfiguration&,
                 const cpp::ProfileSpec&           profile,
                 const CMakeWorkArea&              area,
                 usize                             jobs,
                 const ResolvedProcessEnvironment& environment)
    -> lito::dependency::DependencyResult<empty> {
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

} // namespace lito
