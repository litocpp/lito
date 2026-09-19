cmake_minimum_required(VERSION 3.29)

set(CMAKE_LINKER_TYPE LLD)

project(
  lito
  VERSION 0.8.2
  DESCRIPTION "Module-first C++ builder"
  LANGUAGES C CXX)

include(GNUInstallDirs)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "lito only supports clang++")
endif()

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "lito only supports clang")
endif()

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "Minimum macOS deployment target")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON CACHE BOOL "Enable link-time optimization")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

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
  add_link_options("$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>")
  add_link_options("$<$<LINK_LANGUAGE:CXX>:-nostdlib++>")
endif()

set(RSTD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

include(FetchContent)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deps.json" LITO_DEPS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/deps.json")

function(lito_declare_dependency name)
  string(JSON owner GET "${LITO_DEPS}" "${name}" owner)
  string(JSON repo GET "${LITO_DEPS}" "${name}" repo)
  string(JSON rev GET "${LITO_DEPS}" "${name}" rev)
  FetchContent_Declare(
    ${name}
    GIT_REPOSITORY "https://github.com/${owner}/${repo}.git"
    GIT_TAG "${rev}")
endfunction()

lito_declare_dependency(rstd)
FetchContent_MakeAvailable(rstd)

set(LUATO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
lito_declare_dependency(luato)
FetchContent_MakeAvailable(luato)

if(NOT LITO_USE_SYSTEM_ZSTD)
  lito_declare_dependency(zstd)
  FetchContent_MakeAvailable(zstd)
endif()

lito_declare_dependency(licrypto)
FetchContent_MakeAvailable(licrypto)

add_subdirectory(third-party/zstd)
add_subdirectory(frontend)
add_subdirectory(core)
add_subdirectory(pack)
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
install(FILES ${licrypto_SOURCE_DIR}/LICENSE-MIT
              ${licrypto_SOURCE_DIR}/LICENSE-APACHE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/licrypto)

string(TOLOWER "${CMAKE_SYSTEM_NAME}" LITO_PACKAGE_SYSTEM)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" LITO_PACKAGE_PROCESSOR)
if(LITO_PACKAGE_SYSTEM STREQUAL "darwin")
  set(LITO_PACKAGE_SYSTEM "macos")
endif()
if(LITO_PACKAGE_PROCESSOR STREQUAL "amd64")
  set(LITO_PACKAGE_PROCESSOR "x86_64")
elseif(LITO_PACKAGE_PROCESSOR STREQUAL "arm64")
  set(LITO_PACKAGE_PROCESSOR "aarch64")
endif()

if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_FILE_NAME
    "lito-v${PROJECT_VERSION}-${LITO_PACKAGE_SYSTEM}-${LITO_PACKAGE_PROCESSOR}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_STRIP_FILES TRUE)
include(CPack)
