module;
#include <rstd/macro.hpp>

export module lito.dependency.cmake.invocation;

import rstd;
import lito.error;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.dependency.contract;
import lito.dependency.error_contract;
import lito.cpp;
import lito.system.process;
import lito.system.environment;
import lito.dependency.cmake.model;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto run_cmake(Vec<String>                       arguments,
               ref<str>                          operation,
               const ResolvedProcessEnvironment& environment,
               Option<ref<rstd::path::Path>>     working_directory = None(),
               bool                              stream_output = true) -> DependencyResult<empty> {
    auto output = [&]() -> SystemResult<CommandOutput> {
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
        return Err(
            DependencyError::Operation(String::make(operation), rstd::move(output).unwrap_err()));
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
                        ref<str>              context) -> DependencyResult<empty> {
    auto text = path_text(path, context);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    auto argument = String::make(prefix);
    argument.push_str(text->as_str());
    arguments.push(rstd::move(argument));
    return Ok(empty {});
}

auto push_cmake_toolchain(Vec<String>& arguments, const BuildConfiguration& configuration)
    -> DependencyResult<empty> {
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_C_COMPILER="_str,
                                configuration.toolchain.cc.as_path(),
                                "C compiler"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_COMPILER="_str,
                                configuration.toolchain.cxx.as_path(),
                                "C++ compiler"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_C_USING_LINKER_lito_lld=-fuse-ld="_str,
                                configuration.toolchain.ld.as_path(),
                                "LLD linker"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_USING_LINKER_lito_lld=-fuse-ld="_str,
                                configuration.toolchain.ld.as_path(),
                                "LLD linker"_str));
    arguments.push(String::make("-DCMAKE_LINKER_TYPE=lito_lld"_str));
    rstd_try(push_path_argument(
        arguments, "-DCMAKE_AR="_str, configuration.toolchain.ar.as_path(), "archiver"_str));
    return Ok(empty {});
}

auto push_cmake_search_path(Vec<String>& arguments, const CMakeProviderConfig& provider)
    -> DependencyResult<empty> {
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

auto source_install_current(const CMakeWorkArea& area) -> DependencyResult<bool> {
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

auto configure_source(const ResolvedCMakeDependencyRequirement& requirement,
                      const CMakeProviderConfig&                provider,
                      const BuildConfiguration&                 configuration,
                      const ProfileSpec&                        profile,
                      const CMakeWorkArea&                      area,
                      const ResolvedProcessEnvironment& environment) -> DependencyResult<empty> {
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
                  const ResolvedProcessEnvironment& environment) -> DependencyResult<empty> {
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
                    const ResolvedProcessEnvironment& environment) -> DependencyResult<empty> {
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
    -> DependencyResult<String> {
    auto result = String::make(
        "cmake_minimum_required(VERSION 3.29)\n"
        "project(lito_cmake_probe LANGUAGES C CXX)\n"
        "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
        "set(_LITO_ASSET_RECEIPT \"${CMAKE_BINARY_DIR}/lito-assets-v1.txt\")\n"
        "file(WRITE \"${_LITO_ASSET_RECEIPT}\" \"lito-cmake-assets-v1\\n\")\n"
        "function(lito_export_asset_set)\n"
        "  cmake_parse_arguments(LITO_ASSET \"\" \"NAME;ROOT\" \"FILES\" ${ARGN})\n"
        "  if(NOT LITO_ASSET_NAME OR NOT LITO_ASSET_ROOT)\n"
        "    message(FATAL_ERROR \"lito_export_asset_set requires NAME and ROOT\")\n"
        "  endif()\n"
        "  if(LITO_ASSET_NAME MATCHES \"[\\t\\r\\n]\")\n"
        "    message(FATAL_ERROR \"Lito asset set name contains control characters\")\n"
        "  endif()\n"
        "  foreach(_lito_asset_file IN LISTS LITO_ASSET_FILES)\n"
        "    if(IS_ABSOLUTE \"${_lito_asset_file}\" OR "
        "_lito_asset_file MATCHES \"(^|/)\\.\\.?(/|$)\" OR "
        "_lito_asset_file MATCHES \"[\\t\\r\\n]\")\n"
        "      message(FATAL_ERROR \"Lito asset path is not a normal relative path: "
        "${_lito_asset_file}\")\n"
        "    endif()\n"
        "    cmake_path(ABSOLUTE_PATH _lito_asset_file BASE_DIRECTORY \"${LITO_ASSET_ROOT}\" "
        "NORMALIZE OUTPUT_VARIABLE _lito_asset_source)\n"
        "    if(NOT EXISTS \"${_lito_asset_source}\" OR IS_DIRECTORY \"${_lito_asset_source}\")\n"
        "      message(FATAL_ERROR \"Lito asset source is not a file: ${_lito_asset_source}\")\n"
        "    endif()\n"
        "    file(APPEND \"${_LITO_ASSET_RECEIPT}\" "
        "\"${LITO_ASSET_NAME}\\t${_lito_asset_file}\\t${_lito_asset_source}\\n\")\n"
        "  endforeach()\n"
        "endfunction()\n"_str);
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
                       const CMakeWorkArea&                      area) -> DependencyResult<empty> {
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

auto configure_probe(const ResolvedCMakeDependencyRequirement& requirement,
                     const CMakeProviderConfig&                provider,
                     const BuildConfiguration&                 configuration,
                     const ProfileSpec&                        profile,
                     const CMakeWorkArea&                      area,
                     const ResolvedProcessEnvironment& environment) -> DependencyResult<empty> {
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
                 const ResolvedProcessEnvironment& environment) -> DependencyResult<empty> {
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
