cmake_minimum_required(VERSION 3.29)

set(CMAKE_LINKER_TYPE LLD)

project(
  lito
  VERSION 0.6.2
  DESCRIPTION "Module-first C++ builder"
  LANGUAGES C CXX)

include(GNUInstallDirs)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "lito only supports clang++")
endif()

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "lito only supports clang")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON CACHE BOOL "Enable link-time optimization")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(LITO_OFFICIAL_REGISTRY_PUBLIC_KEY "" CACHE STRING
    "Base64url Ed25519 public key embedded for the official Registry")
option(LITO_USE_SYSTEM_ZSTD "Use the system zstd package" OFF)

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

if(WIN32)
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>")
  add_link_options(
    "$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>"
    "$<$<LINK_LANGUAGE:CXX>:-nostdlib++>")
endif()

set(RSTD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
  rstd
  GIT_REPOSITORY https://github.com/litocpp/rstd.git
  GIT_TAG 0ec1bc101ab9088f1e3b4ac7be5c1d5e415da8c1)

FetchContent_MakeAvailable(rstd)

set(LUATO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  luato
  GIT_REPOSITORY https://github.com/litocpp/luato.git
  GIT_TAG 61dd40dca1e9aeda69eed208ddf0d10b34f59db7)
FetchContent_MakeAvailable(luato)

if(NOT LITO_USE_SYSTEM_ZSTD)
  FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG f8745da6ff1ad1e7bab384bd1f9d742439278e99)
  FetchContent_MakeAvailable(zstd)
endif()

FetchContent_Declare(
  sodium
  GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
  GIT_TAG 77e1ce5d6dee871c49ef211222ba18ef0c486bda)
FetchContent_MakeAvailable(sodium)

add_subdirectory(third-party/zstd)
add_subdirectory(third-party/libsodium)
add_subdirectory(archive)
add_subdirectory(frontend)
add_subdirectory(core)
add_subdirectory(cpp)
add_subdirectory(tools)
add_subdirectory(toolchain)
add_subdirectory(driver)
add_subdirectory(cli)

install(FILES schema/lito.schema.json DESTINATION ${CMAKE_INSTALL_DATADIR}/lito)
install(FILES LICENSE-MIT LICENSE-APACHE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/lito)
if(NOT LITO_USE_SYSTEM_ZSTD)
  install(FILES third-party/zstd/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/zstd)
endif()
install(FILES third-party/libsodium/LICENSE-ISC
              third-party/libsodium/LICENSE-BSD-2-Clause
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/libsodium)
