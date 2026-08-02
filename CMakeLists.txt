cmake_minimum_required(VERSION 3.28)

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

include(FetchContent)

FetchContent_Declare(
  rstd
  GIT_REPOSITORY https://github.com/litocpp/rstd.git
  GIT_TAG 1a39de4fe7587ff0013be71ebb2adc27f638deed)

set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -stdlib=libc++")
FetchContent_MakeAvailable(rstd)

add_library(tenon.lib STATIC)
target_compile_features(tenon.lib PUBLIC cxx_std_20)
target_sources(
  tenon.lib
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/frontend/result.cppm
         src/frontend/lexical/token.cppm
         src/frontend/lexical/source.cppm
         src/frontend/lexical/error.cppm
         src/frontend/lexical/lexer.cppm
         src/frontend/lexical/mod.cppm
         src/frontend/preprocessor/traits.cppm
         src/frontend/preprocessor/macro.cppm
         src/frontend/preprocessor/expression.cppm
         src/frontend/preprocessor/session.cppm
         src/frontend/preprocessor/directive.cppm
         src/frontend/preprocessor/mod.cppm
         src/frontend/parser/module_dependency.cppm
         src/frontend/parser/mod.cppm
         src/frontend/service.cppm
         src/frontend/mod.cppm
         src/model.cppm
         src/build_profile.cppm
         src/process.cppm
         src/manifest/locator.cppm
         src/manifest/schema.cppm
         src/manifest/mod.cppm
         src/config/schema.cppm
         src/config/mod.cppm
         src/modules/convention.cppm
         src/modules/graph.cppm
         src/modules/frontend.cppm
         src/modules/mod.cppm
         src/source_discovery.cppm
         src/workspace/member.cppm
         src/workspace/catalog.cppm
         src/workspace/mod.cppm
         src/package/plan.cppm
         src/package/adapter.cppm
         src/package/source.cppm
         src/package/resolver.cppm
         src/package/mod.cppm
         src/workspace_resolver.cppm
         src/lock_store.cppm
         src/project.cppm
         src/toolchain/command.cppm
         src/toolchain/clang_options.cppm
         src/toolchain/clang_format_options.cppm
         src/toolchain/clang_format.cppm
         src/toolchain/clang_preprocessor_environment.cppm
         src/toolchain/clang.cppm
         src/toolchain/mod.cppm
         src/build_layout.cppm
         src/cache/hash.cppm
         src/cache/mod.cppm
         src/builder.cppm
         src/scanner.cppm
         src/formatter.cppm
         src/mod.cppm)
target_link_libraries(tenon.lib PUBLIC rstd.std rstd.json rstd.toml)

add_executable(tenon)
target_sources(
  tenon
  PRIVATE FILE_SET
          modules
          TYPE
          CXX_MODULES
          FILES
          src/executable/cli.cppm
          src/main.cppm)
target_link_libraries(tenon PRIVATE tenon.lib rstd.argparse rstd::nostd)

include(CTest)

if(BUILD_TESTING)
  add_executable(tenon.preprocessor.test)
  target_sources(tenon.preprocessor.test PRIVATE test/preprocessor/core.cpp)
  target_link_libraries(tenon.preprocessor.test PRIVATE tenon.lib rstd::nostd)
  add_test(NAME tenon.preprocessor.core COMMAND tenon.preprocessor.test)
  add_executable(tenon.clang-builtin-context.test)
  target_sources(tenon.clang-builtin-context.test
                 PRIVATE test/toolchain/clang_builtin_context.cpp)
  target_link_libraries(tenon.clang-builtin-context.test PRIVATE tenon.lib rstd::nostd)
  add_test(NAME tenon.clang-builtin-context COMMAND tenon.clang-builtin-context.test)
  add_test(
    NAME tenon.scan.native
    COMMAND tenon -C ${CMAKE_CURRENT_SOURCE_DIR}/test/preprocessor/native scan
            src/lib.cppm)
  set_tests_properties(
    tenon.scan.native
    PROPERTIES PASS_REGULAR_EXPRESSION
               "\"logical-name\": \"fixture.preprocessor.native:dependency\""
               FAIL_REGULAR_EXPRESSION
               "pragma_operator_failure;builtin_catalog_failure;native_builtin_failure")
  add_test(
    NAME tenon.scan.definitions
    COMMAND tenon -C ${CMAKE_CURRENT_SOURCE_DIR}/test/scan/definitions scan
            src/lib.cppm)
  set_tests_properties(
    tenon.scan.definitions
    PROPERTIES PASS_REGULAR_EXPRESSION
               "\"logical-name\": \"fixture.scan.definitions:defined\""
               FAIL_REGULAR_EXPRESSION
               "fixture.scan.definitions:missing;command_line_undef_failure")
endif()
