cmake_minimum_required(VERSION 3.28)

set(TENON_RSTD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rstd" CACHE PATH
    "Path to the rstd source tree")

set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -stdlib=libc++")

project(
  tenon
  VERSION 0.1.0
  DESCRIPTION "Module-first C++ builder"
  LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "tenon only supports clang++")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-fmodules-reduced-bmi>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wno-gnu-statement-expression>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wunknown-attributes>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fsized-deallocation>")
add_link_options(
  "$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>"
  "$<$<LINK_LANGUAGE:CXX>:-nostdlib++>")

set(RSTD_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RSTD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(RSTD_BENCHMARKS_IN_CTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${TENON_RSTD_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rstd")

find_program(TENON_DEFAULT_CLANGXX NAMES clang++ REQUIRED)
find_program(TENON_DEFAULT_CLANG_SCAN_DEPS NAMES clang-scan-deps REQUIRED)
find_program(TENON_DEFAULT_LLVM_AR NAMES llvm-ar REQUIRED)

add_library(tenon.lib STATIC)
target_compile_features(tenon.lib PUBLIC cxx_std_20)
target_sources(
  tenon.lib
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/model.cppm
         src/build_profile.cppm
         src/process.cppm
         src/manifest_locator.cppm
         src/manifest_schema.cppm
         src/module_convention.cppm
         src/source_discovery.cppm
         src/package_resolver.cppm
         src/workspace_resolver.cppm
         src/lock_store.cppm
         src/package_adapter.cppm
         src/package.cppm
         src/toolchain/clang_options.cppm
         src/toolchain/clang.cppm
         src/module_graph.cppm
         src/artifact_store.cppm
         src/build_layout.cppm
         src/builder.cppm
         src/mod.cppm)
target_link_libraries(tenon.lib PUBLIC rstd.std rstd.json rstd.toml)

add_executable(tenon src/main.cpp)
target_link_libraries(tenon PRIVATE tenon.lib rstd::nostd)
target_compile_definitions(
  tenon
  PRIVATE TENON_DEFAULT_CLANGXX="${TENON_DEFAULT_CLANGXX}"
          TENON_DEFAULT_CLANG_SCAN_DEPS="${TENON_DEFAULT_CLANG_SCAN_DEPS}"
          TENON_DEFAULT_LLVM_AR="${TENON_DEFAULT_LLVM_AR}")
