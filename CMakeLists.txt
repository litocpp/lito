cmake_minimum_required(VERSION 3.29)

set(CMAKE_LINKER_TYPE LLD)

project(
  lito
  VERSION 0.1.0
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

set(RSTD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
  rstd
  GIT_REPOSITORY https://github.com/litocpp/rstd.git
  GIT_TAG a802845810966d738b65a4f86331358eef0ec8c9)

set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -stdlib=libc++")
FetchContent_MakeAvailable(rstd)

set(LUATO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  luato
  GIT_REPOSITORY https://github.com/litocpp/luato.git
  GIT_TAG c09cce8e62c26c824faf913c57e8d174cf0930a9)
FetchContent_MakeAvailable(luato)

add_subdirectory(frontend)
add_subdirectory(core)
add_subdirectory(cli)

install(FILES schema/lito.schema.json DESTINATION ${CMAKE_INSTALL_DATADIR}/lito)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/lito)
