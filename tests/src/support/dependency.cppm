module;

#include <rstd/macro.hpp>

export module lito.test.support.dependency;

import rstd;
import lito.cpp;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.base_support;
import lito.test.support.project;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto has_external_macro(const lito::cpp::CompileContext& context) -> bool {
    if (! context.language.is_Cpp()) return false;
    for (const auto& macro : context.language.as_Cpp().options.preprocessor.macros) {
        if (macro.value.as_str() == "LITO_EXTERNAL_USAGE=1"_str) return true;
    }
    return false;
}

auto pkg_config_target() -> lito::system::TargetInfo {
    return lito::system::TargetInfo {
        .triple = String::make("x86_64-unknown-linux-gnu"_str),
        .architecture =
            lito::system::Architecture {
                .name = String::make("x86_64"_str),
            },
        .os     = String::make("linux"_str),
        .family = lito::system::TargetFamily::Unix,
    };
}

auto native_platform() -> lito::system::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::system::resolve_build_platform(
        lito::system::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        None());
    return rstd::move(resolved).unwrap();
}

auto explicit_platform(ref<str> target_triple) -> lito::system::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::system::resolve_build_platform(
        lito::system::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        Some(target_triple));
    return rstd::move(resolved).unwrap();
}

auto default_profile(const lito::cpp::CppArgumentParser& parser) -> lito::cpp::ProfileSpec {
    auto profile = lito::cpp::make_profile_spec(
        configuration(), lito::manifest::ProjectProfile {}, build_profile("debug"_str), parser);
    return rstd::move(profile).unwrap();
}

auto fixture_cmake() -> lito::dependency::CMakeProviderConfig {
    return lito::dependency::CMakeProviderConfig {
        .executable = rstd::path::PathBuf::from("cmake"_str),
        .generator  = String::make("Ninja"_str),
    };
}

auto cmake_package_project_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "CMakeLists.txt"_str, R"(cmake_minimum_required(VERSION 3.28)
project(LitoFixture VERSION 1.2.3 LANGUAGES CXX)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
if(DEFINED LITO_FIXTURE_CONFIGURE_COUNT)
  file(APPEND "${LITO_FIXTURE_CONFIGURE_COUNT}" "configure\n")
endif()
add_library(lito_fixture STATIC src/fixture.cpp)
set_target_properties(lito_fixture PROPERTIES EXPORT_NAME fixture)
target_compile_features(lito_fixture PUBLIC cxx_std_20)
target_compile_definitions(lito_fixture INTERFACE LITO_CMAKE_USAGE=1)
target_include_directories(lito_fixture PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
add_library(lito_fixture_headers INTERFACE)
set_target_properties(lito_fixture_headers PROPERTIES EXPORT_NAME headers)
target_compile_definitions(lito_fixture_headers INTERFACE LITO_CMAKE_HEADERS=1)
add_library(lito_fixture_order INTERFACE)
set_target_properties(lito_fixture_order PROPERTIES EXPORT_NAME order)
target_link_options(lito_fixture_order INTERFACE "LINKER:--as-needed")
install(TARGETS lito_fixture lito_fixture_headers lito_fixture_order
  EXPORT LitoFixtureTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES runtime/runtime.bin DESTINATION share/lito-fixture/runtime)
install(FILES resources/nested/resource.dat DESTINATION share/lito-fixture/resources/nested)
install(EXPORT LitoFixtureTargets FILE LitoFixtureTargets.cmake
  NAMESPACE LitoFixture:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LitoFixture)
configure_package_config_file(cmake/LitoFixtureConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LitoFixture)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LitoFixture)
)"_str },
        { "cmake/LitoFixtureConfig.cmake.in"_str, R"(@PACKAGE_INIT@
include("${CMAKE_CURRENT_LIST_DIR}/LitoFixtureTargets.cmake")
if(COMMAND lito_export_asset_set)
  lito_export_asset_set(NAME runtime ROOT "${PACKAGE_PREFIX_DIR}/share/lito-fixture/runtime"
    FILES runtime.bin)
  lito_export_asset_set(NAME runtime ROOT "${PACKAGE_PREFIX_DIR}/share/lito-fixture/resources"
    FILES nested/resource.dat)
endif()
)"_str },
        { "include/lito_fixture.hpp"_str, "#pragma once\nint lito_fixture_value();\n"_str },
        { "src/fixture.cpp"_str,
          "#include <lito_fixture.hpp>\nint lito_fixture_value() { return 42; }\n"_str },
        { "runtime/runtime.bin"_str, "runtime\n"_str },
        { "resources/nested/resource.dat"_str, "resource\n"_str },
    };
    return source_tree(files);
}

auto cmake_source_adapter_project_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "CMakeLists.txt"_str, R"(cmake_minimum_required(VERSION 3.28)
project(LitoSourceAdapter VERSION 4.5.6 LANGUAGES CXX)
add_library(lito_source_adapter STATIC src/fixture.cpp)
target_compile_features(lito_source_adapter PUBLIC cxx_std_20)
target_compile_definitions(lito_source_adapter INTERFACE LITO_CMAKE_SOURCE_ADAPTER_USAGE=1)
target_include_directories(lito_source_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
)"_str },
        { "include/lito_source_adapter.hpp"_str,
          "#pragma once\nint lito_source_adapter_fixture();\n"_str },
        { "src/fixture.cpp"_str,
          "#include <lito_source_adapter.hpp>\nint lito_source_adapter_fixture() { return 42; }\n"_str },
        { "adapter.cmake"_str, R"(if(NOT LITO_CMAKE_DEPENDENCY_MODE STREQUAL "source")
  message(FATAL_ERROR "LitoSourceAdapter fixture requires source mode")
endif()
if(NOT LITO_CMAKE_DEPENDENCY_PACKAGE STREQUAL "LitoSourceAdapter")
  message(FATAL_ERROR "LitoSourceAdapter fixture received the wrong package")
endif()
if(NOT DEFINED LITO_CMAKE_DEPENDENCY_SOURCE_DIR)
  message(FATAL_ERROR "LitoSourceAdapter fixture requires a source directory")
endif()
if(NOT TARGET lito_source_adapter)
  add_subdirectory("${LITO_CMAKE_DEPENDENCY_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/fixture-source")
endif()
if(NOT TARGET LitoSourceAdapter::fixture)
  add_library(LitoSourceAdapter::fixture ALIAS lito_source_adapter)
endif()
set(LitoSourceAdapter_VERSION "4.5.6")
)"_str },
    };
    return source_tree(files);
}

auto cmake_find_package_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    constexpr ProjectFile files[] = {
        { "lib/cmake/LitoFindFixture/LitoFindFixtureConfig.cmake"_str,
          R"(get_filename_component(_LITO_FIND_FIXTURE_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(LitoFindFixture_VERSION "7.8.9")
if(NOT TARGET LitoFindFixture::raw)
  add_library(LitoFindFixture::raw INTERFACE IMPORTED)
  set_target_properties(LitoFindFixture::raw PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS LITO_CMAKE_FIND_USAGE=1
    INTERFACE_INCLUDE_DIRECTORIES "${_LITO_FIND_FIXTURE_PREFIX}/include")
endif()
)"_str },
        { "include/lito_find_fixture.hpp"_str, "#pragma once\n"_str },
        { "adapter.cmake"_str,
          R"(if(NOT LITO_CMAKE_DEPENDENCY_MODE STREQUAL "find")
  message(FATAL_ERROR "LitoFindFixture adapter requires find mode")
endif()
if(NOT LITO_CMAKE_DEPENDENCY_PACKAGE STREQUAL "LitoFindFixture")
  message(FATAL_ERROR "LitoFindFixture adapter received the wrong package")
endif()
if(DEFINED LITO_CMAKE_DEPENDENCY_SOURCE_DIR)
  message(FATAL_ERROR "LitoFindFixture adapter received a source directory in find mode")
endif()
find_package(${LITO_CMAKE_DEPENDENCY_PACKAGE} REQUIRED CONFIG)
if(NOT TARGET LitoFindFixture::fixture)
  add_library(LitoFindFixture::fixture INTERFACE IMPORTED)
  set_target_properties(LitoFindFixture::fixture PROPERTIES
    INTERFACE_LINK_LIBRARIES LitoFindFixture::raw)
endif()
)"_str },
    };
    return source_tree(files);
}

auto resolve_cmake_fixtures_with_provider(
    const Vec<lito::PreparedCMakeDependencyRequirement>& declarations,
    const lito::cpp::ProfileSpec&                        profile,
    const lito::system::BuildPlatform&                   platform,
    const lito::cpp::CppArgumentParser&                  parser,
    ref<rstd::path::Path>                                work_root,
    lito::dependency::CMakeProviderConfig                provider,
    usize                                                jobs   = usize(1),
    Vec<lito::ExternalAssetSet>*                         assets = nullptr)
    -> lito::dependency::DependencyResult<Vec<lito::cpp::ResolvedExternalDependency>> {
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(
            rstd::into<lito::dependency::DependencyError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::system::ToolResolver(*environment);
    auto tool     = resolver.resolve(provider.executable.as_path(), "CMake executable"_str);
    if (tool.is_err()) {
        return Err(rstd::into<lito::dependency::DependencyError>(rstd::move(tool).unwrap_err()));
    }
    provider.executable = rstd::move(tool).unwrap().executable;
    auto identified     = lito::identify_cmake_provider(rstd::move(provider), *environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    provider    = rstd::move(identified).unwrap();
    auto result = Vec<lito::cpp::ResolvedExternalDependency>::make();
    for (const auto& declaration : declarations) {
        auto requirement = lito::resolve_cmake_requirement_for_platform(declaration, platform);
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        if (requirement->adapter.is_some() && requirement->adapter_identity.is_empty()) {
            auto contents = rstd::fs::read_to_string(requirement->adapter->as_path());
            if (contents.is_err()) {
                return Err(lito::dependency::DependencyError::Message(
                    rstd::format("cannot read CMake adapter '{}': {}",
                                 requirement->adapter->as_path(),
                                 rstd::move(contents).unwrap_err())));
            }
            requirement->adapter_identity =
                rstd::format("{}\n{}", requirement->adapter->as_path(), contents->as_str());
        }
        auto materialized = lito::materialize_cmake_requirement(*requirement);
        if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
        auto plan = lito::plan_cmake_package(*materialized,
                                             provider,
                                             configuration(),
                                             profile,
                                             platform.compiler_default,
                                             platform.effective_target.triple.as_str(),
                                             work_root,
                                             jobs);
        if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
        auto snapshot = lito::execute_cmake_package(*plan, *environment);
        if (snapshot.is_err()) return Err(rstd::move(snapshot).unwrap_err());
        if (assets != nullptr) {
            for (const auto& set : snapshot->assets) assets->push(set.clone());
        }
        auto usage = lito::materialize_cmake_usage(*plan, *snapshot, parser);
        if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
        result.push(rstd::move(usage).unwrap());
    }
    return Ok(rstd::move(result));
}

auto resolve_cmake_fixtures(const Vec<lito::PreparedCMakeDependencyRequirement>& declarations,
                            const lito::cpp::ProfileSpec&                        profile,
                            const lito::system::BuildPlatform&                   platform,
                            const lito::cpp::CppArgumentParser&                  parser,
                            ref<rstd::path::Path>                                work_root,
                            usize                                                jobs   = usize(1),
                            Vec<lito::ExternalAssetSet>*                         assets = nullptr)
    -> lito::dependency::DependencyResult<Vec<lito::cpp::ResolvedExternalDependency>> {
    return resolve_cmake_fixtures_with_provider(
        declarations, profile, platform, parser, work_root, fixture_cmake(), jobs, assets);
}
auto versioned_fixture(
    ref<str>                                   alias,
    lito::dependency::PkgConfigVersionOperator comparison,
    ref<str>                                   version,
    lito::dependency::PkgConfigQueryMode       mode = lito::dependency::PkgConfigQueryMode::Shared)
    -> lito::dependency::PkgConfigExternalDependency {
    return lito::dependency::PkgConfigExternalDependency {
        .alias = String::make(alias),
        .requirement =
            lito::dependency::PkgConfigDependencyRequirement {
                .module  = String::make("lito-fixture"_str),
                .version = Some(lito::dependency::PkgConfigVersionRequirement {
                    .comparison = comparison,
                    .value      = String::make(version),
                }),
                .mode    = mode,
            },
    };
}

auto external_usage_metadata(lito::dependency::DependencyVisibility visibility,
                             const lito::cpp::CppArgumentParser&    parser)
    -> lito::package::PackageResult<lito::cpp::PackageMetadata> {
    auto raw       = strings("-DLITO_EXTERNAL_USAGE=1"_str);
    auto arguments = parser.parse(raw, "pkg-config test fixture"_str);
    if (arguments.is_err()) {
        return Err(lito::package::PackageError::Message(
            rstd::format("pkg-config test fixture compiler arguments are invalid: {}",
                         rstd::move(arguments).unwrap_err())));
    }
    auto external         = Vec<lito::cpp::ResolvedExternalDependency>::make();
    auto external_targets = Vec<lito::cpp::ResolvedExternalTargetUsage>::make();
    external_targets.push(lito::cpp::ResolvedExternalTargetUsage {
        .name              = String::make("lito-fixture"_str),
        .visibility        = visibility,
        .compile_arguments = rstd::move(arguments).unwrap(),
        .identity          = String::make("fixture-resolution-v1"_str),
    });
    external.push(lito::cpp::ResolvedExternalDependency {
        .alias    = String::make("fixture"_str),
        .provider = String::make("pkg-config"_str),
        .version  = String::make("2.3.4"_str),
        .targets  = rstd::move(external_targets),
        .link_arguments =
            lito::cpp::LinkArgumentSequence {
                .tokens   = strings("-llito_fixture"_str),
                .source   = String::make("pkg-config fixture"_str),
                .identity = String::make("fixture-link-v1"_str),
            },
        .identity = String::make("fixture-resolution-v1"_str),
    });
    auto dependencies = Vec<lito::cpp::DependencySpec>::make();
    dependencies.push(lito::cpp::DependencySpec {
        .target =
            lito::package::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::package::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    auto targets = Vec<lito::cpp::ResolvedTarget>::make();
    targets.push(lito::cpp::ResolvedTarget {
        .id =
            lito::package::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::package::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .artifact_kind         = lito::cpp::ArtifactKind::StaticLibrary,
        .artifact_name         = String::make("library"_str),
        .external_dependencies = rstd::move(external),
    });
    targets.push(lito::cpp::ResolvedTarget {
        .id =
            lito::package::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::package::PackageTargetKind::Binary,
                .name    = String::make("app"_str),
            },
        .artifact_kind = lito::cpp::ArtifactKind::Executable,
        .artifact_name = String::make("app"_str),
        .dependencies  = rstd::move(dependencies),
    });
    auto default_targets = Vec<lito::package::PackageTargetId>::make();
    default_targets.push(targets[usize(1)].id.clone());
    auto profiles = Vec<lito::cpp::ProfileSpec>::make();
    profiles.push(default_profile(parser));
    return Ok(lito::cpp::PackageMetadata {
        .name            = String::make("external-usage"_str),
        .default_profile = String::make("debug"_str),
        .default_targets = rstd::move(default_targets),
        .profiles        = rstd::move(profiles),
        .targets         = rstd::move(targets),
    });
}
} // namespace lito_test
